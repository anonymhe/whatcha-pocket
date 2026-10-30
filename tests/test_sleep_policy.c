#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include "sleep_policy.h"

static sleep_policy_t policy(int64_t dim, int64_t standby, int64_t sleep)
{
    sleep_policy_t p;
    sleep_policy_init(&p, dim, standby, sleep);
    return p;
}

static sleep_input_t input(int64_t idle_us, bool ready, bool caffeine, bool radio_busy)
{
    return (sleep_input_t){
        .idle_us = idle_us,
        .ready = ready,
        .caffeine = caffeine,
        .radio_busy = radio_busy,
    };
}

static sleep_stage_t stage(const sleep_policy_t *p, sleep_input_t in)
{
    return sleep_policy_stage(p, &in);
}

int main(void)
{
    const sleep_policy_t p = policy(1000, 5000, 20000);

    // 正常阶梯。
    assert(stage(&p, input(0, true, false, false)) == SLEEP_STAGE_ACTIVE);
    assert(stage(&p, input(999, true, false, false)) == SLEEP_STAGE_ACTIVE);
    assert(stage(&p, input(1000, true, false, false)) == SLEEP_STAGE_DIM);
    assert(stage(&p, input(4999, true, false, false)) == SLEEP_STAGE_DIM);
    assert(stage(&p, input(5000, true, false, false)) == SLEEP_STAGE_STANDBY);
    assert(stage(&p, input(19999, true, false, false)) == SLEEP_STAGE_STANDBY);
    assert(stage(&p, input(20000, true, false, false)) == SLEEP_STAGE_SLEEP);

    // 开机动画未结束：任何空闲时长都不降级。
    assert(stage(&p, input(999999, false, false, false)) == SLEEP_STAGE_ACTIVE);

    // caffeine：把阶梯冻结在 ACTIVE，即使蓝牙同时在用。
    assert(stage(&p, input(999999, true, true, false)) == SLEEP_STAGE_ACTIVE);
    assert(stage(&p, input(999999, true, true, true)) == SLEEP_STAGE_ACTIVE);

    // 蓝牙占用：可以压暗，但绝不越过 STANDBY/SLEEP。
    assert(stage(&p, input(0, true, false, true)) == SLEEP_STAGE_ACTIVE);
    assert(stage(&p, input(1000, true, false, true)) == SLEEP_STAGE_DIM);
    assert(stage(&p, input(999999, true, false, true)) == SLEEP_STAGE_DIM);

    // 时钟回绕（负空闲）按刚操作过处理。
    assert(stage(&p, input(-1, true, false, false)) == SLEEP_STAGE_ACTIVE);

    // 空指针不崩、不误降级。
    assert(sleep_policy_stage(NULL, &(sleep_input_t){ .ready = true }) == SLEEP_STAGE_ACTIVE);
    assert(sleep_policy_stage(&p, NULL) == SLEEP_STAGE_ACTIVE);

    // 阈值配反：后一段被拉到前一段，不会出现「越晚越不睡」。
    const sleep_policy_t inverted = policy(5000, 1000, 2000);
    assert(inverted.standby_after_us == 5000);
    assert(inverted.sleep_after_us == 5000);
    assert(stage(&inverted, input(5000, true, false, false)) == SLEEP_STAGE_SLEEP);

    // 负阈值归零，且仍然保持单调。
    const sleep_policy_t negative = policy(-1, -1, -1);
    assert(negative.dim_after_us == 0);
    assert(negative.standby_after_us == 0);
    assert(negative.sleep_after_us == 0);
    assert(stage(&negative, input(0, true, false, false)) == SLEEP_STAGE_SLEEP);

    // 关闭 deep sleep（power.c 用的写法）：SLEEP 永远不可达。
    const sleep_policy_t no_deep_sleep = policy(1000, 5000, INT64_MAX);
    assert(stage(&no_deep_sleep, input(INT64_MAX - 1, true, false, false)) == SLEEP_STAGE_STANDBY);

    return 0;
}
