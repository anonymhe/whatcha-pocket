// components/bsp/include/bsp_battery.h
// CellWise CW2017 电量计:I2C 0x63,与 ES8311 共用总线。
// BSP 写入自定义电池 profile,芯片直接给 SOC%,无需外部分压电阻与查表。
#pragma once

#include "esp_err.h"

// 初始化。内部会调 bsp_i2c_init()(幂等)。
// 芯片不应答时返回 ESP_ERR_NOT_FOUND —— 上层可据此在 UI 上标记该项不可用。
esp_err_t bsp_battery_init(void);

// 剩余电量百分比 0..100;读失败返回 -1。
int bsp_battery_soc(void);

// 电池电压 mV;读失败返回 -1。
int bsp_battery_mv(void);

// 电压的 14 bit 原始值(每 LSB = 312.5 µV);读失败返回 -1。
// 用途只有一个:诊断充电末端。1 mV 已经等于 3.2 个 LSB,而满充附近端电压
// 十几分钟才变化几个 LSB —— 用 mV 观察会把整条曲线抹平成一个常数。
int bsp_battery_mv_raw(void);

// 剩余电量以 1/256% 为单位的原始 16 bit 值;读失败返回 -1。
// 高字节即 bsp_battery_soc(),低字节是小数部分,用来分辨「芯片算到顶了」
// 和「小数点后还在慢慢涨」。
int bsp_battery_soc_raw(void);

// 电芯温度,0.1 °C 为单位;读失败返回 -1。
// CW2017 的 TEMP(0x06) 是无符号 8 bit、LSB=0.5 °C、量程 -40..87.5 °C,
// 手册换算为 TEMP(°C) = -40 + Value/2。
int bsp_battery_temp_tenths(void);

// 省电:令 CW2017 进入休眠(写 CONFIG=SLEEP 并回读校验,失败重试一次)。
// 适合长时间低功耗期间停掉电量计;未初始化时为空操作返回 ESP_OK。
// 唤醒需重新 bsp_battery_init()(其内部会按匹配结果切回 ACTIVE)。
esp_err_t bsp_battery_sleep(void);
