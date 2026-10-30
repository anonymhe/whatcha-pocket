// main/sleep_policy.c —— 降级判据，见 sleep_policy.h。
#include "sleep_policy.h"

static int64_t clamp_not_earlier(int64_t value, int64_t floor_us)
{
    if (value < floor_us) return floor_us;
    return value;
}

void sleep_policy_init(sleep_policy_t *policy, int64_t dim_after_us,
                       int64_t standby_after_us, int64_t sleep_after_us)
{
    if (!policy) return;

    if (dim_after_us < 0) dim_after_us = 0;
    standby_after_us = clamp_not_earlier(standby_after_us, dim_after_us);
    sleep_after_us = clamp_not_earlier(sleep_after_us, standby_after_us);

    policy->dim_after_us = dim_after_us;
    policy->standby_after_us = standby_after_us;
    policy->sleep_after_us = sleep_after_us;
}

sleep_stage_t sleep_policy_stage(const sleep_policy_t *policy, const sleep_input_t *in)
{
    if (!policy || !in || !in->ready) return SLEEP_STAGE_ACTIVE;
    if (in->caffeine) return SLEEP_STAGE_ACTIVE;

    // 时钟回绕或首帧（idle 为负）按刚操作过处理，先亮着。
    if (in->idle_us < 0) return SLEEP_STAGE_ACTIVE;

    if (in->radio_busy) {
        // 蓝牙不跨 sleep：链路要活着，最多只能压暗。
        return in->idle_us >= policy->dim_after_us ? SLEEP_STAGE_DIM : SLEEP_STAGE_ACTIVE;
    }

    if (in->idle_us >= policy->sleep_after_us) return SLEEP_STAGE_SLEEP;
    if (in->idle_us >= policy->standby_after_us) return SLEEP_STAGE_STANDBY;
    if (in->idle_us >= policy->dim_after_us) return SLEEP_STAGE_DIM;
    return SLEEP_STAGE_ACTIVE;
}
