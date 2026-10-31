// main/apps/app_battery_gauge.h —— 首页 RPG 血条式电量计
//
// 和顶栏右上角的电量标签不是同一套画法：它是「心形图标 + 血槽 + 百分比」的整行控件，
// 属于首页身份卡的一部分。但从「什么时候更新」看，它和顶栏标签完全一致，所以它
// 也挂进 app_ui 的电量视图注册表，由 power.c 的同一套节奏重绘——不再自带 lv_timer。
//
// 自带 lv_timer 在本项目里是行不通的：STANDBY 每次定时唤醒都会把所有 lv_timer 的
// last_run 重定基准到「现在」，而醒来只有几百毫秒，30s 周期的 timer 永远走不满，
// 于是屏上读数停在开机那一下。见 bsp_display_lvgl.c 与 power-and-sleep.md。
#pragma once
#include "lvgl.h"

// 建一套电量计。x/y 是控件左上角，bar_w 是血槽满宽，bar_h 是血槽高（图标按 11×11
// 格点绘制，格子边长取 bar_h/11，本项目为 1px）。font 是百分比的字号，与同一行其它
// 数值保持一致；icon_color 是爱心颜色。
// 返回注册表用的根对象（销毁它即整行消失并自动注销）；内存不足时返回 NULL。
lv_obj_t *ui_battery_gauge_create(lv_obj_t *parent, int x, int y, int bar_w, int bar_h,
                                  const lv_font_t *font, uint32_t icon_color);
