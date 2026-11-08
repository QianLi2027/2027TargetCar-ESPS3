#include "rail_link.h"

#include <cstdio>

#include "lwip/sockets.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "control_state.h"

namespace
{
    constexpr uint16_t kPort          = 3333;
    constexpr uint32_t kPushPeriodMs  = 100;    // 10Hz 推送
    constexpr uint32_t kTaskStack     = 4096;
    constexpr UBaseType_t kTaskPrio   = 5;
    constexpr BaseType_t  kTaskCore   = 0;      // 网络任务放 Core 0,电机放core 1

    // 发不出去最多等 1 秒,别把任务卡死
    constexpr uint32_t kSendTimeoutMs = 1000;

    // 当前有没有靶车(ESP-01S)连着 TCP。板上指示灯读它。
    volatile bool s_clientUp = false;

    void linkTask(void* arg)
    {
        (void)arg;

        // ── 建监听 socket ──
        const int listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listen_fd < 0)
        {
            vTaskDelete(nullptr);
            return;
        }

        const int yes = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        sockaddr_in addr = {};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port        = htons(kPort);

        if (bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
        {
            close(listen_fd);
            vTaskDelete(nullptr);
            return;
        }

        if (listen(listen_fd, 1) != 0)
        {
            close(listen_fd);
            vTaskDelete(nullptr);
            return;
        }

        for (;;)
        {
            // 等 ESP-01S 连
            sockaddr_in peer = {};
            socklen_t   plen = sizeof(peer);

            const int fd = accept(listen_fd, reinterpret_cast<sockaddr*>(&peer), &plen);
            if (fd < 0)
            {
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }

            // 发送超时
            timeval tv = {};
            tv.tv_sec  = kSendTimeoutMs / 1000;
            tv.tv_usec = (kSendTimeoutMs % 1000) * 1000;
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

            s_clientUp = true;

            for (;;)
            {
                const ControlSnapshot s = ControlState_Get();

                // 网页断开超过 0.8 秒 自动断开
                static uint32_t no_page_since = 0;
                const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
                if (s.online) {
                    no_page_since = 0;
                } else if (no_page_since == 0) {
                    no_page_since = now_ms;
                }
                const bool page_lost = (no_page_since != 0)
                                    && ((now_ms - no_page_since) >= 800);

                char    gm_dir  = s.gm_dir;
                uint8_t gm_gear = s.gm_gear;

                if (s.mode == 0) {
                    gm_dir  = 'F';
                    gm_gear = 1;
                }

                const bool can_run =
                    (s.mode == 0) ||
                    (s.mode == 1 && !page_lost && ControlState_IsControllerActive());

                char buf[32];
                int  n;
                if (can_run) {
                    n = snprintf(buf, sizeof(buf), "%s,%u\n",
                                 ControlState_GmDirName(gm_dir),
                                 (unsigned)gm_gear);
                } else {
                    n = snprintf(buf, sizeof(buf), "IDLE,0\n");
                }

                if (send(fd, buf, n, 0) <= 0)
                {
                    break;
                }

                vTaskDelay(pdMS_TO_TICKS(kPushPeriodMs));
            }

            s_clientUp = false;
            close(fd);
        }
    }
}

bool RailLink_IsConnected()
{
    return s_clientUp;
}

esp_err_t RailLink_Start()
{
    if (xTaskCreatePinnedToCore(linkTask, "rail_link", kTaskStack,nullptr, kTaskPrio, nullptr, kTaskCore) != pdPASS)
    {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
