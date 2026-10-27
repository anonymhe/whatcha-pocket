// main/pages.c —— 最小页面路由
#include "pages.h"
#include "sfx.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "pages";

extern const page_t page_home_def;   // page_home.c
extern const page_t page_fortune;    // 好运猹
extern const page_t page_pomo;       // 番茄猹
extern const page_t page_melon;      // 吃瓜猹
extern const page_t page_type;         // 猹type
extern const page_t page_muyu;       // 木鱼猹
extern const page_t page_1024;       // 1024
extern const page_t page_intro;    // 说明页
extern const page_t page_lab;      // 口袋上新
extern const page_t page_sync;     // 同步页

// 二创玩法统一走一个代理页，实际页面由 page_open_app 指定
static const page_t *s_app;
static void app_enter(void) { if (s_app && s_app->enter) s_app->enter(); }
static void app_exit(void)  { if (s_app && s_app->exit)  s_app->exit(); }
static void app_key(bsp_btn_t b, bsp_btn_ev_t e) { if (s_app && s_app->key) s_app->key(b, e); }
static const page_t page_app_proxy = { "app", app_enter, app_exit, app_key };

static const page_t *PAGES[PAGE_COUNT] = {
    &page_home_def, &page_fortune, &page_pomo, &page_melon,
    &page_type, &page_muyu, &page_1024, &page_intro,
    &page_lab, &page_app_proxy, &page_sync,
};
static int s_cur = -1;

int page_current(void) { return s_cur; }

void page_open(int idx)
{
    if (idx < 0 || idx >= PAGE_COUNT || idx == s_cur) return;
    const page_t *old = (s_cur >= 0) ? PAGES[s_cur] : NULL;
    PAGES[idx]->enter();                 // 先建并载入新屏
    if (old && old->exit) old->exit();   // 再删旧屏（LVGL 不允许删当前活动屏）
    s_cur = idx;
    ESP_LOGI(TAG, "-> %s  free heap %lu", PAGES[idx]->name,
             (unsigned long)esp_get_free_heap_size());
}

void page_home(void) { page_open(PAGE_HOME); }

void page_open_app(const page_t *app)
{
    if (!app || s_cur == PAGE_APP) return;
    s_app = app;
    page_open(PAGE_APP);
}

void page_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (s_cur < 0) return;
    if (s_cur != PAGE_HOME && btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
        if (PAGES[s_cur]->key) PAGES[s_cur]->key(btn, ev);   // 页面先收到，可做收尾
        sfx_play(SFX_BACK);
        if (s_cur == PAGE_APP) page_open(PAGE_LAB);   // 二创玩法长按回实验室
        else page_home();
        return;
    }
    if (PAGES[s_cur]->key) PAGES[s_cur]->key(btn, ev);
}

lv_obj_t *page_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    return scr;
}
