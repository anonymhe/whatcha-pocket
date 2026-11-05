<p align="right">
  <a href="CHANGELOG.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Changelog

## Unreleased

- The first key press after the screen dims now wakes the device without also activating whatever was selected. The wake-up call used to run inside the button timer callback, which meant backlight and audio-codec changes could be made from two tasks at once while the screen task was reading the same power state, so wake and dim behaviour depended on timing. Button events are now only queued from the callback; the wake check and the page dispatch both run in the screen task that already owns the power state.

- Screen switches now free the outgoing screen before building the incoming one. The transition inserts a lightweight curtain screen precisely so the old screen can be released first, but the old screen was being torn down after the new one was already built, so the memory peak the curtain exists to avoid still happened: outgoing screen, curtain and incoming screen were all resident at once. Leaving a page before entering the next also means each page's own cleanup now runs while it is still the visible screen rather than after it has been replaced.

- Sound effects can no longer lose a request or play the wrong pitch. Playback requests were handed to the audio task as two separate shared values, so a task switch between reading and clearing them could pair an older sound with a newer argument, or drop a request submitted in between. A request is now a single queued command carrying both its id and its argument, and a one-slot queue keeps the deliberate "newest sound wins" behaviour for rapid input.

- The Pomodoro countdown now follows elapsed time instead of counting its own callbacks. It subtracted one second per timer callback, so a stalled interface or a late callback made a 25-minute session run longer than 25 minutes. The remaining time is now derived from a monotonic deadline, pausing settles the remainder and resuming starts a fresh deadline, and the timer only refreshes the display.

- Corrected which key opens Pocket New. The documentation and the in-app guide told readers to long-press ▲ on the home screen, but that opens the profile sync screen; Pocket New is the 猹工坊 card selected with OK. The README, CHACKER's guidance and the Pocket New spec now match the firmware.

## v0.1.5 - 2026-11-04

- The device no longer enters light sleep while a USB host is talking to it. On this chip light sleep powers the USB-Serial-JTAG pad and clock down, so the serial port and the enumeration disappear together, which used to cut a debugging session off mid-standby. Idle standby is now refused whenever a real host is on the bus (SOF packets are arriving), and a ten minute grace period runs from the last moment a host was seen, so pulling the cable does not send the device to sleep immediately; a charger or power bank is not a host and does not block the sleep. A refusal leaves the backlight dimmed rather than brightening it, and deep sleep goes through the same gate.
- Made an unexpected battery reading diagnosable instead of a guess. Boot now prints one line naming the fuel gauge's stored battery profile by fingerprint next to the profile compiled into the firmware, plus the fractional SOC byte, the raw 14-bit voltage, the temperature and the gauge's mode and alert registers, and the power layer appends the same values whenever they change while awake. Terminal voltage is logged raw because integer millivolts quantise away the few LSB of movement that separate a charge in progress from a terminated one, and the gauge exposes no current register for the board to read.

- Fixed the battery percentage stopping at 98 %. A full-image flash to v0.1.4 swapped the CW2017 profile from the UTL 520 mAh cell to a Wanli 500 mAh one, but the hardware carries the 520 mAh pack: that curve treats about 4.160 V as 98.2 %, so the reading could never reach 100 % even though the charging circuit still terminated normally. The 520 mAh profile is restored as the default and the choice is now a build-time option, `CONFIG_WATCHA_BATTERY_PROFILE_WANLI_500MAH` (default `n`), between two byte-verified archives under `assets/battery/`. An A/B run on one device, same charger, minutes apart, went 98.2 % -> 100.0 % with the profile as the only variable.
- Converted all 53 image sources from 3-bytes-per-pixel RGB565A8 to LVGL I8 indexed format. The app image shrinks by 535 KB (2177.7 KB to 1643.1 KB, -24.6%), widening the headroom in the 3 MB factory partition from 894 KB to 1429 KB; BLE mini-program install compatibility is unchanged. Fifteen sources use 255 or fewer colors and convert with bit-identical pixels; the other 38 are anti-aliased artwork, where the 256-entry palette keeps the 255 most frequent colors exact and merges the long tail of gradient in-between values, weighting alpha four times higher than color so translucent edges keep their coverage. Measured over those 38 images composited on the panel background, 229 of 164,432 visible pixels (0.14%) differ by more than 32/255 in any channel, the worst single image is 0.87%, and per-image mean error is at most 2.2/255. These are single-pixel specks on anti-aliased edges, not banded regions. I8 images decode line by line so RAM use stays flat, and the decoder's whole-image RAM-load path is now pinned off in sdkconfig.defaults. tools/gen_images.py performs the conversion and has a --verify mode that reports the per-image difference against the original from git history.

- Hardened the Bluetooth sync handler. A profile packet too large for the receive buffer (or shorter than one byte) now replies with the packet-error result instead of silently dropping the transfer, so the client fails fast rather than timing out. Profile writes to flash are now all-or-nothing: the "synced" flag is cleared before the fields are written and only set again if every write succeeds, so a write that fails partway leaves the device as "not synced" instead of mixing old and new profile data, and a storage failure reports a distinct result code. Noted in code that the profile characteristic is intentionally unauthenticated for mini-program compatibility — while the sync screen is open, any device in range can overwrite the profile.

- Released the Bluetooth link the instant a sync succeeds instead of keeping the phone connected through the two-second "done" prompt. The sync screen now calls a dedicated release that stops advertising and drops the active connection as soon as the profile arrives; the NimBLE host stays resident as before, and leaving the page still tears the link down as a fallback. The disconnect now targets the saved connection handle instead of a sentinel NimBLE treated as "not found" (so the link is actually dropped), and advertising recovers after a host reset instead of wedging the sync screen until reboot.
- Holding a key no longer shortens the USB grace period. The ten minute window is anchored to the last moment a host was seen, but pressing a key erased that sighting, so "plug in for four minutes, press a key, pull the cable" had only the idle five minute timer left and went to sleep five minutes after unplug instead of ten, which is exactly the window the buffer exists to protect. The window now survives keypresses; only the "already reported" flag is reset. A backwards clock during the window now counts as still inside it, so an unexpected timer step cannot cut the serial session short either.
- Added a second, later USB check immediately before entering standby. The previous check ran before the 300 ms audio codec settle, and that delay was the one place a cable could finish enumerating unnoticed, so the check now also runs after it; when it refuses, the codec is powered back up so audio is not left silent until the next keypress.
- Fixed the temperature column reporting a plausible reading when none was taken. A failed temperature read returns -1, and that value was being printed as -0.1 °C at boot, in the awake trace and in the standby wake log, which looks like a real measurement of a very cold cell. Those lines now print `n/a`. The success test is one shared helper, `bsp_battery_temp_valid()`, instead of three call sites each testing the sentinel differently; the same helper keeps working if the gauge's 0.5 °C resolution is ever changed to 0.1 °C, where -1 becomes a legal reading.
- Pinned the LVGL stride alignment to the value the generated images are actually laid out with. The indexed-colour sources pad every row to a four byte boundary, but `CONFIG_LV_DRAW_BUF_STRIDE_ALIGN` was set to 1, which makes LVGL expect an unpadded row: a 22-pixel icon computes a stride of 22 where the asset stores 24. Rendering was correct only because the decoder addresses rows through the header stride; the setting and the assets now agree.
- A failed battery profile write now brings the fuel gauge back up before giving up. The write sequence puts the CW2017 to sleep first, so aborting halfway left it asleep, and a gauge that does not answer on the next boot is reported as absent, which hides the real cause. The failure path now attempts to reactivate it and logs if that also fails.
- Fixed the Windows developer environment helper. `tools/idf_env.sh` looked for `ccache` one directory too shallow, so the tool was never on `PATH` and builds through ESP-IDF's compiler cache failed with an unexplained `CreateProcess failed`. The shell and Python helpers are also now pinned to LF endings, because a Windows working tree with `core.autocrlf` was rewriting them to CRLF and `source tools/idf_env.sh` then failed on every line.

## v0.1.4 - 2026-11-02

- Fixed the standby battery readout freezing at its pre-sleep value. A light-sleep wake whose screen-restore step failed reported that failure instead of a plain success, so the power layer misread a perfectly good timer wake as a refused sleep and skipped the scheduled repaint, sometimes for hours. The board layer now answers one question only (did the device sleep and wake), logs restore failures separately, and the periodic wake repaint is visible in the default log level. A button wake also repaints the battery immediately instead of waiting for the next 30 s tick.

- The home screen's battery gauge now keeps up with the charge while the device is asleep, instead of freezing at whatever it read when the page was drawn. It used to run its own 30 s timer, which could never fire during standby: every periodic wake re-times all screen timers to the present, and a wake only lasts a few hundred milliseconds. The gauge is now a self-contained module that registers with the same battery-view list as the top-bar percentage, so one schedule owned by the power layer repaints both, standby wakes included.

- The battery percentage and the home gauge no longer go blank when a reading is momentarily missing or the gauge cannot be created; the top bar keeps its reserved width and the home screen draws without its gauge instead of restarting.

- Ported the upstream CW2017 fuel-gauge profile to the Wanli New Energy 500 mAh cell (profile2 / XIL40X / 2026-10-20), replacing the older 520 mAh bytes so the state-of-charge curve matches the cell the device actually ships with. The exact vendor bytes are archived under `assets/battery/`, the register reads and writes now return `esp_err_t` for clearer failure logs, and a one-line readiness log reports the first SOC and voltage after init.

## v0.1.3 - 2026-10-31

- Made the sync screen's device easier for the WeChat mini-program to find. The Bluetooth advertising interval during sync dropped from 1000 ms to 200 ms, so after scanning the QR code the mini-program discovers the device almost immediately instead of leaving the user waiting. The old 1000 ms value was a leftover power-saving default, which buys nothing on the sync screen since it advertises for at most 90 seconds and stays awake the whole time.

- Fixed the wake delay that grew with how long the device had been asleep — after about an hour of light sleep a key press could take up to three seconds to light the screen. On wake the firmware no longer asks LVGL to replay every scheduler period it missed while asleep; each timer is rebased to the present, so the wake cost no longer scales with sleep duration. The screen clock and countdowns still advance by the real elapsed time.

- The standby screen now refreshes its battery readout on its own instead of only updating on the next key press. While in light sleep the device wakes briefly every 30 seconds to redraw, stretching to every 60 seconds once the battery falls to 50 % or below to save power. The refresh interval, the low-battery interval, and the battery threshold are all tunable constants, ready to move into a settings page later.

- Replaced the idle power-down with a five-state ladder so the device no longer keeps its CPU awake doing nothing. Backlight dims after 30 s, the codec powers down and the MCU enters light sleep after 5 min, and any key wakes it in place with the screen still showing what it did before. Deep sleep stays available but is off by default because waking from it reboots the device. Pages that must stay lit and refreshing, such as the Pomodoro timer, now hold a caffeine flag that freezes the ladder instead of merely keeping the backlight on, and the sync screen holds a radio flag that allows dimming but forbids sleeping so a Bluetooth link is never dropped.

- Gave every detail screen the same top bar: page name on the left, battery percentage on the right, drawn by one shared helper along with the divider, so pages can no longer drift apart. The usage guide, which had no battery readout at all, now shows one; Pocket New keeps its position counter in the right corner, now written in hex (`0x01`, `0x02`, ...) in keeping with the retro styling.

- Split the firmware's page sources into `main/pages/`, leaving the router, entry point, shared services, and generated data where they were, so the component root now lists what it is instead of eleven `page_*` files in the middle of it. Behavior, includes, and the `PAGES[]` registry are unchanged, and the doc references that name a page follow the move.

- Reorganized the repository so code and generated data no longer share one flat folder: the LVGL C arrays generated from artwork and typefaces moved from the root of the firmware component into `main/assets/images/` and `main/assets/fonts/`, and the source typefaces moved from `assets/font/` to `assets/fonts/`. The image directory compiles as a whole, the font list stays explicit so Flash stays under budget, and the work-in-progress sources remain in `assets/`. Contributor and AI-facing documents, the font generator, and the asset-reference check follow the new layout.

- Moved the sync screen's remaining-time countdown onto its own line, shown in the pixel digit font the other numeric readouts use. Keeping it inside the Bluetooth status sentence made the numbers look like a different, smaller typeface next to the Chinese text.

- Fixed the sync screen freezing or restarting the device when it returned to the home screen on its own, either after a completed sync or on the 90-second timeout. The one-shot timers that drive those two returns now clear their own handles before running, so leaving the page no longer frees an already-released timer. Returning with a long press was unaffected.

- Rewrote the workshop intro entry as a short instruction manual written in the mascot's own voice: it introduces CHACKER as a coding assistant the user summons by asking an AI tool to read the repository's guide file, states how it speaks and that it confirms a plan before writing code, and keeps the two-step recipe plus the scope and flashing rules for adding a mini-app.

- Reworked the sync screen's hint line below the QR code so it mirrors the spacing of the hint above it, and changed it to show a live timeout countdown alongside the current Bluetooth status (starting up, ready, or connected) while keeping the device number. A small random mascot now sits in the center of the QR code, on a backing plate sized to the mascot itself rather than to the source artwork, so the white area no longer spreads across a quarter of the code.


- Replaced the fixed six-icon home grid with a scrollable card launcher: an on-device usage guide page, the existing features, and a slot for community mini-games now share one up/down list, with the selected card centered and its neighbors peeking in.

- Added a green wipe transition between screens. The wipe also releases the outgoing screen before building the incoming one, so the device never holds two full screens' worth of drawing memory at once; button presses are ignored while a transition plays.

- Made the Chinese-glyph coverage check font-aware so it catches a bug it previously missed: a headline font can cover only a small subset of characters, and text that looked fine in the editor could still render as empty boxes on the device. The check now validates each file against the exact font it uses and scans every UI source file, not just one folder; documented all shipped pixel fonts, their coverage limits, and the open-source font they are built from.

- Added a check that flags image and font assets compiled into the firmware but never used, so unused data stops wasting limited Flash.

- Hardened the audio power-down used by the inactivity timeout: the codec now runs the full ES8311 suspend register sequence with readback verification and a retry, replacing the previous plain codec close, so the codec reliably reaches its low-power state and resumes cleanly on the next key press. Also added verified CW2017 battery-gauge sleep and deep-sleep audio pin isolation for future shutdown paths.

- Added an LVGL Chinese-text guide describing the shipped `font_fusion12` CJK font and ASCII `font_ps8`/`font_ps16`, how to stay inside their coverage with `check_glyphs.py`, and a missing-glyph troubleshooting order; corrected the coding conventions, which still described the upstream Montserrat-only baseline.

- Shortened the idle backlight dim from sixty to thirty seconds without key input, so the screen spends less time at full brightness while idle; pressing any key restores full brightness. Pomodoro keep-awake sessions are unaffected.

- Rebuilt the embedded pixel fonts from the Fusion Pixel typeface: Chinese text now uses a full GB2312 glyph set (plus the handful of symbols and nickname variants the UI relies on) at the standard 12 px size, and the large headline font was regenerated at 20 px as an exact 2x of the 10 px grid, replacing the previous 24 px headline font. A compact 10 px body font is also available as source but left out of the build to save Flash until something needs it. A reproducible generator (`tools/gen_fonts.py`) rebuilds every font and its glyph-coverage table from the checked-in source typefaces.

- Reworked the home-screen identity area into an RPG character-card layout: the nickname and tier badge are left-aligned (24 px name; tier labels remapped to `用户 - Spectator`, `观猹员 - Player`, and `观猹员 - Player No.1`). Battery now reads as an HP bar with a green/amber/red fill and percentage, replacing the top-bar battery readout. Player stats show raw counts beside pixel icons: ATK (reviews), DEF (posts), and Coins (upvotes), each rendered with a runtime dot-matrix icon helper so no image-conversion toolchain is needed.

- Removed the unused hardware-validation demo pages and their assets from `main/` (Wi-Fi scan, BLE advertising, low-power sleep, display, button, audio, battery pages, the shared radio setup, and the crash-log helper) along with unreferenced fonts, icons, and images. The firmware keeps the page router in `pages.c` with feature pages (`page_*.c`) and community apps (`main/apps/`); the home page module was renamed from `demo_watcha.c` to `page_home.c` with no behavior change. Documentation (capability tables, hardware guide, AI guides) was updated to match.

- Added a 10-minute inactivity timeout: after no key input for ten minutes the device stops BLE advertising and powers down the audio codec to save power; pressing any key restores both the advertising and the audio codec. Pomodoro keep-awake sessions are unaffected.

- Added the supplied 80-byte CW2017 profile for the specified 520 mAh cell, including content/update-flag checks, verified writes, the required restart sequence, and bounded SOC-readiness polling.

- Reorganized the documentation by function area with a dual entry point: the root `AGENTS.md` is now a thin router (hard constraints + task routing only) and the detailed AI workflow lives in `docs/development/ai-guide.md`; `agent-guide.md` was folded in. `docs/development/` gained a second level (`engineering/`, `ci/`, `release/`), and the `plays/` application archive and `experiences/` moved into a `docs/reference/` area with a dedicated README. Removed `docs/software-design/` (empty scaffold); folded the three `assets/{fonts,images,music}/README` leaves into the `assets/` README; flattened the six `project-completion` sub-documents into a single file; and unified each directory to a single README, eliminating every `INDEX` file and a duplicated experience index. All cross-references and bibliographic links were updated; no content was dropped.

- Made mini-program BLE install compatibility a template-level invariant: fixed
  protected `cardid`/Recovery partitions, retained the five-second UP-key
  Recovery boot hook, and added CI validation for merged-image structure,
  partition MD5/ranges, the 3 MB app limit, and protected payload exclusion.
- Documented a release-title convention for multi-app releases: name tags as `v<version>-<app-name>` (e.g. `v0.1.0-voice-keychain`) so the release title carries the version and the app, and confirm the title after the release is published so a release list is scannable by app.
- Added a post-release follow-up workflow: an `issue-suggestions` skill for filing user feedback as issues against the upstream project, an `experience-pr` skill for submitting reusable development experience as a documentation PR, a `docs/experiences/` directory for per-entry experience files, and supporting `project-completion`, `file-issues`, and experience-index documents.
- Simplified the tracked repository root: moved GitHub-recognized community documents into `.github/`, moved the changelog into `docs/`, updated every reference, and added a root-document allowlist to repository checks.
- Repository-wide language policy: every maintained Markdown default `.md` file is English, Simplified Chinese uses a paired `.zh_CN.md`, and both provide language switches. Static checks reject missing peers, missing switches, and Chinese prose in English defaults.
- Phase one of the AI development workflow: streamlined task-based context routing, unified local/CI validation, added PR checks and a template, and committed the dependency lock for reproducible builds.
- PR review fixes: pinned GitHub Actions to full commit SHAs, split build/release jobs by least privilege, disabled persisted sync checkout credentials, added Feature Request and Usage Question forms, clarified private security-report fallback, and corrected stale README, CI-trigger, and branch descriptions.
- Changed commit titles, PR titles, and PR bodies from Chinese-default to English; updated the Chinese punctuation rule so it no longer applies to PR descriptions.
- Reworked `build-firmware.yml` to pass `SDKCONFIG_DEFAULTS=sdkconfig.defaults`, enable `partitions.csv`, preserve the 8 MB image header, merge a flashable `FoloToy-AI-Passport-full.bin`, publish only that artifact, and use Actions cache v5.
- Integrated upstream PR #6 to resolve PR #4 conflicts: Wi-Fi, Bluetooth LE, radio lifecycle, and low-power demos; a 3 MB factory partition; build/menu/configuration updates; hardware-guide coverage; and bilingual capability tables.
- Defined English imperative Conventional Commit formatting for both commits and PR titles.
- Removed stale sync-workflow template comments and generalized an irrelevant Redis TTL rule to cache components.
- Added Chinese punctuation, credential safety, and recoverable file-deletion conventions.
- Expanded source-comment requirements for functions, state, ownership, concurrency, timing, registers, and magic values.
- Removed AI execution instructions from product READMEs so they remain human-facing product and repository overviews.
- Added `docs/development/agent-guide.md` as the focused AI workflow guide.
- Updated `AGENTS.md`, `docs/INDEX.md`, and the development index for the agent guide.
- Documented why the root README path is reserved for fork owners and how GitHub README precedence supports it.
- Created `main-update` from the upstream-aligned baseline and combined the repository-structure, firmware-CI, and upstream-sync work.
- Corrected the merged documentation index, workflow path, project tree, and CI references.
- Moved CI documentation from software design to `docs/development/`.
- Moved fork-only documentation assets from `assets/docs/` to `docs/assets/`.
- Moved the upstream English/Chinese project READMEs under `docs/` and renamed the documentation catalog to `docs/INDEX.md`.
- Initialized `AGENTS.md`, `CLAUDE.md`, and `CHANGELOG.md`.
- Standardized the initial project README language filenames.
- Added the `docs/`, `assets/`, and `skills/` directory structure.
- Moved the upstream hardware guide into `docs/hardware-design/`.
- Standardized subdirectory README capitalization and introduced fork conventions.
- Allowed fork-owned root README and supplemental documentation content on fork `main`.
- Added and documented the fork-only supplemental-document directory.
- Moved the build CI document to its dedicated CI branch before consolidation.
- Documented clean-`main` reasons, the direct-development exception, and Actions enablement for forks.
- Split the original agent rules into contribution, development, and fork documents with a compact root index.
- Updated software-design and project README references for the new documentation structure.
- Added the documentation catalog and task-triggered routing based on the earlier repository model.
- Added bilingual contribution, code-of-conduct, security, and support documents tailored to this ESP-IDF and fork workflow.
