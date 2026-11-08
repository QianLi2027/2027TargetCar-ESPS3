//
// 设备装配/接线文件 —— 实际接线不同,优先改这里,不要改协议文件。
//
// 移植到 ESP32 的说明:
//   ESP32-S3 只有一路 CAN,只装配导轨的两个 M3508(反馈 ID 0x201 / 0x202)。
//   分组帧仍然是 4 个槽位(DJI 协议固定),3/4 号给 nullptr,发出去就是 0 电流。
//
// 任务层里 1 号按目标速度跑、2 号取反(两个电机装在导轨两端,方向相反)。
//

#include "can_device_config.h"

#include "can_dispatcher.h"
#include "motor_manager.h"

DjiMotor dji_motor_1(CanChannel::CAN_1, 0x201);
DjiMotor dji_motor_2(CanChannel::CAN_1, 0x202);

DjiMotorGroup dji_group_1_to_4(
    CanChannel::CAN_1,
    DjiMotorGroup::TxId_1_To_4,   // 导轨工程里 = 0x200
    &dji_motor_1,
    &dji_motor_2,
    nullptr,                      // 3/4 号槽位没人，固定 0 电流
    nullptr
);

esp_err_t CanDeviceConfig::init()
{
    MotorManager::clear();

    MotorManager::addMotor(&dji_motor_1);
    MotorManager::addMotor(&dji_motor_2);

    CanDispatcher::init();

    return BspCan::init();
}
