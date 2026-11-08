//
// ============================================================================
//  网页指令的共享状态（ControlState）
// ============================================================================
//
//  【它管什么】
//    网页（WebSocket 任务）写，控制任务（1ms）和链路任务（10Hz）读。
//    因为读写在不同任务/不同优先级上，必须加锁，否则会读到"改了一半"的数据。
//
//  【数据格式】
//    网页只发 4 个字段：
//      导轨转速, 导轨方向, 6020方向, 6020挡位
//    PID / 前馈参数写死在 C 板固件里，不经过这里。
//
//  【谁写 / 谁读】
//    写：web_server.cpp（收到网页消息）
//    读：horizontal_ctrl（导轨电机）、rail_link（转发给 C 板）
//
// ============================================================================

#include "control_state.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace
{
    // 网页 5Hz 发指令，5 秒没收到才算离线
    constexpr uint32_t kWebTimeoutMs = 5000;   // 网页 5 秒没指令才算离线（原来 1.5s，太容易误判）

    constexpr int16_t kRailRpmMin = 0;
    constexpr int16_t kRailRpmMax = 9000;

    int16_t s_rail_rpm = 3500;
    char    s_rail_dir = 'N';
    char    s_gm_dir   = 'S';
    uint8_t s_gm_gear  = 1;

    bool     s_ws_connected = false;

    // 控制权切换计数（网页每"接管"一次 +1，buzzer 任务据此响提示音）
    volatile uint32_t s_takeover_count = 0;

    // 工作模式：0xFF=未选择  0=默认(自动来回)  1=无线控制
    //   上电必须是"未选择"：连上网但还没点按钮时，任何电机都不能动。
    uint8_t  s_mode = 0xFF;

    // 是否有网页持有控制权
    bool     s_controller_active = false;

    // 最近一次收到网页指令的时刻（ms）。
    //   ★ 0 是哨兵值 = 上电以来还没收到过任何指令。
    //   不能用 0 直接参与 (now - 0) < 超时 的判断 ——
    //   那样上电后前 5 秒会被误判成网页在线，
    //   蜂鸣器就会在开机时误报一次连接断开。
    uint32_t s_last_cmd_ms  = 0;

    portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

    inline uint32_t nowMs()
    {
        return static_cast<uint32_t>(xTaskGetTickCount()) * portTICK_PERIOD_MS;
    }
}

void ControlState_Init()
{
    s_rail_rpm = 3500;
    s_rail_dir = 'N';
    s_gm_dir   = 'S';
    s_gm_gear  = 1;

    s_ws_connected = false;
    s_last_cmd_ms  = 0;
}

void ControlState_SetFromWeb(int16_t rail_rpm, char rail_dir, char gm_dir, uint8_t gm_gear)
{
    if (rail_rpm < kRailRpmMin) rail_rpm = kRailRpmMin;
    if (rail_rpm > kRailRpmMax) rail_rpm = kRailRpmMax;

    if (rail_dir != 'L' && rail_dir != 'R') rail_dir = 'N';
    if (gm_dir   != 'F' && gm_dir   != 'R') gm_dir   = 'S';
    if (gm_gear  <  1   || gm_gear  >  4  ) gm_gear  = 1;

    taskENTER_CRITICAL(&s_lock);
    s_rail_rpm    = rail_rpm;
    s_rail_dir    = rail_dir;
    s_gm_dir      = gm_dir;
    s_gm_gear     = gm_gear;
    s_last_cmd_ms = nowMs();
    taskEXIT_CRITICAL(&s_lock);
}

void ControlState_SetMode(uint8_t mode)
{
    uint8_t m = 0xFF;                       // 默认：未选择
    if (mode == 0)      m = 0;
    else if (mode == 1) m = 1;

    taskENTER_CRITICAL(&s_lock);
    s_mode = m;
    taskEXIT_CRITICAL(&s_lock);
}

uint8_t ControlState_GetMode()
{
    return s_mode;
}

void ControlState_SetControllerActive(bool active)
{
    taskENTER_CRITICAL(&s_lock);
    s_controller_active = active;
    taskEXIT_CRITICAL(&s_lock);
}

bool ControlState_IsControllerActive()
{
    return s_controller_active;
}

void ControlState_BumpTakeover()
{
    s_takeover_count = s_takeover_count + 1;
}

uint32_t ControlState_GetTakeoverCount()
{
    return s_takeover_count;
}

void ControlState_SetWsConnected(bool connected)
{
    taskENTER_CRITICAL(&s_lock);
    s_ws_connected = connected;
    if (connected)
    {
        s_last_cmd_ms = nowMs();
    }
    taskEXIT_CRITICAL(&s_lock);
}

ControlSnapshot ControlState_Get()
{
    ControlSnapshot snap{};

    taskENTER_CRITICAL(&s_lock);
    snap.rail_rpm = s_rail_rpm;
    snap.rail_dir = s_rail_dir;
    snap.gm_dir   = s_gm_dir;
    snap.gm_gear  = s_gm_gear;
    snap.mode     = s_mode;
    // 只认"网页最近有没有发指令" —— 比 WebSocket 握手标志可靠。
    // s_last_cmd_ms == 0 表示"上电以来还没收到过指令"，直接判离线，
    // 否则开机前 5 秒会因为 (now - 0) 很小而被误判成在线。
    const bool recent = (s_last_cmd_ms != 0)
                     && ((nowMs() - s_last_cmd_ms) < kWebTimeoutMs);

    // "在线" 的判定：有网页连着（不管有没有控制权），或者最近收到过指令。
    //   为什么把"网页连着"也算在线：没控制权的网页不发控制指令，
    //   如果因此判成离线，导轨会自动进入"离线来回"模式跑起来 —— 很危险。
    snap.online = s_ws_connected || recent;
    taskEXIT_CRITICAL(&s_lock);

    return snap;
}

const char* ControlState_GmDirName(char d)
{
    switch (d)
    {
        case 'F': return "FORWARD";
        case 'R': return "REVERSE";
        case 'S': return "STOP";
        default:  return "?";
    }
}
