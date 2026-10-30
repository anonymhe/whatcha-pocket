<p align="right">
  <strong>English</strong> · <a href="power-and-sleep.zh_CN.md">简体中文</a>
</p>

# Power States and Idle Sleep

This document describes the idle power ladder the firmware runs, which layer owns
each decision, and what has to be true before the MCU is allowed to sleep.

## The five states

`main/sleep_policy.h` defines the complete state set. Only four are reachable by
software; `OFF` exists so the ladder is not silently incomplete.

| State | Entered when | What happens | How it ends |
| --- | --- | --- | --- |
| `ACTIVE` | a key was just pressed, or a page holds `POWER_CAFFEINE` | backlight 100%, everything running | idle timeout |
| `DIM` | 30 s without a key | backlight 3%, everything else unchanged | any key, or longer idle |
| `STANDBY` | 5 min without a key | codec powered down, backlight 0, MCU enters **light sleep** | any key (or the optional periodic timer) |
| `SLEEP` | 60 min without a key, **disabled by default** | deep sleep; panel, backlight, codec and gauge are powered down | button press, but the chip cold-boots |
| `OFF` | the user holds the physical power key | hardware power-off | the power key |

`OFF` is not a firmware state. The power key is not wired to the MCU, so the
firmware can neither observe it nor enter this state; it is listed only so that
the set of states, and every document that enumerates them, stays complete.

## Why `STANDBY` is light sleep and not deep sleep

An ST7789 panel keeps refreshing its own GRAM as long as the 3.3 V rail is up, so
a light-sleeping MCU with a static frame left on screen still shows that frame.
Deep sleep would keep the frame too, but the backlight is driven by LEDC, which
deep sleep turns off, so the badge would simply go dark. Light sleep therefore
buys a display that stays readable while the CPU draws almost nothing.

`CONFIG_PM_ENABLE` is deliberately **not** enabled: the firmware calls
`esp_light_sleep_start()` itself instead of using automatic tickless idle. The
manual entry point already stops and restarts the task watchdog and recomputes
`esp_timer` from the RTC, which is all this design needs. FreeRTOS ticks are *not*
stepped by the manual path, which is why every idle decision uses
`esp_timer_get_time()` and never a tick counter.

## Who decides, who acts

| Layer | File | Responsibility |
| --- | --- | --- |
| Policy | `main/sleep_policy.c` | pure function of (idle time, ready, caffeine, radio) to a stage; no IDF, no LVGL, covered by a host test |
| Application | `main/power.c` | owns the timers, applies backlight and codec changes, calls light/deep sleep |
| Board | `components/bsp/bsp_power.c` | arms and disarms wake sources, sleeps, powers peripherals down |

Pages never talk to the BSP sleep layer. They call `power_hold()` from
`main/power.h`:

- `POWER_CAFFEINE` freezes the ladder at `ACTIVE`. The Pomodoro page holds it
  while a timer runs: that screen must stay lit and refresh every second, so it
  must not be dimmed and must never reach `STANDBY`. Releasing it restarts the
  idle clock from that moment.
- `POWER_RADIO_ACTIVE` allows dimming but forbids sleeping. BLE links do not
  survive a sleep, so the sync screen holds it while it advertises.

Both are bit flags and stack. A page that forgets to release a hold leaves the
device unable to sleep, which is why every hold has a matching release on the
page's `exit`.

## The wake source is borrowed from the ADC

The board has one input pin for all three buttons: GPIO0, an ADC resistor ladder
with an external 10 kΩ pull-up. It is the only possible wake pin, so
`bsp_power_wake_source_arm()` temporarily reconfigures it as a digital input with
a low-level wake condition, and `bsp_power_wake_source_disarm()` hands it back to
`ADC1_CH0`.

`adc_oneshot_read()` does not touch the pad. Only
`adc_oneshot_config_channel()` re-routes it back to the analog function, so
`bsp_button_restore_adc()` reconfigures the channel after every wake. Skipping it
leaves all three keys reading garbage.

Two traps worth knowing:

- The first parameter of `esp_deep_sleep_enable_gpio_wakeup()` is a **pin bit
  mask**, not a pin number. `1ULL << 0` is correct; `0` is rejected and, if the
  return value is ignored, produces a sleep with no wake source at all.
- `CONFIG_ESP_SLEEP_GPIO_ENABLE_INTERNAL_RESISTORS` is `n` in
  `sdkconfig.defaults`. The board already has an external pull-up, and stacking
  the internal one on top shifts the whole divider ladder, so the three key
  windows drift.

## Periodic wake while in STANDBY

`power_set_standby_refresh_us()` adds a timer wake source alongside the button.
The current firmware leaves it at `0` (button only) because both the home screen
and the badge-style display are static.

If a future screen needs to redraw while idle, for example a clock that updates on
the minute mark, set that period here rather than shortening the idle timeout.
`power_service()` runs once per wake, so the screen repaints and the device sleeps
again immediately. Keep the period long enough not to defeat the savings: one wake
per second is roughly equivalent to never sleeping.

The backlight has to survive light sleep for such a screen to remain visible.
`bsp_display_backlight_keep_alive(true)` switches the LEDC channel to
`LEDC_SLEEP_MODE_KEEP_ALIVE` and keeps the current duty; the timer source is
already `LEDC_USE_RC_FAST_CLK` because APB stops during sleep. This path exists and
is tested only through the build; no current screen uses it.

## Turning deep sleep back on

`main/power.c` has:

```c
#define POWER_ENABLE_DEEP_SLEEP 0
```

Set it to `1` and `SLEEP` becomes reachable after `SLEEP_AFTER_US`. Waking from it
cold-boots the application: `bsp_power_boot_cause()` then reports
`BSP_WAKE_CAUSE_BUTTON`, and nothing that was on screen survives. Check
`power_booted_from_sleep()` before replaying a boot animation if that matters.

## Verifying it on the device

```text
Build: PASS / FAIL            ./tools/validate.sh --firmware
Host tests: PASS / FAIL       ./tools/validate.sh --static
Device tests: PASS / FAIL     the steps below
```

On-device checks that no host test can cover:

1. Leave the device idle for 5 minutes. The screen goes dark and the USB-Serial
   console drops: light sleep powers the wireless peripherals and the USB PHY
   down, so a closed port is expected, not a crash.
2. Press any key. The device wakes in place, the same screen returns, and the
   key that woke it does not also activate the focused item.
3. Press keys after waking and confirm all three still work. If only one responds,
   `bsp_button_restore_adc()` did not run or the channel config failed.
4. Open the sync screen, wait 10 minutes, and confirm the device does **not**
   sleep: `POWER_RADIO_ACTIVE` must hold it in `DIM`.
5. Start the Pomodoro timer and confirm the screen never dims while it runs.
6. Compare current draw in `ACTIVE`, `DIM` and `STANDBY` with a meter. The
   magnitudes in this document are relative, not measured on this board.
