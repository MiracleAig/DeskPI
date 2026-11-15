# Validation, performance and remaining limits

Follow-up: the receiver has now been deployed and paired on the real Pi. See
[the live deployment check](DEPLOYMENT_CHECK.md) for hardware observations, the
user-confirmed track/artwork display, and the remaining untested interactions.
The original workstation results below are retained as measured.

Built and tested on CachyOS x86-64, GCC 16.2.1, Qt 6.12.0, glibc 2.44.
Rendering was Qt Quick's offscreen/software backend at 800×480. These are
workstation measurements, not Raspberry Pi 3B+ or physical touchscreen results.

## Build results

- Top-level Release build: appdeskpi, deskbuddy-agent, and all test executables built.
- Standalone `cmake -S companion -B build-agent -DCMAKE_BUILD_TYPE=Release`: built
  without Qt Quick/Widgets; executable `--version` verified. `ldd` confirms only
  Qt Core, Gui, Network, WebSockets, DBus and Concurrent Qt dependencies.
- Receiver-only `-DDESKBUDDY_BUILD_AGENT=OFF -DBUILD_TESTING=OFF`: built successfully.
- Five CTest suites pass: brightness, admin, brightness_ui, media, media_ui.
- `git diff --check`, Python syntax checks and shell installer syntax checks pass.
- Existing bundled cpp-httplib emits OpenSSL 4.1 deprecation warnings on this
  workstation; these are warnings in the existing dependency, not build failures.
- The architecture-specific release archive was extracted; its actual executable
  runs and matches the standalone build. The authenticated HTTPS download was
  byte-count and SHA-256 verified against its release manifest (benchmark.json).

Reproduce:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
DESKBUDDY_STRESS=1 DESKBUDDY_UI_CAPTURE="$PWD/docs/media/media-screen.png" \
  ctest --test-dir build --output-on-failure --timeout 60
python3 tools/media_benchmark.py --seconds 8
```

Tests use temporary configs/certificates, fake brightness hardware, an isolated
session bus and real TLS WebSockets. They neither pair with your real Pi nor
control your real media player. `--inspect` additionally read an actual active
`plasma-browser-integration` MPRIS session on this workstation; the committed
redacted diagnostic proves title/artwork/duration/capability availability without
storing personal titles. No playback command was sent to that live session.
Spotify and VLC were not installed here and were not tested. Browser-control
compatibility and hardware validation remain explicit deployment checks.

## Automated coverage

| Area | Exercised |
| --- | --- |
| Discovery / ownership | Register players after startup; two simultaneous services; removal; initial deterministic selection; preferred source |
| Metadata / progress | Microseconds to milliseconds, artist/title, state parsing, paused and playing interpolation, duration clamp |
| Controls | Real D-Bus Pause and SetPosition/Seeked, WebSocket routing/ACKs, multi-host preferred routing, simulated screen button click |
| Artwork | Missing/unsupported URI fallback, PNG resize to JPEG ≤400 px, palette, invalid/oversized input, actual companion worker → TLS → receiver image transfer/hash validation |
| Connectivity / security | Actual TLS pairing and approval, stored hashes, reconnect after receiver replacement, wrong certificate pin, revoked credential, malformed JSON, oversized binary |
| Settings | Shared web/QML model, typed patches, persistence and owner-only credentials, config import/export, media API CSRF and release downloads |
| Screen lifecycle | Automatic activation, pause timeout, dismissal/reopen, disconnect timeout, settings overlay preserved |
| Rendering / memory | Actual 800×480 offscreen screenshot, plain text metadata, controls, static frame grabs, 120 different artwork/track transitions |

This is functional coverage, not exhaustive fuzzing or a security audit. The
45-second heartbeat expiry and 120-second pairing expiry use monotonic timers
but are not subjected to a full-duration network-loss soak in the automated suite.

## Measured process and system use

Raw results: [before implementation](baseline.json), [current process benchmark](benchmark.json),
[test measurements](test-results.json), [rendered screen](media-screen.png).
Each process workload below was sampled over eight seconds; CPU is percentage
of one core derived from /proc tick deltas. Zero means below sampling resolution.

| Workload / process | RSS KiB | PSS KiB | CPU % |
| --- | ---: | ---: | ---: |
| clock_only_current_binary_no_listeners / deskpi | 65280 | 55563 | 0.0 |
| existing_dashboard_tls_media_disabled / deskpi | 69664 | 59833 | 0.0 |
| receiver_idle / deskpi | 73124 | 63273 | 0.0 |
| simulated_playback_visible / deskpi | 76796 | 57458 | 0.25 |
| simulated_playback_visible / agent | 32404 | 16545 | 0.125 |
| host_disconnected / deskpi | 76848 | 66933 | 0.125 |

The pre-implementation isolated clock run measured 65,212 KiB RSS / 32,637 KiB PSS
and 0% sampled CPU over five seconds. Its build had the original default build
flags; the final build is Release. The current clock-only run is approximately
the same RSS. To isolate media cost more fairly, compare the final binary's
warmed, authenticated HTTPS dashboard with media disabled against its receiver
idle case: **3,460 KiB (3.38 MiB) incremental RSS**, under the 5 MiB engineering
goal on this workstation. PSS shifts as Qt/crypto libraries become shared with
the companion and other desktop processes, so do not interpret inter-workload
PSS changes as an allocation leak. Total system-used RAM is recorded in JSON;
other desktop workloads make it unsuitable as a causal feature allocation metric.

The companion uses **32,404 KiB RSS / 16,545 KiB PSS** during the simulated idle
playback workload, above the 20 MB RSS target. Investigation found roughly
4.4 MiB resident libcrypto, 4.3 MiB Qt Core, 3.1 MiB heap, 1.5 MiB Qt Network,
1.4 MiB Qt Gui plus C++/libc/ICU/TLS shared pages. There is no historical artwork
cache or active artwork worker in this idle sample. The gap is largely runtime /
TLS mapped pages rather than retained images; a smaller libdbus/GLib/native
codec agent remains a possible later optimization but would require a separately
maintained transport/image stack. Do not claim the host RSS goal is met.

Latest functional timing/memory measurements:

- Artwork processing ms 13 JPEG bytes 2328
- Reconnect ms 2368
- Metadata change to software frame ms (50ms polling resolution) 52
- 30 software frame grabs ms 1
- 120 artwork changes ms 7337 RSS before/after KiB 94188 95996 delta 1808

The metadata-to-frame measurement uses 50ms QtTest polling and a local TLS socket;
it is a coarse upper-bound observation for metadata with cached artwork, not a
network-independent latency distribution. Static repeated frame grabs do not
establish DSI/GPU frame rate or input-to-photon latency. The artwork measurement
uses one synthetic 1200×800 PNG; real compression/decoding costs and JPEG sizes
vary. The 120-track run reports allocator warmup/retention across one test (below
the 8 MiB growth guard); it is not proof against every long-duration leak.

Startup-to-HTTPS-health was **53.5 ms**, approval-to-first-synchronized simulator
state **1349.1 ms** (including reconnect backoff). These are local workstation
samples. Jitter makes reconnect latency vary. Cold-storage startup and physical
screen responsiveness have not been measured on a Pi.

## Known limits and deployment work

- The initial implementation run had no Pi access. A subsequent native ARM64 Pi
  receiver build and live connection were verified (see DEPLOYMENT_CHECK.md). No
  ARM companion package or physical touch-control test has been produced.
- The supplied prebuilt package is a **CachyOS/Arch x86-64 development build**,
  requiring compatible Qt 6.12, glibc 2.44 and libstdc++. Build distro-specific
  artifacts on the intended oldest distro; do not offer this binary as universal.
- Copy releases/manifest.json and its archive to the configured external release
  directory to make it available in a deployed dashboard. The repository artifact
  has been built/verified; it has not been installed onto an unavailable Pi.
- Installer/service files and fallback syntax are verified, but real logout/login
  across GNOME, KDE, Xfce and non-systemd systems has not been exercised. XDG
  autostart lacks systemd's crash restart behavior.
- Certificate enrollment/rotation is explicit; RSA PEM identity and time-valid
  exact certificate pin are required. There is no automatic certificate issuance,
  pin rollover or mDNS discovery daemon. Manual IP/DNS and existing .local name
  resolution work. No insecure transport fallback.
- No reliable music/video classification exists in standard MPRIS. Unknown sessions
  are controlled normally; disabling non-music auto-switch also excludes unknown.
- HTTP-only and redirecting artwork are omitted. Artwork caching deliberately
  retains only the current processed image per host, with no historical disk cache.
- Windows backend, Windows autostart/credential storage, and signed Windows
  packages remain future work; see ARCHITECTURE.md. The v1 receiver protocol
  already carries platform-independent session state and commands.
