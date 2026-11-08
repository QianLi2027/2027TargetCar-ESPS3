# ============================================================================
#  Task 层 —— 板子的"功能模块"，每个功能一个子目录
# ============================================================================
#
#   Horizontal_ctrl/  导轨水平控制（2×M3508 双速度环 + 定零状态门 + 自动来回）
#   Locating/         定零状态机（未定零 / 正在回零 / 已定零）
#   Communication/    与 C 板的无线链路（TCP 3333：只发 6020 方向/挡位）
#
#   Led / Buzzer 属于设备层，在 components/device/ 下。
#
#   每个子目录都是：
#       Inc/xxx.h     对外接口（只有几个函数）
#       Src/xxx.cpp   实现
#
#   依赖方向：Task → Net（网页/状态）/ Device（电机、灯、蜂鸣器）/ Algorithm（PID）
# ============================================================================
idf_component_register(
    SRCS
        "Horizontal_ctrl/Src/horizontal_ctrl.cpp"
        "Locating/Src/locating.cpp"
        "Communication/Src/rail_link.cpp"
    INCLUDE_DIRS
        "."
        "Horizontal_ctrl/Inc"
        "Locating/Inc"
        "Communication/Inc"
    REQUIRES
        device algorithm net freertos
)
