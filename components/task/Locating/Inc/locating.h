#ifndef ESP_HORIZONTAL_CONTROL_LOCATING_H
#define ESP_HORIZONTAL_CONTROL_LOCATING_H

#pragma once

#include "esp_err.h"
#include <cstdint>

// 定零点状态：只有两个
enum class LocateState : uint8_t
{
    NotLocated = 0,   // 未回零：位置不可信，只有"回零"能动机
    Located,          // 已回零：位置可信，交给控制环
};

constexpr uint32_t Locating_Retry_ms = 5000;   // 一次回零失败后，隔多久再试

// 阻塞跑一次回零（最多 30 秒）。
//   ESP_OK   = 撞到墙了，零点已建立，状态变 Located
//   ESP_FAIL = 超时，状态留在 NotLocated，5 秒后再试
esp_err_t Locating_Run(bool paint_led);

// 起定零任务：一直活着。
//   已回零  → 每 50ms 醒一次，几乎不干活
//   未回零  → 阻塞跑一次 Locating_Run()，失败等 5 秒再来
//   收到 Locating_Request() → 状态打回"未回零"，重新来一轮
void Locating_Task_Start();

// 请求重新回零：任何条件都可以调（故障检测 / 网页按钮 / 重新标定 …）。
//   下一个任务醒来时就会把状态打回"未回零"并重新跑一轮。
void Locating_Request();

// 定零任务的心跳（毫秒时间戳）。
//   控制环用它判断任务还活着：未回零时心跳超过 100ms 没更新，
//   就自己发 0 电流兜底，别让电调一直保持最后一条指令。
uint32_t Locating_BeatMs();

LocateState Locating_State();
bool        Locating_IsLocated();

// 正在找墙（"未回零"内部的瞬态，灯拿它决定红闪还是红常亮）
bool Locating_IsBusy();

// ── 位置查询（相对回零点）
int32_t Position_Counts();

//软限位 ，手动测量
constexpr int32_t Travel_Total_Counts = 4480261;

// 软限位范围（总行程的百分比）。
//   ⚠️ 默认模式的往返范围（horizontal_ctrl 里的 Auto_* ）不要超出这个区间，
//      否则"到站"永远等不到，会一直顶着软限位。
constexpr float Limit_Low_Percent  = 0.06f;
constexpr float Limit_High_Percent = 0.94f;

bool Position_SoftLimitHit(int16_t target_speed);

#endif //ESP_HORIZONTAL_CONTROL_LOCATING_H
