#include "led.h"
#include "locating.h"

#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace
{
    constexpr int      RMT_LED_STRIP_GPIO_NUM  = 48;
    constexpr int      RMT_LED_COUNT = 2;
    constexpr uint32_t RMT_LED_STRIP_RESOLUTION_HZ    = 10000000;  // 10MHz → 1 tick = 0.1us


    rmt_channel_handle_t RMT_CHANNEL    = nullptr;  // RMT 发送通道
    rmt_encoder_handle_t RMT_ENCODER = nullptr;  // 编码器
    bool RMT_READY = false;

    uint8_t RMT_GRB[RMT_LED_COUNT * 3] = {0};//不是RGB，是GRB

    inline uint32_t nowMs()
    {
        return static_cast<uint32_t>(xTaskGetTickCount()) * portTICK_PERIOD_MS;
    }

    inline rmt_symbol_word_t makeSym(uint16_t d0, uint16_t l0, uint16_t d1, uint16_t l1)
    {
        rmt_symbol_word_t rmt = {};
        rmt.duration0 = d0;
        rmt.level0    = l0;
        rmt.duration1 = d1;
        rmt.level1    = l1;
        return rmt;
    }

    // ── 三个固定波形 ──────────────────────────────────────────────────────
    //   "0": 高 3 tick(0.3us)+ 低 9 tick(0.9us)
    const rmt_symbol_word_t Zero  = makeSym(3, 1, 9, 0);
    //   "1": 高 9 tick(0.9us)+ 低 3 tick(0.3us)
    const rmt_symbol_word_t One   = makeSym(9, 1, 3, 0);
    //   复位:低电平共 500 tick(50us),拆成两段 250+250 输出
    const rmt_symbol_word_t Reset = makeSym(250, 0, 250, 0);

    size_t encoderCallback(const void* data, size_t data_size,
                           size_t symbols_written, size_t symbols_free,
                           rmt_symbol_word_t* symbols, bool* done, void* arg)
    {
        (void)arg;

        // 一个字节要展开成 8 个符号。
        // 如果缓冲区还剩不到 8 个位置,这次就先不写,等 RMT 播完一些再来问。
        if (symbols_free < 8)
        {
            return 0;
        }

        // 已经写了 N 个符号 = 已经编码了 N/8 个字节
        const size_t data_pos = symbols_written / 8;
        const uint8_t* bytes  = static_cast<const uint8_t*>(data);

        if (data_pos < data_size)
        {
            // 还有字节没发完:取当前这个字节,从最高位开始逐位转成符号
            size_t n = 0;
            for (int bitmask = 0x80; bitmask != 0; bitmask >>= 1)
            {
                symbols[n++] = (bytes[data_pos] & bitmask) ? One : Zero;
            }
            return n;   // 写了 8 个符号
        }

        symbols[0] = Reset;
        *done = true;
        return 1;
    }
}

esp_err_t Led_Init()
{
    if (RMT_READY)
    {
        return ESP_OK;      // 已经初始化过
    }

    // 发送通道
    rmt_tx_channel_config_t CHAN_CFG = {};
    CHAN_CFG.clk_src           = RMT_CLK_SRC_DEFAULT;   // 默认时钟源
    CHAN_CFG.gpio_num          = static_cast<gpio_num_t>(RMT_LED_STRIP_GPIO_NUM);
    CHAN_CFG.mem_block_symbols = 64;      // 通道缓冲区大小
    CHAN_CFG.resolution_hz     = RMT_LED_STRIP_RESOLUTION_HZ;  //决定 1 tick 多长时间
    CHAN_CFG.trans_queue_depth = 4;       // 能排几次发送

    esp_err_t err = rmt_new_tx_channel(&CHAN_CFG, &RMT_CHANNEL);
    if (err != ESP_OK)
    {
        return err;
    }

    // 回调交给 RMT
    rmt_simple_encoder_config_t enc_cfg = {};
    enc_cfg.callback = encoderCallback;

    err = rmt_new_simple_encoder(&enc_cfg, &RMT_ENCODER);
    if (err != ESP_OK)
    {
        return err;
    }

    // 使能
    err = rmt_enable(RMT_CHANNEL);
    if (err != ESP_OK)
    {
        return err;
    }

    RMT_READY = true;

    Led_SetColor(0, 0, 0);

    return ESP_OK;
}

static void pushPixels()
{
    rmt_transmit_config_t tx_cfg = {};
    tx_cfg.loop_count = 0;

    if (rmt_transmit(RMT_CHANNEL, RMT_ENCODER, RMT_GRB, sizeof(RMT_GRB), &tx_cfg) != ESP_OK)
    {
        return;
    }

    rmt_tx_wait_all_done(RMT_CHANNEL, pdMS_TO_TICKS(20));
}

//异色
void Led_SetPixel(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (!RMT_READY || index >= RMT_LED_COUNT)
    {
        return;
    }

    RMT_GRB[index * 3 + 0] = g;   // G
    RMT_GRB[index * 3 + 1] = r;   // R
    RMT_GRB[index * 3 + 2] = b;   // B

    pushPixels();
}

//同色
void Led_SetColor(uint8_t r, uint8_t g, uint8_t b)
{
    if (!RMT_READY)
    {
        return;
    }

    for (int i = 0; i < RMT_LED_COUNT; ++i)
    {
        RMT_GRB[i * 3 + 0] = g;   // G
        RMT_GRB[i * 3 + 1] = r;   // R
        RMT_GRB[i * 3 + 2] = b;   // B
    }

    pushPixels();
}


// ── 定零状态优先显示（两颗灯一起）─────────────────────────────
//   未定零   ：红常亮（导轨不让动，在等下一次自动回零）
//   正在定位 ：红闪 250ms
//   已定零   ：返回 false，交给下面的运动状态去画
//   状态直接问 locating 要，调用方不用传。
static bool Led_ShowLocateState(uint32_t now_ms)
{
    static uint8_t last_key = 0xFF;

    const LocateState state = Locating_State();
    if (state == LocateState::Located) {
        last_key = 0xFF;          // 下次回到未定零时，保证重新画一次红
        return false;
    }

    const uint8_t key = Locating_IsBusy()
                      ? static_cast<uint8_t>((now_ms / 250u) & 1u)   // 1 = 灭, 0 = 亮
                      : static_cast<uint8_t>(2);                     // 常红

    if (key != last_key) {
        last_key = key;
        if (key == 1) {
            Led_SetColor(0, 0, 0);    // 红闪的灭相
        } else {
            Led_SetColor(30, 0, 0);   // 红
        }
    }
    return true;
}

void Led_UpdateMode(uint8_t mode, bool online, bool link_ok, bool can_ok)
{
    // 内部 100ms 节流：Led_SetPixel 要阻塞发 RMT 波形（约 60µs/颗）
    const uint32_t now_ms = nowMs();
    static uint32_t last_ms = 0;
    if ((now_ms - last_ms) < 100) return;
    last_ms = now_ms;

    if (Led_ShowLocateState(now_ms)) {
        return;
    }

    // ── 灯 0：导轨 CAN ──
    //   绿 = 至少一个 M3508 在回反馈
    //   红 = 一帧都没有（线没接 / 电机没电 / 收发器坏）
    if (can_ok) {
        Led_SetPixel(0, 0, 30, 0);      // 绿
    } else {
        Led_SetPixel(0, 30, 0, 0);      // 红
    }

    // ── 灯 1：靶车（主体）──
    //   紫 = 默认模式（自动来回中）
    //   灭 = 网页没连
    //   蓝 = 网页在线 + 靶车链路已连
    //   黄闪 = 网页在线，但靶车链路没连（最需要看到的故障态）
    if (mode == 0) {
        Led_SetPixel(1, 25, 0, 25);     // 紫
    } else if (!online) {
        Led_SetPixel(1, 0, 0, 0);       // 灭：网页没连
    } else if (link_ok) {
        Led_SetPixel(1, 0, 0, 30);      // 蓝：靶车在线
    } else if ((now_ms / 250) & 1u) {
        Led_SetPixel(1, 30, 20, 0);     // 黄：网页在，靶车不在
    } else {
        Led_SetPixel(1, 0, 0, 0);       // 黄闪的灭相
    }
}


void Led_LocatingProgress(uint32_t elapsed_ms)
{
    static uint8_t last_on = 0xFF;
    const uint8_t on = static_cast<uint8_t>(((elapsed_ms / 250u) & 1u) == 0);
    if (on == last_on) return;
    last_on = on;

    if (on) {
        Led_SetColor(30, 0, 0);   // 亮：红
    } else {
        Led_SetColor(0, 0, 0);    // 灭
    }
}

void Led_LocatingDone(void)
{
    Led_SetColor(0, 25, 25);
}

void Led_LocatingFail(void)
{
    Led_SetColor(30, 0, 0);
}
