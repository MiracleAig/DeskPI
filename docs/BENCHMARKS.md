# Raspberry Pi measurements — 2026-11-14

Raspberry Pi 3B+, 926,624 KiB usable RAM, Debian 13, Qt 6.8.2, 800×480 DSI under
Cage. The before run uses the saved original executable; the after run uses 0.2.0.
Both were built with the existing CMake/Ninja defaults and run as admin.

## Before / after

| Metric | Before | After |
|---|---:|---:|
| DeskBuddy idle RSS | 121.93 MiB | 127.44 MiB |
| System used RAM (`MemTotal - MemAvailable`) | 267.00 MiB | 259.07 MiB |
| DeskBuddy idle CPU, percent of one core | 0.199% | 0.399% |
| Application startup, child creation → first Wayland surface | 1.296 s | 1.743 s |
| Authenticated status response, mean / p95, 30 requests | N/A | 6.83 / 10.43 ms |

Warmed idle RSS increased **5.51 MiB**, below the aspirational 15 MiB target. Before
exercising all features, the new app's first idle RSS was 124.19 MiB (+2.26 MiB).
The primary after idle sample was taken after login, light/dark themes, physical
settings controls, configuration import and restart had been exercised, with the
original clock/display preferences restored and no request workload running.

CPU/RSS samples cover 20 seconds each. CPU values are for the application, with
100% meaning one fully used CPU core (the Pi has four). System RAM is an observed
whole-device value, not an attributable saving: cache, swap and other processes
vary between samples. RSS includes shared library pages and GUI caches; it is not
private heap usage. An initial sample that overlapped startup showed 3.144% CPU;
it is retained in the raw workload data, but is not presented as steady-state idle.

Startup is the difference between Cage's logged `Child process created` and the
first `new xdg_surface`, excluding seat/DRM setup. These are single observed samples,
not a statistically controlled cold-boot benchmark. The saved logs gave 1.296 and
1.743 seconds respectively. They do not imply a measured full-system boot time.

## Active workloads

All rows are measured on the physical Pi. A Python standard-library HTTP client
simulated browser traffic; no browser host was available in the session. This
measures server cost/latency, not desktop/mobile browser rendering performance.

| Workload | Duration | App RSS at end | App CPU, one core | Mean / p95 latency |
|---|---:|---:|---:|---:|
| One browser equivalent: settings + status every 8 seconds | 20.02 s | 124.77 MiB | 0.549% | 14.05 / 16.50 ms per two-request poll |
| Status refresh every second | 20.05 s | 124.82 MiB | 1.147% | 10.01 / 10.43 ms per request |
| Brightness adjustment, attempted 4 Hz (76 completed writes) | 20.19 s | 124.87 MiB | 8.568% | 80.39 / 83.81 ms per request |

Brightness latency includes settings persistence on the SD card. The original
configuration was restored after the workload. No extra metric-sampling timer runs
inside DeskBuddy while browsers are absent. The benchmark harness itself consumes
some system RAM, equally present during before/after sampling.

## Physical and synchronization checks

- Authenticated brightness API updates: 40% → raw actual 102; 70% → raw actual 179.
- Live 24-hour format, seconds, date, light theme and accent applied to the DSI screen.
- Screenshots showed the clock advancing from **15:04:57 to 15:05:02** while the
  server handled **75 status requests over about four seconds**, with no HTTP errors.
  This demonstrates clock progress; it is not a measurement of every rendered frame.
- Eight concurrent authenticated status requests all returned running status.
- Injected touchscreen input changed manual brightness from 70% to 39%; the API
  returned the new shared value without restarting.
- Confirmed application restart re-executed under Cage, invalidated the old session,
  and restored saved clock/appearance settings. Original preferences were restored.
- Temporary test credentials were removed and the service restarted. The final
  listener is 127.0.0.1:8080 with `setupRequired:true` and no default password.

The GUI event-loop test also checks that its timer continues during password hashing.
No noticeable clock-stall issue was observed in these checks. A human browser/mobile
layout/accessibility review remains recommended because the browser host was unavailable.

## Reproduction and raw data

- [Before idle](benchmark-data/before.json)
- [After workload measurements](benchmark-data/after-workloads.json)
- [After warmed idle](benchmark-data/after-idle.json)
- [Live API/hardware check output](benchmark-data/live-check.txt)

Run `python3 tools/benchmark.py --seconds 20 --authenticated --output result.json`
on the Pi after setting up your own administrator password. The script prompts
without echo, polls through HTTP, exercises safe brightness values, and restores
an exported configuration afterward. It is a development tool, never an app service.
Avoid running other tests, compilation or API clients during an idle sample.
