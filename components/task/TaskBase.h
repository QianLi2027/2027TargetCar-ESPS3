//
// Created by Lenovo on 2026/1/11.
//
// 移植到 ESP32:
//   STM32 上用的是 CMSIS-OS v1 的 osThreadCreate,ESP-IDF 没有那套封装,
//   改成原生的 xTaskCreatePinnedToCore。
//
// ★ 两个坑:
//   1) 栈大小单位不同 —— CMSIS 是"字"(4 字节),ESP-IDF 是"字节"。
//      原来写 512 是 2KB,这里必须换算成字节。
//   2) ESP32 是双核,控制环一定要用 PinnedToCore 钉到某个核上,
//      否则会和 WiFi/蓝牙抢核,造成控制周期抖动。
//

#ifndef ESP_TARGET_ROBOT_TASKBASE_H
#define ESP_TARGET_ROBOT_TASKBASE_H

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
class TaskBase {
public:
    virtual ~TaskBase() = default;

    // 每个任务必须实现的函数，即逻辑部分
    virtual void run() = 0;

    // 启动任务，包装 xTaskCreatePinnedToCore
    //   stackSizeBytes : 栈大小,单位字节(注意和 CMSIS 的"字"不同)
    //   priority       : FreeRTOS 优先级,数值越大越高(IDF 范围 0~25)
    //   coreId         : 钉到哪个核(0 或 1)
    void start(const char* name,
               uint32_t stackSizeBytes,
               UBaseType_t priority,
               BaseType_t coreId = 1) {
        xTaskCreatePinnedToCore(taskEntry,
                                name,
                                stackSizeBytes,
                                this,
                                priority,
                                nullptr,
                                coreId);
    }

private:
    // 静态入口函数，真正调用对象的 run()
    static void taskEntry(void* argument) {
        auto* task = static_cast<TaskBase*>(argument);
        task->run();
    }
};
#endif

#endif //ESP_TARGET_ROBOT_TASKBASE_H
