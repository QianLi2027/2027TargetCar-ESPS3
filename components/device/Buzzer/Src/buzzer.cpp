//
// ============================================================================
//  提示模块实现 (Buzzer) —— 蜂鸣器提示音 + 连接状态监测
// ============================================================================
//
//  【结构】
//    一个"播放任务" + 一个"监测任务"，两者用队列通信：
//
//      监测任务(50ms 周期)              播放任务
//        ├ 看网页 online              ← 收到的音效编号
//        ├ 看 C 板链路                → 逐条播放 beep 序列
//        └ 变化时 Buzzer_PlayXxx()
//
//  【为什么分成两个任务】
//    "响 3+3 下"要 1.2 秒左右，如果放在监测任务里同步阻塞，
//    这 1.2 秒就看不到状态变化了（可能漏掉"断了又连上"）。
//    播放交给独立任务，监测永远 50ms 一次。
//
// ============================================================================

#include "buzzer.h"

#include "driver/ledc.h"   // LEDC PWM（驱动无源蜂鸣器）
#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "control_state.h"
#include "rail_link.h"

namespace {


    // ── 硬件 ────────────────────────────────────────────────────────────
    constexpr gpio_num_t Buzzer_Gpio      = GPIO_NUM_9;   // 原理图：BUZZER 网络
    constexpr ledc_mode_t Buzzer_Speed    = LEDC_LOW_SPEED_MODE;
    constexpr ledc_channel_t Buzzer_Ch    = LEDC_CHANNEL_0;
    constexpr ledc_timer_t Buzzer_Timer   = LEDC_TIMER_0;
    constexpr uint32_t Buzzer_Duty        = 512;         // 10 位分辨率，50% 占空比

    // ── 音效参数 ────────────────────────────────────────────────────────
    constexpr uint32_t Tone_Freq          = 4000;   // 无源蜂鸣器的谐振频率
    constexpr uint32_t Beep_On_ms         = 120;    // 每一下响多久
    constexpr uint32_t Beep_Gap_ms        = 120;    // 同一组内两下的间隔
    constexpr uint32_t Group_Gap_ms       = 800;    // 两组之间的间隔（你的要求 0.8s）
    constexpr uint32_t Connect_Beep_ms    = 80;     // 连上时短促一声

    // ── 任务参数 ────────────────────────────────────────────────────────
    constexpr uint32_t    Task_Stack    = 3072;
    constexpr UBaseType_t Task_Priority = 5;     // 比控制环低，提示音不急
    constexpr uint32_t    Watch_Period_ms = 50;

    // 一条 beep：响 freq 持续 on_ms，然后静音 off_ms
    struct BeepStep
    {
        uint32_t freq_hz;
        uint32_t on_ms;
        uint32_t off_ms;
    };


    constexpr BeepStep kConnected[] = {
        { Tone_Freq, Connect_Beep_ms, 0 },
    };

    // 连接断开：3 下 + 0.8s 间隔 + 3 下
    // 控制权切换：两短声，比"连接成功"更容易区分
    constexpr BeepStep kTakeover[] = {
        { Tone_Freq, 70, 90 },
        { Tone_Freq, 70, 0  },
    };

    // 上电回零完成：两短声，和"接管"区分（频率略高一点）
    constexpr BeepStep kHomed[] = {
        { Tone_Freq, 70, 90 },
        { Tone_Freq, 70, 0  },
    };

    constexpr BeepStep kDisconnected[] = {
        { Tone_Freq, Beep_On_ms, Beep_Gap_ms },   // 第 1 下
        { Tone_Freq, Beep_On_ms, Beep_Gap_ms },   // 第 2 下
        { Tone_Freq, Beep_On_ms, Group_Gap_ms },  // 第 3 下 + 组间隔
        { Tone_Freq, Beep_On_ms, Beep_Gap_ms },   // 第 4 下
        { Tone_Freq, Beep_On_ms, Beep_Gap_ms },   // 第 5 下
        { Tone_Freq, Beep_On_ms, 0 },             // 第 6 下
    };

    // 给任务播放的音效编号
    enum SoundId : uint8_t
    {
        SOUND_CONNECTED = 0,
        SOUND_DISCONNECTED,
        SOUND_TAKEOVER,
        SOUND_HOMED,
    };

    QueueHandle_t s_sound_queue = nullptr;

    // 把 LEDC 设成某个频率并输出 50% 占空比（= 开始发声）
    void buzzerOn(uint32_t freq_hz)
    {
        ledc_set_freq(Buzzer_Speed, Buzzer_Timer, freq_hz);
        ledc_set_duty(Buzzer_Speed, Buzzer_Ch, Buzzer_Duty);
        ledc_update_duty(Buzzer_Speed, Buzzer_Ch);
    }

    // 占空比清零（= 停止发声）
    void buzzerOff()
    {
        ledc_set_duty(Buzzer_Speed, Buzzer_Ch, 0);
        ledc_update_duty(Buzzer_Speed, Buzzer_Ch);
    }

    void playSteps(const BeepStep* steps, size_t count)
    {
        for (size_t i = 0; i < count; ++i)
        {
            buzzerOn(steps[i].freq_hz);
            vTaskDelay(pdMS_TO_TICKS(steps[i].on_ms));
            buzzerOff();

            if (steps[i].off_ms != 0) {
                vTaskDelay(pdMS_TO_TICKS(steps[i].off_ms));
            }
        }
    }

    // ── 播放任务：从队列取音效，逐条播放 ────────────────────────────────
    void playTask(void* arg)
    {
        (void)arg;

        for (;;)
        {
            uint8_t id = 0;
            if (xQueueReceive(s_sound_queue, &id, portMAX_DELAY) != pdTRUE) {
                continue;
            }

            switch (id)
            {
                case SOUND_CONNECTED:
                    playSteps(kConnected, sizeof(kConnected) / sizeof(kConnected[0]));
                    break;

                case SOUND_DISCONNECTED:
                    playSteps(kDisconnected, sizeof(kDisconnected) / sizeof(kDisconnected[0]));
                    break;

                case SOUND_HOMED:
                    playSteps(kHomed, sizeof(kHomed) / sizeof(kHomed[0]));
                    break;

                case SOUND_TAKEOVER:
                    playSteps(kTakeover, sizeof(kTakeover) / sizeof(kTakeover[0]));
                    break;

                default:
                    break;
            }
        }
    }

    // 把音效塞进队列（队列满了就丢弃，提示音不值得阻塞调用者）
    void postSound(SoundId id)
    {
        if (s_sound_queue == nullptr) return;

        const uint8_t v = static_cast<uint8_t>(id);
        (void)xQueueSend(s_sound_queue, &v, 0);
    }

    // ── 监测任务：盯住"网页在线"和"C 板链路" ────────────────────────────
    void watchTask(void* arg)
    {
        (void)arg;

        // 上电后是否曾经连上过。一次都没连上过就不响（was_connected 天然保证）。
        bool was_connected  = false;

        // 控制权切换计数：网页每"接管"一次就 +1，我们盯着它变化
        uint32_t last_takeover = ControlState_GetTakeoverCount();

        for (;;)
        {
            const bool web_online  = ControlState_Get().online;
            const bool cart_link   = RailLink_IsConnected();
            const bool connected   = web_online || cart_link;

            // 掉线提示：必须"持续断开超过 3 秒"才响。
            //   手机 WiFi 短暂抖动（1~2 秒）不吵人，只有真的断了才提示。
            static uint32_t lost_since = 0;
            if (connected) {
                lost_since = 0;
            } else if (lost_since == 0) {
                lost_since = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            }

            const bool lost_long = (lost_since != 0)
                && (((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - lost_since) >= 3000);

            if (was_connected && !connected && lost_long) {
                postSound(SOUND_DISCONNECTED);
            }

            if (!was_connected && connected) {
                postSound(SOUND_CONNECTED);
            }

            // ── 控制权切换：两短声 ──
            const uint32_t takeover = ControlState_GetTakeoverCount();
            if (takeover != last_takeover) {
                last_takeover = takeover;
                postSound(SOUND_TAKEOVER);
            }

            was_connected = connected;
            vTaskDelay(pdMS_TO_TICKS(Watch_Period_ms));
        }
    }

} // namespace

// ============================================================================
//  对外接口
// ============================================================================

esp_err_t Buzzer_Init()
{
    // 蜂鸣器引脚：先拉低，避免上电瞬间"咔"一声
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << Buzzer_Gpio);
    io.mode         = GPIO_MODE_OUTPUT;
    io.pull_up_en   = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&io);
    gpio_set_level(Buzzer_Gpio, 0);

    // LEDC：4kHz、10 位分辨率
    ledc_timer_config_t timer = {};
    timer.speed_mode      = Buzzer_Speed;
    timer.timer_num       = Buzzer_Timer;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.freq_hz         = Tone_Freq;
    timer.clk_cfg         = LEDC_AUTO_CLK;
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        return err;
    }

    ledc_channel_config_t ch = {};
    ch.gpio_num   = Buzzer_Gpio;
    ch.speed_mode = Buzzer_Speed;
    ch.channel    = Buzzer_Ch;
    ch.timer_sel  = Buzzer_Timer;
    ch.duty       = 0;
    ch.hpoint     = 0;
    err = ledc_channel_config(&ch);
    if (err != ESP_OK) {
        return err;
    }

    s_sound_queue = xQueueCreate(4, sizeof(uint8_t));
    if (s_sound_queue == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void Buzzer_Start()
{
    if (s_sound_queue == nullptr) {
        return;
    }

    xTaskCreate(playTask,  "buzzer_play",  Task_Stack, nullptr, Task_Priority, nullptr);
    xTaskCreate(watchTask, "buzzer_watch", Task_Stack, nullptr, Task_Priority, nullptr);
}

void Buzzer_PlayLocated()
{
    postSound(SOUND_HOMED);
}
