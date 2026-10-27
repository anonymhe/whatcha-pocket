// main/pages.h —— 页面路由：0=首页，1-6=六个功能（顺序同首页卡片）
#pragma once
#include "bsp_button.h"
#include "lvgl.h"

// 统一颜色（所有页面用这套）
#define C_BG      0x0F0F1A
#define C_GREEN   0x5AB055
#define C_GREEN_D 0x2E6B2C
#define C_CARD    0x151520
#define C_LINE    0x242436
#define C_TEXT    0xF4F4EA
#define C_MUTED   0x6B6B7B
#define C_GOLD    0xE6B93C

typedef struct {
    const char *name;                             // 日志用
    void (*enter)(void);                          // 建屏并 lv_screen_load
    void (*exit)(void);                           // 删 timer、删屏，必须删干净
    void (*key)(bsp_btn_t btn, bsp_btn_ev_t ev);  // 收按键（子页长按确认已被 pages.c 拦截）
} page_t;

#define PAGE_HOME   0
#define PAGE_APP_BASE 1      // 1-6：六个功能页（顺序同首页卡片），说明页是其 +6
#define PAGE_INTRO  7        // 功能说明页（进入 PAGE_APP_BASE+n 前先看这里）
#define PAGE_LAB    8        // 口袋上新（二创玩法列表）
#define PAGE_APP    9        // 当前打开的二创玩法
#define PAGE_SYNC   10       // 同步页（连接观猹小程序）
#define PAGE_GUIDE  11       // 使用说明页
#define PAGE_COUNT  12

void page_open(int idx);                         // 切页
void page_open_app(const page_t *app);           // 打开口袋上新里的玩法
void page_home(void);                            // 回首页
void page_intro_set(int target_idx);
void page_key(bsp_btn_t btn, bsp_btn_ev_t ev);   // main.c 按键入口
int  page_current(void);

// 子页面公用：黑底空屏
lv_obj_t *page_screen_create(void);

// 屏幕亮度（main.c）：on=立刻点亮并暂停自动调暗；off=恢复，从现在起重新计 30 秒
void power_keep_awake(bool on);
