# cpp-httplib

Vendored upstream v0.66.0 (MIT), unmodified `httplib.h` and `LICENSE`:
https://github.com/yhirose/cpp-httplib/releases/tag/v0.66.0

Qt 6.8's QHttpServer lacks pre-allocation request body/header limits (these were
added in Qt 6.11). DeskBuddy uses cpp-httplib's bounded parser, bounded single-worker
queue, timeouts and OpenSSL TLS support instead of maintaining a custom parser.
No runtime backend/framework is introduced. The server is compiled into DeskBuddy.

Security updates should replace the pinned header/license together and rerun the
HTTP integration tests. Do not enable httplib's optional zlib, brotli, websocket,
static-directory mounts, or default unbounded/growing thread pool.
