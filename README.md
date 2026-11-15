# DeskPI

Qt Quick clock/dashboard for an 800×480 Raspberry Pi DSI display under Cage.

Home-network dashboard: **https://deskpi.local:8443** (no SSH tunnel). The Pi
starts DeskPI automatically at boot. Sign in with your administrator password.

The embedded web dashboard, local certificate trust, API and configuration recovery are
documented in [Web administration](docs/WEB_ADMIN.md).
The CMake target and executable are named **appdeskpi**.

## Build and run

Requires Qt 6.8+ Quick (including the QtQuick Controls and Layouts QML modules),
CMake, a C++17 compiler, Ninja, Qt Network, and OpenSSL development headers. Qt Test is required with `BUILD_TESTING=ON`
(the CTest default); use `-DBUILD_TESTING=OFF` for an application-only build.

```sh
cmake -S . -B build -G Ninja
cmake --build build -j2
ctest --test-dir build --output-on-failure
timeout 5s env QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/appdeskpi
# Expected timeout exit: 124, with no Qt/QML startup errors.
```

From the Pi's physical logged-in console (not a seatless SSH session):

```sh
LIBSEAT_BACKEND=logind cage -d -- env QT_QPA_PLATFORM=wayland ./build/appdeskpi
```

The app runs as the normal user and hides the mouse cursor. No compositor,
boot, display orientation, or login configuration is changed by brightness control.

## Brightness settings

Press and hold the clock screen for **two seconds** to reveal a gear in the
top-right corner, then tap it to open Settings. Short taps and drags do not reveal
the gear. It hides again after opening Settings, keeping the clock clear when
you return. The scrollable panel
contains a 0–100% manual slider, hardware-reported percentage, automatic dimming,
day/night levels, start times in 24-hour format, and minimum brightness.
Controls are at least 48 pixels tall. Tap **Back** to return to the clock.

The default minimum is **5%**; values below it (including slider zero) are
clamped. The minimum can be configured from 1–100%. Brightness is converted
using `ceil(max_brightness * percentage / 100)`, so even a coarse-range device
cannot round the safe minimum down to zero. The displayed percentage reflects
`actual_brightness` when available, and otherwise `brightness`; small rounding
differences are expected on coarse hardware.

Automatic dimming is **off by default**. When enabled, defaults are:

- Day: 07:00–21:59 at 80%.
- Night: 22:00–06:59 at 20%.

Schedules use local system time, support either interval crossing midnight,
and are evaluated once per second. Start times must differ. Changing schedule
settings applies the new schedule immediately and clears any manual override.
A manual slider adjustment overrides the schedule until the **next transition**;
the panel shows the expiry time and a **Resume schedule now** button.
Disabling automatic mode restores the last manual value. Overrides are temporary
and are intentionally not restored after restart; an enabled schedule applies the
correct current period on startup. System clock/time-zone changes are reflected
by evaluation of local time; override expiry is an absolute next-transition instant.

QSettings stores manual level, enabled state, both scheduled levels and times,
and the minimum. On Linux this is normally
`~/.config/DeskPI/DeskPI.conf`. Missing settings use defaults (the first
manual level adopts the current hardware brightness). Invalid settings are
validated, clamped, or replaced with defaults; equal start times revert to 07:00/22:00.

## Hardware and permissions

Discovery reads `/sys/class/backlight/*/{max_brightness,brightness,actual_brightness}`.
It prefers the Raspberry Pi DSI `rpi_touchscreen_attiny` / `rpi_backlight` device;
otherwise a single valid backlight is accepted. Ambiguous multiple devices are
rejected rather than adjusting an arbitrary display. Removal and permission
changes are rechecked every two seconds. A clear unavailable/error state disables
controls; no visual dimming overlay is used.

On the tested Pi:

- Device: `/sys/class/backlight/10-0045`.
- Driver: `rpi_touchscreen_attiny`.
- Raw range: **0–255** (discovered, never hard-coded).
- `brightness` is already owned by `root:video`, mode `0664`; admin is in `video`.
- Existing `/usr/lib/udev/rules.d/60-backlight.rules` grants the video group write
  access to the backlight brightness attribute on device creation.

No new privileges or service are needed on this Pi. **Do not run the app as root.**
On another installation, an administrator can grant a dedicated group write
access to only the identified device's `brightness` attribute via a device-specific
udev rule. Do not make the whole sysfs tree writable or grant unrestricted sudo.

All backlight discovery, reads and writes run on a dedicated Qt thread. Slider
requests are coalesced to at most one every 120 ms; identical raw values are not
written again. Settings saves are debounced. External hardware changes appear in
the slider/readout on the next poll, without fighting the external writer. The
schedule writes again at its next transition. Recovery after device loss reapplies
the current saved/manual/override policy.

## Components and validation

- `BacklightIo`: hardware discovery, safe sysfs I/O, permission/error reporting.
- `BrightnessPolicy`: percentage conversion, settings validation and schedule policy.
- `BrightnessController`: Qt properties/signals, worker lifecycle, scheduling,
  override expiry, request throttling and QSettings persistence.
- `BrightnessSettings.qml`, `BrightnessIcon.qml`: touchscreen panel and drawn icons.
- `tests/brightness_tests.cpp`: conversion/range/minimum, persistence/corruption,
  day/night and overnight boundaries, override expiry, missing/ambiguous/removed
  hardware, denied permissions, redundant writes, and controller recovery.
  Tests use temporary fake sysfs trees and never write the real backlight.
  Run tests as a normal user to exercise permission denial (skipped as root).
- `tests/brightness_ui_tests.cpp`: offscreen touchscreen-style interaction through
  the actual QML settings button and slider, hardware readback against a fake
  backlight, and zero-to-minimum clamping.

### Physical touchscreen checks

1. Hold the clock screen for two seconds, tap the top-right gear, and drag the slider: confirm the screen's light output changes,
   the percentage updates, and zero stops at the configured minimum.
2. Enable automatic dimming and change the active period's level. Confirm hardware
   brightness changes, then adjust the manual slider and check the override notice.
3. Set a transition a minute ahead, choose visibly different safe day/night levels,
   and wait: the override should end and the scheduled brightness should apply.
4. Restart the app. Confirm saved settings and the correct scheduled level return.
5. Return to the clock and confirm the black background, local time and minute updates.

Screenshots validate rendering, not physical luminance. Hardware acceptance can be
checked with `cat /sys/class/backlight/10-0045/{brightness,actual_brightness}`;
an observer at the Pi must confirm visible light-output changes.

### Verification on the development Pi (2026-11-14)

Both CTest suites passed on CachyOS (Qt 6.12) and the Pi (Qt 6.8.2), including
unprivileged permission denial and the actual QML slider interaction against fake
hardware. QML lint passed. The offscreen application remained running until timeout.

Real touchscreen input operated the deployed panel. Hardware readback confirmed
13/255 (protected 5%) and 204/255 (80%); the user watching the physical screen
confirmed visible dimming and brightening. A temporary local-time test schedule
started at 20%, accepted an 80% manual override, and at 13:52 cleared the override
and changed requested/actual brightness to 153/255 (60%). Normal 07:00/22:00 times,
80%/20% levels and disabled automatic mode were restored after testing.

## DeskBuddy cross-device media (Linux first)

A native, headless `deskbuddy-agent` monitors the desktop user's MPRIS sessions
and connects to DeskPI over a certificate-pinned TLS WebSocket. The existing
application now includes a touch media screen, host artwork/palette processing,
transport controls, Connected Devices pairing/revocation, Media Settings and
prebuilt companion downloads in the original dashboard. No audio is streamed.

Start with [Linux installation and pairing](docs/media/INSTALL.md). See the
[architecture](docs/media/ARCHITECTURE.md), [v1 protocol](docs/media/PROTOCOL.md),
[validation and performance report](docs/media/VALIDATION.md), and
[implementation file inventory](docs/media/FILES.md). The verified workstation
[screenshot](docs/media/media-screen.png) is an offscreen rendering, not a Pi
hardware test. Existing Cage/brightness/dashboard operation is retained.
