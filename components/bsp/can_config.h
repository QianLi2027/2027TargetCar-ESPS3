//
// Created by 20852 on 2026/7/8.
//
// 移植到 ESP32 的说明:
//   ESP32-S3 只有 1 个 TWAI(CAN)控制器,所以枚举里只剩 CAN_1,所有帧都标它。
//

#ifndef ESP_TARGET_ROBOT_CAN_CONFIG_H
#define ESP_TARGET_ROBOT_CAN_CONFIG_H

#pragma once

#include <cstdint>

enum class CanChannel
{
    CAN_1,
};

struct CanFrame
{
    CanChannel channel;
    uint32_t std_id;
    uint8_t data[8];
    uint8_t dlc = 8;
};

using CanRxCallback = void (*)(const CanFrame& frame);

#endif //ESP_TARGET_ROBOT_CAN_CONFIG_H
