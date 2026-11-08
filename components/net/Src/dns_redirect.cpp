//
// DNS 重定向实现 —— 一个极简的"万能 DNS 服务器"
//
// 收到的每一个 A 记录查询,都回答同一个 IP(板子自己的)。
// 所以手机访问任何网址,都会被引导到板子上。
//

#include "dns_redirect.h"

#include <cstring>

#include "esp_log.h"
#include "esp_netif.h"

#include "lwip/sockets.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace
{
    constexpr const char* TAG = "DnsRedirect";

    constexpr uint16_t kDnsPort   = 53;
    constexpr int      kMaxPacket = 256;

    constexpr uint32_t kTaskStack    = 4096;
    constexpr UBaseType_t kTaskPrio  = 5;
    constexpr BaseType_t  kTaskCore  = 0;   // 网络任务放 Core 0

    // 所有查询都回答这个 IP(启动时从 AP 接口读出来)
    uint32_t s_answer_ip = 0;
    bool     s_started   = false;

    // ──────────────────────────────────────────────────────────────────────
    //  收到一个 DNS 查询包,组一个回答
    //
    //  请求结构:
    //    [0..11]   头部(12 字节)
    //    [12..]    问题段:QNAME(变长) + QTYPE(2) + QCLASS(2)
    //
    //  回答结构:
    //    [0..11]   头部
    //    [12..]    问题段(原样抄回来)
    //    [..]      答案段:名字指针 + TYPE + CLASS + TTL + RDLENGTH + IP
    //
    //  返回:回复包总长度;<=0 表示这个包不用回
    // ──────────────────────────────────────────────────────────────────────
    int buildReply(const uint8_t* req, int req_len, uint8_t* out, int out_max)
    {
        if (req_len < 12 || req_len > out_max)
        {
            return -1;
        }

        // ── 找 QNAME 的结尾 ──
        //   QNAME 是 "www.example.com" 这种形式,存成:
        //     [3]www [7]example [3]com [0]
        //   每个标签前面一个长度字节,最后以 0 结束。
        int pos = 12;
        while (pos < req_len && req[pos] != 0)
        {
            pos += req[pos] + 1;   // 跳过"长度字节 + 内容"
        }
        if (pos + 5 > req_len)     // 0 + TYPE(2) + CLASS(2)
        {
            return -1;
        }

        // ── 只回答 A 记录(IPv4 地址,type = 1)──
        //   AAAA(IPv6)、MX 之类的不管,直接丢掉。
        const uint16_t qtype = static_cast<uint16_t>((req[pos + 1] << 8) | req[pos + 2]);
        if (qtype != 1)
        {
            return -1;
        }

        const int q_end = pos + 5;   // 问题段结束位置

        // ── 组回复:先把头部 + 问题段原样抄过来 ──
        std::memcpy(out, req, q_end);

        // 头部标志位
        out[2] = static_cast<uint8_t>((req[2] & 0x01) | 0x80 | 0x04);
        //        保留请求的 RD ───┘        │      └─ AA(authoritative answer)=1
        //                                   └─ QR(这是响应)=1
        out[3] = 0x80;   // RA(支持递归)=1,RCODE(无错误)=0

        // 计数:1 个问题、1 个答案、0 个权威、0 个附加
        out[4] = 0; out[5] = 1;
        out[6] = 0; out[7] = 1;
        out[8] = 0; out[9] = 0;
        out[10] = 0; out[11] = 0;

        // ── 追加答案段 ──
        int n = q_end;

        out[n++] = 0xC0; out[n++] = 0x0C;   // 名字指针:指回偏移 12(就是上面的 QNAME)
        out[n++] = 0x00; out[n++] = 0x01;   // TYPE  = A
        out[n++] = 0x00; out[n++] = 0x01;   // CLASS = IN
        out[n++] = 0x00; out[n++] = 0x00;   // TTL 高 16 位
        out[n++] = 0x00; out[n++] = 0x3C;   // TTL = 60 秒
        out[n++] = 0x00; out[n++] = 0x04;   // RDLENGTH = 4 字节
        std::memcpy(&out[n], &s_answer_ip, 4);   // IP 地址
        n += 4;

        return n;
    }

    void dnsTask(void* arg)
    {
        (void)arg;

        // ── 建一个 UDP socket,绑到 53 端口 ──
        const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sock < 0)
        {
            ESP_LOGE(TAG, "socket() 失败: errno=%d", errno);
            vTaskDelete(nullptr);
            return;
        }

        sockaddr_in addr = {};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port        = htons(kDnsPort);

        if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        {
            ESP_LOGE(TAG, "bind(53) 失败: errno=%d", errno);
            close(sock);
            vTaskDelete(nullptr);
            return;
        }


        uint8_t rx[kMaxPacket];
        uint8_t tx[kMaxPacket];

        for (;;)
        {
            sockaddr_in from = {};
            socklen_t   from_len = sizeof(from);

            const int len = recvfrom(sock, rx, sizeof(rx), 0,
                                     reinterpret_cast<sockaddr*>(&from), &from_len);
            if (len <= 0)
            {
                continue;
            }

            const int reply_len = buildReply(rx, len, tx, sizeof(tx));
            if (reply_len > 0)
            {
                sendto(sock, tx, reply_len, 0,
                       reinterpret_cast<sockaddr*>(&from), from_len);
            }
        }
    }
}

esp_err_t DnsRedirect_Start()
{
    if (s_started)
    {
        return ESP_OK;
    }

    // 从 AP 网络接口读出板子自己的 IP(默认 192.168.4.1)
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (netif == nullptr)
    {
        ESP_LOGE(TAG, "找不到 WIFI_AP_DEF 接口 —— WifiAp_Start() 调了吗?");
        return ESP_ERR_INVALID_STATE;
    }

    esp_netif_ip_info_t ip_info = {};
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK)
    {
        ESP_LOGE(TAG, "读取 AP 的 IP 失败");
        return ESP_FAIL;
    }
    s_answer_ip = ip_info.ip.addr;

    if (xTaskCreatePinnedToCore(dnsTask, "dns_redirect", kTaskStack,
                                nullptr, kTaskPrio, nullptr, kTaskCore) != pdPASS)
    {
        ESP_LOGE(TAG, "创建 DNS 任务失败");
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    return ESP_OK;
}
