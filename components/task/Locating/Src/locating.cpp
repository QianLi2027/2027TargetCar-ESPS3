#include "locating.h"

#include "can_app.h"
#include "led.h"
#include "buzzer.h"
#include "speed_pid.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cmath>

namespace {
    constexpr int16_t  Locating_Speed      = 1200;    // 定位速度
    constexpr uint32_t Locating_Timeout_ms = 30000;   // 定位最长时间
    constexpr uint32_t Run_Grace_ms        = 800;     // 出发后 0.8s 内不判堵转

    // 堵转判定：转速低 + 电流大，连续成立一段时间才算撞到墙
    constexpr float    Stall_Speed_Rpm  = 50.0f;
    constexpr float    Stall_Current    = 3000.0f;
    constexpr uint32_t Stall_Confirm_ms = 400;

    // 软限位（计数）
    constexpr int32_t Limit_Min =
        static_cast<int32_t>(Travel_Total_Counts * Limit_Low_Percent);
    constexpr int32_t Limit_Max =
        static_cast<int32_t>(Travel_Total_Counts * Limit_High_Percent);

    // 定零任务
    constexpr uint32_t    Task_Stack     = 3072;
    constexpr UBaseType_t Task_Priority  = 19;      // 比控制环(20)低一级
    constexpr BaseType_t  Task_Core      = 1;
    constexpr uint32_t    Idle_Poll_ms   = 50;      // 已回零时多久醒一次

    LocateState s_state       = LocateState::NotLocated;   // 定零状态
    bool        s_busy        = false;                   // 正在找墙（未回零的瞬态）
    uint32_t    s_retry_at_ms = 0;                       // 失败后，到这个时刻再试
    int32_t     s_zero_counts = 0;                       // 回零点的累计计数

    volatile bool     s_request = false;   // 外部请求重新回零
    volatile uint32_t s_beat_ms = 0;

    // 一次找墙的过程量
    uint32_t s_step_t0    = 0;    // 本次找墙开始时刻
    uint32_t s_step_stall = 0;    // 堵转条件连续成立的起点（0 = 未堵）

    enum StepResult : uint8_t
    {
        STEP_RUNNING = 0,
        STEP_DONE,
        STEP_TIMEOUT,
    };

    SpeedPID motor1_pid(5.0f, 1.0f, 0.00f, 10000.0f, 2500.0f);
    SpeedPID motor2_pid(5.0f, 1.0f, 0.00f, 10000.0f, 2500.0f);

    inline uint32_t nowMs()
    {
        return static_cast<uint32_t>(xTaskGetTickCount()) * portTICK_PERIOD_MS;
    }

    void resetZero()
    {
        s_zero_counts = CAN_DjiMotor_GetTotalAngle(1);
    }

    void stepBegin(uint32_t now_ms)
    {
        s_step_t0    = now_ms;
        s_step_stall = 0;
        s_state      = LocateState::NotLocated;   // 回零过程中仍然算"未回零"
        s_busy       = true;
    }

    StepResult stepTick(uint32_t now_ms, int16_t& out_speed)
    {
        out_speed = Locating_Speed;

        if ((now_ms - s_step_t0) >= Locating_Timeout_ms) {
            out_speed = 0;
            return STEP_TIMEOUT;
        }

        if ((now_ms - s_step_t0) >= Run_Grace_ms) {
            const float speed   = std::abs(CAN_DjiMotor_GetVelocity(1));
            const float current = std::abs(CAN_DjiMotor_GetCurrent(1));

            if (speed < Stall_Speed_Rpm && current > Stall_Current) {
                if (s_step_stall == 0) {
                    s_step_stall = now_ms;
                } else if ((now_ms - s_step_stall) >= Stall_Confirm_ms) {
                    out_speed = 0;
                    resetZero();
                    return STEP_DONE;
                }
            } else {
                s_step_stall = 0;
            }
        }

        return STEP_RUNNING;
    }

    // 一次找墙失败：状态留在"未回零"，5 秒后再试
    void failToNotLocated(uint32_t now_ms)
    {
        s_state       = LocateState::NotLocated;
        s_busy        = false;
        s_retry_at_ms = now_ms + Locating_Retry_ms;
    }

    inline void beat()
    {
        s_beat_ms = nowMs();
    }

    void locatingTask(void* arg)
    {
        (void)arg;

        for (;;) {
            if (Locating_IsLocated() && !s_request) {
                beat();
                vTaskDelay(pdMS_TO_TICKS(Idle_Poll_ms));
                continue;
            }

            // 未回零
            const uint32_t now = nowMs();
            if (!s_request && static_cast<int32_t>(s_retry_at_ms - now) > 0) {
                beat();
                vTaskDelay(pdMS_TO_TICKS(Idle_Poll_ms));
                continue;
            }

            // 打回"未回零"，重新跑一轮（阻塞，最多 30 秒）
            s_request = false;
            (void)Locating_Run(false);
            s_request = false;      // 回零过程中收到的请求一并吞掉

            beat();
        }
    }

}

// 定零点
esp_err_t Locating_Run(bool paint_led)
{
    motor1_pid.Clear();
    motor2_pid.Clear();

    const uint32_t t0 = nowMs();
    stepBegin(t0);

    int16_t    current1 = 0;
    int16_t    current2 = 0;
    StepResult step_result = STEP_RUNNING;

    for (;;) {
        const uint32_t now = nowMs();
        s_beat_ms = now;

        // 定位过程闪红灯
        if (paint_led) Led_LocatingProgress(now - t0);

        int16_t loc_speed = 0;
        step_result = stepTick(now, loc_speed);

        if (step_result != STEP_RUNNING) {
            current1 = 0;
            current2 = 0;
        } else if (!(CAN_DjiMotor_IsOnline(1) && CAN_DjiMotor_IsOnline(2))) {
            current1 = 0;
            current2 = 0;
            motor1_pid.Clear();
            motor2_pid.Clear();
        } else {
            current1 = static_cast<int16_t>(motor1_pid.Calculate(loc_speed, CAN_DjiMotor_GetVelocity(1), 0.001f));
            current2 = static_cast<int16_t>(motor2_pid.Calculate(-loc_speed, CAN_DjiMotor_GetVelocity(2), 0.001f));
        }

        CAN_DjiMotor_SetCurrent(1, current1);
        CAN_DjiMotor_SetCurrent(2, current2);
        CAN_DjiGroup_SendCurrent(1);

        if (step_result != STEP_RUNNING) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    CAN_DjiMotor_SetCurrent(1, 0);
    CAN_DjiMotor_SetCurrent(2, 0);
    for (int i = 0; i < 50; ++i) {
        CAN_DjiGroup_SendCurrent(1);
        if (paint_led) Led_LocatingProgress(nowMs() - t0);
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (step_result == STEP_DONE) {
        if (paint_led) Led_LocatingDone();   // 青色 回零成功
        Buzzer_PlayLocated();            // 嘀嘀
        vTaskDelay(pdMS_TO_TICKS(500));

        s_state = LocateState::Located;
        s_busy  = false;
        return ESP_OK;
    }

    // 超时
    failToNotLocated(nowMs());
    if (paint_led) Led_LocatingFail();       // 红色  回零失败
    return ESP_FAIL;
}

    void Locating_Task_Start(){
    xTaskCreatePinnedToCore(locatingTask, "locating", Task_Stack,nullptr, Task_Priority, nullptr, Task_Core);
}

    void Locating_Request(){
    s_request = true;
}

    uint32_t Locating_BeatMs(){
    return s_beat_ms;
}

    LocateState Locating_State(){
    return s_state;
}

    bool Locating_IsLocated(){
    return s_state == LocateState::Located;
}

    bool Locating_IsBusy(){
    return s_busy;
}

    int32_t Position_Counts(){
    return -(CAN_DjiMotor_GetTotalAngle(1) - s_zero_counts);
}

    bool Position_SoftLimitHit(int16_t target_speed){
    if (s_state != LocateState::Located) {
        return false;
    }

    if (target_speed == 0) {
        return false;
    }

    const int32_t dir = (target_speed > 0) ? -1 : 1;
    const int32_t pos = Position_Counts();

    if (dir > 0 && pos >= Limit_Max) return true;
    if (dir < 0 && pos <= Limit_Min) return true;

    return false;
}
