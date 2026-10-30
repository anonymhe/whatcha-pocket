// components/bsp/include/bsp_display.h
// ST7789P3 240x320 显示:SPI 面板初始化 + 厂商专属寄存器 + LEDC 背光调光。
#pragma once

#include "esp_err.h"
#include "esp_lcd_types.h"
#include <stdbool.h>
#include <stdint.h>

// 初始化 SPI 总线、面板、厂商寄存器、背光 LEDC。成功后屏幕已上电但内容未定。
esp_err_t bsp_display_init(void);

// 取底层面板句柄。想直接 esp_lcd_panel_draw_bitmap 画,或接 LVGL 以外的 GUI 时用。
// 未初始化返回 NULL。
esp_lcd_panel_handle_t bsp_display_panel(void);

// 取底层 panel io 句柄(LVGL 接入需要)。未初始化返回 NULL。
esp_lcd_panel_io_handle_t bsp_display_io(void);

// 背光亮度 0..100(%)。LEDC PWM,0=全灭。
// light sleep 期间 PWM 仍在输出(RC_FAST 时钟 + LEDC_SLEEP_MODE_KEEP_ALIVE),
// 所以「屏幕还亮不亮」只由这里的占空比决定:想灭就传 0。
void bsp_display_backlight(uint8_t percent);

// 面板进入/退出睡眠(0x10 SLPIN / 0x11 SLPOUT)。深睡前调用可再省一部分电流;
// light sleep 不需要——面板会自己从 GRAM 继续刷新静态画面。
esp_err_t bsp_display_sleep(void);
esp_err_t bsp_display_wake(void);

// ---------------------------------------------------------------------------
// LVGL 接入(可选层)。必须先 bsp_display_init() 成功后再调。
// 不想用 LVGL 的开发者可忽略本段,直接用 bsp_display_panel() 自己画。
// ---------------------------------------------------------------------------
// 前向声明:LVGL 里 lv_display_t 是 `typedef struct _lv_display_t lv_display_t;`,
// 故此处用 struct 形式即可,避免本头文件强行 include lvgl.h。
struct _lv_display_t;

// 启动 LVGL 与其渲染任务,返回 lv_display_t*。失败返回 NULL。
struct _lv_display_t *bsp_lvgl_init(void);

// LVGL 非线程安全:在【非 LVGL 任务】里操作任何 lv_* 对象前后必须加解锁。
bool bsp_lvgl_lock(int timeout_ms);
void bsp_lvgl_unlock(void);

// 暂停/恢复 LVGL 的 5ms tick 定时器。入睡前必须 stop:esp_timer 的周期定时器会在
// light sleep 醒来后逐个补跑漏掉的每一拍,长时间待机等于几万次 lv_tick_inc 堵住
// LVGL 任务。恢复时把 slept_ms 一次性补进 lv_tick,画面上的时钟/倒计时才对得上
// 真实时间;传 0 表示不补。stop 幂等,resume 需与 stop 成对。
void bsp_lvgl_tick_stop(void);
void bsp_lvgl_tick_resume(uint32_t slept_ms);
