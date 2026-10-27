// main/page_home.c —— 观猹首页（按 mock v14 布局）；身份区读蓝牙同步的资料
#include "pages.h"
#include "sfx.h"
#include "storage.h"
#include "ble_sync.h"
#include "bsp_battery.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(font_fusion12);
LV_FONT_DECLARE(font_ps8);
LV_FONT_DECLARE(font_ps16);
LV_FONT_DECLARE(font_fusion20);
#include "cha_pool.h"
LV_IMAGE_DECLARE(ic_clover);
LV_IMAGE_DECLARE(ic_tomato);
LV_IMAGE_DECLARE(ic_melon);
LV_IMAGE_DECLARE(ic_type);
LV_IMAGE_DECLARE(ic_cha_mallet);
LV_IMAGE_DECLARE(ic_tiles);


// ---- 布局常量（屏幕像素）：身份区为 RPG 角色卡风格 ----
#define AV_X      12    // 头像左列，72px 宽
#define AV_Y      AV_X
#define BADGE_H   15

// 右侧信息区（头像右边）：标题 + 名称 + badge
// WATCHA 作为 RPG 角色的「属地名」贴在名称正上方；名称+badge 整体下移，与头像高度对齐
#define INFO_X    (AV_X + 78)              // 右侧区左边界
#define TITLE_Y   (AV_Y + 6)              // 标题（属地名）贴在名称上方
#define NAME_Y    (TITLE_Y + 12)              // 名称下移，使名称+badge 与头像垂直居中对齐
#define BADGE_IY  (NAME_Y + 27)   // badge 在名称下方
#define ICON_CELL 1               // 点阵图标每点像素（11×11 → 11px 见方）
#define ICON_W    (11 * ICON_CELL)

// RPG 状态区：与头像同缩进（左对齐 AV_X），头像下方三行（HP / ATK+DEF / Coins）。
// 全部相对定位：行距 SROW_DY，图标宽 ICON_W，数值离图标 STAT_GAP，第二列再偏 STAT_COL2。
#define STAT_X     AV_X                         // 与头像/表情包同缩进
#define STAT_GAP   6                            // 图标与数值的水平间距
#define STAT_COL2  96                           // ATK→DEF 的列间距
#define SROW_DY    22                           // 三行的行距
#define SROW1_Y    (AV_Y + 72)                  // 第一行 HP
#define SROW2_Y    (SROW1_Y + SROW_DY)          // 第二行 ATK / DEF
#define SROW3_Y    (SROW2_Y + SROW_DY)          // 第三行 Coins
#define STAT_DIV_Y (SROW3_Y + 20)               // 状态区下方分隔线

#define HP_BAR_X   (STAT_X + ICON_W + 6)        // HP 血条起点（心形图标之后）
#define HP_BAR_W   128                          // HP 血条满宽（拉到正常宽度）
#define HP_BAR_H   ICON_W
#define ATK_X      STAT_X                       // ATK 列
#define DEF_X      (STAT_X + STAT_COL2)         // DEF 列
#define COIN_X     STAT_X                       // Coins 列（独立一行，回到首列）

// 应用列表：上下翻页的长方形卡片（左 icon / 右 title+desc）
// 选中卡居中显示，上/下相邻卡各漏出一截作翻页提示（滚动式，首尾相接）
#define LIST_X     16                            // 卡片左边界
#define LIST_W     208                           // 卡片宽度
#define CARD_H     48                            // 卡片高度
#define CARD_GAP   8                             // 卡片间距
#define CARD_STEP  (CARD_H + CARD_GAP)           // 相邻卡中心距
#define LIST_TOP   (STAT_DIV_Y + 1)              // 列表可视区上边界（紧贴分隔线）
#define LIST_VIS_H (276 - LIST_TOP)              // 列表可视区高度
#define LIST_MID   ((LIST_VIS_H - CARD_H) / 2)   // 选中卡的 y（相对裁剪容器）

static lv_obj_t *s_scr;
static lv_obj_t *s_av;
static lv_obj_t *s_badge, *s_badge_t;
static lv_obj_t *s_name;
static lv_obj_t *s_hp, *s_hp_bar, *s_atk, *s_def, *s_coins;   // RPG 数值
#define NITEMS 8
static lv_obj_t *s_cards[NITEMS];
static lv_obj_t *s_labels[NITEMS];
static lv_obj_t *s_descs[NITEMS];
static lv_obj_t *s_icons[NITEMS];
static lv_timer_t *s_timer;
static lv_timer_t *s_sync_timer;
static int s_sel = 0;

static lv_obj_t *block(lv_obj_t *p, int x, int y, int w, int h, uint32_t c)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(c), 0);
    return o;
}

static lv_obj_t *label(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t c, int x, int y)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

// 运行时点阵图标：11 行字符串，'#' 亮，其它为空；每个亮点画 cell×cell 像素方块。
// 同一行连续亮点合并成一条横向方块，省 LVGL 对象。返回 11*cell 见方的容器。
static lv_obj_t *icon_make(lv_obj_t *parent, const char *const rows[11], int cell, uint32_t col)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(box, 11 * cell, 11 * cell);
    lv_obj_set_style_radius(box, 0, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    for (int r = 0; r < 11; r++) {
        const char *row = rows[r];
        int c = 0;
        while (c < 11) {
            if (row[c] != '#') { c++; continue; }
            int run = 1;
            while (c + run < 11 && row[c + run] == '#') run++;
            block(box, c * cell, r * cell, run * cell, cell, col);
            c += run;
        }
    }
    return box;
}

// 两色点阵图标：'#' 用 col_a，'o' 用 col_b，其它透明；每点画 cell×cell 方块。
// 同色连续点合并成横条省对象。22×22（cell=2）正好与现有 ic_* 位图同尺寸。
static lv_obj_t *icon_make2(lv_obj_t *parent, const char *const rows[11], int cell,
                            uint32_t col_a, uint32_t col_b)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(box, 11 * cell, 11 * cell);
    lv_obj_set_style_radius(box, 0, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    for (int r = 0; r < 11; r++) {
        const char *row = rows[r];
        int c = 0;
        while (c < 11) {
            char ch = row[c];
            if (ch != '#' && ch != 'o') { c++; continue; }
            int run = 1;
            while (c + run < 11 && row[c + run] == ch) run++;
            block(box, c * cell, r * cell, run * cell, cell, ch == '#' ? col_a : col_b);
            c += run;
        }
    }
    return box;
}

// 四维图标点阵（11×11）
static const char *const ICON_HP[11] = {    // 爱心（左右对称，顶部圆润、底部收尖）
    "..##...##..", 
    ".####.####.", 
    "###########", 
    "###########",
    "###########", 
    "###########", 
    ".#########.", 
    "..#######..",
    "...#####...", 
    "....###....", 
    ".....#.....",
};
static const char *const ICON_ATK[11] = {   // 剑
    "....#....##", 
    ".##..#..###", 
    "...#...####", 
    "##....####.",
    "..#..####..", 
    "....####...", 
    "...####....", 
    ".#.###.....",
    "###.###....", 
    ".####.#....", 
    "##...#.....",
};
static const char *const ICON_DEF[11] = {   // 盾
    "..#######..", 
    ".#########.", 
    ".####.####.", 
    ".####.####.",
    ".##..#..##.",
    ".####.####.", 
    "..###.###..", 
    "..#######..", 
    "...#####...",
    "....###....", 
    ".....#.....", 
};
static const char *const ICON_COIN[11] = {  // 金币
    "...#####...", 
    ".##.....##.", 
    ".#..###..#.", 
    "#...#.#...#",
    "#..#...#..#", 
    "#..#...#..#", 
    "#..#...#..#", 
    "#...#.#...#", 
    ".#..###..#.",
    ".##.....##.", 
    "...#####...", 
};

// 使用说明：翻开的书（左右两页 + 中缝 'o'）
static const char *const ICON_BOOK[11] = {
    "...........",
    "...##......",
    ".####.####.",
    ".#.###...#.",
    ".#.###.###.",
    ".#.###...#.",
    ".#.###.###.",
    ".#.###...#.",
    ".#..##...#.",
    ".####.####.",
    "...........",
};
// 猹工坊：游戏手柄（圆角机身 + 左十字键 'o' + 右两键 'o'）
static const char *const ICON_PAD[11] = {
    ".....#.....",
    ".....#.....",
    ".#########.",
    "###########",
    "##.#####.##",
    "#.#.###.#.#",
    "##.#####.##",
    "###########",
    "####...####",
    ".##.....##.",
    "...........",
};

// 首页卡片列表：第一个是使用说明，中间六个是功能，最后一个是猹工坊。
// 图标两种来源：IC_IMG 用现成位图（ic_*），IC_GLYPH 用两色点阵（icon_make2）。
typedef enum { ACT_GUIDE, ACT_APP, ACT_LAB } card_act_t;
static const struct {
    const char *name;
    const char *desc;
    bool glyph;                       // true=点阵图标，false=位图
    const lv_image_dsc_t *img;        // glyph=false 时用
    const char *const *pat;           // glyph=true 时用（11 行）
    card_act_t act;
    int app_idx;                      // act=ACT_APP 时对应的功能序号（0..5）
} ITEMS[NITEMS] = {
    {"使用说明", "主页玩法与仓库",  true,  NULL,           ICON_BOOK, ACT_GUIDE, 0},
    {"好运猹",   "每日运势 一键获取", false, &ic_clover,     NULL,      ACT_APP,   0},
    {"番茄猹",   "专注计时器",       false, &ic_tomato,     NULL,      ACT_APP,   1},
    {"吃瓜猹",   "反应速度测试",     false, &ic_melon,      NULL,      ACT_APP,   2},
    {"什么猹",   "AI人格测试",       false, &ic_type,       NULL,      ACT_APP,   3},
    {"木鱼猹",   "电子木鱼",         false, &ic_cha_mallet, NULL,      ACT_APP,   4},
    {"1024",     "合成1024",         false, &ic_tiles,      NULL,      ACT_APP,   5},
    {"猹工坊",   "社区二创玩法",     true,  NULL,           ICON_PAD,  ACT_LAB,   0},
};

// 把同步资料画到身份区（首次进页 + 收到新同步时调用）
static void apply_profile(void)
{
    profile_t p;
    profile_load(&p);

    // 身份标签（RPG 风格）：普通 用户 - Spectator / 观猹员 - Player / 金牌 观猹员 - Player No.1
    uint32_t col = C_MUTED;
    const char *tag = "用户 - Spectator";
    if (p.valid && p.tier == TIER_MEMBER) { col = C_GREEN; tag = "观猹员 - Player"; }
    if (p.valid && p.tier == TIER_GOLD)   { col = C_GOLD;  tag = "观猹员 - Player No.1"; }
    lv_label_set_text(s_badge_t, tag);
    lv_obj_set_style_text_color(s_badge_t, lv_color_hex(col), 0);
    lv_obj_update_layout(s_badge_t);
    int bw = lv_obj_get_width(s_badge_t) + 10;
    lv_obj_set_size(s_badge, bw, BADGE_H);
    lv_obj_set_pos(s_badge, INFO_X, BADGE_IY);
    lv_obj_set_style_border_color(s_badge, lv_color_hex(col), 0);
    lv_obj_center(s_badge_t);

    // 角色名：24px 左对齐，超宽省略
    if (!p.valid || !p.nick[0]) {
        lv_label_set_text(s_name, "未同步");
        lv_obj_set_style_text_color(s_name, lv_color_hex(C_MUTED), 0);
    } else {
        char nick[sizeof p.nick];
        memcpy(nick, p.nick, sizeof nick);
        nick[sizeof nick - 1] = 0;
        lv_label_set_text(s_name, nick);
        lv_obj_set_style_text_color(s_name, lv_color_hex(p.tier == TIER_GOLD ? C_GOLD : C_TEXT), 0);
    }

    // RPG 数值：ATK=评价(reviews) / DEF=发帖(posts) / Coins=获赞(upvotes)，均原始数不换算；HP 由 tick 刷新
    char a[16], d[16], co[16];
    if (p.valid) {
        snprintf(a,  sizeof a,  "%lu", (unsigned long)p.reviews);
        snprintf(d,  sizeof d,  "%lu", (unsigned long)p.posts);
        snprintf(co, sizeof co, "%lu", (unsigned long)p.upvotes);
    } else { strcpy(a, "--"); strcpy(d, "--"); strcpy(co, "--"); }
    lv_label_set_text(s_atk, a);
    lv_label_set_text(s_def, d);
    lv_label_set_text(s_coins, co);
    uint32_t sc = (p.valid && p.tier == TIER_GOLD) ? C_GOLD : C_TEXT;
    lv_obj_set_style_text_color(s_atk, lv_color_hex(sc), 0);
    lv_obj_set_style_text_color(s_def, lv_color_hex(sc), 0);
}

static void refresh_sel(void)
{
    for (int i = 0; i < NITEMS; i++) {
        int off = i - s_sel;                          // 以选中卡为中心的相对偏移
        if (off >  NITEMS / 2) off -= NITEMS;          // 环形换算，取最近方向
        if (off < -NITEMS / 2) off += NITEMS;
        bool on = (off == 0);
        bool peek = (off >= -1 && off <= 1);          // 只显示中心及上下各一张
        if (!peek) {
            lv_obj_add_flag(s_cards[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(s_cards[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_y(s_cards[i], LIST_MID + off * CARD_STEP);
        lv_obj_set_style_border_color(s_cards[i], lv_color_hex(on ? C_GREEN : C_LINE), 0);
        lv_obj_set_style_border_width(s_cards[i], on ? 2 : 1, 0);
        lv_obj_set_style_bg_color(s_cards[i], lv_color_hex(on ? C_CARD : C_BG), 0);
        lv_obj_set_style_text_color(s_labels[i], lv_color_hex(on ? C_GREEN : C_TEXT), 0);
        if (!ITEMS[i].glyph) {                        // 位图图标随选中变绿；点阵图标用固定配色
            lv_obj_set_style_image_recolor(s_icons[i], lv_color_hex(on ? C_GREEN : 0xFFFFFF), 0);
            lv_obj_set_style_image_recolor_opa(s_icons[i], LV_OPA_COVER, 0);
        }
    }
}

static void tick(lv_timer_t *t)
{
    (void)t;
    int soc = bsp_battery_soc();
    char buf[16];
    if (soc < 0) snprintf(buf, sizeof buf, "--%%");
    else         snprintf(buf, sizeof buf, "%d%%", soc);
    lv_label_set_text(s_hp, buf);
    // HP 血条：按电量宽度 + 绿/黄/红三色（>=50 绿，>=20 黄，否则红）
    int pct = soc < 0 ? 0 : soc;
    uint32_t hc = pct >= 50 ? C_GREEN : pct >= 20 ? 0xE6B93C : 0xE05050;
    lv_obj_set_width(s_hp_bar, HP_BAR_W * pct / 100);
    lv_obj_set_style_bg_color(s_hp_bar, lv_color_hex(hc), 0);
    lv_obj_set_style_text_color(s_hp, lv_color_hex(hc), 0);
}

static void sync_tick(lv_timer_t *t)
{
    (void)t;
    if (ble_sync_take_updated()) apply_profile();
}

static void home_enter(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);

    // 头像
    s_av = lv_image_create(s_scr);
    lv_image_set_src(s_av, cha_random_72());
    lv_obj_set_pos(s_av, AV_X, AV_Y);

    // 标题（原顶栏文字，移到名称上方）
    label(s_scr, "WATCHA", &font_ps8, C_GREEN, INFO_X, TITLE_Y);

    // 角色名（24px 左对齐，超宽省略）
    s_name = label(s_scr, "", &font_fusion20, C_TEXT, INFO_X, NAME_Y);
    lv_obj_set_width(s_name, 240 - INFO_X - 6);
    lv_label_set_long_mode(s_name, LV_LABEL_LONG_DOT);

    // 身份 badge（RPG 文案，中英混排）
    s_badge = block(s_scr, INFO_X, BADGE_IY, 48, BADGE_H, C_BG);
    lv_obj_set_style_border_width(s_badge, 1, 0);
    s_badge_t = label(s_badge, "", &font_fusion12, C_MUTED, 0, 0);

    // 第一行 HP：爱心图标 + 血条（绿/黄/红，正常宽度）+ 百分比（16px，与其它 stats 统一）
    // 图标 11px / 血条 14px / 数值 16px，各自在行内垂直居中对齐
    lv_obj_set_pos(icon_make(s_scr, ICON_HP, ICON_CELL, 0xE05050), STAT_X, SROW1_Y + 3);
    block(s_scr, HP_BAR_X, SROW1_Y + 1, HP_BAR_W, HP_BAR_H, C_LINE);   // 血条底槽
    s_hp_bar = block(s_scr, HP_BAR_X, SROW1_Y + 1, HP_BAR_W, HP_BAR_H, C_GREEN);
    s_hp = label(s_scr, "--", &font_ps16, C_GREEN, HP_BAR_X + HP_BAR_W + 6, SROW1_Y);

    // 第二行 ATK / DEF：各自图标 + 原始数值（不换算）
    lv_obj_set_pos(icon_make(s_scr, ICON_ATK, ICON_CELL, C_GREEN), ATK_X, SROW2_Y + 3);
    s_atk = label(s_scr, "--", &font_ps16, C_TEXT, ATK_X + ICON_W + STAT_GAP, SROW2_Y);
    lv_obj_set_pos(icon_make(s_scr, ICON_DEF, ICON_CELL, C_GREEN), DEF_X, SROW2_Y + 3);
    s_def = label(s_scr, "--", &font_ps16, C_TEXT, DEF_X + ICON_W + STAT_GAP, SROW2_Y);

    // 第三行 Coins：金币图标 + 原始数值（独立一行）
    lv_obj_set_pos(icon_make(s_scr, ICON_COIN, ICON_CELL, C_GOLD), COIN_X, SROW3_Y + 3);
    s_coins = label(s_scr, "--", &font_ps16, C_GOLD, COIN_X + ICON_W + STAT_GAP, SROW3_Y);

    block(s_scr, 0, STAT_DIV_Y, 240, 1, C_LINE);
    ble_sync_take_updated();   // 进页即读最新，旧标志作废
    apply_profile();

    // 应用列表：上下翻页的长方形卡片，裁剪容器把漏出的相邻卡裁在可视区内
    lv_obj_t *clip = block(s_scr, 0, LIST_TOP, 240, LIST_VIS_H, C_BG);
    lv_obj_set_style_clip_corner(clip, true, 0);
    for (int i = 0; i < NITEMS; i++) {
        lv_obj_t *card = block(clip, LIST_X, 0, LIST_W, CARD_H, C_BG);
        lv_obj_set_style_radius(card, 6, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(C_LINE), 0);
        lv_obj_t *ic;
        if (ITEMS[i].glyph) {                         // 两色点阵图标（22×22，cell=2）
            ic = icon_make2(card, ITEMS[i].pat, 2, C_GREEN, C_GOLD);
        } else {                                      // 现成位图图标
            ic = lv_image_create(card);
            lv_image_set_src(ic, ITEMS[i].img);
        }
        lv_obj_align(ic, LV_ALIGN_LEFT_MID, 10, 0);
        s_icons[i] = ic;
        // 文字列：flex 纵向，标题 + 描述整体在卡片中线居中
        lv_obj_t *txt = lv_obj_create(card);
        lv_obj_remove_flag(txt, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(txt, 0, 0);
        lv_obj_set_style_border_width(txt, 0, 0);
        lv_obj_set_style_bg_opa(txt, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(txt, 0, 0);
        lv_obj_set_style_pad_row(txt, 3, 0);
        lv_obj_set_size(txt, LIST_W - 46, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_COLUMN);
        lv_obj_align(txt, LV_ALIGN_LEFT_MID, 42, 0);
        lv_obj_t *lb = label(txt, ITEMS[i].name, &font_fusion12, C_TEXT, 0, 0);
        lv_obj_t *ds = label(txt, ITEMS[i].desc, &font_fusion12, C_MUTED, 0, 0);
        s_cards[i] = card;
        s_labels[i] = lb;
        s_descs[i] = ds;
    }
    refresh_sel();

    // 底部两行
    block(s_scr, 0, 276, 240, 1, C_LINE);
    /* 大会纪念票根：日期 + 本机编号（蓝牙地址后 6 位，每台不同），两端像素小方块当打孔 */
    char tk[64];
    snprintf(tk, sizeof tk, "观猹大会 10.24 · #E6B93C No.%s#", ble_sync_device_id());
    lv_obj_t *l1 = label(s_scr, tk, &font_fusion12, C_MUTED, 0, 0);
    lv_label_set_recolor(l1, true);
    lv_obj_align(l1, LV_ALIGN_TOP_MID, 0, 284);
    lv_obj_update_layout(l1);
    {
        int tx0 = lv_obj_get_x(l1), tx1 = tx0 + lv_obj_get_width(l1);
        block(s_scr, tx0 - 10, 289, 4, 4, C_GOLD);
        block(s_scr, tx1 + 6,  289, 4, 4, C_GOLD);
    }
    char rec[128];
    uint32_t mbest = store_get_u32("melon_best", 0);
    uint32_t muyu  = store_get_u32("muyu_best", 0);
    uint32_t g1024 = store_get_u32("g1024_best", 0);
    char ms[16], mu[16], gs[16];
    if (mbest) snprintf(ms, sizeof ms, "%lums", (unsigned long)mbest); else snprintf(ms, sizeof ms, "--");
    if (muyu)  snprintf(mu, sizeof mu, "%lu",   (unsigned long)muyu);  else snprintf(mu, sizeof mu, "--");
    if (g1024) snprintf(gs, sizeof gs, "%lu",   (unsigned long)g1024); else snprintf(gs, sizeof gs, "--");
    snprintf(rec, sizeof rec, "吃瓜 %s · 功德 %s · 合成 %s", ms, mu, gs);
    lv_obj_t *l2 = label(s_scr, rec, &font_fusion12, C_MUTED, 0, 0);
    lv_obj_align(l2, LV_ALIGN_TOP_MID, 0, 300);

    s_timer = lv_timer_create(tick, 30000, NULL);
    s_sync_timer = lv_timer_create(sync_tick, 300, NULL);
    tick(NULL);
    lv_screen_load(s_scr);
}

static void home_exit(void)
{
    if (s_timer)      { lv_timer_delete(s_timer);      s_timer = NULL; }
    if (s_sync_timer) { lv_timer_delete(s_sync_timer); s_sync_timer = NULL; }
    if (s_scr)        { lv_obj_delete(s_scr);          s_scr = NULL;   }
}

static void home_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (btn == BSP_BTN_UP && ev == BSP_BTN_LONG) {          // 首页长按 ▲：进同步页
        sfx_play(SFX_OK);
        page_open(PAGE_SYNC);
        return;
    }
    if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {          // 首页长按确认：换一只猹
        if (s_av) lv_image_set_src(s_av, cha_reroll_72());
        sfx_play(SFX_POP);
        return;
    }
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_UP)   { s_sel = (s_sel + NITEMS - 1) % NITEMS; sfx_play(SFX_NAV); }
    if (btn == BSP_BTN_DOWN) { s_sel = (s_sel + 1) % NITEMS;         sfx_play(SFX_NAV); }
    if (btn == BSP_BTN_OK) {
        sfx_play(SFX_OK);
        switch (ITEMS[s_sel].act) {
        case ACT_GUIDE: page_open(PAGE_GUIDE); return;
        case ACT_LAB:   page_open(PAGE_LAB);   return;
        case ACT_APP:   page_intro_set(ITEMS[s_sel].app_idx + PAGE_APP_BASE); page_open(PAGE_INTRO); return;
        }
    }
    refresh_sel();
}

const page_t page_home_def = { "home", home_enter, home_exit, home_key };
