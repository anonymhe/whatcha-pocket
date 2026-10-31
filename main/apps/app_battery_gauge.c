// main/apps/app_battery_gauge.c —— 首页 RPG 血条式电量计
#include "app_battery_gauge.h"
#include "app_ui.h"
#include "bsp_battery.h"

// 行内布局：全部从「整行左上角」(x, y) 派生，和同一行其它数值对齐。
#define GAUGE_ICON_DY  3    // 图标比文字下沉 3px（11px 图标在 16px 文字行里视觉居中）
#define GAUGE_BAR_DY   1    // 血槽再往下 1px
#define GAUGE_GAP      6    // 图标→血槽、血槽→百分比的水平间距

// 11×11 爱心点阵。首页只负责把这一行放在哪，图形本身归本模块所有。
static const char *const GAUGE_ICON_HP[11] = {
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

typedef struct {
    lv_obj_t *bar;     // 血槽填充，宽度按电量变化
    lv_obj_t *label;   // 百分比
    int       bar_w;   // 血槽满宽
} gauge_t;

static void gauge_on_delete(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

// 注册表回调：读到一帧电量就重绘这一行。只碰自己的三个对象。
static void gauge_paint(void *ctx, int soc)
{
    gauge_t *g = ctx;
    if (!g) return;

    char buf[16];
    ui_battery_text(buf, sizeof buf, soc);
    lv_label_set_text(g->label, buf);

    // 读不到电量时血条按 0 宽显示，颜色仍取「不报警」的绿：一次 I2C 失败不该变红。
    const int pct = soc < 0 ? 0 : soc;
    const uint32_t col = ui_battery_color(soc);
    lv_obj_set_width(g->bar, g->bar_w * pct / 100);
    lv_obj_set_style_bg_color(g->bar, lv_color_hex(col), 0);
    lv_obj_set_style_text_color(g->label, lv_color_hex(col), 0);
}

lv_obj_t *ui_battery_gauge_create(lv_obj_t *parent, int x, int y, int bar_w, int bar_h,
                                  const lv_font_t *font, uint32_t icon_color)
{
    if (!parent || bar_w <= 0 || bar_h <= 0) return NULL;

    gauge_t *g = lv_malloc(sizeof *g);
    if (!g) return NULL;
    g->bar_w = bar_w;

    // 点阵格子：图标固定 11×11 格，格子边长由血槽高度定（本项目是 1px）。
    const int cell  = bar_h >= 11 ? bar_h / 11 : 1;
    const int icon_w = 11 * cell;
    const int bar_x  = icon_w + GAUGE_GAP;
    // 容器本身不画东西，只作为注册表里的「根对象」：屏销毁时它先收到 LV_EVENT_DELETE，
    // 据此把注册项和 g 一起释放。LVGL 默认把子对象裁在自己范围内，所以宽度要覆盖
    // 到最宽的百分比文字本身，不能只按血槽算。
    int text_w = 0;
    for (const char *p = "100%"; *p; p++) text_w += lv_font_get_glyph_width(font, *p, '0');
    const int root_w = bar_x + bar_w + GAUGE_GAP + text_w;
    const int root_h = bar_h + GAUGE_ICON_DY + 2;

    lv_obj_t *root = ui_block(parent, x, y, root_w, root_h, 0x000000);
    if (!root) { lv_free(g); return NULL; }
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    // 注销回调先挂上：下面任何一步失败都靠删 root 来同时释放 g。
    lv_obj_add_event_cb(root, gauge_on_delete, LV_EVENT_DELETE, g);

    // 爱心图标：每行连续亮点合并成一条横块，省 LVGL 对象。
    lv_obj_t *icon = ui_block(root, 0, GAUGE_ICON_DY, icon_w, icon_w, 0x000000);
    if (!icon) { lv_obj_delete(root); return NULL; }
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
    for (int r = 0; r < 11; r++) {
        const char *row = GAUGE_ICON_HP[r];
        int c = 0;
        while (c < 11) {
            if (row[c] != '#') { c++; continue; }
            int run = 1;
            while (c + run < 11 && row[c + run] == '#') run++;
            ui_block(icon, c * cell, r * cell, run * cell, cell, icon_color);
            c += run;
        }
    }

    ui_block(root, bar_x, GAUGE_BAR_DY, bar_w, bar_h, C_LINE);        // 血槽底槽
    g->bar = ui_block(root, bar_x, GAUGE_BAR_DY, bar_w, bar_h, C_GREEN);

    g->label = ui_label(root, "--%", font, C_GREEN);
    lv_obj_set_pos(g->label, bar_x + bar_w + GAUGE_GAP, 0);
    if (!g->bar || !g->label) { lv_obj_delete(root); return NULL; }

    ui_battery_register(root, gauge_paint, g);
    gauge_paint(g, bsp_battery_soc());   // 建好即出数，不用等下一次刷新节奏
    return root;
}
