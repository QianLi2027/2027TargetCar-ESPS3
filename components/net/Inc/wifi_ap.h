//
// ============================================================================
//  WiFi 热点 (AP 模式)
// ============================================================================
//
//  【对外只有一个函数】WifiAp_Start()
//    在 main.cpp 里调一次,板子就变成一个 WiFi 热点。
//
//  【连上之后】
//    热点名 : TARGET_CAR
//    密码   : cquqianli
//    板子 IP: http://192.168.4.1
//
//  【谁连它】
//    · 手机 —— 打开网页控制
//    · C 板上的 ESP-01 —— 连上来收 6020 指令
//
//  【启动顺序(在 main.cpp 里)】
//    必须在 WebServer_Start() 和 RailLink_Start() **之前**调用 ——
//    那两者都依赖这里建好的网络接口和 IP。
//
// ============================================================================

#ifndef ESP_TARGET_ROBOT_WIFI_AP_H
#define ESP_TARGET_ROBOT_WIFI_AP_H

#pragma once

#include "esp_err.h"

// 开热点。
// 内部会依次做:初始化 NVS → 协议栈 → 事件循环 → 网络接口 → WiFi 驱动
//              → 配置 SSID/密码/信道 → 设 AP 模式 → 启动。
//
// 返回:
//   ESP_OK        成功,手机已经能搜到热点
//   ESP_ERR_xxx   失败(多半是 NVS 或内存问题)
esp_err_t WifiAp_Start();

#endif //ESP_TARGET_ROBOT_WIFI_AP_H
