# Algorithm 层:PID（纯头文件 —— SpeedPID 只是给 PidBase 起个名字）
# 对应 STM32 工程的 PID/
#   Inc/  —— speed_pid.h
#   根目录 —— PIDBase.h(speed_pid.h 依赖它)
#
# REQUIRES bsp —— PIDBase.h 里的 magictools.h 在 bsp 组件里
idf_component_register(
    INCLUDE_DIRS "." "Inc"
    REQUIRES bsp
)
