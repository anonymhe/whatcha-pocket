// main/page_transition.c —— 切页动作顺序（纯逻辑，无 LVGL/ESP-IDF 依赖）
#include "page_transition.h"

#include <stddef.h>

int page_transition_plan(bool has_old, page_act_t out[PAGE_ACT_COUNT])
{
    if (!out) return 0;

    int n = 0;
    if (has_old) out[n++] = PAGE_ACT_OLD_EXIT;
    out[n++] = PAGE_ACT_NEW_ENTER;
    return n;
}
