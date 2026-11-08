//
// 小工具（与平台无关）
//
//   clamp   把值夹到 [min, max]，模板，任意可比较类型
//

#ifndef ESP_TARGET_ROBOT_MAGICTOOLS_H
#define ESP_TARGET_ROBOT_MAGICTOOLS_H

#pragma once

#ifdef __cplusplus
template <typename T>
inline T clamp(T value, T min_val, T max_val)
{
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}
#endif

#endif // ESP_TARGET_ROBOT_MAGICTOOLS_H
