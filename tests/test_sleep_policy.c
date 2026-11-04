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
        .usb_attached = false,
        .standby_deferred = false,
    };
}

// USB 相关的两路输入单独成型，避免每个用例都摆一遍 6 个字段。
static sleep_input_t usb_input(int64_t idle_us, bool usb_attached, bool standby_deferred)
{
    return (sleep_input_t){
        .idle_us = idle_us,
        .ready = true,
        .usb_attached = usb_attached,
        .standby_deferred = standby_deferred,
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

    // USB 主机在场：与蓝牙同为「封顶 DIM」，但未操作时仍要能亮着。
    assert(stage(&p, usb_input(0, true, false)) == SLEEP_STAGE_ACTIVE);
    assert(stage(&p, usb_input(999, true, false)) == SLEEP_STAGE_ACTIVE);
    assert(stage(&p, usb_input(1000, true, false)) == SLEEP_STAGE_DIM);
    assert(stage(&p, usb_input(5000, true, false)) == SLEEP_STAGE_DIM);
    assert(stage(&p, usb_input(20000, true, false)) == SLEEP_STAGE_DIM);
    assert(stage(&p, usb_input(999999, true, false)) == SLEEP_STAGE_DIM);

    // 退避期：即便 USB 已经不在了（刚拔线），这段时间同样不入睡。
    assert(stage(&p, usb_input(0, false, true)) == SLEEP_STAGE_ACTIVE);
    assert(stage(&p, usb_input(1000, false, true)) == SLEEP_STAGE_DIM);
    assert(stage(&p, usb_input(5000, false, true)) == SLEEP_STAGE_DIM);
    assert(stage(&p, usb_input(999999, false, true)) == SLEEP_STAGE_DIM);

    // 两路同时为假时，阶梯必须与原来逐字一致（新增输入不能有副作用）。
    assert(stage(&p, usb_input(5000, false, false)) == SLEEP_STAGE_STANDBY);
    assert(stage(&p, usb_input(20000, false, false)) == SLEEP_STAGE_SLEEP);
    assert(stage(&p, usb_input(999, false, false)) == SLEEP_STAGE_ACTIVE);
    assert(stage(&p, usb_input(1000, false, false)) == SLEEP_STAGE_DIM);

    // caffeine 优先级最高：USB 在场时也一样冻结在 ACTIVE。
    assert(stage(&p, (sleep_input_t){ .idle_us = 999999, .ready = true,
                                      .caffeine = true, .usb_attached = true,
                                      .standby_deferred = true }) == SLEEP_STAGE_ACTIVE);
    // 开机动画未结束时 USB 检测也无权让屏幕降级。
    assert(stage(&p, (sleep_input_t){ .idle_us = 999999, .ready = false,
                                      .usb_attached = true }) == SLEEP_STAGE_ACTIVE);

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

    // USB 退避窗口。grace = 10 分钟。
    const int64_t grace = 600000000;
    // 没见过主机（0）或时钟异常：不算退避，别拿「永远不睡」当默认。
    assert(!sleep_policy_usb_defer_active(0, 1000, grace));
    assert(!sleep_policy_usb_defer_active(-5, 1000, grace));
    // 窗口内（含刚看到的那一刻）都还在退避；一到边界就放行。
    assert(sleep_policy_usb_defer_active(1000, 1000, grace));
    assert(sleep_policy_usb_defer_active(1000, 1001, grace));
    assert(sleep_policy_usb_defer_active(1000, 1000 + grace - 1, grace));
    assert(!sleep_policy_usb_defer_active(1000, 1000 + grace, grace));
    // 插两分钟就拔线：因为窗口从「最后一次看到」起算，拔线后仍有完整缓冲。
    const int64_t plug_in = 5000000;              // 插线时刻
    const int64_t unplug = plug_in + 120000000;   // 两分钟后拔线
    assert(sleep_policy_usb_defer_active(unplug, unplug, grace));
    assert(sleep_policy_usb_defer_active(unplug, unplug + grace - 1, grace));
    assert(!sleep_policy_usb_defer_active(unplug, unplug + grace, grace));
    // grace 非正数时不退避，避免配置错误造成永不入睡。
    assert(!sleep_policy_usb_defer_active(1000, 1000, 0));
    assert(!sleep_policy_usb_defer_active(1000, 1000, -1));

    // 按键不得抹掉「最后一次看到主机」的记录：mark_activity() 只重新武装日志，
    // 不清 seen。否则「插线 4 分钟 → 按一下键 → 立刻拔线」会只剩 idle 的 5 分钟，
    // 比承诺的 10 分钟缓冲更早入睡，正好丢掉要保的串口。
    // 这里用同一个 seen 值模拟「按键之后仍在窗口内」：仍在退避。
    assert(sleep_policy_usb_defer_active(unplug, unplug + 1000, grace));
    // 时钟倒退（now < seen）时按「仍在窗口内」处理：宁可晚睡，不可早睡。
    assert(sleep_policy_usb_defer_active(1000, 999, grace));

    return 0;
}
