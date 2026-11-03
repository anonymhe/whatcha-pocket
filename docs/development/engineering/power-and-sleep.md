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
| `DIM` | 30 s without a key | backlight 10%, everything else unchanged | any key, or longer idle |
| `STANDBY` | 5 min without a key | codec powered down, backlight 10%, MCU enters **light sleep** | any key (or the optional periodic timer) |
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
| Policy | `main/sleep_policy.c` | pure function of (idle time, ready, caffeine, radio, USB host, USB back-off) to a stage; no IDF, no LVGL, covered by a host test |
| Application | `main/power.c` | owns the timers, applies backlight and codec changes, drives the battery readout cadence, calls light/deep sleep |
| Board | `components/bsp/bsp_power.c` | arms and disarms wake sources, sleeps, powers peripherals down |

Pages never talk to the BSP sleep layer. They call `power_hold()` from
`main/power.h`:

- `POWER_CAFFEINE` freezes the ladder at `ACTIVE`. The Pomodoro page holds it
  while a timer runs: that screen must stay lit and refresh every second, so it
  must not be dimmed and must never reach `STANDBY`. Releasing it restarts the
  idle clock from that moment.
- `POWER_RADIO_ACTIVE` allows dimming but forbids sleeping. BLE links do not
  survive a sleep, so the sync screen holds it while it advertises. The BLE link
  itself is released as soon as sync succeeds (`ble_sync_release_link()` in the
  completion handler), not only on page exit, so the phone is dropped
  immediately instead of lingering through the two-second "done" prompt. The
  NimBLE host stays resident for the device lifetime — only advertising and the
  active connection are toggled per page.

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

## A connected USB host blocks the sleep

Light sleep powers the USB-Serial-JTAG pad and clock down (`SOC_USB_SERIAL_JTAG_SUPPORT_LIGHT_SLEEP`
is not defined for the ESP32-C3), so the endpoint disappears from the bus and the
serial port drops. That is acceptable for a badge left on a desk, but not while
someone is debugging it over the same port. The firmware therefore refuses to
sleep while a real USB host is on the bus, and keeps a ten minute grace period
after the host is gone.

Detection is not a pin: the board exposes no VBUS sense GPIO, and every usable
pin is already allocated. Instead the firmware asks the chip, via
`bsp_power_usb_host_attached()` → `usb_serial_jtag_is_connected()`, whether it is
receiving SOF packets. A host sends one every millisecond; a charger or power
bank sends none. So the predicate matches "a real host is talking to us", not
"something is feeding 5 V".

Three properties are worth remembering:

- The IDF connection monitor is already linked in through
  `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED`, so its tick hook costs nothing
  extra here. IDF's own `CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION` would have solved a
  related problem, but it `depends on PM_ENABLE`, which this firmware deliberately
  leaves off.
- The monitor assumes "connected" until three consecutive ticks see no SOF, so
  the very first milliseconds after reset can report a false positive. That is a
  millisecond-scale artifact measured against a minute-scale decision.
- A USB device attached *while* the device is asleep cannot wake it: on the C3
  the USB-Serial-JTAG is not a light sleep wake source. The port reappears on the
  next periodic wake, 30–60 s later.

`sleep_input_t` carries two related bits. `usb_attached` is the live reading.
`standby_deferred` covers the ten minutes after the host was last seen, so
unplugging the cable does not send the device to sleep the instant the host
vanishes. The clock starts at the last sighting rather than at the first refusal,
because plugging in for two minutes and pulling the cable before the idle ladder
reaches standby never produces a refusal at all, and starting the back-off only
at that point would let the device sleep the moment the cable came out.
Both cap the ladder at `DIM`, exactly like `POWER_RADIO_ACTIVE`; `power.c` also
re-checks before `esp_light_sleep_start()` and before deep sleep, because up to
500 ms pass between the decision and the action, which is long enough for an
enumeration to start. A refusal leaves the backlight dimmed rather than restoring
`ACTIVE`, since a screen that brightens whenever the cable moves is worse than
one that stays dim.

## Periodic wake while in STANDBY and the battery readout

The battery percentage shown in the top bar has to keep up with reality in two very
different situations: while the user is actively on a screen, and while the device
is light-sleeping in `STANDBY`. Rather than let each screen run its own refresh
timer, `main/power.c` owns the refresh cadence for both, so there is a single
source of truth for *when* the reading updates.

- The cadence is one interval, defined by the `BATTERY_REFRESH_*` constants in
  `power.c`: 30 s normally, stretching to 60 s once the charge is at or below 50 %.
  The interval is chosen from the charge read on the previous repaint, so the
  500 ms service tick does not poll the gauge over I2C; `-1` (read failed) is
  treated as "not low" and keeps the default interval.
- While awake (`ACTIVE`/`DIM`), `power_service()` runs every 500 ms and calls
  `maybe_refresh_battery()`, which repaints only after the interval has elapsed.
- In `STANDBY`, the same interval is armed as a timer wake alongside the button.
  That wake repaints unconditionally instead of going through the elapsed-time
  check: the real sleep is `period - sleep_time_adjustment`, so a threshold test
  would skip the very repaint the wake was armed for. It is still one repaint per
  period.
  `power_set_standby_refresh_us()` leaves the period at `0` by default, meaning
  "do not override, pick the interval by charge". A non-zero value forces a fixed
  period and is reserved for a settings page or debugging.

The UI side holds no cadence of its own. `main/apps/app_ui.c` keeps a registry of
battery **views**: any widget that shows the charge registers a paint callback and
`ui_battery_refresh_all()` repaints them all. It returns the reading it painted, so
`power.c` can price the next interval without a second I2C read. A view unregisters
itself on `LV_EVENT_DELETE`, so the list cannot outlive its screen. This is what
removed the earlier coupling, where each label carried an independent 30 s `lv_timer`
and the standby reading depended on whether that timer could ever fire.

Two views live in it today: the top-bar percentage that `ui_topbar()` adds, and the
home screen's HP-style gauge (`main/apps/app_battery_gauge.c`), which owns the heart
icon, the bar and the percentage as one widget and is only placed by `page_home.c`.
Both are repainted together on the same schedule.

That shared schedule is also why no battery view may keep its own `lv_timer`: the
tick-resume path below rebases every LVGL timer to the present on each standby wake,
and a wake lasts only a few hundred milliseconds, so a 30 s timer would never reach
its period and the readout would freeze at its first paint — which is exactly what
the page-owned timer used to do before it moved into the registry.

A timer wake is deliberately not treated as activity: it must not restart the idle
clock, or a refreshing screen would keep re-qualifying itself as `ACTIVE` and never
sleep again. `bsp_power_light_sleep()` reports what woke it, and `enter_standby()`
refreshes the battery then goes straight back to bed on `BSP_WAKE_CAUSE_TIMER`,
leaving the stage and the brightness untouched. Only a button wake, or a refused
sleep, resets the idle clock and returns to `ACTIVE`, and that path repaints the
battery right away too.

`bsp_power_light_sleep()` answers one question only: did the MCU sleep. It returns
`ESP_OK` whenever the device actually slept and woke, even if the wake path's
restore step (handing GPIO0 back to the ADC) failed; that failure is logged and
nothing more. Folding the restore error into the return value made the caller
misread a successful timer wake as a refused sleep, skip the battery repaint, and
freeze the standby readout at whatever it was before bedtime.

The backlight has to survive light sleep for the screen to remain visible, and
it does: `bsp_display.c` configures the LEDC channel once with
`LEDC_SLEEP_MODE_KEEP_ALIVE` and the timer with `LEDC_USE_RC_FAST_CLK`, because
APB stops during sleep. Nothing has to be switched at runtime, so brightness is
the only thing that decides whether the screen is lit. That is also why `STANDBY`
shortens the duty cycle instead of switching the output off.

## Why waking is cheap no matter how long the sleep was

LVGL's tick is paused across light sleep (`bsp_lvgl_tick_stop()`), so on wake
`bsp_lvgl_tick_resume(slept_ms)` jumps `lv_tick` forward by the whole sleep
duration to keep clocks and countdowns time-correct. That jump alone is a trap: the
scheduler's next pass would see every periodic `lv_timer` as overdue by the entire
sleep and try to replay every missed period in one `lv_timer_handler` call (a 10 ms
timer overdue by an hour is roughly 360k catch-up iterations), freezing the UI for
seconds and making the wake cost scale with how long the device slept. So
`bsp_lvgl_tick_resume()` also resets every timer's `last_run` to now, which rebases
the scheduler to the present without undoing the tick jump. That rebase is why the
battery readout cannot rely on an `lv_timer`: a standby wake is far shorter than the
refresh period, so such a timer would be re-timed from now on every wake and never
reach its period.

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

1. Leave the device idle for 5 minutes with the USB cable unplugged. The screen
   dims to the `DIM` level and stops updating, and the USB-Serial console drops:
   light sleep powers the wireless peripherals and the USB PHY down, so a closed
   port is expected, not a crash. The frame left on screen stays visible, because
   the backlight keeps running and the panel refreshes its own GRAM.
2. Plug the cable into a computer, leave the device idle for 5 minutes, and
   confirm it does **not** sleep: the backlight drops to the `DIM` level, the
   console stays alive, and one log line reports the refusal. Repeat with a
   charger only: no host, no SOF, so the device sleeps as usual.
3. Unplug the cable and wait. The device must not sleep for ten minutes after the
   last moment a host was seen, then must resume the normal ladder. Plugging in
   for only a minute or two and pulling the cable again must still leave the full
   ten minutes of back-off. Pressing any key cancels the remaining back-off
   immediately.
4. Press any key. The device wakes in place, the same screen returns, and the
   key that woke it does not also activate the focused item.
5. Press keys after waking and confirm all three still work. If only one responds,
   `bsp_button_restore_adc()` did not run or the channel config failed.
6. Open the sync screen, wait 10 minutes, and confirm the device does **not**
   sleep: `POWER_RADIO_ACTIVE` must hold it in `DIM`.
7. Start the Pomodoro timer and confirm the screen never dims while it runs.
8. Compare current draw in `ACTIVE`, `DIM` and `STANDBY` with a meter. The
   magnitudes in this document are relative, not measured on this board.
