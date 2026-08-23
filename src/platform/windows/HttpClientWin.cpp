#include "wannaviewer/network/HttpClient.hpp"

#include <atomic>
#include <stdexcept>
#include <vector>

#include <windows.h>
#include <winhttp.h>

namespace wannaviewer {
namespace {

class InternetHandle final {
public:
    InternetHandle() = default;
    explicit InternetHandle(HINTERNET handle) : handle_(handle) {}
    ~InternetHandle() { if (handle_) WinHttpCloseHandle(handle_); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    [[nodiscard]] HINTERNET Get() const noexcept { return handle_; }
private:
    HINTERNET handle_{nullptr};
};

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) throw std::runtime_error("Invalid UTF-8 URL or header");
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    (void)MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}

std::string WideToUtf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    (void)WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}

std::wstring QueryStringHeader(HINTERNET request, DWORD query) {
    DWORD bytes = 0;
    (void)WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &bytes, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes < sizeof(wchar_t)) return {};
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, value.data(), &bytes, WINHTTP_NO_HEADER_INDEX)) return {};
    value.resize(bytes / sizeof(wchar_t));
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    return value;
}

} // namespace

HttpResponse HttpClient::Get(const HttpRequest& request, std::stop_token stopToken) const {
    if (!request.url.IsHttp()) throw std::invalid_argument("Only HTTP(S) URLs are supported");
    if (request.url.IsPrivateHostLiteral())
        throw std::invalid_argument("Private or loopback host literals are not accepted by page resolvers");

    const auto wideUrl = Utf8ToWide(request.url.Value());
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &components))
        throw std::runtime_error("WinHttpCrackUrl failed");

    InternetHandle session(WinHttpOpen(L"WannaViewer/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.Get()) throw std::runtime_error("WinHttpOpen failed");
    const int connectTimeout = static_cast<int>(request.connectTimeout.count());
    const int receiveTimeout = static_cast<int>(request.receiveTimeout.count());
    (void)WinHttpSetTimeouts(session.Get(), 5'000, connectTimeout, connectTimeout, receiveTimeout);

    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    InternetHandle connection(WinHttpConnect(session.Get(), host.c_str(), components.nPort, 0));
    if (!connection.Get()) throw std::runtime_error("WinHttpConnect failed");
    std::wstring target(components.lpszUrlPath, components.dwUrlPathLength);
    target.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    if (target.empty()) target = L"/";
    const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    InternetHandle httpRequest(WinHttpOpenRequest(connection.Get(), L"GET", target.c_str(), nullptr,
                                                  WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!httpRequest.Get()) throw std::runtime_error("WinHttpOpenRequest failed");
    DWORD redirects = request.maximumRedirects;
    (void)WinHttpSetOption(httpRequest.Get(), WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &redirects, sizeof(redirects));
    if (request.url.IsHttps()) {
        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
        (void)WinHttpSetOption(httpRequest.Get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));
    }

    for (const auto& [name, value] : request.headers) {
        if (name.find_first_of("\r\n:") != std::string::npos || value.find_first_of("\r\n") != std::string::npos)
            throw std::invalid_argument("Unsafe HTTP header");
        const auto header = Utf8ToWide(name + ": " + value);
        if (!WinHttpAddRequestHeaders(httpRequest.Get(), header.c_str(), static_cast<DWORD>(header.size()), WINHTTP_ADDREQ_FLAG_ADD))
            throw std::runtime_error("WinHttpAddRequestHeaders failed");
    }
    if (stopToken.stop_requested()) throw std::runtime_error("HTTP request cancelled");
    if (!WinHttpSendRequest(httpRequest.Get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(httpRequest.Get(), nullptr))
        throw std::runtime_error("HTTP request failed");
    if (stopToken.stop_requested()) throw std::runtime_error("HTTP request cancelled");

    DWORD status = 0;
    DWORD statusBytes = sizeof(status);
    if (!WinHttpQueryHeaders(httpRequest.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusBytes, WINHTTP_NO_HEADER_INDEX))
        throw std::runtime_error("Unable to read HTTP status");
    HttpResponse response;
    response.status = status;
    response.contentType = WideToUtf8(QueryStringHeader(httpRequest.Get(), WINHTTP_QUERY_CONTENT_TYPE));
    response.finalUrl = WideToUtf8(QueryStringHeader(httpRequest.Get(), WINHTTP_QUERY_LOCATION));
    if (response.finalUrl.empty()) response.finalUrl = request.url.Value();

    DWORD available = 0;
    do {
        if (stopToken.stop_requested()) throw std::runtime_error("HTTP request cancelled");
        if (!WinHttpQueryDataAvailable(httpRequest.Get(), &available)) throw std::runtime_error("HTTP response read failed");
        if (available == 0) break;
        if (response.body.size() + available > request.maximumResponseBytes) throw std::runtime_error("HTTP response exceeds the size limit");
        const auto offset = response.body.size();
        response.body.resize(offset + available);
        DWORD received = 0;
        if (!WinHttpReadData(httpRequest.Get(), response.body.data() + offset, available, &received))
            throw std::runtime_error("HTTP response read failed");
        response.body.resize(offset + received);
    } while (available > 0);
    return response;
}

} // namespace wannaviewer
