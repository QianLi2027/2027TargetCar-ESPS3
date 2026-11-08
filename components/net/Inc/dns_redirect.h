//
// ============================================================================
//  DNS 重定向 —— 强制门户(Captive Portal)的关键一环
// ============================================================================
//
//  【它解决什么问题】
//    手机连上 WiFi 后,系统会偷偷访问一个固定网址来"探测有没有外网"。
//    比如:
//      iOS      → captive.apple.com
//      Android  → connectivitycheck.gstatic.com
//      Windows  → www.msftconnecttest.com
//
//    这个组件开一个 DNS 服务器(端口 53),
//    **把所有域名查询都回答成板子自己的 IP**。
//
//    结果:手机探测时被引到 192.168.4.1,发现返回的不是预期内容
//         → 判定"这是个强制门户" → **自动弹出登录页**。
//
//  【配套的 HTTP 侧】
//    web_server.cpp 里注册了 404 处理器,把任何未知路径 303 回 "/",
//    所以手机探测的路径最终都会落到你的控制页面上。
//
//  【限制】
//    只能劫持 HTTP,劫持不了 HTTPS —— 但我们的页面是 http://,够了。
//
// ============================================================================

#ifndef ESP_TARGET_ROBOT_DNS_REDIRECT_H
#define ESP_TARGET_ROBOT_DNS_REDIRECT_H

#pragma once

#include "esp_err.h"

// 启动 DNS 重定向服务。
// 必须在 WiFi AP 启动之后调用(要读 AP 的 IP)。
// 重复调用是安全的。
esp_err_t DnsRedirect_Start();

#endif //ESP_TARGET_ROBOT_DNS_REDIRECT_H
