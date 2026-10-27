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
extern const page_t page_guide;    // 使用说明页

// 二创玩法统一走一个代理页，实际页面由 page_open_app 指定
static const page_t *s_app;
static void app_enter(void) { if (s_app && s_app->enter) s_app->enter(); }
static void app_exit(void)  { if (s_app && s_app->exit)  s_app->exit(); }
static void app_key(bsp_btn_t b, bsp_btn_ev_t e) { if (s_app && s_app->key) s_app->key(b, e); }
static const page_t page_app_proxy = { "app", app_enter, app_exit, app_key };

static const page_t *PAGES[PAGE_COUNT] = {
    &page_home_def, &page_fortune, &page_pomo, &page_melon,
    &page_type, &page_muyu, &page_1024, &page_intro,
    &page_lab, &page_app_proxy, &page_sync, &page_guide,
};
static int s_cur = -1;

int page_current(void) { return s_cur; }

// ---- 切页转场：绿色幕布擦除 ----
// 目的有二：(1) 眼糖——上/下擦除的过场；(2) 省内存——切页本是「先建新屏再删旧屏」，
// 过渡瞬间两张重屏并存，是 LVGL 池的峰值。这里插入一张「极轻的纯绿幕布屏」作屏障：
// 幕布盖满后先删旧重屏、再建新重屏，于是任何时刻只有「幕布(1 个对象) + 一张重屏」并存，
// 峰值降一整张屏。LVGL 不允许删当前活动屏，故必须用一张真正的中间屏来承接。
//
// 时序：遮(在旧屏上长绿条 0→H) → 载幕布屏、删旧屏 → 建新屏、载入 → 揭(在新屏上缩绿条 H→0) → 删幕布屏。
#define TR_DUR   240                     // 单段(遮/揭)时长 ms
static lv_obj_t *s_curtain;              // 幕布屏（纯绿，1 个对象）
static lv_obj_t *s_wipe;                 // 擦除用的绿条（挂在活动屏上）
static int s_tr_to = -1;                 // 转场目标页；-1=无转场
static bool s_in_tr;                     // 转场进行中（期间吞按键）

bool page_busy(void) { return s_in_tr; }

static void apply_page(int idx)
{
    const page_t *old = (s_cur >= 0) ? PAGES[s_cur] : NULL;
    PAGES[idx]->enter();                 // 先建并载入新屏
    if (old && old->exit) old->exit();   // 再删旧屏（LVGL 不允许删当前活动屏）
    s_cur = idx;
    ESP_LOGI(TAG, "-> %s  free heap %lu", PAGES[idx]->name,
             (unsigned long)esp_get_free_heap_size());
}

static lv_obj_t *wipe_make(lv_obj_t *par)
{
    lv_obj_t *w = lv_obj_create(par);
    lv_obj_remove_style_all(w);
    lv_obj_set_pos(w, 0, 0);
    lv_obj_set_width(w, LV_HOR_RES);
    lv_obj_set_style_bg_color(w, lv_color_hex(C_GREEN), 0);
    lv_obj_set_style_bg_opa(w, LV_OPA_COVER, 0);
    return w;
}

static void set_h(void *obj, int32_t v) { lv_obj_set_height((lv_obj_t *)obj, v); }

static void reveal_done(lv_anim_t *a)    // 揭完：删幕布屏 + 绿条，转场结束
{
    (void)a;
    if (s_wipe)    { lv_obj_delete(s_wipe);    s_wipe = NULL; }
    if (s_curtain) { lv_obj_delete(s_curtain); s_curtain = NULL; }
    s_tr_to = -1;
    s_in_tr = false;
}

static void cover_done(lv_anim_t *a)     // 遮完：屏障到位，换屏并开始揭
{
    (void)a;
    if (s_wipe) { lv_obj_delete(s_wipe); s_wipe = NULL; }

    // 屏障：极轻的纯绿屏，先载入它再删旧重屏——此刻只剩幕布，池占用降到最低
    s_curtain = lv_obj_create(NULL);
    lv_obj_remove_flag(s_curtain, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_curtain, lv_color_hex(C_GREEN), 0);
    lv_obj_set_style_bg_opa(s_curtain, LV_OPA_COVER, 0);
    lv_screen_load(s_curtain);

    apply_page(s_tr_to);                 // 删旧重屏、建新重屏（只与幕布并存）

    // 揭：在新屏上盖一条满高绿条，缩回 0 露出新屏
    s_wipe = wipe_make(lv_screen_active());
    lv_obj_set_height(s_wipe, LV_VER_RES);
    lv_anim_t an; lv_anim_init(&an);
    lv_anim_set_var(&an, s_wipe);
    lv_anim_set_values(&an, LV_VER_RES, 0);
    lv_anim_set_duration(&an, TR_DUR);
    lv_anim_set_path_cb(&an, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&an, set_h);
    lv_anim_set_completed_cb(&an, reveal_done);
    lv_anim_start(&an);
}

void page_open(int idx)
{
    if (idx < 0 || idx >= PAGE_COUNT) return;
    if (s_in_tr || idx == s_cur) return;   // 转场中或已在该页：忽略

    if (s_cur < 0) { apply_page(idx); return; }   // 首次无旧屏，直接建

    s_in_tr = true;
    s_tr_to = idx;
    // 遮：在当前屏上长一条绿条 0→满高，盖住旧屏
    s_wipe = wipe_make(lv_screen_active());
    lv_obj_set_height(s_wipe, 0);
    lv_anim_t an; lv_anim_init(&an);
    lv_anim_set_var(&an, s_wipe);
    lv_anim_set_values(&an, 0, LV_VER_RES);
    lv_anim_set_duration(&an, TR_DUR);
    lv_anim_set_path_cb(&an, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&an, set_h);
    lv_anim_set_completed_cb(&an, cover_done);
    lv_anim_start(&an);
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
    if (s_cur < 0 || s_in_tr) return;   // 转场中吞按键，避免打断动画或重入切页
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
