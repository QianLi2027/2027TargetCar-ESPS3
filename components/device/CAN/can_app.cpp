//
// 移植到 ESP32:逻辑原样保留,去掉 LK/DM 分支,HAL_StatusTypeDef → esp_err_t。
//

#include "can_app.h"

#include "can_device_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace
{
    // 毫秒时间戳（FreeRTOS tick 换算，和 dji_motor.cpp 保持一致）
    inline uint32_t nowMs()
    {
        return static_cast<uint32_t>(xTaskGetTickCount()) * portTICK_PERIOD_MS;
    }

    DjiMotor* GetDjiMotor(uint8_t motor_index)
    {
        switch (motor_index) {
            case 1: return &dji_motor_1;
            case 2: return &dji_motor_2;
            default: return nullptr;
        }
    }

    DjiMotorGroup* GetDjiGroup(uint8_t group_index)
    {
        switch (group_index) {
            case 1: return &dji_group_1_to_4;
            default: return nullptr;
        }
    }
}

extern "C" {

esp_err_t CAN_Device_Init(void)
{
    return CanDeviceConfig::init();
}

void CAN_DjiMotor_SetCurrent(uint8_t motor_index, int16_t current)
{
    DjiMotor* motor = GetDjiMotor(motor_index);

    if (motor != nullptr) {
        motor->setCurrent(current);
    }
}

esp_err_t CAN_DjiGroup_SendCurrent(uint8_t group_index)
{
    DjiMotorGroup* group = GetDjiGroup(group_index);

    if (group == nullptr) {
        return ESP_FAIL;
    }

    return group->sendCurrent();
}


float CAN_DjiMotor_GetVelocity(uint8_t motor_index)
{
    DjiMotor* motor = GetDjiMotor(motor_index);

    if (motor == nullptr) {
        return 0.0f;
    }

    return motor->feedback().velocity;
}

float CAN_DjiMotor_GetCurrent(uint8_t motor_index)
{
    DjiMotor* motor = GetDjiMotor(motor_index);

    if (motor == nullptr) {
        return 0.0f;
    }

    return motor->feedback().current;
}

int32_t CAN_DjiMotor_GetTotalAngle(uint8_t motor_index)
{
    DjiMotor* motor = GetDjiMotor(motor_index);

    if (motor == nullptr) {
        return 0;
    }

    return motor->feedback().total_angle;
}

bool CAN_DjiMotor_IsOnline(uint8_t motor_index)
{
    DjiMotor* motor = GetDjiMotor(motor_index);

    if (motor == nullptr) {
        return false;
    }

    // 时间戳在这一层取，调用方直接 if (CAN_DjiMotor_IsOnline(1)) 就行
    return motor->isOnline(nowMs());
}




} // extern "C"
