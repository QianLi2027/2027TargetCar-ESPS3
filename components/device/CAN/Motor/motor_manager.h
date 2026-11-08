//
// 移植到 ESP32:内容原样保留,与平台无关。
// 这个"注册制 + 按 ID 软件分拣"的设计在 ESP32 上尤其重要 ——
// ESP32-S3 的 TWAI 只有 1 组滤波器,只能全部放通后靠这里分拣。
//

#ifndef ESP_TARGET_ROBOT_MOTOR_MANAGER_H
#define ESP_TARGET_ROBOT_MOTOR_MANAGER_H

#pragma once

#include "motor_common.h"

class MotorManager
{
public:
    static constexpr int MaxMotorNum = 24;

    static void clear();

    static bool addMotor(MotorBase* motor);

    static bool onCanRx(const CanFrame& frame);

    static int count();

private:
    static MotorBase* motors_[MaxMotorNum];
    static int motor_count_;
};

#endif //ESP_TARGET_ROBOT_MOTOR_MANAGER_H
