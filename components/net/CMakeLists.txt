# Net 层:WiFi 热点 + 网页 + DNS 重定向 + 导轨无线链路
#
#   Inc/  —— 对外头文件
#   Src/  —— 实现
idf_component_register(
    SRCS
        "Src/control_state.cpp"
        "Src/wifi_ap.cpp"
        "Src/web_server.cpp"
        "Src/dns_redirect.cpp"
    INCLUDE_DIRS "." "Inc"
    REQUIRES esp_wifi esp_http_server nvs_flash lwip freertos
)
