//
// ============================================================================
//  网页服务（WebServer）—— HTTP + WebSocket
// ============================================================================
//
//  【它管什么】
//    1. 在 80 端口提供内嵌的控制页面（HTML 直接编进固件，不占文件系统）
//    2. 在 /ws 提供 WebSocket，网页每 200ms 发一次 4 个字段的指令
//    3. 最多允许 4 个网页同时连，但同一时刻只有一个持有控制权
//
//  【配套模块】
//    dns_redirect.cpp  强制门户：手机连上热点后自动弹出本页面
//    control_state.cpp 网页指令写到这里，控制任务从那读
//
// ============================================================================

#ifndef ESP_TARGET_ROBOT_WEB_SERVER_H
#define ESP_TARGET_ROBOT_WEB_SERVER_H

#pragma once

#include "esp_err.h"

// 起 HTTP 服务器(内嵌网页)+ WebSocket 端点 /ws
esp_err_t WebServer_Start();

#endif //ESP_TARGET_ROBOT_WEB_SERVER_H
