//
// 网页指令的共享状态(导轨主机版)
//
// 网页只发 4 个字段:
//   导轨转速, 导轨方向, 6020方向, 6020挡位
//
// 导轨板自己用前两个(直接驱动 2×M3508),
// 后两个通过 TCP 转发给 C 板(它管 GM6020)。
//
// 谁写:HTTP/WebSocket 任务(收到网页消息时)
// 谁读:控制任务(每 1ms)+ 导轨链路任务(10Hz)
// 所以用临界区加锁。
//

#ifndef ESP_HORIZONTAL_CONTROL_CONTROL_STATE_H
#define ESP_HORIZONTAL_CONTROL_CONTROL_STATE_H

#pragma once

#include <cstdint>

struct ControlSnapshot
{
    // ── 导轨(2×M3508)—— 本板自己用
    int16_t rail_rpm;    // 0 ~ 9000
    char    rail_dir;    // 'L' 左 / 'R' 右 / 'N' 停

    // ── 主体(GM6020)—— 转发给 C 板
    char    gm_dir;      // 'F' 正转 / 'S' 停 / 'R' 反转
    uint8_t gm_gear;     // 1=低 2=中 3=高

    // ── 工作模式（网页上选，不再靠"链路断没断"自动切换）
    //    0 = 默认      导轨自动来回，靶车停
    //    1 = 无线控制  导轨和靶车都听网页
    // 0xFF=未选择  0=默认(自动来回)  1=无线控制
    uint8_t mode;

    // 网页在线（有没有网页连着，用于蜂鸣器提示）
    bool    online;

};

void ControlState_Init();

void ControlState_SetFromWeb(int16_t rail_rpm, char rail_dir, char gm_dir, uint8_t gm_gear);

// 工作模式：0=默认(导轨自动来回)  1=无线控制
void ControlState_SetMode(uint8_t mode);
uint8_t ControlState_GetMode();

// 是否有网页持有控制权（没有的话，链路任务只发 IDLE 保活）
void ControlState_SetControllerActive(bool active);
bool ControlState_IsControllerActive();

void ControlState_SetWsConnected(bool connected);


// ── 控制权切换事件 ──────────────────────────────────────
//   网页点"接管控制"时 +1。
//   为什么用计数而不是直接调蜂鸣器：net 组件不能依赖 task 组件（会循环），
//   所以 net 只负责计数，notify 任务轮询这个数，发现变了就响。
void     ControlState_BumpTakeover();
uint32_t ControlState_GetTakeoverCount();

ControlSnapshot ControlState_Get();

// 方向转可读名字(链路任务用)
const char* ControlState_GmDirName(char d);   // "FORWARD" / "STOP" / "REVERSE"

#endif //ESP_HORIZONTAL_CONTROL_CONTROL_STATE_H
