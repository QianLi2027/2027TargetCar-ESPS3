//
// Created by Lenovo on 2026/10/6.
// 移植到 ESP32:结构原样保留。
//

#ifndef ESP_HORIZONTAL_CONTROL_HORIZONTAL_CTRL_H
#define ESP_HORIZONTAL_CONTROL_HORIZONTAL_CTRL_H

#pragma once
#include "TaskBase.h"

#ifdef __cplusplus

class Horizontal_Control_Task : public TaskBase {
public:
    void run() override;
};

#endif

#ifdef __cplusplus
extern "C" {
#endif

    void Horizontal_Control_Task_Init();

#ifdef __cplusplus
}
#endif

#endif //ESP_HORIZONTAL_CONTROL_HORIZONTAL_CTRL_H
