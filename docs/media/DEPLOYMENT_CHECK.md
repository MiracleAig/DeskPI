# Live deployment check — 2026-11-14

The desktop agent initially appeared to run but could not display anything on the
Pi. Investigation established three concrete causes:

- The Pi still ran the pre-media build. Its HTTPS page had no Connected Devices
  page, and TCP 8091 refused connections.
- The desktop had no saved companion URL, certificate pin or device credential.
- An unconfigured agent called QCoreApplication::exit before its event loop began,
  so it remained running. Qt logging configuration could also suppress its message.

## Corrections and actual verification

Compiled the receiver natively as admin on the Raspberry Pi 3B+ (aarch64, Qt 6.8.2),
after the user installed Debian's qt6-websockets-dev package. Preserved the old
executable, source and settings in `~/.cache/deskpi-media-update/` on the Pi.
The updated executable is at `~/deskpi/build/appdeskpi`; a separate `build-media`
contains the native build. The existing Cage/systemd launcher remains in use.

Verified the HTTPS certificate fingerprint against the documented Pi identity.
Confirmed the updated dashboard at https://deskpi.local:8443 and TLS WebSocket
listener on 8091. The user started pairing in the dashboard and explicitly
approved `miraclea Linux`. The user then confirmed that the actual track and
artwork appeared on the physical Pi display.

Installed companion 1.0.1 at `~/.local/bin/deskbuddy-agent` on the desktop and
enabled `deskbuddy-agent.service` in its normal user's systemd manager. Stopped
the obsolete unconfigured process and the temporary pairing process; exactly one
service-managed companion remains. The credential file has mode 0600. Verified
that its credential hash matches the Pi's persisted device record without exposing
the credential. The user service was active and enabled at completion.

Restarted the Pi receiver after the final UI fix. The companion automatically
re-established its TLS connection (observed approximately 7.32s after initiating
the restart, including Pi/Cage startup and reconnect backoff). The Pi service
remained active and the final application journal had no new warnings. Original
clock, brightness and appearance values were compared and remained unchanged;
the media section and preferred paired host were added.

Also corrected and regression-tested two issues found during this check:

- Restore typed booleans/numbers when loading media settings from INI strings in
  a fresh process, instead of silently falling back to defaults.
- Treat absent connection/capability fields as false in the empty media screen,
  preventing repeated Qt 6.8 `undefined to bool` warnings.

The unconfigured CLI now exits with code 2 in milliseconds and always prints clear
pairing instructions to stderr. A subprocess regression test checks this behavior.
All five workstation test suites passed after the startup/settings fixes; the
media and media_ui suites passed again after the empty-state UI correction. The
updated 1.0.1 release archive was downloaded through an isolated authenticated
HTTPS dashboard and verified against its SHA-256 manifest; see
[benchmark-agent-1.0.1.json](benchmark-agent-1.0.1.json).

## Hardware observations and limits

Raw observations: [before](pi-before-deployment.json),
[after](pi-after-deployment.json). The connected media receiver used 142,256 KiB
RSS and approximately 0.80% of one Pi CPU core over five seconds. Total system
used memory was 237,736 KiB. `/proc/PID/smaps_rollup` denied PSS access to this SSH
session, so PSS was not measured. The before sample overlaps compilation and the
old build used different optimization flags; these are not controlled incremental
memory benchmarks. No physical touch input or real-player transport action was
performed in this deployment check. Transport routing, seeking and simulated
screen clicks have automated coverage. Actual logout/login autostart is still
untested; service enablement and live service execution were verified.

Useful desktop checks:

```sh
systemctl --user status deskbuddy-agent
journalctl --user -u deskbuddy-agent -n 30 --no-pager
~/.local/bin/deskbuddy-agent --inspect
```

The companion now starts with the graphical desktop session; do not start a second
manual copy while its service is running. Manage devices in Connected Devices.
