//
// Created by 20852 on 2026/7/8.
// 移植到 ESP32:内容原样保留,与平台无关。
//

#ifndef ESP_TARGET_ROBOT_CAN_BYTE_UTILS_H
#define ESP_TARGET_ROBOT_CAN_BYTE_UTILS_H

#pragma once

#include <cstdint>

static inline int16_t CanReadInt16BE(const uint8_t* data)
{
    return static_cast<int16_t>((static_cast<uint16_t>(data[0]) << 8) |
                                 static_cast<uint16_t>(data[1]));
}

static inline uint16_t CanReadUint16BE(const uint8_t* data)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) |
                                  static_cast<uint16_t>(data[1]));
}

static inline void CanWriteInt16BE(uint8_t* data, int16_t value)
{
    data[0] = static_cast<uint8_t>((static_cast<uint16_t>(value) >> 8) & 0xFF);
    data[1] = static_cast<uint8_t>( static_cast<uint16_t>(value)       & 0xFF);
}

#endif //ESP_TARGET_ROBOT_CAN_BYTE_UTILS_H
