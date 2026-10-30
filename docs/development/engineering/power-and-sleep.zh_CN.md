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
| `DIM` | 30 秒无按键 | 背光 3%，其余不变 | 任意按键，或继续空闲 |
| `STANDBY` | 5 分钟无按键 | codec 下电、背光归零，MCU 进 **light sleep** | 任意按键（或可选的定时唤醒） |
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
| 应用 | `main/power.c` | 掌管计时器，执行背光与 codec 变更，调用 light/deep sleep |
| 板级 | `components/bsp/bsp_power.c` | 装/卸唤醒源、入睡、逐个下电外设 |

页面不直接调 BSP 的睡眠接口，而是调用 `main/power.h` 里的 `power_hold()`：

- `POWER_CAFFEINE` 把阶梯冻结在 `ACTIVE`。番茄猹在计时期间持有它：那个界面必须常亮且每秒刷新，既不能调暗，也绝不能进 `STANDBY`。释放后从那一刻重新计时。
- `POWER_RADIO_ACTIVE` 允许压暗但禁止入睡。蓝牙链路不跨 sleep，所以同步页在广播期间持有它。

两者都是位标志、可以叠加。页面忘记释放会让设备再也睡不下去，因此每个 hold 在页面 `exit` 里都有对应的释放。

## 唤醒脚是从 ADC 借来的

板上三个按键共用一个输入脚：GPIO0，一条带外部 10 kΩ 上拉的 ADC 电阻阶梯。它是唯一可用的唤醒脚，所以 `bsp_power_wake_source_arm()` 会把它临时改成低电平触发的数字输入，`bsp_power_wake_source_disarm()` 再把它还给 `ADC1_CH0`。

`adc_oneshot_read()` 不动引脚。只有 `adc_oneshot_config_channel()` 会把 pad 重新接到模拟功能上，所以每次唤醒后都靠 `bsp_button_restore_adc()` 重配一次通道。漏掉这一步，三个按键读到的电压全是错的。

两个值得记住的坑：

- `esp_deep_sleep_enable_gpio_wakeup()` 的第一个参数是**引脚位掩码**，不是引脚号。正确的是 `1ULL << 0`；传 `0` 会被判非法，若又忽略返回值，就得到一次没有任何唤醒源的睡眠。
- `sdkconfig.defaults` 里 `CONFIG_ESP_SLEEP_GPIO_ENABLE_INTERNAL_RESISTORS` 设为 `n`。板上已有外部上拉，再叠内部上拉会把整条分压阶梯抬高，三个按键的电压窗口随之漂移。

## STANDBY 期间的定期唤醒

`power_set_standby_refresh_us()` 会在按键之外再加一个定时唤醒源。当前固件保持为 `0`（只靠按键），因为首页和工牌式显示都是静态的。

如果以后有空闲时也要重绘的界面，例如整分刷新一次的时钟，请在这里设周期，而不是把空闲超时改短。`power_service()` 每次唤醒都会跑一遍，画面重绘完立刻再睡回去。周期要留得足够长，否则省电就白费了：每秒醒一次基本等同于从不入睡。

这类界面要保持可见，背光必须能扛过 light sleep。`bsp_display_backlight_keep_alive(true)` 把 LEDC 通道切到 `LEDC_SLEEP_MODE_KEEP_ALIVE` 并保留当前占空比；时钟源已经是 `LEDC_USE_RC_FAST_CLK`，因为 APB 在睡眠期间停止。这条路径存在，但只经过编译验证，目前没有界面用到它。

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

1. 静置 5 分钟。屏幕变黑、USB-Serial 控制台断开：light sleep 会关掉无线外设和 USB PHY，端口掉线是预期行为，不是崩溃。
2. 按任意键。设备原地唤醒，同一屏画面回来，而且用来唤醒的这一下不会顺带触发当前选中项。
3. 唤醒后逐个按键，确认三个都还有效。只有一个响应，说明 `bsp_button_restore_adc()` 没跑到或通道配置失败。
4. 打开同步页等 10 分钟，确认设备**没有**入睡：`POWER_RADIO_ACTIVE` 必须把它挡在 `DIM`。
5. 启动番茄计时，确认计时期间屏幕不调暗。
6. 用万用表对比 `ACTIVE`、`DIM`、`STANDBY` 的电流。本文里的量级都是相对大小，未在本板实测。
