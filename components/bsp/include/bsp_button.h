// components/bsp/include/bsp_button.h
// 三个按键共用一个 ADC 引脚,靠分压电阻区分。电压窗口见 bsp_pins.h。
#pragma once

#include "esp_err.h"

// 按键索引。数量用 bsp_pins.h 的 BSP_BTN_COUNT(硬件属性,归引脚表管),
// 这里不再定义尾项计数,避免出现 BSP_BTN_COUNT / BSP_BTN_COUNT_ 两个近似名字。
typedef enum {
    BSP_BTN_UP = 0,
    BSP_BTN_DOWN,
    BSP_BTN_OK,
} bsp_btn_t;

typedef enum {
    BSP_BTN_PRESS = 0,   // 按下瞬间(低延迟,适合游戏类即时响应)
    BSP_BTN_CLICK,       // 单击(按下并抬起)
    BSP_BTN_DOUBLE,      // 双击
    BSP_BTN_LONG,        // 长按
} bsp_btn_ev_t;

// 按键事件回调。运行于 button 组件的定时器任务,勿在其中阻塞或做重活。
typedef void (*bsp_btn_cb_t)(bsp_btn_t btn, bsp_btn_ev_t ev, void *user);

esp_err_t bsp_button_init(bsp_btn_cb_t cb, void *user);

// 把按键 ADC 引脚重新配置回模拟采样状态。
// 省电流程会把这一脚临时借给 GPIO 唤醒（见 bsp_power.h）；gpio_config() 会摘掉
// 模拟功能，唤醒后不还回来按键电压就全是错的。正常使用中无需调用。
esp_err_t bsp_button_restore_adc(void);

// 暂停/恢复按键扫描。入睡前必须 stop()：
// button 组件用 5ms 周期的 esp_timer 轮询，而这类周期定时器在 light sleep 醒来后
// 会被 esp_timer 逐个补跑漏掉的每一次（5 分钟 ≈ 6 万次回调），把 esp_timer 任务
// 长时间堵住，表现为「醒来后按键失灵」。stop/resume 成对调用，两者都幂等。
esp_err_t bsp_button_scan_stop(void);
esp_err_t bsp_button_scan_resume(void);

// 读当前 ADC 原始电压(mV)。松开时约 3300;按住某键时约为该键的分压值。
// ★ 换了分压/上拉阻值后,用它测出自己的三档电压,再改 bsp_pins.h 的 BSP_BTN_MV_TABLE。
// 读取失败返回 -1。
int bsp_button_read_mv(void);
