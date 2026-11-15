# DeskBuddy Media Protocol v1

A single desktop-initiated RFC 6455 WebSocket over TLS 1.2 or newer, default
TCP 8091. UTF-8 JSON text events carry `version: 1` and a string `type`.
No audio, arbitrary executable names, URLs to fetch on the receiver, D-Bus types,
or platform-specific paths are interpreted by the receiver. New Windows providers
can use exactly this wire format. Sizes below are bytes (KiB = 1024 bytes).

## Transport and compatibility

Only `wss://` is supported. The companion requires an explicitly verified lowercase
SHA-256 fingerprint of the server's DER certificate. Exact pin verification occurs
before ANY application credential is sent, including with publicly trusted
certificates. Pinning deliberately replaces hostname/CA verification; certificate
time validity is still checked. Verify the fingerprint in an authenticated HTTPS
dashboard whose identity has already been verified, or on the Pi via SSH/console.
TLS record authentication supplies integrity, freshness and replay protection.
Revocation invalidates a stolen bearer credential. Do not forward the ports to the
internet. The listener accepts loopback/RFC1918/ULA clients only, and rejects
browser Origin headers. This is a trusted-LAN service, not a hardened public edge.

Unsupported versions, invalid JSON, invalid fields, unexpected message types,
invalid authentication and bad artwork close the connection. Policy errors use
1008; JSON/state protocol errors use 1002; WebSocket oversize messages use the
library's 1009 behavior. Normal disconnects and transport errors reconnect; a
1008 refusal or certificate mismatch needs operator attention. Optional unknown
object fields are ignored within known messages to permit additive extension.
New required fields or changed semantics require a new protocol version.

Limits: 16 KiB JSON; 160 KiB JPEG plus 64-byte binary header by default; configurable
JPEG ceiling 16–160 KiB. The receiver caps frames and assembled messages before
application decoding. 8 paired hosts, 12 connected/pairing sockets, 4 pending
pairing requests, 64 application messages/second/connection, 12 unauthenticated attempts
per minute globally. New sockets have 5 seconds to submit a greeting; pending
approval sockets have 120 seconds maximum lifetime. Handshakes time out in 5s.
Peers send WebSocket ping every 15 seconds, answer pings with the Qt protocol
stack, and disconnect after 45 seconds without a pong/response. No metadata
heartbeat polling. Metadata writes are coalesced for 40ms and bounded by the
socket's outgoing queue. Companion artwork uploads also check backpressure.

Reconnect starts at 1s, doubles to a 32s base, and adds uniform 0–25% jitter
(maximum approximately 40s). Successful authentication resets the backoff.
The initial state after EVERY `ready` is a complete snapshot, not a delta.
A connection establishes a fresh sequence epoch; duplicate/older state sequence
numbers are ignored. The receiver discards the previous live state on auth, so
a stale playing state cannot activate the screen before synchronization.

## Pairing and authentication

The existing dashboard session/CSRF/origin checks protect all administration.
POST `/api/v1/media/pair` with `{}` creates a 48-bit random pairing code valid for
120 seconds. Dashboard displays the certificate SHA-256 fingerprint as well.
A host with that code and a verified pin sends:

```json
{"version":1,"type":"pair","code":"short-lived-code","name":"Office Linux","os":"linux"}
```

`name` is nonempty and at most 80 characters; `os` at most 32. Receiver answers
`approval_pending`. GET `/api/v1/media/devices` lists requests, paired host IDs,
connection status and selected host. POST `/api/v1/media/approve` with `{"id":"request-uuid"}`
requires an unexpired request and connected agent. It atomically saves the device
credential hash before issuing:

```json
{"version":1,"type":"paired","device_id":"uuid","credential":"64-lowercase-hex-characters"}
```

Credential entropy is 256 bits from the OS random source. Receiver stores SHA-256
hashes with owner-only file permissions and compares hashes in constant time.
Agent atomically stores the credential in an owner-only config file, then
reconnects after the receiver closes. Approval consumes the pairing code.
Unapproved requests never receive credentials. POST `/api/v1/media/revoke` with
`{"id":"uuid"}` persists revocation before closing its socket.

Subsequent connection greeting:

```json
{"version":1,"type":"authenticate","device_id":"uuid","credential":"64-hex-characters"}
```

Receiver replies `ready` with `preferred_player` (string, may be empty) and
`max_artwork_bytes` (integer). One active socket per credential is permitted;
concurrent duplicate connections are refused. Receiver sends `preferences` with
the same preference/limit fields on settings changes. Credential material is
excluded from configuration export and device list responses.

## Full media state (agent → receiver)

Required fields in EVERY `media_state`:

| Field | Type / bounds |
| --- | --- |
| version, type | integer 1, string `media_state` |
| seq | integer 1 through 2^53−1, strictly increasing per connection |
| session_id, track_id | strings up to 1024 characters; may be empty when stopped |
| title, artist, album | strings up to 1024 characters; empty means unavailable |
| duration_ms, position_ms | integer 0 through 10^12; duration 0 means unknown |
| playback_rate | finite number −16 through 16; normally 1 |
| status | `playing`, `paused`, `stopped` |
| can_control, can_play, can_pause, can_next, can_previous, can_seek | booleans |
| artwork_id | empty string, or SHA-256 of processed JPEG as 64 lowercase hex digits |
| palette | object with primary, secondary, accent; each `#RRGGBB` |

`media_kind` is optional (`music`, `video`, `unknown`). Missing/unknown kinds are
excluded from automatic switching when the user disables non-music media.
The current Linux implementation sends `unknown`, because MPRIS has no reliable
music/video classification. Other metadata continues to work normally.

```json
{"version":1,"type":"media_state","seq":5,"session_id":"org.mpris.MediaPlayer2.spotify","track_id":"/track/abc","title":"Example","artist":"Artist","album":"Album","duration_ms":215000,"position_ms":42000,"playback_rate":1,"status":"playing","can_control":true,"can_play":true,"can_pause":true,"can_next":true,"can_previous":true,"can_seek":true,"artwork_id":"","palette":{"primary":"#263a52","secondary":"#604151","accent":"#be8c62"},"media_kind":"unknown"}
```

Times are always milliseconds. Linux converts signed MPRIS microseconds with
integer division by 1000 and clamps to supported bounds. A monotonic receipt
clock interpolates position on the Pi only while playing. Progress clamps to
known duration and never becomes negative. Pauses, seeks, metadata updates and a
30-second refresh of the currently playing MPRIS session correct interpolation.
There are no one-second position network updates. The UI timer runs only when
visible and playing. Hidden UI is unloaded; decoded artwork is released.

## Artwork (on demand)

If a state's artwork hash differs from its per-host cached hash, receiver sends
`artwork_request` with `artwork_id`. The agent answers with one binary message:

```
64 ASCII lowercase hex bytes of artwork_id | JPEG bytes
```

The receiver verifies hash, current-host association, current state's requested
ID, JPEG format, width/height 1–400, and configured compressed size limit before
retaining it. Dimensions are inspected before decoding. The host sends no artwork
ID when its compressed image exceeds the negotiated limit. Receiver retains one
compressed image per paired host, with at most one displayed decoded image.
Identical hashes are not retransferred, including on reconnect if still cached.
There is no disk artwork cache on the Pi. The host retains the latest processed
image, URI and palette; no growing disk cache or historical bitmap collection.

The agent allows local regular `file://` images and HTTPS images without URL
userinfo. Downloads are capped at 4 MiB and 5 seconds; all redirects are rejected.
PNG/JPEG/optional WebP inputs must be ≤8192 on either axis and ≤16 megapixels;
Qt's decode allocation limit is 64 MiB. Decode, resize to fit 400×400, JPEG encode
at quality 82 and 32×32 color quantization execute in one worker with latest-only
pending work. Output is capped at 160 KiB. Three colors come from 4-level RGB
histogram bins with distinct-color selection; background colors are darkened.
Failed/missing artwork yields a placeholder and default palette.

## Commands and acknowledgments

Receiver → agent:

```json
{"version":1,"type":"command","command_id":"uuid","session_id":"org.mpris.MediaPlayer2.spotify","track_id":"/track/abc","action":"seek","position_ms":60000}
```

Allowed actions ONLY: `play`, `pause`, `toggle`, `previous`, `next`, `seek`.
`position_ms` is an absolute nonnegative integer, used only by seek. The agent
checks session, current track and capability again, then calls the corresponding
provider method. Linux seek calls MPRIS SetPosition with track object path and
milliseconds ×1000. Every command has a unique ID and routes to exactly the
selected authenticated host socket. UI disabled controls cannot issue commands.
Agent → receiver:

```json
{"version":1,"type":"command_ack","command_id":"uuid","ok":true,"error":""}
```

`ok` false includes a bounded diagnostic string. The receiver permits 16 pending
commands with 5s timeout; Linux D-Bus method calls time out at 2s. The agent
suppresses duplicate in-flight IDs and caches up to 64 acknowledgments for the
connection so replayed IDs do not repeat transport actions. No automatic
command retry after reconnect. Unknown actions never reach a shell or process.

## Deterministic arbitration

Linux: available configured full MPRIS service name first; otherwise retain the
currently playing selected session; otherwise choose the lexicographically first
playing service; otherwise retain the existing available inactive session;
otherwise choose the first available service. Discovery order does not determine
selection. Up to 32 concurrent MPRIS sessions are tracked. No oscillation between
inactive sessions or already playing competitors. Explicit preference overrides
this stable policy. Session ownership changes destroy old state and re-query.

Pi hosts use the same sticky policy, ordered by stable device UUID; available
preferred host overrides automatic choice. Each host has independent state,
sequence counter, socket, artwork and monotonic position reference. The visible
host/session is printed on the screen. Dismissal lasts until a new track/host or
playing transition. Pause/stop/disconnect retain the screen for the configured
1–600s (default 30), then clock. Settings remain visible during auto-activation;
media appears after the user exits them. Reopen uses the existing clock navigation.

## Dashboard and release endpoints

Existing GET/PATCH `/api/v1/settings` / `/api/v1/settings/media` expose one shared
settings model with QML. Config exports include media settings but no device
credentials. Legacy three-section imports retain current media preferences.
All settings validate before applying. GET `/api/v1/media/releases` returns only
manifest entries with existing regular non-symlink release artifacts. GET
`/api/v1/media/download/<manifest-file>` streams a previously built artifact in
bounded chunks after existing administrator authentication. No build-on-download.

Normative Linux mapping: [MPRIS Player specification](https://specifications.freedesktop.org/mpris/latest/Player_Interface.html).
