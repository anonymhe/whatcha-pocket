// main/pages/page_1024.c — 1024(2048) 合成游戏
#include "pages.h"
#include "app_ui.h"
#include "storage.h"
#include "sfx.h"
#include "esp_random.h"
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(font_fusion12);
LV_FONT_DECLARE(font_ps8);
LV_FONT_DECLARE(font_ps16);
LV_IMAGE_DECLARE(art_1024_over);
LV_IMAGE_DECLARE(art_1024_win);

/* ---- 常量 ---- */
#define ROWS   4
#define COLS   4
#define CELL   50
#define GAP    3
#define GW     (CELL*COLS + GAP*(COLS+1))   /* 215 */
#define GH     (CELL*ROWS + GAP*(ROWS+1))
#define GX     ((240-GW)/2)                 /* 12  */
#define GY     52
#define NVS_BEST  "g1024_best"
#define DBL_MS 250

typedef enum { ST_INTRO, ST_PLAY, ST_OVER, ST_WIN } state_t;
typedef enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT } dir_t;

/* 索引 = log2(value)，0=空格 */
static const uint32_t TILE_BG[] = {
    0x1E1E2A, 0x3C3C4B, 0x465046, 0x5A8C50, 0x5AB055, 0x46A046,
    0x328C32, 0xC8AA32, 0xD29B28, 0xDC8C1E, 0xE6B93C, 0xE6D23C,
};
#define TILE_COUNT (sizeof(TILE_BG)/sizeof(TILE_BG[0]))

static int ilog2(uint16_t v) { int n = 0; while (v >>= 1) n++; return n; }

/* ---- 状态 ---- */
static uint16_t s_board[ROWS][COLS];
static uint32_t s_score, s_best;
static state_t  s_state;
static bool     s_won_shown;

static lv_obj_t *s_scr;
static lv_obj_t *s_nums[ROWS][COLS];   /* 每格一个带背景的 label，兼作方块 */
static lv_obj_t *s_score_lbl, *s_best_lbl;
/* 说明页 / 结束页 / 胜利页：全部扁平放在 s_scr 上，用列表记指针方便整组删除
   （标签放进不透明容器里实机会卡死，原因未查明，规避） */
#define OVL_MAX 12
static lv_obj_t *s_ovl[OVL_MAX];
static int s_ovl_n;
static lv_obj_t *s_go_score_lbl, *s_go_best_lbl;
static lv_timer_t *s_dbl_timer;
static int s_ok_count;
static lv_obj_t *mk(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t col)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    return l;
}

static lv_obj_t *rect(lv_obj_t *p, int x, int y, int w, int h, uint32_t col)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

/* ---- 游戏逻辑 ---- */
static int spawn_val(void)   /* 降难度：2 / 4 / 8 = 50% / 40% / 10% */
{
    uint32_t r = esp_random() % 10;
    return r < 5 ? 2 : (r < 9 ? 4 : 8);
}

static void add_tile(void)
{
    int empty[ROWS*COLS][2], n = 0;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (!s_board[r][c]) { empty[n][0] = r; empty[n][1] = c; n++; }
    if (!n) return;
    int i = esp_random() % n;
    s_board[empty[i][0]][empty[i][1]] = spawn_val();
}

static void rc_of(dir_t dir, int i, int j, int *r, int *c)
{
    switch (dir) {
    case DIR_LEFT:  *r = i;   *c = j;   break;
    case DIR_RIGHT: *r = i;   *c = 3-j; break;
    case DIR_UP:    *r = j;   *c = i;   break;
    default:        *r = 3-j; *c = i;   break;
    }
}

/* 每格一次滑动只能合并一次（标准 2048 规则） */
static int s_mx;   /* 本次滑动合出的最大数，用来定音高 */
static bool slide(dir_t dir)
{
    bool moved = false;
    uint32_t pts = 0;
    for (int i = 0; i < 4; i++) {
        uint16_t line[4], out[4] = {0};
        bool merged[4] = {0};
        for (int j = 0; j < 4; j++) { int r, c; rc_of(dir, i, j, &r, &c); line[j] = s_board[r][c]; }
        int wi = 0;
        for (int j = 0; j < 4; j++) {
            if (!line[j]) continue;
            if (wi > 0 && !merged[wi-1] && out[wi-1] == line[j]) {
                out[wi-1] *= 2; merged[wi-1] = true; pts += out[wi-1]; if ((int)out[wi-1] > s_mx) s_mx = (int)out[wi-1];
            } else {
                out[wi++] = line[j];
            }
        }
        for (int j = 0; j < 4; j++) {
            int r, c; rc_of(dir, i, j, &r, &c);
            if (s_board[r][c] != out[j]) moved = true;
            s_board[r][c] = out[j];
        }
    }
    s_score += pts;
    if (s_score > s_best) s_best = s_score;
    return moved;
}

static bool can_move(void)
{
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            if (!s_board[r][c]) return true;
            if (c < COLS-1 && s_board[r][c] == s_board[r][c+1]) return true;
            if (r < ROWS-1 && s_board[r][c] == s_board[r+1][c]) return true;
        }
    return false;
}

static bool has_1024(void)
{
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (s_board[r][c] >= 1024) return true;
    return false;
}

/* ---- 渲染 ---- */
static void render(void)
{
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            uint16_t v = s_board[r][c];
            int idx = v ? ilog2(v) : 0;
            if (idx >= (int)TILE_COUNT) idx = TILE_COUNT - 1;
            lv_obj_set_style_bg_color(s_nums[r][c], lv_color_hex(TILE_BG[idx]), 0);
            lv_obj_set_style_text_color(s_nums[r][c], lv_color_hex(v >= 16 ? C_BG : C_TEXT), 0);
            char buf[8];
            if (v) snprintf(buf, sizeof buf, "%u", (unsigned)v); else buf[0] = 0;
            lv_label_set_text(s_nums[r][c], buf);
        }
    char buf[16];
    snprintf(buf, sizeof buf, "%lu", (unsigned long)s_score);
    lv_label_set_text(s_score_lbl, buf);
    snprintf(buf, sizeof buf, "%lu", (unsigned long)s_best);
    lv_label_set_text(s_best_lbl, buf);
}

/* ---- 说明页 / 覆盖层 ---- */
static void list_kill(lv_obj_t **list, int *n)
{
    for (int i = 0; i < *n; i++) if (list[i]) lv_obj_delete(list[i]);
    *n = 0;
}
#define ADD(list, n, obj) do { if ((n) < OVL_MAX) (list)[(n)++] = (obj); } while (0)



static lv_obj_t *dim(void)
{
    lv_obj_t *d = rect(s_scr, 0, 0, 240, 320, C_BG);
    lv_obj_set_style_bg_opa(d, 230, 0);
    return d;
}

static void build_gameover(void)
{
    lv_obj_t *o;
    ADD(s_ovl, s_ovl_n, dim());
    o = lv_image_create(s_scr); lv_image_set_src(o, &art_1024_over); lv_obj_align(o, LV_ALIGN_CENTER, 0, -100); ADD(s_ovl, s_ovl_n, o);
    o = mk(s_scr, "GAME", &font_ps16, C_TEXT);  lv_obj_align(o, LV_ALIGN_CENTER, 0, -40); ADD(s_ovl, s_ovl_n, o);
    o = mk(s_scr, "OVER", &font_ps16, C_TEXT);  lv_obj_align(o, LV_ALIGN_CENTER, 0, -10); ADD(s_ovl, s_ovl_n, o);
    s_go_best_lbl  = mk(s_scr, "", &font_fusion12, C_GOLD); lv_obj_align(s_go_best_lbl,  LV_ALIGN_CENTER, 0, 30); ADD(s_ovl, s_ovl_n, s_go_best_lbl);
    s_go_score_lbl = mk(s_scr, "", &font_fusion12, C_TEXT); lv_obj_align(s_go_score_lbl, LV_ALIGN_CENTER, 0, 52); ADD(s_ovl, s_ovl_n, s_go_score_lbl);
    o = mk(s_scr, "小猹曰：千里之行始于合成", &font_fusion12, C_MUTED); lv_obj_align(o, LV_ALIGN_CENTER, 0, 85); ADD(s_ovl, s_ovl_n, o);
}

static void build_win(void)
{
    lv_obj_t *o;
    ADD(s_ovl, s_ovl_n, dim());
    o = lv_image_create(s_scr); lv_image_set_src(o, &art_1024_win); lv_obj_align(o, LV_ALIGN_CENTER, 0, -85); ADD(s_ovl, s_ovl_n, o);
    o = mk(s_scr, "1024!", &font_ps16, C_GOLD);        lv_obj_align(o, LV_ALIGN_CENTER, 0, -15); ADD(s_ovl, s_ovl_n, o);
    o = mk(s_scr, "按确认继续", &font_fusion12, C_GREEN); lv_obj_align(o, LV_ALIGN_CENTER, 0, 25);  ADD(s_ovl, s_ovl_n, o);
}

/* ---- 状态切换 ---- */
static void show_gameover(void)
{
    sfx_play(SFX_BAD);
    list_kill(s_ovl, &s_ovl_n);
    build_gameover();
    char buf[32];
    snprintf(buf, sizeof buf, "最高分 %lu", (unsigned long)s_best);  lv_label_set_text(s_go_best_lbl, buf);
    snprintf(buf, sizeof buf, "本次 %lu",   (unsigned long)s_score); lv_label_set_text(s_go_score_lbl, buf);
    store_set_u32(NVS_BEST, s_best);
    s_state = ST_OVER;
}

static void restart(void)
{
    memset(s_board, 0, sizeof s_board);
    s_score = 0; s_won_shown = false;
    add_tile(); add_tile();
    render();
    list_kill(s_ovl, &s_ovl_n);
    s_state = ST_PLAY;
}

static void do_slide(dir_t dir)
{
    s_mx = 0;
    if (slide(dir)) add_tile();
    if (s_mx) sfx_merge(s_mx);
    render();
    if (has_1024() && !s_won_shown) {
        s_won_shown = true;
        list_kill(s_ovl, &s_ovl_n);
        build_win();
        s_state = ST_WIN;
        sfx_play(SFX_CLEAR);
        return;
    }
    if (!can_move()) show_gameover();
}

/* ---- 双击检测：单击=左，连按=右 ---- */
static void cancel_dbl(void)
{
    if (s_dbl_timer) { lv_timer_delete(s_dbl_timer); s_dbl_timer = NULL; }
    s_ok_count = 0;
}

static void dbl_timeout(lv_timer_t *t)
{
    (void)t;
    s_dbl_timer = NULL;      /* repeat_count=1，LVGL 自己删 */
    s_ok_count = 0;
    do_slide(DIR_LEFT);
}

static void handle_ok(void)
{
    s_ok_count++;
    if (s_ok_count == 1) {
        s_dbl_timer = lv_timer_create(dbl_timeout, DBL_MS, NULL);
        lv_timer_set_repeat_count(s_dbl_timer, 1);
    } else {
        cancel_dbl();
        do_slide(DIR_RIGHT);
    }
}

/* ---- page (分两阶段建 UI，避免单次 timer 周期内创建过多对象) ---- */
static lv_timer_t *s_init_timer;

/* 第二阶段：建格子 + render + 说明页（在下一个 timer 周期跑） */
static void init_phase2(lv_timer_t *t)
{
    (void)t; s_init_timer = NULL;
    if (!s_scr) return;

    rect(s_scr, GX, GY, GW, GH, C_CARD);
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            lv_obj_t *n = lv_label_create(s_scr);
            lv_label_set_text(n, "");
            lv_obj_set_size(n, CELL, CELL);
            lv_obj_set_pos(n, GX + GAP + c * (CELL + GAP), GY + GAP + r * (CELL + GAP));
            lv_obj_set_style_bg_color(n, lv_color_hex(TILE_BG[0]), 0);
            lv_obj_set_style_bg_opa(n, LV_OPA_COVER, 0);
            lv_obj_set_style_text_font(n, &font_ps8, 0);
            lv_obj_set_style_text_align(n, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_pad_top(n, (CELL - 8) / 2, 0);
            s_nums[r][c] = n;
        }
    rect(s_scr, 0, 292, 240, 1, C_LINE);

    memset(s_board, 0, sizeof s_board);
    s_score = 0; s_won_shown = false;
    add_tile(); add_tile();
    render();

    s_state = ST_PLAY;
}

/* 第一阶段：只建屏幕 + 顶栏 + 分数行（轻量），然后让出 timer 周期 */
static void page_enter(void)
{
    s_scr = page_screen_create();
    s_best = store_get_u32(NVS_BEST, 0);
    s_ok_count = 0; s_dbl_timer = NULL;
    s_ovl_n = 0;
    s_state = ST_INTRO; /* 按键暂不响应游戏操作 */

    lv_obj_t *l;
    ui_topbar_ex(s_scr, "1024", &font_ps8);

    l = mk(s_scr, "分数", &font_fusion12, C_MUTED);     lv_obj_set_pos(l, 18, 32);
    s_score_lbl = mk(s_scr, "0", &font_ps8, C_GREEN);  lv_obj_set_pos(s_score_lbl, 50, 36);
    l = mk(s_scr, "最高", &font_fusion12, C_MUTED);     lv_obj_set_pos(l, 140, 32);
    s_best_lbl = mk(s_scr, "0", &font_ps8, C_MUTED);   lv_obj_set_pos(s_best_lbl, 172, 36);

    lv_screen_load(s_scr);

    /* 50ms 后进入第二阶段 */
    s_init_timer = lv_timer_create(init_phase2, 50, NULL);
    lv_timer_set_repeat_count(s_init_timer, 1);
}

static void page_exit(void)
{
    cancel_dbl();
    if (s_init_timer) { lv_timer_delete(s_init_timer); s_init_timer = NULL; }
    store_set_u32(NVS_BEST, s_best);
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; }
    s_ovl_n = 0;
}

static void page_key_cb(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) { cancel_dbl(); return; }
    if (ev != BSP_BTN_PRESS) return;

    switch (s_state) {
    case ST_INTRO:   /* 格子还没建好；init_phase2 建完会切到 PLAY */
        break;
    case ST_PLAY:
        if      (btn == BSP_BTN_UP)   do_slide(DIR_UP);
        else if (btn == BSP_BTN_DOWN) do_slide(DIR_DOWN);
        else if (btn == BSP_BTN_OK)   handle_ok();
        break;
    case ST_OVER:
        restart();
        break;
    case ST_WIN:
        list_kill(s_ovl, &s_ovl_n);
        s_state = ST_PLAY;
        break;
    }
}

const page_t page_1024 = { "page_1024", page_enter, page_exit, page_key_cb };
