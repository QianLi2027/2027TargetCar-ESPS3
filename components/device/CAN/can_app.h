//
// 移植到 ESP32:这一层仍然是"给 C 文件调用的 C 接口",形状不变。
// 去掉了未实现的 DM / LK / 上下半板 / IMU 声明(主体工程里它们也只有声明没有实现)。
//

#ifndef ESP_TARGET_ROBOT_CAN_APP_H
#define ESP_TARGET_ROBOT_CAN_APP_H

#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 这一层是专门给 main.c / C 文件调用的 C 接口。
// C 文件不要直接 include dji_motor.h、motor_manager.h、can_device_config.h 这些 C++ 头文件。

esp_err_t CAN_Device_Init(void);

// DJI Motor，motor_index 使用 1 开始编号，对应 dji_motor_1、dji_motor_2...
void CAN_DjiMotor_SetCurrent(uint8_t motor_index, int16_t current);
esp_err_t CAN_DjiGroup_SendCurrent(uint8_t group_index);

float CAN_DjiMotor_GetVelocity(uint8_t motor_index);
float CAN_DjiMotor_GetCurrent(uint8_t motor_index);
int32_t CAN_DjiMotor_GetTotalAngle(uint8_t motor_index);
// 电机是否在线（有没有 CAN 反馈）。时间戳由本层自己取，调用方不用传。
bool CAN_DjiMotor_IsOnline(uint8_t motor_index);

#ifdef __cplusplus
}
#endif

#endif //ESP_TARGET_ROBOT_CAN_APP_H
