# Investigation (before implementation)

The existing application is C++17 / Qt 6.8 minimum (this workstation has Qt 6.12).
Qt Quick/QML renders one 800×480 Window; a long press reveals brightness settings.
There is no page router. Preserve brightness interactions and add a media overlay.
SettingsManager shares QSettings persistence with BrightnessController and validates
HTTP patches. The embedded cpp-httplib server has one bounded worker thread,
OpenSSL TLS, password authentication, session cookies, CSRF and origin checks.
Reuse it for device administration; do not add a database or HTTP server.
AdminServer.cpp, README.md, docs/WEB_ADMIN.md, tests/admin_tests.cpp,
tools/benchmark.py and deployment files already have user edits; preserve them.

Cage runs appdeskpi as the configured normal user under logind on tty8. Keep
that arrangement. Qt Network, Gui and image codecs are already loaded by Quick.
Qt WebSockets and DBus development modules are installed here, but must be
packaged explicitly on the Pi/host. No reusable artwork processor exists.

No running DeskPI/Cage or Raspberry Pi is connected. baseline.json measures a
fresh isolated offscreen/software workstation run, not hardware performance.

## Implementation boundaries

One in-process Qt WebSockets TLS receiver; a headless Qt Core event loop companion
with DBus on Linux, Qt Gui image codecs (no QGuiApplication/display), Network and
WebSockets. This reuses the project's Qt deployment and avoids implementing TLS,
WebSockets and D-Bus with several other libraries. A GLib/libdbus implementation
could reduce some Qt mapped RSS but adds a second runtime and separate image/
network/security integration. Measure Qt first. The protocol has no Linux types.
Use the existing administrator certificate and explicitly verified SHA-256 pin
on the companion; never allow ws://. Pairing requires a dashboard code and explicit
approval. Store only credential hashes on the Pi. Keep bounded host states and
one compressed image per host. Artwork work runs in one bounded worker.

## Implemented layout

- `media/Protocol.h`: portable validation and monotonic progress arithmetic.
- `media/MediaReceiver.*`: secure Qt WebSockets listener on the application's GUI
  event loop, bounded per-host state, pairing, credential hashes, selection,
  command acknowledgments, heartbeat and screen lifecycle.
- `media/RoundedArtwork.h`, `MediaScreen.qml`: cached native painted rounded image,
  QML gradient and 320 ms color transitions, touch controls, elided/wrapped text,
  contrast calculation with a dark scrim, locally interpolated progress. Loader
  destroys hidden media UI, freeing its decoded image/paint target. No blur effects.
- `companion/MediaProvider.h`, `MprisProvider.*`: platform boundary and Linux
  session D-Bus backend. Initial discovery waits for bounded property reads before
  selection; runtime ownership/property/seek events update state.
- `companion/ArtworkProcessor.*`: one Qt Concurrent worker, bounded latest-only
  work, bounded asynchronous HTTPS fetches, Qt image codecs, histogram palette.
- `companion/ProtocolClient.*`, `AgentConfig.h`: portable transport, certificate
  pin, authentication, atomic persistence, bounded reconnect/command state.
- `AdminServer.*`, `SettingsManager.*`, existing `web/`: additional authenticated
  routes and pages in the original server/model, no new database or frontend stack.
- `companion/main.cpp`: CLI, Linux signal integration and simulation/inspection;
  `companion/deskbuddy-agent.service`: normal-user graphical-session service.
- `tools/package_agent.py`: release-builder-only packager. Dashboard streams
  manifest-listed files from an external release directory in 64 KiB chunks.

Idle path has no artwork worker thread or progress rendering. The sole artwork
worker starts on demand; Qt manages D-Bus/network I/O internally. No dedicated
receiver thread is added. There are up to eight compressed per-host images and
one decoded displayed image; neither side writes an artwork cache to disk.
Settings persistence keeps the existing debounce/flush implementation.

## Windows next steps

Implement MediaProvider with Windows Global System Media Transport Controls;
map Windows playback capabilities, timeline and thumbnails into the same snapshot.
Keep artwork processing, pinned TLS, pairing, config and wire messages. Isolate
Linux signalfd/session startup behind platform entry points, replace normal-user
systemd autostart with the Windows user-session mechanism, and use a Windows
credential store. Add a Windows-only build target and backend tests without
introducing Windows dependencies to the Linux targets. Release/sign separately
for each Windows architecture. The receiver requires no protocol change.
