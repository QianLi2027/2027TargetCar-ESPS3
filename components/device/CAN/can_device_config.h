//
// 移植到 ESP32:去掉未使用的 LK 电机,返回类型 HAL_StatusTypeDef → esp_err_t。
//

#ifndef ESP_TARGET_ROBOT_CAN_DEVICE_CONFIG_H
#define ESP_TARGET_ROBOT_CAN_DEVICE_CONFIG_H

#pragma once

#include "esp_err.h"
#include "dji_motor.h"

extern DjiMotor dji_motor_1;
extern DjiMotor dji_motor_2;

extern DjiMotorGroup dji_group_1_to_4;

class CanDeviceConfig
{
public:
    static esp_err_t init();
};

#endif //ESP_TARGET_ROBOT_CAN_DEVICE_CONFIG_H
