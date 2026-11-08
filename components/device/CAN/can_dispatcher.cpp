//
// Created by 20852 on 2026/7/8.
// 移植到 ESP32:内容原样保留。
//
// 注意执行上下文:这个函数现在是从 ESP32 的 CAN 接收任务里被调用的
// (不是 STM32 那样的中断上下文),所以里面做解码是安全的。
//

#include "can_dispatcher.h"

#include "bsp_can.h"
#include "motor_manager.h"

void CanDispatcher::init()
{
    BspCan::setRxCallback(CanDispatcher::onRxFrame);
}

void CanDispatcher::onRxFrame(const CanFrame& frame)
{
    if (MotorManager::onCanRx(frame)) {
        return;
    }
    // 未识别的 CAN 帧可以在这里添加调试计数。
}
