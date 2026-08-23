# Security model

All URLs, HTML, JSON, helper output, filenames, shader presets, and network
headers are untrusted.

Implemented controls include:

- only HTTP(S) remote URLs; remote `file:`, script, and custom schemes are
  rejected;
- URLs with embedded credentials or CR/LF are rejected;
- WinHTTP/Foundation certificate validation, finite timeouts, redirect limits,
  and response-size limits;
- loopback/private address literals in page-resolver URLs are rejected; direct
  user-entered media URLs still go to mpv and can intentionally target a local
  media server;
- no shell invocation for yt-dlp, a literal argv terminator before the URL,
  bounded JSON output, timeout/cancellation, restricted inherited handles, and
  child-tree termination;
- CR/LF/header-name validation before network or mpv headers are set;
- canonical shader paths restricted to the portable shader directory;
- redaction of Cookie, Authorization, proxy authorization, API keys, and token
  query parameters before logging;
- rolling logs and conservative default levels;
- no automatic browser-cookie import and no credential storage.

The project intentionally does not implement DRM/key extraction, paywall,
authentication, CAPTCHA or anti-bot bypass, browser-cookie theft, or arbitrary
provider script execution. Web content is never evaluated by the operating
system shell.

## Remaining hardening work before public distribution

Run fuzzing against URL/config/HTML parsers, enable sanitizers on macOS, perform
long-running leak tests, audit the exact prebuilt mpv dependency closure, sign
Windows binaries, and notarize macOS. A resolver hostname can still resolve or
rebind to a private address because WinHTTP/Foundation own DNS resolution and
redirect connections; do not treat WannaViewer as an SSRF isolation boundary
for hostile URLs on a sensitive network. Report vulnerabilities privately to
the maintainer; do not include credentials or copyrighted media in reports.
