#include <assert.h>
#include <stdint.h>
#include "countdown.h"

#define SEC ((int64_t)1000000)

// 定时器迟到：回调晚了 3 秒，剩余时间必须按真实流逝算，而不是只少 1 秒。
// 这是「25 分钟跑成 26 分钟」的回归点。
static void test_late_tick_counts_real_elapsed(void)
{
    const int64_t start = 1000 * SEC;
    const int64_t deadline = countdown_deadline_from(start, 1500);

    assert(countdown_left_secs(deadline, start) == 1500);
    assert(countdown_left_secs(deadline, start + 1 * SEC) == 1499);
    // 回调整整迟到 3 秒：剩余必须是 1497，不是 1499。
    assert(countdown_left_secs(deadline, start + 4 * SEC) == 1496);
}

// 一整段卡顿（比如 UI 卡了 20 秒）只跳一次显示，但剩余时间照样扣满 20 秒。
static void test_stall_does_not_stretch_the_session(void)
{
    const int64_t start = 5000 * SEC;
    const int64_t deadline = countdown_deadline_from(start, 1500);

    assert(countdown_left_secs(deadline, start + 20 * SEC) == 1480);
    // 卡顿之后紧接着到期：必须判为 0，而不是还剩很多。
    assert(countdown_left_secs(deadline, deadline - SEC / 2) == 1);
    assert(countdown_left_secs(deadline, deadline) == 0);
    assert(countdown_left_secs(deadline, deadline + 60 * SEC) == 0);
}

// 向上取整：开头完整显示 25:00，而不是立刻跳成 24:59。
// 只要还剩一点零头（哪怕 1 微秒），读数就仍是 25:00；零头耗尽才变 24:59。
static void test_rounds_up_so_the_first_second_reads_full(void)
{
    const int64_t start = 0;
    const int64_t deadline = countdown_deadline_from(start, 1500);

    assert(countdown_left_secs(deadline, start) == 1500);
    assert(countdown_left_secs(deadline, 1) == 1500);
    assert(countdown_left_secs(deadline, SEC - 1) == 1500);
    // 正好走满 1 秒：零头用尽，读数落到 24:59。
    assert(countdown_left_secs(deadline, SEC) == 1499);
    assert(countdown_left_secs(deadline, SEC + 1) == 1499);
}

// 暂停：把剩余从截止时刻结算下来；停在 0 秒时钳到 1 秒，
// 否则「继续」会立刻判定完成，等于跳过剩余的那一点点时间。
static void test_pause_settles_remaining(void)
{
    const int64_t start = 0;
    const int64_t deadline = countdown_deadline_from(start, 600);

    assert(countdown_pause_secs(deadline, start + 100 * SEC) == 500);
    assert(countdown_pause_secs(deadline, deadline) == 1);
    assert(countdown_pause_secs(deadline, deadline + 30 * SEC) == 1);
    // 未设定截止时刻（0）时同样钳到 1，不会给出 0 或负数。
    assert(countdown_pause_secs(0, 12345) == 1);
}

// 恢复：从当前剩余重新起算，暂停期间流逝的时间不算。
static void test_resume_restarts_from_remaining(void)
{
    const int64_t paused_at = 200 * SEC;
    const int64_t resumed = countdown_deadline_from(paused_at, 500);

    assert(countdown_left_secs(resumed, paused_at) == 500);
    // 暂停了很久再恢复：剩余仍是 500 秒，不该被暂停时长吃掉。
    assert(countdown_left_secs(resumed, paused_at) == 500);
    assert(countdown_left_secs(resumed, paused_at + 10 * SEC) == 490);
}

// 未设定截止时刻返回 0；非正的时长立即到期。
static void test_unset_and_nonpositive(void)
{
    assert(countdown_left_us(0, 12345) == 0);
    assert(countdown_left_secs(0, 12345) == 0);
    assert(countdown_left_us(-5, 12345) == 0);
    assert(countdown_deadline_from(777, 0) == 777);
    assert(countdown_deadline_from(777, -3) == 777);
}

// 微秒级结算本身。
static void test_microseconds(void)
{
    const int64_t deadline = countdown_deadline_from(0, 2);
    assert(countdown_left_us(deadline, 0) == 2 * SEC);
    assert(countdown_left_us(deadline, SEC) == SEC);
    assert(countdown_left_us(deadline, 2 * SEC) == 0);
    assert(countdown_left_us(deadline, 3 * SEC) == 0);
}

int main(void)
{
    test_late_tick_counts_real_elapsed();
    test_stall_does_not_stretch_the_session();
    test_rounds_up_so_the_first_second_reads_full();
    test_pause_settles_remaining();
    test_resume_restarts_from_remaining();
    test_unset_and_nonpositive();
    test_microseconds();
    return 0;
}
