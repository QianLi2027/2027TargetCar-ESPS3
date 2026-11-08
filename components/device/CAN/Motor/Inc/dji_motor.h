//
// 移植到 ESP32:类结构原样保留,只换了 include 路径和返回类型。
//
// ⚠️ 发送 ID 三个常量的值**和主体工程不一样**,这是从导轨工程
//    F407-TargetRobot 原样搬过来的:
//      导轨工程(本工程):TxId_1_To_4 = 0x200
//      主体工程:          TxId_1_To_4 = 0x1FE
//    名字一样、值不一样。别"按标准值"统一,那样反而会跑不起来。
//

#ifndef ESP_HORIZONTAL_CONTROL_DJI_MOTOR_H
#define ESP_HORIZONTAL_CONTROL_DJI_MOTOR_H

#pragma once

#include "esp_err.h"
#include "motor_common.h"
#include "bsp_can.h"

class DjiMotor : public MotorBase
{
public:
    DjiMotor(CanChannel channel, uint32_t feedback_id)
        : MotorBase(channel, feedback_id)
    {
    }

    void decode(const CanFrame& frame) override;

    void setCurrent(int16_t current)
    {
        current_cmd_ = current;
    }

    int16_t currentCommand() const
    {
        return current_cmd_;
    }

private:
    void updateRoundCount(uint16_t new_angle);

private:
    int16_t current_cmd_ = 0;

    uint16_t last_raw_angle_ = 0;
    bool angle_inited_ = false;
};

class DjiMotorGroup
{
public:
    static constexpr uint32_t TxId_1_To_4 = 0x200;
    DjiMotorGroup(CanChannel channel,
                  uint32_t tx_id,
                  DjiMotor* m1,
                  DjiMotor* m2,
                  DjiMotor* m3,
                  DjiMotor* m4);

    esp_err_t sendCurrent();

private:
    CanChannel channel_;
    uint32_t tx_id_ = 0;
    DjiMotor* motors_[4] = {nullptr};
};

#endif //ESP_HORIZONTAL_CONTROL_DJI_MOTOR_H
