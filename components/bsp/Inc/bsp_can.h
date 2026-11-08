//
// Created by 20852 on 2026/3/26.
//
// 移植到 ESP32:原来基于 STM32 HAL CAN,现在底层是 ESP-IDF 的 TWAI。
// 对外接口形状保持不变(init / send / setRxCallback),上层 device 层不用改。
//

#ifndef ESP_TARGET_ROBOT_BSP_CAN_H
#define ESP_TARGET_ROBOT_BSP_CAN_H

#pragma once

#include "esp_err.h"
#include "can_config.h"

class BspCan
{
public:
    // 安装并启动 TWAI 驱动,同时拉起接收任务。
    static esp_err_t init();

    static esp_err_t send(const CanFrame& frame);

    static void setRxCallback(CanRxCallback callback);

private:
    // 接收任务入口。
    // ★ 和 STM32 不同:ESP-IDF 的 TWAI 是"队列 + 阻塞接收"模型,
    //   不能在中断回调里做解码,所以这里开一个任务阻塞在 twai_receive 上。
    static void rxTaskEntry(void* arg);

    static CanRxCallback rx_callback_;
    static bool started_;
};

#endif //ESP_TARGET_ROBOT_BSP_CAN_H
