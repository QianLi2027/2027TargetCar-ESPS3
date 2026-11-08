# Device 层：具体外设（电机、指示灯、蜂鸣器）
#
#   CAN/            —— can_app / can_device_config / can_dispatcher
#   CAN/Motor/      —— motor_common.h / motor_manager
#   CAN/Motor/Inc/  —— dji_motor.h
#   CAN/Motor/Src/  —— dji_motor.cpp
#   Led/            —— 板载 WS2812（GPIO48，RMT）
#   Buzzer/         —— 无源蜂鸣器（GPIO9，LEDC）+ 连接状态监测
idf_component_register(
    SRCS
        "CAN/can_app.cpp"
        "CAN/can_device_config.cpp"
        "CAN/can_dispatcher.cpp"
        "CAN/Motor/Src/dji_motor.cpp"
        "CAN/Motor/motor_manager.cpp"
        "Led/Src/led.cpp"
        "Buzzer/Src/buzzer.cpp"
    INCLUDE_DIRS
        "CAN"
        "CAN/Motor"
        "CAN/Motor/Inc"
        "Led/Inc"
        "Buzzer/Inc"
    REQUIRES bsp freertos net task
        esp_driver_ledc esp_driver_gpio esp_driver_rmt
)
