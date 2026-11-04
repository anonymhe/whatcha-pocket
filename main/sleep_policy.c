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

    if (in->radio_busy || in->usb_attached || in->standby_deferred) {
        // 三者效果相同，都封顶在 DIM：
        //   radio_busy   —— 蓝牙链路不跨 sleep，链路要活着；
        //   usb_attached —— 有个真实 USB 主机在通信，睡下去就断枚举、断串口；
        //   standby_deferred —— 刚因 USB 被拒睡，退避期内不再重复尝试。
        return in->idle_us >= policy->dim_after_us ? SLEEP_STAGE_DIM : SLEEP_STAGE_ACTIVE;
    }

    if (in->idle_us >= policy->sleep_after_us) return SLEEP_STAGE_SLEEP;
    if (in->idle_us >= policy->standby_after_us) return SLEEP_STAGE_STANDBY;
    if (in->idle_us >= policy->dim_after_us) return SLEEP_STAGE_DIM;
    return SLEEP_STAGE_ACTIVE;
}

bool sleep_policy_usb_defer_active(int64_t seen_us, int64_t now_us, int64_t grace_us)
{
    // 没见过主机：没有退避可言。
    if (seen_us <= 0) return false;
    if (grace_us <= 0) return false;
    const int64_t elapsed = now_us - seen_us;
    // 时钟倒退（now 早于 seen）：不知道真实间隔，按「还在窗口内」处理。
    // 这里宁可晚睡也不能早睡 —— 早睡的代价是把正在通信的串口断掉，
    // 晚睡只是多耗一点 USB 供电的电，而那本来就不吃电池。
    if (elapsed < 0) return true;
    return elapsed < grace_us;
}
