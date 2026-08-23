#include "TestHarness.hpp"
#include "wannaviewer/core/Redaction.hpp"
#include "wannaviewer/core/Url.hpp"

WV_TEST("URL accepts HTTP media and rejects dangerous schemes") {
    const auto media = wannaviewer::Url::Parse("https://cdn.example.test/video/master.m3u8?token=value");
    WV_REQUIRE(media.has_value());
    WV_REQUIRE(media->IsDirectMedia());
    WV_REQUIRE(media->HostIs("example.test"));
    WV_REQUIRE(!wannaviewer::Url::Parse("file:///C:/secret.txt"));
    WV_REQUIRE(!wannaviewer::Url::Parse("javascript:alert(1)"));
    WV_REQUIRE(!wannaviewer::Url::Parse("https://user:password@example.test/video.mp4"));
    WV_REQUIRE(!wannaviewer::Url::Parse("https://example.test/video.mp4\r\nX: injected"));
    WV_REQUIRE(wannaviewer::Url::Parse("https://127.0.0.1/page")->IsPrivateHostLiteral());
    WV_REQUIRE(wannaviewer::Url::Parse("https://192.168.1.2/page")->IsPrivateHostLiteral());
    WV_REQUIRE(!wannaviewer::Url::Parse("https://203.0.113.10/page")->IsPrivateHostLiteral());
}

WV_TEST("logging redacts headers and query tokens") {
    const auto result = wannaviewer::RedactSecrets(
        "Authorization: Bearer-secret\nCookie: SID=secret\nhttps://x.test/a?token=abc&ok=yes");
    WV_REQUIRE(result.find("Bearer-secret") == std::string::npos);
    WV_REQUIRE(result.find("SID=secret") == std::string::npos);
    WV_REQUIRE(result.find("token=abc") == std::string::npos);
    WV_REQUIRE(result.find("ok=yes") != std::string::npos);
}
