// main/boot_anim.h —— 开屏终端动画（main.c 在载入首页前调一次）
#pragma once

void boot_anim_start(void (*done)(void));   // 动画播完后回调 done（LVGL 任务上下文）
void boot_anim_skip(void);                  // 任意按键跳过（按键回调上下文安全）
