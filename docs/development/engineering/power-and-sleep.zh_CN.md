<p align="right">
  <strong>简体中文</strong> · <a href="power-and-sleep.md">English</a>
</p>

# 电源状态与空闲休眠

本文说明固件里的空闲降级阶梯、每一层各自负责什么，以及 MCU 入睡前必须满足哪些前提。

## 五个状态

完整状态集定义在 `main/sleep_policy.h`。其中只有四个能由软件进入；`OFF` 列在那里，是为了让阶梯和所有枚举它的文档保持完整。

| 状态 | 进入条件 | 做了什么 | 如何结束 |
| --- | --- | --- | --- |
| `ACTIVE` | 刚按键，或页面持有 `POWER_CAFFEINE` | 背光 100%，一切照常 | 空闲超时 |
| `DIM` | 30 秒无按键 | 背光 10%，其余不变 | 任意按键，或继续空闲 |
| `STANDBY` | 5 分钟无按键 | codec 下电、背光 10%，MCU 进 **light sleep** | 任意按键（或可选的定时唤醒） |
| `SLEEP` | 60 分钟无按键，**默认关闭** | deep sleep；面板、背光、codec、电量计逐个下电 | 按键，但芯片是冷启动 |
| `OFF` | 用户长按机身电源键 | 硬件断电 | 电源键 |

`OFF` 不是固件状态。电源键没有接到 MCU，固件既看不到它，也进不去这一态；列出来只是为了让状态集和所有逐条列举状态的文档都不缺一块。

## 为什么 `STANDBY` 用 light sleep 而不是 deep sleep

ST7789 面板在 3.3 V 轨带电时自己按帧刷新 GRAM，所以 light sleep 中的 MCU 只要把静态画面留在屏上，画面就还在。deep sleep 同样会保住画面，但背光由 LEDC 驱动，而 deep sleep 会关掉 LEDC，工牌就只剩一块黑屏。因此 light sleep 换来的是「屏幕仍然可读，而 CPU 几乎不耗电」。

`CONFIG_PM_ENABLE` 是**故意**不打开的：固件自己调用 `esp_light_sleep_start()`，不走自动 tickless idle。手动入口本身会停掉再重启任务看门狗，并用 RTC 重算 `esp_timer`，这套设计需要的它都已经做了。手动路径**不会**补 FreeRTOS tick，所以每一处空闲判断都用 `esp_timer_get_time()`，绝不用 tick 计数。

## 谁判断，谁执行

| 层 | 文件 | 职责 |
| --- | --- | --- |
| 策略 | `main/sleep_policy.c` | 纯函数：（空闲时长、是否就绪、caffeine、蓝牙占用）→ 状态；不碰 IDF、不碰 LVGL，有 host test 覆盖 |
| 应用 | `main/power.c` | 掌管计时器，执行背光与 codec 变更，驱动电量读数的刷新节奏，调用 light/deep sleep |
| 板级 | `components/bsp/bsp_power.c` | 装/卸唤醒源、入睡、逐个下电外设 |

页面不直接调 BSP 的睡眠接口，而是调用 `main/power.h` 里的 `power_hold()`：

- `POWER_CAFFEINE` 把阶梯冻结在 `ACTIVE`。番茄猹在计时期间持有它：那个界面必须常亮且每秒刷新，既不能调暗，也绝不能进 `STANDBY`。释放后从那一刻重新计时。
- `POWER_RADIO_ACTIVE` 允许压暗但禁止入睡。蓝牙链路不跨 sleep，所以同步页在广播期间持有它。蓝牙链路本身在同步成功的瞬间就被释放（完成回调里调 `ble_sync_release_link()`），而不是等到离页，于是手机会被立即断开，而不是在两秒的「完成」提示期间继续占着。NimBLE 主机在设备全生命周期常驻 —— 按页切换的只是广播和当前连接。

两者都是位标志、可以叠加。页面忘记释放会让设备再也睡不下去，因此每个 hold 在页面 `exit` 里都有对应的释放。

## 唤醒脚是从 ADC 借来的

板上三个按键共用一个输入脚：GPIO0，一条带外部 10 kΩ 上拉的 ADC 电阻阶梯。它是唯一可用的唤醒脚，所以 `bsp_power_wake_source_arm()` 会把它临时改成低电平触发的数字输入，`bsp_power_wake_source_disarm()` 再把它还给 `ADC1_CH0`。

`adc_oneshot_read()` 不动引脚。只有 `adc_oneshot_config_channel()` 会把 pad 重新接到模拟功能上，所以每次唤醒后都靠 `bsp_button_restore_adc()` 重配一次通道。漏掉这一步，三个按键读到的电压全是错的。

两个值得记住的坑：

- `esp_deep_sleep_enable_gpio_wakeup()` 的第一个参数是**引脚位掩码**，不是引脚号。正确的是 `1ULL << 0`；传 `0` 会被判非法，若又忽略返回值，就得到一次没有任何唤醒源的睡眠。
- `sdkconfig.defaults` 里 `CONFIG_ESP_SLEEP_GPIO_ENABLE_INTERNAL_RESISTORS` 设为 `n`。板上已有外部上拉，再叠内部上拉会把整条分压阶梯抬高，三个按键的电压窗口随之漂移。

## STANDBY 期间的定期唤醒与电量读数

顶栏里的电量百分比要在两种截然不同的场景下跟上真实情况：用户正在某个界面操作时，以及设备在 `STANDBY` 里 light sleep 时。比起让每个界面各自跑一个刷新定时器，`main/power.c` 把两者的刷新节奏都握在手里，这样「何时刷新」只有一个来源。

- 节奏是一个间隔，由 `power.c` 里的 `BATTERY_REFRESH_*` 常量定义：默认 30s，电量掉到 50% 或以下时拉长到 60s。`bsp_battery_soc()` 返回 `-1`（读失败）按「没掉到低电」处理，用默认间隔。
- 醒着时（`ACTIVE`/`DIM`），`power_service()` 每 500 ms 跑一次，调 `maybe_refresh_battery()`，只在间隔到了才重绘。
- 在 `STANDBY`，同一个间隔会被装成定时唤醒源，与按键并列。`power_set_standby_refresh_us()` 默认留在 `0`，表示「不覆盖，按电量自动选间隔」。设成非 0 则强制用该固定周期，留给设置页或调试。

UI 侧不再握有任何节奏。`main/apps/app_ui.c` 维一份电量标签注册表；`ui_battery_refresh_all()` 重绘它们，标签在 `LV_EVENT_DELETE` 里自动注销，因此链表不会比所属屏幕活得长。这就是消除之前那个耦合的做法——过去每个标签各带一个独立的 30s `lv_timer`，它只因为恰好跟唤醒周期对齐，才能在 `STANDBY` 里做出可见的刷新。

首页那条 HP 血条式的电量是页面自己的组件，不是顶栏标签，所以它保留自己的 30s 定时器。它在定时唤醒时依旧能正确刷新，因为下面的 tick resume 路径把每个 LVGL 定时器都重基到了当下。

定时唤醒**不算**操作：它不能重置空闲计时，否则会定期刷新的界面每次醒来都把自己续成 `ACTIVE`，永远睡不回去。`bsp_power_light_sleep()` 会回报唤醒原因，`enter_standby()` 在 `BSP_WAKE_CAUSE_TIMER` 时先刷一次电量再直接睡回去，状态与亮度都保持原样。只有按键叫醒（或本次入睡被拒）才重置计时并回到 `ACTIVE`。

要保持屏幕可见，背光必须能扛过 light sleep，而它本来就能：`bsp_display.c` 一次性把 LEDC 通道配成 `LEDC_SLEEP_MODE_KEEP_ALIVE`，时钟源配成 `LEDC_USE_RC_FAST_CLK`（APB 在睡眠期间停止）。运行期不需要任何切换，所以「屏幕亮不亮」只由占空比决定，`STANDBY` 因此只是把占空比压低，而不是关掉输出。

## 为什么睡多久唤醒都不会变慢

LVGL 的 tick 在 light sleep 期间是停的（`bsp_lvgl_tick_stop()`），所以唤醒时 `bsp_lvgl_tick_resume(slept_ms)` 会把 `lv_tick` 一下往前跳整段睡眠时长，让时钟和倒计时和真实时间对得上。但单跳 tick 就是个坑：调度器下一拍会发现每个周期性 `lv_timer` 都「欠跑了整段睡眠」，于是在同一次 `lv_timer_handler` 里把每个欠跑的周期都补跑一遍（10ms 周期欠一小时 ≈ 36 万次），把界面堵死数秒，表现为睡得越久、唤醒越慢。所以 `bsp_lvgl_tick_resume()` 还会把每个 timer 的 `last_run` 重置到「现在」，把调度器重基到当下而不撤销 tick 跳变。这也是电量和首页定时器能在定时唤醒时及时触发的原因。

## 重新打开 deep sleep

`main/power.c` 里有：

```c
#define POWER_ENABLE_DEEP_SLEEP 0
```

改成 `1`，`SLEEP` 就会在 `SLEEP_AFTER_US` 之后可达。从它唤醒是应用冷启动：`bsp_power_boot_cause()` 会报告 `BSP_WAKE_CAUSE_BUTTON`，屏上保留的内容全部丢失。如果在意这点，重放开机动画前先查 `power_booted_from_sleep()`。

## 真机验证

```text
Build: PASS / FAIL            ./tools/validate.sh --firmware
Host tests: PASS / FAIL       ./tools/validate.sh --static
Device tests: PASS / FAIL     下面的步骤
```

host test 覆盖不到、必须上机确认的项：

1. 静置 5 分钟。屏幕压到 `DIM` 那一档亮度并停止刷新，USB-Serial 控制台断开：light sleep 会关掉无线外设和 USB PHY，端口掉线是预期行为，不是崩溃。屏上那一帧仍然看得见，因为背光还在输出，面板也仍在自己刷 GRAM。
2. 按任意键。设备原地唤醒，同一屏画面回来，而且用来唤醒的这一下不会顺带触发当前选中项。
3. 唤醒后逐个按键，确认三个都还有效。只有一个响应，说明 `bsp_button_restore_adc()` 没跑到或通道配置失败。
4. 打开同步页等 10 分钟，确认设备**没有**入睡：`POWER_RADIO_ACTIVE` 必须把它挡在 `DIM`。
5. 启动番茄计时，确认计时期间屏幕不调暗。
6. 用万用表对比 `ACTIVE`、`DIM`、`STANDBY` 的电流。本文里的量级都是相对大小，未在本板实测。
