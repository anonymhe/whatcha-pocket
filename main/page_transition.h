// main/page_transition.h —— 切页动作的执行顺序（纯逻辑，可在主机上测试）
//
// 转场插入了一张极轻的幕布屏做屏障，目的就是让「旧重屏 + 幕布 + 新重屏」的三屏
// 并存不出现。要做到这一点，动作顺序必须是「先拆旧屏、再建新屏」：幕布载入之后
// 旧屏已经不是活动屏，LVGL 允许删除它，此刻池子里只剩幕布；接着才建新屏。
// 反过来先建新屏，屏障就白插了，峰值仍是两张重屏加一张幕布。
//
// 顺序写成数据而不是直接写死在 pages.c 的 if/else 里，是为了让主机测试能断言它，
// 而不是靠读代码确认。
#pragma once

#include <stdbool.h>

typedef enum {
    PAGE_ACT_OLD_EXIT = 0,   // 调用旧页的 exit()：删定时器、删屏
    PAGE_ACT_NEW_ENTER,      // 调用新页的 enter()：建屏并载入
    PAGE_ACT_COUNT,
} page_act_t;

// 按执行顺序填充 out（容量至少 PAGE_ACT_COUNT），返回要执行的动作个数。
// has_old=false（首次进页，无旧屏可拆）时跳过 PAGE_ACT_OLD_EXIT。
int page_transition_plan(bool has_old, page_act_t out[PAGE_ACT_COUNT]);
