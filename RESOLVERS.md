# URL resolvers

Resolution order is deliberately separated from playback:

1. a direct media suffix (`mp4`, `m3u8`, `mpd`, and other supported types) is
   passed directly to mpv;
2. known page adapters parse stable structured metadata;
3. yt-dlp is invoked once as a child process and parsed as JSON;
4. a bounded generic static-HTML resolver looks for explicit public media URLs;
5. otherwise the URL is unsupported.

## YummyAnime

The adapter supports both supplied domain families. It prefers JSON/JSON-LD and
the modern `__staticRouterHydrationData` payload. For the legacy page it reads
semantic `data-params` provider metadata, calls the public same-origin controller
with a Referer, and normalizes returned provider embed URLs as the next resolver
stage. It does not depend solely on CSS class names.

As observed in August 2026, the legacy sample exposes Alloha and Kodik public
embed URLs through its controller. The modern sample hydrates title and episode
counts but reports no partner video for that licensed item. The application
therefore reports metadata without pretending a protected source exists.

Provider pages are handed back through yt-dlp/generic resolution. No adapter
bypasses DRM, login, subscription, CAPTCHA, anti-bot challenges, encryption, or
entitlement. A normal public manifest is playable; otherwise the result is
`Provider unsupported: protected/DRM stream` or a clear unsupported error.

Fixture tests live under `tests/fixtures`. They use sanitized synthetic domains
and require no network. `scripts/test-resolvers-live.ps1` opts into live checks;
live site changes never make the default unit suite flaky.

## Adding an adapter

Implement `IPageResolver`, give it a stable ID and narrow `CanHandle`, bound all
network input, parse structured data before HTML selectors, and add a sanitized
fixture. Secrets belong only in `StreamVariant.headers`; logger redaction is a
defense in depth, not permission to log those values.
