//
// ESP32 主板 —— 导轨(2×M3508)+ 网页主机
//
// 分工:
//   这块 ESP32   : 直接接 2×M3508 管导轨,同时开热点跑网页、当整套系统的主机
//   C 板 + ESP-01: 接 GM6020 管主体,通过 WiFi 连到这里收指令(TCP 3333)
//
// 控制逻辑在 Horizontal_Control_Task 里,app_main 只做初始化然后返回。
//
// ⚠️ 上电会先跑 Locating_Run()：电机慢速朝 + 方向顶墙（撞墙定零）。
//    没装真实导轨的话，一次找墙顶到 30s 超时；失败后控制环每 5 秒自动重试，
//    在定零成功之前导轨不会执行任何位移指令。
//

#include "esp_err.h"
#include "esp_log.h"

#include "can_app.h"
#include "led.h"
#include "buzzer.h"
#include "horizontal_ctrl.h"
#include "locating.h"

#include "control_state.h"
#include "rail_link.h"
#include "web_server.h"
#include "wifi_ap.h"
#include "dns_redirect.h"

static const char* TAG = "MAIN";

extern "C" void app_main(void)
{

    // 1) 指令状态先初始化
    ControlState_Init();

    // 2) 装配电机(2×M3508)+ 启动 TWAI + 拉起 CAN 接收任务
    esp_err_t err = CAN_Device_Init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "CAN_Device_Init failed: %s", esp_err_to_name(err));
        return;
    }

    // 3) 板载 WS2812 指示灯(GPIO48)—— 控制环按状态刷新
    if (Led_Init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Led_Init failed");
    }

    // 3.5) 提示模块：蜂鸣器 + 连接状态监测
    //      规则：上电后连上过、之后断链 -> 滴滴滴，滴滴滴（两组各三下，间隔 0.8s）
    //            一次都没连上过就不响
    if (Buzzer_Init() == ESP_OK) {
        Buzzer_Start();
    } else {
        ESP_LOGE(TAG, "Buzzer_Init failed");
    }

    // 3.6) ★ 上电回零（阻塞）——必须在 WiFi 之前！
    //      理由：回零期间电机会慢速顶墙。如果这时候已经能联网，
    //            别人点一下网页按钮就会和回零动作打架。
    //      回零中：红灯闪；成功：变青 + 蜂鸣器嘀—嘀两声。
    //      回零失败：状态回"未定零"，红灯常亮；定零任务每 5 秒自动重试，
    //                期间默认模式和网页控制都不输出（locating 的状态机管这个）。
    Locating_Run(true);   // 上电没人画灯，自己点

    // 3.7) 定零任务：一直活着，负责"失败重试"和"重新定零请求"
    Locating_Task_Start();

    // 4) 开热点:手机连 TARGET_CAR;C 板的 ESP-01 也连这个热点
    if (WifiAp_Start() != ESP_OK)
    {
        ESP_LOGE(TAG, "WifiAp_Start failed");
    }

    // 4.5) DNS 重定向(强制门户):手机连上热点后,系统探测外网会被引到板子上,
    //      从而自动弹出控制页面。必须在 WifiAp_Start() 之后(要读 AP 的 IP)。
    if (DnsRedirect_Start() != ESP_OK)
    {
        ESP_LOGE(TAG, "DnsRedirect_Start failed");
    }

    // 5) 起网页服务:浏览器访问 http://192.168.4.1
    if (WebServer_Start() != ESP_OK)
    {
        ESP_LOGE(TAG, "WebServer_Start failed");
    }

    // 6) 导轨无线链路:TCP 端口 3333,等 C 板的 ESP-01 连过来,推 6020 指令
    if (RailLink_Start() != ESP_OK)
    {
        ESP_LOGE(TAG, "RailLink_Start failed");
    }

    // 7) 起水平控制任务(定零状态门 + 双速度环 + 默认模式自动来回)
    Horizontal_Control_Task_Init();

}
