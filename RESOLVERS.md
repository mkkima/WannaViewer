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
the modern `__staticRouterHydrationData` payload. Modern catalog pages hydrate
with `need_videos=false`, so the adapter follows the page's public
`api.yani.tv/anime/<slug>?need_videos=true` contract and keeps the CVH variants
plus the public Kodik and Alloha embeds that the provider pipeline can actually
turn into media. For the legacy page it reads
semantic `data-params` provider metadata, calls the public same-origin controller
with a Referer, and normalizes returned provider embed URLs as the next resolver
stage. It does not depend solely on CSS class names.

## AnimeGo

AnimeGo pages publish a lazy player URL such as `/player/<anime-id>`. The
adapter requests its JSON form with the same `X-Requested-With` header used by
the site's controller, returns the episode list, and fetches
`/player/videos/<episode-id>` only after an episode is chosen. This avoids dozens
of speculative requests. Provider choices are grouped by voice; CVH, AniBoom,
and Kodik are exposed when present.

## CVH, AniBoom, Kodik, and Alloha

CVH iframe configuration leads to the provider's public playlist API. The
adapter filters the requested season, episode, and voice, then obtains a fresh,
short-lived HLS master URL immediately before playback. The iframe `Origin` is
used only for the CVH API request and is deliberately removed before libmpv
fetches media: the OK media CDN rejects otherwise-valid signed segments when
that unrelated origin is forwarded. AniBoom embeds publish an escaped HLS/DASH
URL in their player metadata; the adapter prefers HLS.

Kodik and Alloha use normal browser JavaScript to create their public media
requests. On Windows, selecting either provider starts an allowlisted hidden
WebView2 session with the original AnimeGo/YummyAnime page as Referer. The
provider runs its own public player code in the WebView2 sandbox; WannaViewer
observes the resulting HLS/DASH/MP4 request and passes its URL and required
request headers to libmpv. CORS preflight requests and Plyr placeholder media
are ignored; for Alloha HLS, the first successful media-segment request supplies
the provider-defined segment headers when WebView exposes it. If a captured
short-lived source is rejected before playback starts, the UI performs one fresh
provider resolution and never loops indefinitely. It does not decode or emulate either provider's
private/obfuscated request protocol. Pop-ups and browser permissions are denied,
and cleanup of the isolated session profile is attempted after resolution.

Unrecognized provider pages may still fall through to yt-dlp/generic resolution. No adapter
bypasses DRM, login, subscription, CAPTCHA, anti-bot challenges, encryption, or
entitlement. A normal public manifest is playable; otherwise the result is
`Provider unsupported: protected/DRM stream` or a clear unsupported error.

The WebView2 provider fallback requires the Microsoft Edge WebView2 Evergreen
Runtime. Windows 11 normally supplies it; if it is missing, only Kodik/Alloha
resolution fails with an explicit message and local/CVH/AniBoom playback remains
available.

Fixture tests live under `tests/fixtures`. They use sanitized synthetic domains
and require no network. `scripts/test-resolvers-live.ps1` opts into live checks;
live site changes never make the default unit suite flaky.

## Adding an adapter

Implement `IPageResolver`, give it a stable ID and narrow `CanHandle`, bound all
network input, parse structured data before HTML selectors, and add a sanitized
fixture. Secrets belong only in `StreamVariant.headers`; logger redaction is a
defense in depth, not permission to log those values.
