//
// Created by 20852 on 2026/7/8.
// 移植到 ESP32:内容原样保留。
//

#ifndef ESP_TARGET_ROBOT_CAN_DISPATCHER_H
#define ESP_TARGET_ROBOT_CAN_DISPATCHER_H

#pragma once

#include "can_config.h"

class CanDispatcher
{
public:
    static void init();

    static void onRxFrame(const CanFrame& frame);
};

#endif //ESP_TARGET_ROBOT_CAN_DISPATCHER_H
