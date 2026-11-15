# DeskBuddy Linux companion setup

Run the display and companion as normal users. The companion needs no GUI,
tray, X server, audio capture permission, root service, or desktop environment.

## Build and dependencies

Pi: retain the existing Qt Quick 6.8+, Network and OpenSSL dependencies; add
Qt WebSockets runtime/development packages. DBus/Concurrent are companion-only.
The top-level build currently builds both targets, so also install these development
modules there. For a receiver-only distribution build, use
`-DDESKBUDDY_BUILD_AGENT=OFF` (and `-DBUILD_TESTING=OFF`).

Host: C++17, CMake 3.16+, Qt 6.8+ Core, DBus, Network, WebSockets, Gui image
codecs and Concurrent. Qt Gui supplies image codecs without creating a GUI
application; neither Qt Widgets nor Qt Quick is required by the standalone agent.
JPEG and PNG support must be available; WebP input is optional. OpenSSL's Qt TLS
backend must be installed. Distribution package names vary (for example Debian
`qt6-base-dev`, `qt6-websockets-dev`, `qt6-image-formats-plugins`; distro Qt must
meet the version requirement). No Python is needed at runtime.

Build only on the host or a release builder:

```sh
cmake -S companion -B build-agent -DCMAKE_BUILD_TYPE=Release
cmake --build build-agent -j2
```

The binary is `build-agent/deskbuddy-agent`. The repository's top-level build
puts it at `build/companion/deskbuddy-agent`. Never compile on the Pi to service
a download request. Build x86-64 and ARM64 separately on matching environments
or using an appropriate cross-toolchain/sysroot.

## Prepare the Pi

First configure the existing administrator password and HTTPS listener using
`appdeskpi --admin-set-password` and the existing `--admin-configure` command.
See `docs/WEB_ADMIN.md` in the source repository. A practical self-signed identity:

```sh
install -d -m 700 "$HOME/.config/DeskPI/tls"
openssl req -x509 -newkey rsa:3072 -nodes -days 365 \
  -keyout "$HOME/.config/DeskPI/tls/key.pem" \
  -out "$HOME/.config/DeskPI/tls/cert.pem" -subj '/CN=deskbuddy.local'
chmod 600 "$HOME/.config/DeskPI/tls/key.pem"
./build/appdeskpi --admin-configure --bind lan --host deskbuddy.local \
  --tls-cert "$HOME/.config/DeskPI/tls/cert.pem" \
  --tls-key "$HOME/.config/DeskPI/tls/key.pem"
```

Run these as the SAME user that runs DeskPI under Cage; its configuration and
certificate permissions belong to that user. Securely verify the browser's
certificate using the Pi console or SSH before entering the administrator
password. Use an SSH tunnel to the loopback dashboard if preferred. The media
listener shares the configured address and RSA PEM identity, on TCP 8091 (override
`DESKBUDDY_MEDIA_PORT` at process startup). Restrict both HTTPS and media ports
to your trusted LAN using the existing host firewall. Do not port-forward them.
Without certificate/key configuration, the media listener fails closed and the
existing clock/dashboard continue functioning. Restart DeskPI after configuration.

## Install and pair a host

Download the matching package from **Connected Devices**, extract it, inspect
`BUILD.txt` and `INSTALL.md`, then run `./install.sh` as your desktop user. This
copies the executable to `~/.local/bin` and installs autostart without starting an
unconfigured agent. The included executable is dynamically linked: it requires
the Qt/glibc versions documented in BUILD.txt. It is not a universal Linux binary.
If building from source, copy the binary, service and scripts into one directory,
write `uname -m > ARCHITECTURE`, then use the same installer.

1. Sign in to DeskPI's existing dashboard and open **Connected Devices**.
2. Click **Start pairing**. A random code and SHA-256 certificate fingerprint
   appear for 120 seconds. Copy the fingerprint through this trusted channel.
3. In the host's desktop terminal (not a root shell):

```sh
read -r -s -p 'Pairing code: ' DESKBUDDY_PAIR_CODE; echo
export DESKBUDDY_PAIR_CODE
~/.local/bin/deskbuddy-agent --url wss://deskbuddy.local:8091 \
  --pin VERIFIED_64_HEX_DIGIT_SHA256_FINGERPRINT --name 'My Linux computer'
unset DESKBUDDY_PAIR_CODE
```

4. Confirm the computer name in the dashboard and click **Approve computer**.
   The agent stores its unique credential with mode 0600, then reconnects.
5. Stop the foreground agent with Ctrl+C before starting the user service:

```sh
systemctl --user start deskbuddy-agent
systemctl --user status deskbuddy-agent
journalctl --user -u deskbuddy-agent -f
```

`--pair-code` is available for test use, but the environment/hidden prompt keeps
codes out of shell history and process arguments. Credentials are saved at
`~/.config/deskbuddy-agent/config.json` unless `--config` is specified. The Pi
stores only SHA-256 credential hashes in its existing configuration directory.
There is no silent trust on first connection and no insecure WebSocket fallback.
Manual addresses (IP, DNS, or an already resolvable `.local` name) are supported;
the agent does not implement its own mDNS advertiser/browser.

## Desktop autostart and removal

On GNOME/KDE and other desktops with `graphical-session.target` active, install.sh
enables a systemd USER service bound to that target. The user manager provides
`/run/user/UID/bus` through Qt's standard session bus discovery; no system bus,
root service, lingering or exported root session environment is used. If the
desktop does not activate this target or has no systemd user manager, the installer
writes `~/.config/autostart/deskbuddy-agent.desktop`. Ensure the binary is paired
before the next login. Do not enable both mechanisms. XDG autostart itself does
not restart crashes; systemd is preferred where available.

Manual service controls: `systemctl --user stop deskbuddy-agent`,
`systemctl --user restart deskbuddy-agent`, and
`systemctl --user disable --now deskbuddy-agent`. The service waits 10 seconds
between crashes and permits five starts per two minutes. Network reconnects
happen inside the agent using bounded exponential backoff and jitter.

Run `./uninstall.sh` from the package to stop/remove it. It preserves credentials
for a reinstall; remove `~/.config/deskbuddy-agent` to erase them. Also click
**Revoke** in Connected Devices. Revocation disconnects the device immediately
and denies later authentication.

## Publish prebuilt downloads

After building AND verifying a binary on the release builder:

```sh
python3 tools/package_agent.py --binary build/companion/deskbuddy-agent \
  --output releases --platform 'Exact distribution; Qt X.Y+; glibc X.Y+; architecture'
```

Copy the generated tar.gz and `manifest.json` to
`/usr/local/share/deskbuddy/releases` on the Pi, or set `DESKBUDDY_RELEASE_DIR`
to a readable directory before starting DeskPI. Artifacts are streamed by the
existing authenticated dashboard and never embedded in appdeskpi. The manifest
lists architecture, build constraints, size and SHA-256. Only previously built
files in that directory are offered. The supplied development artifact, if
present, is **CachyOS/Arch x86-64, Qt 6.12 / glibc 2.44**; do not use it on an
older Debian/Ubuntu host or on ARM64. Rebuild on the oldest supported distro for
a broader release. No ARM64 release is claimed.

## Test actual players

Play a track in Spotify, then verify title, artist, artwork and Pause/Next. Repeat
with VLC (enable its D-Bus/MPRIS control interface where required). In Firefox
or Chromium/Chrome, start a media session on a compatible site and check the
browser's media-key/MPRIS integration and relevant browser settings. Browser
sessions may expose video, omit duration/artwork/seeking, aggregate several tabs,
or disappear when paused; browser flags, sandboxing and distro packaging vary.
MPRIS has no reliable music-versus-video field, so the agent labels media
`unknown`; disabling non-music auto-activation also excludes these unknown sessions.
The manual media button still opens them. Capabilities always come from the player.

For an artificial source use `deskbuddy-agent --simulate` (plus normal pairing
arguments/config). `--artwork file:///absolute/path/image.jpg` supplies fixture
art. `ctest` runs an isolated real D-Bus test player via `dbus-run-session`.

Troubleshooting: check service logs, session bus availability, matching fingerprint,
HTTPS configuration and the current pairing window. A changed/expired identity
requires deliberate pin verification and reconfiguration; a revoked device must
pair again. Check that only one companion is running for a credential. Artwork
supports file URIs and HTTPS with no redirects; HTTP-only artwork is intentionally
omitted. Enable image format plugins for WebP input. Normal transport controls
work without artwork. Use the full MPRIS service name in Preferred Media Player.
