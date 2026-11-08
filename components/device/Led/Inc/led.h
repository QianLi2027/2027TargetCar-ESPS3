//
// ============================================================================
//  板载指示灯 —— 2 颗 WS2812，数据脚 GPIO48（从原理图读出）
// ============================================================================
//
//  【用途】不接电脑就能看出系统状态。
//
//    定零状态（两颗灯一起，优先级最高）：
//      红常亮 = 未定零（在等下一次自动回零，导轨不允许动）
//      红闪   = 正在回零（找墙）
//      —— 这个状态由本模块自己从 locating 读，调用方不用传。
//
//    灯 0（靠数据脚那颗）—— 导轨 CAN
//      绿常亮 = 至少一个 M3508 在回反馈
//      红常亮 = 一帧都没有（线没接 / 电机没电 / 收发器坏）
//
//    灯 1 —— 靶车（主体）
//      紫常亮 = 默认模式（自动来回运行中）
//      灭     = 网页没连
//      蓝常亮 = 网页在线 + 靶车链路已连
//      黄闪   = 网页在线，但靶车链路没连（最需要看到的故障态）
//
//  【对外接口】
//    Led_Init()               上电调一次
//    Led_UpdateMotion(...)    控制环每拍调一次，刷新"运动状态"（内部 100ms 节流）
//    Led_SetColor / SetPixel  直接设颜色（调试、回零流程用）
//
//  【分工】
//    "什么状态配什么颜色"、"定零状态从哪读"都在这个组件里；
//    控制环只负责把运动相关的事实（模式 / 网页在线 / 链路 / 电机在线）传进来。
//
//  【实现细节在 led.cpp】
//    WS2812 是"单线 + 脉宽编码"协议，时序精度 ±150ns，
//    普通 GPIO 翻转做不到，必须用 RMT 外设产生精确波形。
//
// ============================================================================

#ifndef ESP_TARGET_ROBOT_LED_H
#define ESP_TARGET_ROBOT_LED_H

#pragma once

#include "esp_err.h"
#include <cstdint>

esp_err_t Led_Init();

// 刷新"运动状态"灯（控制环每拍调一次，内部 100ms 节流）。
//   mode    : 0=默认模式  1=无线控制  0xFF=未选择
//   online  : 网页在线
//   link_ok : 靶车链路（TCP）已连上
//   can_ok  : 至少一个 M3508 有反馈
//
// 定零状态（未定零 / 正在回零）由本模块自己读 locating，会优先覆盖两颗灯，
// 所以这里不需要传。
void Led_UpdateMode(uint8_t mode, bool online, bool link_ok, bool can_ok);

// 两颗设成同一个颜色（调试用）
void Led_SetColor(uint8_t r, uint8_t g, uint8_t b);

// 只设第 index 颗（index: 0 / 1；0 是靠数据脚那颗，1 是它下游那颗）
void Led_SetPixel(uint8_t index, uint8_t r, uint8_t g, uint8_t b);


// ── 回零（Locating）阶段的三种显示 ──
//   进行中：红灯闪烁（250ms 亮 / 250ms 灭）
//   elapsed_ms：从开始回零算起的毫秒数
void Led_LocatingProgress(uint32_t elapsed_ms);

//   成功：青色常亮
void Led_LocatingDone(void);

//   失败：红色常亮
void Led_LocatingFail(void);

#endif //ESP_TARGET_ROBOT_LED_H
