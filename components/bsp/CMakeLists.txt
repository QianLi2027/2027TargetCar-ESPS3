# Bsp 层:板级支持包
# 对应 STM32 工程的 Bsp/CAN/,但底层从 HAL CAN 换成了 ESP-IDF 的 TWAI
#
#   Inc/  —— bsp_can.h
#   Src/  —— bsp_can.cpp
#   根目录 —— can_config.h / can_byte_utils.h(与平台无关的工具头)
idf_component_register(
    SRCS "Src/bsp_can.cpp"
    INCLUDE_DIRS "." "Inc"
    REQUIRES driver freertos
)
