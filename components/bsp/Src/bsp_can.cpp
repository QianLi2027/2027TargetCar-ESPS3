//
// Created by 20852 on 2026/3/26.
//
// 移植到 ESP32:底层由 STM32 HAL CAN 换成 ESP-IDF TWAI,对外行为保持一致。
//

#include "bsp_can.h"

#include <cstring>

#include "driver/twai.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace
{
    constexpr const char* TAG = "BspCan";

    constexpr gpio_num_t CanTxGpio = GPIO_NUM_39;
    constexpr gpio_num_t CanRxGpio = GPIO_NUM_38;

    constexpr uint32_t  RxTaskStackSize = 4096;
    constexpr UBaseType_t RxTaskPriority = 10;
    constexpr BaseType_t  RxTaskCore = 1;   // 钉 Core 1,和 WiFi 分开

    // ★ 必须是 0(非阻塞)★
    //   STM32 的 HAL_CAN_AddTxMessage() 是非阻塞的:邮箱满了立刻返回错误,主循环照跑。
    //   这里如果给非零超时,队列满时 twai_transmit 会阻塞等满超时,
    //   把控制循环从 1kHz 拖成 100Hz —— 实测过,就是这么被坑的。
    //   给 0 就等价于 STM32 的行为:塞不进去就丢这一帧,下一帧 1ms 后照发。
    constexpr uint32_t TxTimeoutMs = 0;

    // ── CAN 总线模式（不是调试开关，是正式配置）──
    //   TWAI_MODE_NORMAL      = 0 : 正常上总线(要发货)
    //   TWAI_MODE_NO_ACK      = 1 : 自收自发
    //   TWAI_MODE_LISTEN_ONLY = 2 : 只听不发,连 ACK 都不发 —— 用来"偷听"总线
    constexpr twai_mode_t CanMode = TWAI_MODE_NORMAL;
}

CanRxCallback BspCan::rx_callback_ = nullptr;
bool BspCan::started_ = false;

esp_err_t BspCan::init()
{
    if (started_)
    {
        return ESP_OK;
    }

    // 1 Mbps,和原 STM32 工程一致(DJI 电机标准波特率)
    twai_general_config_t g_config =
        TWAI_GENERAL_CONFIG_DEFAULT(CanTxGpio, CanRxGpio, CanMode);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_1MBITS();
    // ★ 只有 1 组滤波器,全部放通,靠上层 MotorManager 按 ID 软件分拣
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "twai_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    err = twai_start();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "twai_start failed: %s", esp_err_to_name(err));
        twai_driver_uninstall();
        return err;
    }

    started_ = true;

    if (xTaskCreatePinnedToCore(rxTaskEntry,
                                "can_rx",
                                RxTaskStackSize,
                                nullptr,
                                RxTaskPriority,
                                nullptr,
                                RxTaskCore) != pdPASS)
    {
        ESP_LOGE(TAG, "failed to create CAN RX task");
        twai_stop();
        twai_driver_uninstall();
        started_ = false;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t BspCan::send(const CanFrame& frame)
{
    if (frame.dlc > 8)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!started_)
    {
        return ESP_ERR_INVALID_STATE;
    }

    twai_message_t msg = {};
    msg.identifier = frame.std_id;
    msg.data_length_code = frame.dlc;
    msg.extd = 0;   // 标准帧
    msg.rtr = 0;    // 数据帧

    // ★ 单次发送(不自动重传)★
    //   TWAI 默认会自动重传:一帧收不到 ACK 就永远重试,导致发送队列被永久占满,
    //   之后所有发送全部失败 —— 实测踩过这个坑(发送成功卡在 6 不动)。
    //   控制环 1kHz,丢一帧无所谓,下一帧 1ms 后就到,所以用单次发送。
    msg.ss = 1;

    std::memcpy(msg.data, frame.data, frame.dlc);

    return twai_transmit(&msg, pdMS_TO_TICKS(TxTimeoutMs));
}

void BspCan::setRxCallback(CanRxCallback callback)
{
    rx_callback_ = callback;
}

void BspCan::rxTaskEntry(void* arg)
{
    (void)arg;

    twai_message_t msg;

    for (;;)
    {
        if (twai_receive(&msg, portMAX_DELAY) != ESP_OK)
        {
            continue;
        }

        // 只要标准数据帧
        if (msg.extd || msg.rtr)
        {
            continue;
        }

        CanFrame frame{};
        frame.channel = CanChannel::CAN_1;
        frame.std_id = msg.identifier;
        frame.dlc = (msg.data_length_code > 8) ? 8 : msg.data_length_code;

        std::memcpy(frame.data, msg.data, frame.dlc);

        if (rx_callback_ != nullptr)
        {
            rx_callback_(frame);
        }
    }
}
