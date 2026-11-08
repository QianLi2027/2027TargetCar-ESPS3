
#include "horizontal_ctrl.h"
#include "speed_pid.h"
#include "can_app.h"
#include "control_state.h"
#include "rail_link.h"
#include "locating.h"
#include "led.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

    constexpr float Rotor_Dt = 0.001f;
    constexpr TickType_t Rotor_Period = pdMS_TO_TICKS(1);

    constexpr int16_t  Auto_Speed            = 8000;

    // 往返范围百分比。

    constexpr float    Auto_Low_Percent      = 0.06f;
    constexpr float    Auto_High_Percent     = 0.94f;

    // 自动来回必须落在软限位里面，否则 Routine 永远等不到"到站"
    static_assert(Auto_Low_Percent  >= Limit_Low_Percent,  "Auto_Low_Percent 超出软限位");
    static_assert(Auto_High_Percent <= Limit_High_Percent, "Auto_High_Percent 超出软限位");

    constexpr uint32_t Pause_Time_ms         = 800;    // 每次换向先停 0.8 秒

    // 掉线保护
    constexpr uint32_t Page_Hold_Time_ms = 800;

    //  任务参数
    constexpr uint32_t   Task_Stack    = 4096;  // 字节。注意 ESP-IDF 是字节,
                                                // 不是 CMSIS 那种"字"(4字节)
    constexpr UBaseType_t Task_Priority = 20;   // 0~24,超了会 assert 重启
    constexpr UBaseType_t Task_Core     = 1;    // 钉在 Core 1,和 WiFi(Core 0)分开

    inline uint32_t nowMs()
    {
        return static_cast<uint32_t>(xTaskGetTickCount()) * portTICK_PERIOD_MS;
    }
}


static SpeedPID rotor1_speed_pid(5.0f, 1.0f, 0.00f, 10000.0f, 2500.0f);
static SpeedPID rotor2_speed_pid(5.0f, 1.0f, 0.00f, 10000.0f, 2500.0f);

//  任务主体
void Horizontal_Control_Task::run() {
    rotor1_speed_pid.Clear();
    rotor2_speed_pid.Clear();

    int16_t target_speed = 0;

    // 默认模式状态机
    enum State {
        Pause,
        Routine
    };

    State auto_state         = Pause;
    int16_t   auto_dir       = 1;                  // +1 = 朝墙, -1 = 远离墙
    uint32_t  state_ms       = 0;                  // 当前阶段的起点时刻

    bool was_located = false;                      // 上一拍是否已定零（抓切换瞬间）

    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {

        // 接受网页命令
        const ControlSnapshot cmd = ControlState_Get();
        const uint32_t now = nowMs();

        // 掉线保护
        static uint32_t no_page_since = 0;
        if (cmd.online) {
            no_page_since = 0;
        } else if (no_page_since == 0) {
            no_page_since = now;
        }
        const bool page_lost = (no_page_since != 0) && ((now - no_page_since) >= Page_Hold_Time_ms);

        const bool located = Locating_IsLocated();

        // 刚定零成功：状态机和 PID 清到起点，默认模式从"停 0.8s"干净开始
        if (located && !was_located) {
            auto_state = Pause;
            auto_dir   = 1;
            state_ms   = now;
            rotor1_speed_pid.Clear();
            rotor2_speed_pid.Clear();
        }
        was_located = located;

        {
            static uint8_t prev_mode = 0xFF;
            if (prev_mode != cmd.mode) {
                if (cmd.mode == 0) {
                    auto_state = Pause;
                    auto_dir   = 1;
                    state_ms   = now;
                }
                prev_mode = cmd.mode;
            }
        }

        if (located) {
            if (cmd.mode == 0xFF) {
                /* ================= 未选择：停 ================= */
                target_speed = 0;
                rotor1_speed_pid.Clear();
                rotor2_speed_pid.Clear();

                auto_state = Pause;
                auto_dir   = 1;
                state_ms   = now;

            } else if (cmd.mode == 1) {
                /* ================= 无线控制：听网页 ================= */
                if (page_lost) {
                    rotor1_speed_pid.Clear();
                    rotor2_speed_pid.Clear();
                    target_speed = 0;
                } else if (cmd.rail_dir == 'L') {
                    target_speed = static_cast<int16_t>(-cmd.rail_rpm);
                } else if (cmd.rail_dir == 'R') {
                    target_speed = cmd.rail_rpm;
                } else {
                    target_speed = 0;
                }

            } else {
                /* ================= 默认模式：自动来回 ================= */
                switch (auto_state) {

                    case Pause:
                        target_speed = 0;
                        if ((now - state_ms) >= Pause_Time_ms) {
                            auto_dir   = static_cast<int16_t>(-auto_dir);
                            state_ms   = now;
                            auto_state = Routine;
                        }
                        break;

                    case Routine: {
                        target_speed = static_cast<int16_t>(auto_dir * Auto_Speed);

                        const int32_t travel = Travel_Total_Counts;
                        const int32_t pos_hi = static_cast<int32_t>(travel * Auto_High_Percent );
                        const int32_t pos_lo = static_cast<int32_t>(travel * Auto_Low_Percent  );

                        // auto_dir < 0 : 远离墙，位置变大 → 去 pos_hi
                        // auto_dir > 0 : 朝墙，  位置变小 → 去 pos_lo
                        const int32_t pos_now = Position_Counts();
                        bool arrived = (auto_dir < 0) ? (pos_now >= pos_hi)
                                                      : (pos_now <= pos_lo);

                        if (arrived) {
                            auto_state   = Pause;
                            state_ms     = now;
                            target_speed = 0;
                            rotor1_speed_pid.Clear();
                            rotor2_speed_pid.Clear();
                        }
                        break;
                    }
                }
            }

            // 软限位
            if (Position_SoftLimitHit(target_speed)) {
                target_speed = 0;
            }

            int16_t current1 = static_cast<int16_t>(rotor1_speed_pid.Calculate( target_speed, CAN_DjiMotor_GetVelocity(1), Rotor_Dt));
            int16_t current2 = static_cast<int16_t>(rotor2_speed_pid.Calculate(-target_speed, CAN_DjiMotor_GetVelocity(2), Rotor_Dt));

            if (!CAN_DjiMotor_IsOnline(1)) current1 = 0;
            if (!CAN_DjiMotor_IsOnline(2)) current2 = 0;

            CAN_DjiMotor_SetCurrent(1, current1);
            CAN_DjiMotor_SetCurrent(2, current2);
            CAN_DjiGroup_SendCurrent(1);
        } else if ((now - Locating_BeatMs()) > 100) {
            // 定零任务没心跳（没起来 / 卡死）：兜底把电流清零，
            // 别让电调一直保持定零时发出的那条指令
            CAN_DjiMotor_SetCurrent(1, 0);
            CAN_DjiMotor_SetCurrent(2, 0);
            CAN_DjiGroup_SendCurrent(1);
        }

        Led_UpdateMode(cmd.mode,cmd.online,RailLink_IsConnected(),CAN_DjiMotor_IsOnline(1) || CAN_DjiMotor_IsOnline(2));

        vTaskDelayUntil(&last_wake, Rotor_Period);
    }
}


extern "C" {
    static Horizontal_Control_Task rotor_task;

    void Horizontal_Control_Task_Init() {
        rotor_task.start("HorizontalTask", Task_Stack, Task_Priority, Task_Core);
    }
}
