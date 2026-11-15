# Web administration

DeskPI 0.2 adds an embedded administration dashboard. The production runtime is
one Qt application: no Python/Node backend, database, cloud service, or extra
systemd unit. The existing Cage launch and brightness permissions are retained.

## Home-network access on this Pi

Open **https://deskpi.local:8443** on a computer or phone connected to your home
network. The current direct IP address also works: **https://192.168.0.213:8443**.
Sign in with your existing DeskPI administrator password. No SSH tunnel is needed.

DeskPI now starts automatically when the Pi boots and restarts after a crash.
The persistent unit is `/etc/systemd/system/deskpi-clock.service`, enabled under
`multi-user.target`. It runs Cage and DeskPI as admin. The one fixed privileged
pre-start command activates tty8 so logind can grant the physical display session.
No root web application or separate backend is introduced.

The Pi advertises `deskpi.local` through its existing Avahi service. Its operating
system hostname is independent. LAN listening accepts changing local IPv4 addresses,
so a DHCP address change does not require editing the listener or restarting the app;
the `.local` address is the preferred bookmark.

### Local HTTPS certificate

The certificate was generated locally for this Pi. Because a public certificate
authority did not issue it, a browser can display a certificate warning on first use.
Add a certificate exception only for this Pi's address after checking its fingerprint;
this is a one-time browser trust decision, not a requirement to keep SSH open.

SHA-256 certificate fingerprint:

```
B7:C2:FE:80:3A:2E:F1:38:4E:AD:2E:44:38:6E:54:5C:45:56:CC:A2:EA:C8:03:4C:62:FF:B0:56:82:2B:C1:CA
```

Certificate: `~/.config/DeskPI/tls/deskpi-cert.pem`.
Private key: `~/.config/DeskPI/tls/deskpi-key.pem`, owner-only permissions.
A certificate trusted by your devices can replace these files if desired. Never
copy or publish the private key. Keep the dashboard on the home network; no router
port forwarding or public exposure was configured.

### Setup and maintenance

The binary still defaults to loopback on a fresh installation until authentication
and LAN HTTPS have been explicitly configured. The deployed Pi is already configured.
To change your password from a local or SSH terminal:

```sh
~/deskpi/build/appdeskpi --admin-set-password
sudo systemctl restart deskpi-clock.service
```

Choose a password with at least 12 letters, numbers, or symbols. A phrase made from
a few words is an easy option. Nothing appears while you type; this is normal.
Your saved settings are retained after a restart.

The deployed listener was configured with:

```sh
~/deskpi/build/appdeskpi --admin-configure --bind lan --port 8443 \
  --host deskpi.local --host deskpi \
  --tls-cert /home/admin/.config/DeskPI/tls/deskpi-cert.pem \
  --tls-key /home/admin/.config/DeskPI/tls/deskpi-key.pem
```

`--bind lan` uses an IPv4 listener on all interfaces while accepting only private/
loopback source addresses and exact local/configured Host names. It requires both
a configured password and TLS. Arbitrary wildcard/public bind addresses and
unencrypted LAN access remain rejected.

For loopback-only access instead, configure `--bind 127.0.0.1 --port 8080`, restart,
and use an SSH forward. This is an optional restriction, not the deployed default.

The checked-in `deployment/deskpi-clock.service` is the persistent launcher. It
waits for network-online and logind, activates tty8, and uses `Restart=always`.
`--admin-disabled` remains available for troubleshooting.

## Architecture and dependencies

Browser → bounded embedded HTTP server → `SettingsManager` → existing
`BrightnessController` / QML clock and appearance.

`SettingsManager` wraps the existing authoritative brightness model and exposes
clock/appearance properties to QML. All application preferences remain in the same
QSettings INI file. HTTP changes are marshalled to the GUI thread using Qt queued
invocations. The HTTP worker waits for persistence without blocking the GUI. Reads
of `/proc`, `/sys` and network information occur only on authenticated status requests.

Settings writes (including touchscreen brightness writes) are serialized on the
existing backlight I/O thread. Touchscreen changes are returned by subsequent API
reads. Brightness policy, safety minimum, coalescing and manual overrides are
preserved. A browser manual adjustment is a manual override; configuration import
restores the enabled schedule rather than creating an override.

Qt 6.8's optional QHttpServer was evaluated. It lacks the pre-allocation body and
header size controls introduced in Qt 6.11, so this version uses pinned, vendored
**cpp-httplib v0.66.0** (MIT) with explicit parser/queue limits. See
[upstream documentation](https://github.com/yhirose/cpp-httplib/tree/v0.66.0) and
[Qt's limits API](https://doc.qt.io/qt-6/qhttpserverconfiguration.html).

Additional build requirements are Qt Network, OpenSSL development headers and the
platform thread library. Qt Network and OpenSSL runtime libraries already exist on
this Pi; `libssl-dev` was installed for TLS compilation (apt also updated the
existing OpenSSL command-line package). No Qt HTTP Server/WebSockets package was
installed. OpenSSL-backed HTTPS is compiled in, not a separate service.

The library uses exactly one accepting thread and one serialized request worker,
with at most four queued connections and no dynamic worker growth. The accept
thread blocks between requests (up to a 60-second idle select); the worker sleeps
on its queue. Login hashing and HTTP/file operations never execute on the Qt GUI
thread. The GUI receives only small settings validation/property-update callbacks.

## Security model

- Qt `QPasswordDigestor` PBKDF2-HMAC-SHA256, 600,000 iterations, 32-byte random salt
  and 32-byte verifier; system-generated 256-bit session tokens.
- Session lookup stores SHA256(token), not the bearer token. Sessions expire after
  20 minutes idle or 8 hours absolute; logout and app restart invalidate them.
- `HttpOnly; SameSite=Strict` cookies, plus `Secure` and HSTS under HTTPS.
- Same-origin checks on every mutation, CSRF tokens on authenticated mutations,
  exact Host allowlisting (including port), no CORS, and cross-site fetch rejection.
- At most 5 login attempts per source per minute, 10 globally; bounded session and
  rate-limit maps. Login failures are generic and passwords are never logged.
- 16 KiB body limit, 256-byte URL limit, at most 32 header fields of 2048 bytes,
  read/write timeouts, bounded queue and one request per connection.
- Fixed routes/resources only: no directory mounts, user-selected filesystem paths,
  shell interpolation, arbitrary command endpoint or unfiltered configuration export.
- CSP restricts scripts/styles/connections to this origin; no framing, external
  assets, inline handlers, eval, localStorage credentials or externally hosted fonts.

This is a small private-device server, not an internet-facing general-purpose
service. One slow request/login can delay other HTTP requests in the serialized
worker; the touchscreen is isolated from that work. Read timeouts and bounded
buffers limit cost, but this is not a replacement for an internet edge proxy.

## API v1

All mutation bodies are JSON objects with `Content-Type: application/json` and
`Origin` exactly matching the allowed dashboard origin. After login, include the
session cookie and `X-CSRF-Token`. Responses use JSON; failures consistently contain
`{"error":{"message":"..."}}`.

| Method | Endpoint | Purpose |
|---|---|---|
| GET | `/api/v1/health` | Minimal unauthenticated `{"ok":true}` |
| GET | `/api/v1/session` | Authentication/setup state; CSRF token only for a valid session |
| POST | `/api/v1/login` | `{"password":"..."}`; returns CSRF token and HttpOnly cookie |
| POST | `/api/v1/logout` | Empty object; invalidates session |
| GET | `/api/v1/status` | Authenticated hostname, version, uptime, CPU, temperature, RAM, disk, IPs and brightness |
| GET | `/api/v1/settings` | Shared display, clock and appearance settings plus live hardware state |
| PATCH | `/api/v1/settings/display` | Any supported display fields below |
| PATCH | `/api/v1/settings/clock` | Any supported clock fields below |
| PATCH | `/api/v1/settings/appearance` | Theme/accent fields below |
| GET | `/api/v1/config/export` | Credential-free versioned JSON attachment |
| POST | `/api/v1/config/import` | Complete versioned export document |
| POST | `/api/v1/application/restart` | Requires exactly `{"confirm":true}`; returns 202 |

Display fields: `manual`, `day`, `night` (integer percentages at least the configured
minimum and at most 100); `automatic` (boolean); `dayStart` and `nightStart`
(integer minutes since midnight, 0–1439, different); `minimum` (1–100). The web slider
uses at least 5% and respects higher configured minimums. Hardware read-only fields
are `current`, `available`, `error`, `overrideActive`, `status` and cannot be patched.
The existing controller applies a manual request asynchronously (coalesced at 120 ms);
responses report saved target preferences and the latest hardware readback. Subsequent
polls report the applied hardware state. Unavailable hardware returns 409 for changes.

Clock fields: `format24`, `seconds`, `date` (booleans), and `dateFormat` from
`ddd, MMM d yyyy`, `yyyy-MM-dd`, `dd/MM/yyyy`, `MM/dd/yyyy`.
Appearance fields: `theme` (`dark` or `light`) and `accent` (`#RRGGBB`). Clock background,
text/date and the physical settings panel update live. There is no general skin editor.

Unknown fields, wrong types, fractional integers, invalid colors/times and unsafe
brightness values are rejected. Codes: 400 validation, 401 authentication, 403
Host/origin/CSRF, 404 route, 409 hardware/import conflict, 413 size, 415 content type,
429 login rate limit, 500 persistence/internal failure, 503 password setup required.

Restart is narrowly scoped: after confirmation and settings flush, DeskPI closes
its own objects and calls `execv` with its original executable/arguments. The PID
stays under Cage; no root, sudo, arbitrary executable or systemd action is involved.

## Configuration and recovery

- Preferences: `~/.config/DeskPI/DeskPI.conf` (`brightness`, `clock`, `appearance`).
- Credential verifier only: `~/.config/DeskPI/admin-auth.ini` (0600).
- Listener/TLS paths: `~/.config/DeskPI/admin-listener.json` (0600 when configured).
- Previous imported preferences: `DeskPI.conf.previous.json` beside the INI (0600).

Exports are `{"schemaVersion":1,"settings":{"display":{...},"clock":{...},"appearance":{...}}}`.
Import requires complete supported sections; all validation finishes before any
live change. A recovery export is atomically written before import, then preferences
are applied through the shared model and persisted. Invalid imports leave live
preferences untouched. Storage failures are reported rather than claimed successful;
settings are not a multi-file database transaction. Recovery is available by importing
the previous JSON or restoring its preferences locally. Credentials and listener/TLS
configuration are always excluded from export/import.

## Frontend and synchronization

The frontend is HTML/CSS/vanilla JavaScript with system fonts, desktop sidebar and
mobile navigation. It has loading/error states, native form validation, explicit
restart/import confirmation and automatic retries for connection loss. It polls
settings/status every eight seconds only while signed in and visible. Unsaved edits
are preserved, and slider requests are debounced at 220 ms. There are no server-side
metric timers or browser-client polling tasks when nobody is connected.

Assets are embedded Qt resources with gzip variants, ETags and static caching.
Run `python3 tools/compress_web.py` after frontend edits; gzip files are committed,
so Python is not a build or application dependency.

## Tests and performance

```sh
cmake -S . -B build -G Ninja
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Tests cover HTTP/API responses, invalid JSON/ranges/unknown fields, authentication,
Host/CSRF, login limits, idle/absolute session expiry, logout, TLS Secure cookies,
persistence, all-or-nothing validation on import, backups, hardware updates, missing
sensors, concurrent requests and a GUI-event-loop heartbeat during login hashing.
The actual frontend runs against a lightweight DOM/fetch harness in Qt's JavaScript
engine, covering navigation, form submission, polling interval and disruptive-action
confirmation. No Node or browser test framework is required.

See [BENCHMARKS.md](BENCHMARKS.md) for measured Pi results. The optional development
`tools/benchmark.py` uses only Python's standard library; it is not an application
backend and is never started by DeskPI.

A full browser host was unavailable in this development session. Automated frontend
logic and deployed DSI rendering are tested; final desktop/mobile layout and browser
accessibility should also be checked in a real browser at the URL above.

## Implementation file map

Created:

- `AdminServer.{h,cpp}` — bounded HTTP/TLS routing, security headers, local CLI setup.
- `AdminAuth.{h,cpp}` — password verification, sessions, expiry, rate limiting.
- `SettingsManager.{h,cpp}` — shared validated preferences and backup schema.
- `SystemMetrics.{h,cpp}` — request-driven system information.
- `web/index.html`, `web/style.css`, `web/app.js` and committed gzip variants.
- `third_party/httplib.h`, license, provenance and checksum.
- `tests/admin_tests.cpp`, `tools/benchmark.py`, `tools/compress_web.py`.
- `docs/WEB_ADMIN.md`, `docs/BENCHMARKS.md`, and measured JSON/text data.

Modified:

- `main.cpp`, `CMakeLists.txt` — integrated startup, re-exec, dependencies/resources/tests.
- `BacklightIo.{h,cpp}`, `BrightnessController.{h,cpp}` — serialized background
  persistence, validated batch updates and browser/manual-override integration.
- `Main.qml`, `BrightnessSettings.qml` — live clock/date/theme/accent properties;
  the hidden two-second-hold gear and cursor-free clock are preserved.
- `tests/brightness_ui_tests.cpp`, `README.md` — clock/theme checks and documentation.

## DeskPI deployment paths

The active Pi checkout is `~/deskpi` and the persistent Cage unit is
`deskpi-clock.service`. Preferences and the administrator password verifier are
preserved in `~/.config/DeskPI/` during the rename. Deployment backups are kept
privately under `~/.cache/deskpi-migration/`.

The direct home-network web address is listed above. Sign in again after an update;
the renamed session cookie does not carry over an earlier browser session.
