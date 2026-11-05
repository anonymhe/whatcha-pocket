#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include "page_transition.h"

// 断言「先拆旧屏、再建新屏」：幕布已经载入，旧屏不是活动屏，可以先删。
// 反过来会让旧重屏、幕布、新重屏三张屏同时占着 LVGL 池，正是转场要避开的内存峰值。
static void test_with_old_page(void)
{
    page_act_t plan[PAGE_ACT_COUNT];
    const int n = page_transition_plan(true, plan);

    assert(n == 2);
    assert(plan[0] == PAGE_ACT_OLD_EXIT);
    assert(plan[1] == PAGE_ACT_NEW_ENTER);
}

// 首次进页没有旧屏可拆：只建新屏，且不能凭空多一步 OLD_EXIT。
static void test_first_page(void)
{
    page_act_t plan[PAGE_ACT_COUNT];
    const int n = page_transition_plan(false, plan);

    assert(n == 1);
    assert(plan[0] == PAGE_ACT_NEW_ENTER);
}

// 建新屏必须恰好一次，且必须是最后一步：旧屏清理完才轮到新屏分配。
static void test_enter_is_last_and_once(void)
{
    page_act_t plan[PAGE_ACT_COUNT];
    const int n = page_transition_plan(true, plan);

    int enters = 0, exits = 0;
    for (int i = 0; i < n; i++) {
        if (plan[i] == PAGE_ACT_NEW_ENTER) enters++;
        if (plan[i] == PAGE_ACT_OLD_EXIT) exits++;
    }
    assert(enters == 1);
    assert(exits == 1);
    assert(plan[n - 1] == PAGE_ACT_NEW_ENTER);
}

// 空指针不崩、不写。
static void test_null_out(void)
{
    assert(page_transition_plan(true, NULL) == 0);
    assert(page_transition_plan(false, NULL) == 0);
}

int main(void)
{
    test_with_old_page();
    test_first_page();
    test_enter_is_last_and_once();
    test_null_out();
    return 0;
}
