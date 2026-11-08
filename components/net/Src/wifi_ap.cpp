//
// ============================================================================
//  WiFi 热点 (AP 模式)
// ============================================================================
//
//  【它干什么】
//    把 ESP32 变成一个 WiFi 热点,手机 / C 板的 ESP-01 连上来之后,
//    就能访问板子上的 HTTP 服务和 TCP 服务。
//
//  【AP 还是 STA?】
//    AP   = Access Point,开热点的那一方(路由器角色)。本文件就是。
//    STA  = Station,连别人热点的那一方(手机 / ESP-01)。
//    一个 ESP32 可以同时是 AP+STA(WIFI_MODE_APSTA),本工程不需要。
//
//  【连上之后板子的地址】
//    ESP-IDF 的 AP 模式默认 IP 固定是 **192.168.4.1**,
//    所以手机浏览器直接打开 http://192.168.4.1 就行。
//
//  【启动顺序(七步,不能乱)】
//    ① NVS 存储   → WiFi 驱动要存校准数据
//    ② 协议栈     → TCP/IP 层
//    ③ 事件循环   → WiFi 的各种事件要有地方投递
//    ④ 网络接口   → 挂一块"虚拟网卡",分配 192.168.4.1
//    ⑤ WiFi 驱动  → 初始化
//    ⑥ 填配置     → SSID / 密码 / 信道
//    ⑦ 设模式 → 应用 → 启动
//
//    ①②③④ 是 ESP-IDF 网络项目的固定套路,换任何网络示例都长这样;
//    真正"我要当 AP"的配置从第 ⑥ 步开始。
//
// ============================================================================

#include "wifi_ap.h"

#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

namespace
{
    constexpr const char* TAG = "WifiAp";

    constexpr const char* ID       = "TARGET_CAR";
    constexpr const char* PassWord = "cquqianli";

    // ── 信道 ──────────────────────────────────────────────────────────────
    //   2.4GHz 被切成 1~13 个信道,每个占 20MHz、间隔 5MHz,相邻的会互相干扰。
    //   真正互不干扰的只有 1 / 6 / 11,这里选中间的 6。
    //
    //   ★ 为什么特意标注 ★
    //     ESP-NOW 和 WiFi 共用同一个 2.4GHz 射频,双方必须在**同一信道**上,
    //     否则收不到对方。以后如果改用 ESP-NOW 直连从板,从板也要在信道 6。
    //     最省事的做法:从板用 STA 模式连上本热点,信道会自动同步。
    constexpr uint8_t WiFi_Channel = 6;
    constexpr uint8_t MaxConn = 4;
}

// ══════════════════════════════════════════════════════════════════════════
//  启动热点
// ══════════════════════════════════════════════════════════════════════════
esp_err_t WifiAp_Start()
{
    // ── ① 初始化 NVS(非易失存储)────────────────────────────────────────
    //   NVS 是 flash 上的一块键值存储,掉电不丢。
    //   WiFi 驱动要靠它存射频校准数据、连接记录等。
    esp_err_t err = nvs_flash_init();

    // 两种"存不了"的情况:空间满了、或者数据是旧格式。
    // 标准处理:整块擦掉重来(ESP-IDF 官方示例也是这么写的)。
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK)
    {
        // 这里手动检查而不是 ESP_ERROR_CHECK —— 因为前面还有补救逻辑
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return err;
    }

    // ── ② 初始化 TCP/IP 协议栈 ───────────────────────────────────────────
    //   没有这一步,后面建不了 socket,也起不了 HTTP 服务器。
    ESP_ERROR_CHECK(esp_netif_init());

    // ── ③ 创建默认事件循环 ───────────────────────────────────────────────
    //   WiFi 连上/断开/拿到 IP 都会发事件,得有地方投递。
    //   (本工程没注册处理函数,因为 AP 模式是"被连接",不需要监听状态)
    ESP_ERROR_CHECK(esp_event_loop_create_default());


    // ── ④ 创建 AP 网络接口 ───────────────────────────────────────────────
    //   挂一块虚拟网卡,并自动分配默认 IP 192.168.4.1。
    esp_netif_create_default_wifi_ap();

    // ── ⑤ 初始化 WiFi 驱动 ───────────────────────────────────────────────
    //   WIFI_INIT_CONFIG_DEFAULT() 展开成一整套默认参数
    //   (缓冲区数量、任务优先级等),99% 的场景直接用就行。
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    // ── ⑥ 填 AP 配置 ─────────────────────────────────────────────────────
    wifi_config_t wcfg = {};   // ★ 先清零,否则未赋值的字段是垃圾

    // 为什么用 memcpy 而不是 strcpy?
    //   wcfg.ap.ssid 是定长数组(uint8_t[32]),里面不一定有 '\0' 结尾。
    //   strcpy 会往后多写一个 '\0' 可能越界;memcpy 只拷指定长度,精确。
    std::memcpy(wcfg.ap.ssid, ID, std::strlen(ID));
    // 必须单独设长度 —— 因为数组里可能残留上次的垃圾字节
    wcfg.ap.ssid_len       = static_cast<uint8_t>(std::strlen(ID));

    std::memcpy(wcfg.ap.password, PassWord, std::strlen(PassWord));

    wcfg.ap.channel        = WiFi_Channel;          // 信道 6(见上面注释)
    wcfg.ap.max_connection = MaxConn;               // 最多 4 个设备

    // 加密方式。设成 WPA2_PSK 就必须有密码,否则 set_config 会失败。
    wcfg.ap.authmode       = WIFI_AUTH_WPA2_PSK;

    // ── ⑦ 设模式 → 应用配置 → 启动 ──────────────────────────────────────
    //   顺序不能换:必须先设成 AP 模式,才能配 WIFI_IF_AP;
    //   配置灌进去之后,最后 start 才真正开始发信标(手机才能搜到)。
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wcfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    // 到这一步,手机已经能搜到 ID 这个热点了
    return ESP_OK;
}
