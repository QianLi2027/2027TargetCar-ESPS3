//
// Created by Lenovo on 2025/12/21.
// 移植到 ESP32:内容原样保留,只补了 <cstdint>。
//

#ifndef ESP_TARGET_ROBOT_SPEED_PID_H
#define ESP_TARGET_ROBOT_SPEED_PID_H

#pragma once

#include <cstdint>
#include "PIDBase.h"

#ifdef __cplusplus
class SpeedPID : public PidBase {
public:
    using PidBase::PidBase;
};
#endif

#endif //ESP_TARGET_ROBOT_SPEED_PID_H
