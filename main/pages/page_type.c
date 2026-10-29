// main/pages/page_type.c — 什么猹: AI人格测试
#include "pages.h"
#include "app_ui.h"
#include "sfx.h"
#include "storage.h"
#include <stdio.h>

LV_FONT_DECLARE(font_fusion12);
LV_FONT_DECLARE(font_ps8);
LV_FONT_DECLARE(font_ps16);
LV_FONT_DECLARE(font_fusion20);

/* 本命 AI 卡片左侧的 32x32 方块：观猹用自己的 logo；其他产品不放第三方 logo，
   用像素文字铭牌（像元素周期表的格子），顺序同 RESULTS */
LV_IMAGE_DECLARE(logo32_watcha);
static const lv_image_dsc_t *const LOGOS[16] = { [12] = &logo32_watcha };
static const char *const PLAQUE[16] = {
    "Cl", "Cf", "Mj", "Cu", "v0", "Fx", "Gk", "Px",
    "Lv", "NL", "DS", "Gm", NULL, "TD", "GPT", "豆",
};

/* 猹位：左下 72x64，左上角 (20,220)。换图时声明图片并定义 TYPE_CHA_IMG，例如
   LV_IMAGE_DECLARE(cha_type);  #define TYPE_CHA_IMG (&cha_type) */
#define TYPE_CHA_X 20
#define TYPE_CHA_Y 220
LV_IMAGE_DECLARE(art_type);
#define TYPE_CHA_IMG (&art_type)

typedef enum { ST_QUESTION, ST_RESULT } state_t;

/* ---- 题目数据 ---- */
typedef struct { const char *q; const char *a; const char *b; } qitem_t;

static const qitem_t QUESTIONS[8] = {
    { "一般怎么发现新的AI工具？",     "自己搜索研究",         "看到推荐了解"         },
    { "如何对待爆火的AI产品？",       "不意外，早就用了",     "好奇，先看评价"       },
    { "找到好用的AI工具后？",         "深入使用，轻易不换",   "新鲜感过了就下一个"   },
    { "日常使用的AI工具多吗？",       "固定几款，各有分工",   "很多，经常换新"       },
    { "使用AI工具时更享受什么？",     "探索它还能做什么",     "高效完成手上的事"     },
    { "上手一款新AI工具时？",         "先把各种功能都试一遍", "直接做需要做的事"     },
    { "发现好用的AI产品会？",         "主动分享",             "默默使用"             },
    { "用AI做出满意的成果后？",       "发出来让大家看看",     "存着自己欣赏就好"     },
};

/* ---- 结果数据（16 型） ----
   index = d0*8 + d1*4 + d2*2 + d3
   d0: 发现 0=M 1=F   d1: 深度 0=D 1=W
   d2: 方式 0=T 1=P   d3: 社交 0=S 1=Q */
typedef struct { const char *product; const char *name; const char *desc; } ritem_t;

static const ritem_t RESULTS[16] = {
    [0]  = { "Claude",        "转折大师猹",       "You're absolutely\nright! But..." }, /* MDTS */
    [1]  = { "ComfyUI",       "工作流匠猹",     "工作流比作品更精致"               }, /* MDTQ */
    [2]  = { "Midjourney",    "炼图传教士猹",     "--ar 16:9 --s 750"                }, /* MDPS */
    [3]  = { "Cursor",        "效率孤猹",       "先不跟我说话，\n我在写代码"         }, /* MDPQ */
    [4]  = { "v0",            "全栈先锋猹",       "朋友圈是AI周刊"                   }, /* MWTS */
    [5]  = { "Flux",          "开源炼丹猹",     "模型在我硬盘里\n我比较安心"         }, /* MWTQ */
    [6]  = { "Grok",          "冲浪前线猹",       "什么都知道，\n什么都敢说"           }, /* MWPS */
    [7]  = { "Perplexity",    "信息考据猹",     "我需要一个citation"               }, /* MWPQ */
    [8]  = { "Lovart",        "读心术士猹",       "需求我只说一遍"                   }, /* FDTS */
    [9]  = { "NotebookLM",    "吃灰克星猹",       "Welcome to the\nDeep Dive." },          /* FDTQ */
    [10] = { "DeepSeek",      "极简主义猹",       "Less is more"                     }, /* FDPS */
    [11] = { "Gemini",        "生态原住猹",     "我司雇我在"                       }, /* FDPQ */
    [12] = { "观猹",          "掌管测评的猹神",   "玩AI上观猹"                       }, /* FWTS */
    [13] = { "TokenDance",    "掌管API的猹神","Token is all\nyou need"             }, /* FWTQ */
    [14] = { "ChatGPT",       "数字氧气猹",       "我会稳稳接住你"                   }, /* FWPS */
    [15] = { "豆包",          "包的选手猹",       "让我用最直白最不\n绕弯的方式告诉你" }, /* FWPQ */
};

/* ---- 静态变量 ---- */
static lv_obj_t *s_scr;

static state_t s_state;
static int s_q_idx;            /* 当前题号 0-7 */
static int s_answers[8];       /* 每题答案 0=A(上) 1=B(下) */
static int s_sel;              /* 当前选择 0=A 1=B, -1=未选 */
static int s_result_idx;       /* 结果索引 0-15 */

/* ---- 工具 ---- */

static lv_obj_t *mk_label(lv_obj_t *par, const char *txt,
                           const lv_font_t *f, uint32_t c)
{
    lv_obj_t *l = lv_label_create(par);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
    return l;
}

static lv_obj_t *mk_rect(lv_obj_t *par, int x, int y, int w, int h, uint32_t c)
{
    lv_obj_t *r = lv_obj_create(par);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, w, h);
    lv_obj_set_pos(r, x, y);
    lv_obj_set_style_bg_color(r, lv_color_hex(c), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    return r;
}

static void hline(lv_obj_t *par, int y)
{
    mk_rect(par, 0, y, 240, 1, C_LINE);
}

/* ---- 计算结果 ---- */

static int calc_result(void)
{
    int d[4];
    for (int i = 0; i < 4; i++) {
        int sum = s_answers[i * 2] + s_answers[i * 2 + 1];
        if (sum == 0)      d[i] = 0;  /* both A */
        else if (sum == 2) d[i] = 1;  /* both B */
        else               d[i] = s_answers[i * 2]; /* tie: first Q */
    }
    return d[0] * 8 + d[1] * 4 + d[2] * 2 + d[3];
}

/* ---- 绘制页面 ---- */

static void draw_topbar(void)
{
    ui_topbar(s_scr, "什么猹");
}

static void draw_bottom(const char *txt)
{
    hline(s_scr, 292);
    lv_obj_t *l = mk_label(s_scr, txt, &font_fusion12, C_MUTED);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 298);
}

static void build_question(void)
{
    lv_obj_clean(s_scr);
    lv_obj_set_style_border_width(s_scr, 0, 0);   /* 结果页的绿框不带到题目页 */
    draw_topbar();

    /* 题号 */
    char num[16];
    snprintf(num, sizeof num, "%d / 8", s_q_idx + 1);
    lv_obj_t *nl = mk_label(s_scr, num, &font_ps8, C_MUTED);
    lv_obj_align(nl, LV_ALIGN_TOP_MID, 0, 36);

    /* 题目 — fusion16，设宽度自动换行 */
    const qitem_t *qi = &QUESTIONS[s_q_idx];
    lv_obj_t *ql = mk_label(s_scr, qi->q, &font_fusion12, C_TEXT);
    lv_obj_set_style_text_align(ql, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(ql, 216);
    lv_obj_align(ql, LV_ALIGN_TOP_MID, 0, 56);

    /* 选项 A（上键）— fusion16 */
    lv_obj_t *au = mk_label(s_scr, "[▲]", &font_fusion12,
                             s_sel == 0 ? C_GREEN : C_LINE);
    lv_obj_align(au, LV_ALIGN_TOP_MID, 0, 110);

    lv_obj_t *al = mk_label(s_scr, qi->a, &font_fusion12,
                             s_sel == 0 ? C_GREEN : C_MUTED);
    lv_obj_set_style_text_align(al, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(al, 216);
    lv_obj_align(al, LV_ALIGN_TOP_MID, 0, 128);

    /* 分割线 */
    hline(s_scr, 170);

    /* 选项 B（下键）— fusion16 */
    lv_obj_t *bl = mk_label(s_scr, qi->b, &font_fusion12,
                             s_sel == 1 ? C_GREEN : C_MUTED);
    lv_obj_set_style_text_align(bl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(bl, 216);
    lv_obj_align(bl, LV_ALIGN_TOP_MID, 0, 195);

    lv_obj_t *bd = mk_label(s_scr, "[▼]", &font_fusion12,
                             s_sel == 1 ? C_GREEN : C_LINE);
    lv_obj_align(bd, LV_ALIGN_TOP_MID, 0, 220);
}

static void build_result(void)
{
    lv_obj_clean(s_scr);
    draw_topbar();

    const ritem_t *ri = &RESULTS[s_result_idx];

    lv_obj_t *sub = mk_label(s_scr, "你是", &font_fusion12, C_MUTED);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 46);

    /* 人格名：页面上最大最显眼 */
    lv_obj_t *nml = mk_label(s_scr, ri->name, &font_fusion20, C_GREEN);
    lv_obj_align(nml, LV_ALIGN_TOP_MID, 0, 62);

    /* 金句：24px，1-2 行（数据里用 \n 手动断行），在 y=86..206 内垂直居中 */
    int lines = 1;
    for (const char *p = ri->desc; *p; p++) if (*p == '\n') lines++;
    lv_obj_t *dl = mk_label(s_scr, ri->desc, &font_fusion20, C_TEXT);
    lv_obj_set_style_text_align(dl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(dl, 4, 0);
    lv_obj_set_width(dl, 232);
    lv_obj_align(dl, LV_ALIGN_TOP_MID, 0, 86 + (120 - lines * 28) / 2);

    /* 右下产品卡：logo + 「本命AI」+ 产品名 */
    mk_rect(s_scr, 94, 222, 129, 49, C_GREEN_D);
    mk_rect(s_scr, 95, 223, 127, 47, C_CARD);
    if (LOGOS[s_result_idx]) {
        lv_obj_t *lg = lv_image_create(s_scr);
        lv_image_set_src(lg, LOGOS[s_result_idx]);
        lv_obj_set_pos(lg, 102, 230);
    } else {
        const char *t = PLAQUE[s_result_idx];
        bool ascii = ((unsigned char)t[0]) < 0x80;
        mk_rect(s_scr, 102, 230, 32, 32, C_GREEN);
        mk_rect(s_scr, 104, 232, 28, 28, C_CARD);
        lv_obj_t *pt = mk_label(s_scr, t, ascii ? &font_ps8 : &font_fusion12, C_GREEN);
        lv_obj_set_width(pt, 32);
        lv_obj_set_style_text_align(pt, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(pt, 102, ascii ? 242 : 240);
    }
    lv_obj_t *cap = mk_label(s_scr, "本命AI", &font_fusion12, C_MUTED);
    lv_obj_set_pos(cap, 142, 229);
    lv_obj_t *pl = mk_label(s_scr, ri->product, &font_fusion12, C_TEXT);
    lv_obj_set_pos(pl, 142, 246);

    /* 左下猹位：暂时留空 */
#ifdef TYPE_CHA_IMG
    lv_obj_t *ch = lv_image_create(s_scr);
    lv_image_set_src(ch, TYPE_CHA_IMG);
    lv_obj_set_pos(ch, TYPE_CHA_X, TYPE_CHA_Y);
#endif

    /* 完成闪屏 */
    lv_obj_set_style_border_color(s_scr, lv_color_hex(C_GREEN), 0);
    lv_obj_set_style_border_width(s_scr, 2, 0);

    /* 存结果到 NVS */
    store_set_u32("type_result", (uint32_t)s_result_idx);

    draw_bottom("小猹曰：未知猹 焉知己");
}

/* ---- 状态机 ---- */

static void enter_state(state_t st)
{
    s_state = st;
    switch (st) {
    case ST_QUESTION:
        s_sel = -1;
        build_question();
        break;
    case ST_RESULT:
        s_result_idx = calc_result();
        sfx_play(SFX_REVEAL);
        build_result();
        break;
    }
}

/* ---- 页面接口 ---- */

static void page_enter(void)
{
    s_scr = page_screen_create();
    s_q_idx = 0;
    for (int i = 0; i < 8; i++) s_answers[i] = 0;

    enter_state(ST_QUESTION);
    lv_screen_load(s_scr);
}

static void page_exit(void)
{
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; }
}

static void page_key_cb(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return;

    switch (s_state) {


    case ST_QUESTION:
        if (btn == BSP_BTN_UP) {
            if (s_sel == 0) {
                /* 已选A，再按上 = 确认A，下一题 */
                s_answers[s_q_idx] = 0;
                s_q_idx++; sfx_play(SFX_BLIP);
                if (s_q_idx >= 8) enter_state(ST_RESULT);
                else              enter_state(ST_QUESTION);
            } else {
                s_sel = 0;
                build_question();
            }
        }
        else if (btn == BSP_BTN_DOWN) {
            if (s_sel == 1) {
                /* 已选B，再按下 = 确认B，下一题 */
                s_answers[s_q_idx] = 1;
                s_q_idx++; sfx_play(SFX_BLIP);
                if (s_q_idx >= 8) enter_state(ST_RESULT);
                else              enter_state(ST_QUESTION);
            } else {
                s_sel = 1;
                build_question();
            }
        }
        break;

    case ST_RESULT:
        if (btn == BSP_BTN_OK) {
            s_q_idx = 0;
            for (int i = 0; i < 8; i++) s_answers[i] = 0;
            enter_state(ST_QUESTION);
        }
        break;
    }
}

const page_t page_type = { "page_type", page_enter, page_exit, page_key_cb };
