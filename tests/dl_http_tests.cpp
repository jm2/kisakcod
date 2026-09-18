// SPDX-License-Identifier: GPL-3.0-only
//
// dl_http_tests.cpp -- pins the pure download-redirect protocol helpers
// (URL parsing, GET request formatting, response head parsing). The binary
// runs the full suite with no arguments; each stage names its checks and a
// failing check reports the stage that owned it. No sockets, no engine
// dependencies: the exact bytes a download transport emits and accepts are
// pinned here on any host.

#include <qcommon/dl_http.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
const char *checkStage = "startup";

bool Check(const bool condition, const char *const stage)
{
    if (!condition)
    {
        checkStage = stage;
        return false;
    }
    return true;
}

bool CheckString(const char *const actual,
    const char *const expected,
    const char *const stage)
{
    return Check(actual != nullptr && std::strcmp(actual, expected) == 0,
        stage);
}

void StageUrlParseBasics()
{
    DlRedirectUrl url{};
    Check(Dl_ParseRedirectUrl("http://cdn.example.com/maps/mp_ship.map",
              &url)
            == DlUrlStatus::Ok,
        "url-basic");
    CheckString(url.host, "cdn.example.com", "url-basic");
    CheckString(url.path, "/maps/mp_ship.map", "url-basic");
    Check(url.port == 80, "url-basic");
    Check(!url.hasBasicAuth, "url-basic");

    Check(Dl_ParseRedirectUrl("http://cdn.example.com", &url)
            == DlUrlStatus::Ok,
        "url-no-path");
    CheckString(url.path, "/", "url-no-path");

    Check(Dl_ParseRedirectUrl("http://cdn.example.com:8080/a b.map?x=1#frag",
              &url)
            == DlUrlStatus::Ok,
        "url-port-query");
    Check(url.port == 8080, "url-port-query");
    CheckString(url.path, "/a b.map?x=1", "url-port-query");
}

void StageUrlParseCredentials()
{
    DlRedirectUrl url{};
    Check(Dl_ParseRedirectUrl("http://user:secret@cdn.example.com/f.map",
              &url)
            == DlUrlStatus::Ok,
        "url-credentials");
    Check(url.hasBasicAuth, "url-credentials");
    CheckString(url.user, "user", "url-credentials");
    CheckString(url.password, "secret", "url-credentials");

    // Percent-escaped credentials decode once, as the retail library did
    // before Basic encoding.
    Check(Dl_ParseRedirectUrl("http://u%40ser:p%3Ass@cdn.example.com/f.map",
              &url)
            == DlUrlStatus::Ok,
        "url-escaped-credentials");
    Check(url.hasBasicAuth, "url-escaped-credentials");
    CheckString(url.user, "u@ser", "url-escaped-credentials");
    CheckString(url.password, "p:ss", "url-escaped-credentials");

    // Userinfo without a password.
    Check(Dl_ParseRedirectUrl("http://token@cdn.example.com/f.map", &url)
            == DlUrlStatus::Ok,
        "url-user-only");
    CheckString(url.user, "token", "url-user-only");
    CheckString(url.password, "", "url-user-only");
    Check(url.hasBasicAuth, "url-user-only");
}

void StageUrlParseFailures()
{
    DlRedirectUrl url{};

    Check(Dl_ParseRedirectUrl(nullptr, &url) == DlUrlStatus::InvalidArgument,
        "url-null");
    Check(Dl_ParseRedirectUrl("", &url) == DlUrlStatus::InvalidArgument,
        "url-empty");
    Check(Dl_ParseRedirectUrl("http://cdn.example.com/f.map", nullptr)
            == DlUrlStatus::InvalidArgument,
        "url-null-out");

    Check(Dl_ParseRedirectUrl("https://cdn.example.com/f.map", &url)
            == DlUrlStatus::UnsupportedScheme,
        "url-https");
    Check(Dl_ParseRedirectUrl("ftp://cdn.example.com/f.map", &url)
            == DlUrlStatus::UnsupportedScheme,
        "url-ftp");
    Check(Dl_ParseRedirectUrl("cdn.example.com/f.map", &url)
            == DlUrlStatus::UnsupportedScheme,
        "url-no-scheme");
    Check(Dl_ParseRedirectUrl("://cdn.example.com", &url)
            == DlUrlStatus::UnsupportedScheme,
        "url-empty-scheme");

    Check(Dl_ParseRedirectUrl("http://", &url)
            == DlUrlStatus::InvalidArgument,
        "url-empty-authority");
    Check(Dl_ParseRedirectUrl("http:///f.map", &url)
            == DlUrlStatus::InvalidArgument,
        "url-hostless");
    Check(Dl_ParseRedirectUrl("http://user@/f.map", &url)
            == DlUrlStatus::InvalidArgument,
        "url-user-empty-host");
    Check(Dl_ParseRedirectUrl("http://host:/f.map", &url)
            == DlUrlStatus::InvalidArgument,
        "url-empty-port");
    Check(Dl_ParseRedirectUrl("http://host:99999999/f.map", &url)
            == DlUrlStatus::InvalidArgument,
        "url-port-too-many-digits");
    Check(Dl_ParseRedirectUrl("http://host:80a/f.map", &url)
            == DlUrlStatus::InvalidArgument,
        "url-port-alpha");
    Check(Dl_ParseRedirectUrl("http://host:70000/f.map", &url)
            == DlUrlStatus::InvalidArgument,
        "url-port-range");
    Check(Dl_ParseRedirectUrl("http://host:%zz/f.map", &url)
            == DlUrlStatus::InvalidArgument,
        "url-escaped-garbage-port");

    // A failure never leaves a partially populated URL behind.
    Check(url.host[0] == '\0' && url.path[0] == '\0', "url-fail-closed");
}

void StageUrlParseBounds()
{
    DlRedirectUrl url{};

    char longHost[300];
    for (int index = 0; index < 290; ++index)
        longHost[index] = 'a';
    longHost[290] = '\0';
    char longHostUrl[340];
    std::snprintf(longHostUrl, sizeof(longHostUrl), "http://%s/f.map",
        longHost);
    Check(Dl_ParseRedirectUrl(longHostUrl, &url) == DlUrlStatus::TooLong,
        "url-host-too-long");

    char longPath[1200];
    longPath[0] = '\0';
    for (int index = 0; index < 200; ++index)
        std::strncat(longPath, "/aaaaaaaaaa", sizeof(longPath)
            - std::strlen(longPath) - 1);
    char longPathUrl[1600];
    std::snprintf(longPathUrl, sizeof(longPathUrl),
        "http://cdn.example.com%s", longPath);
    Check(Dl_ParseRedirectUrl(longPathUrl, &url) == DlUrlStatus::TooLong,
        "url-path-too-long");

    // A component at exactly the bound still parses.
    char maxHost[256];
    for (int index = 0; index < 255; ++index)
        maxHost[index] = 'b';
    maxHost[255] = '\0';
    char maxHostUrl[300];
    std::snprintf(maxHostUrl, sizeof(maxHostUrl), "http://%s/", maxHost);
    Check(Dl_ParseRedirectUrl(maxHostUrl, &url) == DlUrlStatus::Ok,
        "url-host-at-bound");
    Check(std::strcmp(url.host, maxHost) == 0, "url-host-at-bound");
}

void StageRequestFormat()
{
    char request[2048];
    std::uint32_t length = 0;

    DlRedirectUrl url{};
    Check(Dl_ParseRedirectUrl("http://cdn.example.com/maps/mp_ship.map",
              &url)
            == DlUrlStatus::Ok,
        "request-parse");
    Check(Dl_FormatGetRequest(url, request, sizeof(request), &length)
            == DlRequestStatus::Ok,
        "request-simple");
    static const char *const expectedSimple =
        "GET /maps/mp_ship.map HTTP/1.1\r\n"
        "Host: cdn.example.com\r\n"
        "User-Agent: ID_DOWNLOAD/1.0\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n";
    Check(length == std::strlen(expectedSimple), "request-simple");
    Check(std::memcmp(request, expectedSimple, length) == 0,
        "request-simple");

    Check(Dl_ParseRedirectUrl("http://cdn.example.com:8080/f.map", &url)
            == DlUrlStatus::Ok,
        "request-parse");
    Check(Dl_FormatGetRequest(url, request, sizeof(request), &length)
            == DlRequestStatus::Ok,
        "request-port");
    static const char *const expectedPort =
        "GET /f.map HTTP/1.1\r\n"
        "Host: cdn.example.com:8080\r\n"
        "User-Agent: ID_DOWNLOAD/1.0\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n";
    Check(length == std::strlen(expectedPort), "request-port");
    Check(std::memcmp(request, expectedPort, length) == 0, "request-port");

    Check(Dl_ParseRedirectUrl("http://user:pass@cdn.example.com/f.map",
              &url)
            == DlUrlStatus::Ok,
        "request-parse");
    Check(Dl_FormatGetRequest(url, request, sizeof(request), &length)
            == DlRequestStatus::Ok,
        "request-auth");
    static const char *const expectedAuth =
        "GET /f.map HTTP/1.1\r\n"
        "Host: cdn.example.com\r\n"
        "User-Agent: ID_DOWNLOAD/1.0\r\n"
        "Accept: */*\r\n"
        "Authorization: Basic dXNlcjpwYXNz\r\n"
        "Connection: close\r\n"
        "\r\n";
    Check(length == std::strlen(expectedAuth), "request-auth");
    Check(std::memcmp(request, expectedAuth, length) == 0, "request-auth");

    // Contract rejections.
    Check(Dl_FormatGetRequest(url, nullptr, sizeof(request), &length)
            == DlRequestStatus::InvalidArgument,
        "request-null-buffer");
    Check(Dl_FormatGetRequest(url, request, sizeof(request), nullptr)
            == DlRequestStatus::InvalidArgument,
        "request-null-length");
    DlRedirectUrl broken{};
    Check(Dl_FormatGetRequest(broken, request, sizeof(request), &length)
            == DlRequestStatus::InvalidArgument,
        "request-unparsed-url");
    Check(Dl_FormatGetRequest(url, request, 16, &length)
            == DlRequestStatus::TooLong,
        "request-too-long");
}

void StageHeadParseComplete()
{
    static const char *const response =
        "HTTP/1.1 200 OK\r\n"
        "Server: redirector\r\n"
        "Content-Length: 11\r\n"
        "Content-Type: application/octet-stream\r\n"
        "\r\n"
        "hello world";

    char buffer[512];
    std::uint32_t length =
        static_cast<std::uint32_t>(std::strlen(response));
    std::memcpy(buffer, response, length);

    DlResponseHead head{};
    Check(Dl_ParseResponseHead(buffer, &length, &head)
            == DlResponseEvent::HeadComplete,
        "head-complete");
    Check(head.statusCode == 200, "head-complete");
    Check(head.hasContentLength && head.contentLength == 11,
        "head-complete");
    Check(!head.hasLocation && !head.chunked, "head-complete");

    // The head was consumed; only the body bytes remain at the front.
    Check(length == 11, "head-complete");
    Check(std::memcmp(buffer, "hello world", 11) == 0, "head-complete");
}

void StageHeadParseIncremental()
{
    DlResponseHead head{};
    char buffer[512];
    std::uint32_t length = 0;

    // Feed the head in fragments; every incomplete prefix is NeedMoreData
    // and preserves the accumulated bytes.
    static const char *const response =
        "HTTP/1.0 301 Moved\r\nLocation: http://origin/f.map\r\n\r\nBODY";
    const std::uint32_t total =
        static_cast<std::uint32_t>(std::strlen(response));
    for (std::uint32_t feed = 1; feed < total; feed += 7)
    {
        length = feed;
        std::memcpy(buffer, response, feed);
        Check(Dl_ParseResponseHead(buffer, &length, &head)
                == DlResponseEvent::NeedMoreData,
            "head-incremental");
        Check(length == feed, "head-incremental");
    }

    length = total;
    std::memcpy(buffer, response, total);
    Check(Dl_ParseResponseHead(buffer, &length, &head)
            == DlResponseEvent::HeadComplete,
        "head-incremental-final");
    Check(head.statusCode == 301, "head-incremental-final");
    Check(head.hasLocation, "head-incremental-final");
    CheckString(head.location, "http://origin/f.map",
        "head-incremental-final");
    Check(length == 4 && std::memcmp(buffer, "BODY", 4) == 0,
        "head-incremental-final");
}

void StageHeadParseHeaders()
{
    // Case-insensitive names, first occurrence wins, continuation lines
    // skipped, lenient LF LF terminator accepted.
    static const char *const response =
        "HTTP/1.1 404 Not Found\r\n"
        "content-length: 5\r\n"
        "CONTENT-LENGTH: 999\r\n"
        "Location: /other.map\r\n"
        " location continued\r\n"
        "Transfer-Encoding: gzip, chunked\r\n"
        "\n\r\nrest";

    char buffer[512];
    std::uint32_t length = static_cast<std::uint32_t>(std::strlen(response));
    std::memcpy(buffer, response, length);

    DlResponseHead head{};
    Check(Dl_ParseResponseHead(buffer, &length, &head)
            == DlResponseEvent::HeadComplete,
        "head-headers");
    Check(head.statusCode == 404, "head-headers");
    Check(head.hasContentLength && head.contentLength == 5,
        "head-headers-first-wins");
    CheckString(head.location, "/other.map", "head-headers-location");
    Check(head.chunked, "head-headers-chunked");
    // The LF LF at "\n\r\n" is the earliest terminator; the remainder
    // (including the stray CRLF) stays body.
    Check(length == 6 && std::memcmp(buffer, "\r\nrest", 6) == 0,
        "head-headers-body");
}

void StageHeadParseFailures()
{
    DlResponseHead head{};
    char buffer[256];

    // Not HTTP at all.
    static const char *const garbage = "NOT HTTP\r\n\r\nbody";
    std::uint32_t length = static_cast<std::uint32_t>(std::strlen(garbage));
    std::memcpy(buffer, garbage, length);
    Check(Dl_ParseResponseHead(buffer, &length, &head)
            == DlResponseEvent::StatusError,
        "head-garbage");
    Check(head.statusCode == 0, "head-garbage");

    // Non-decimal status.
    static const char *const badStatus = "HTTP/1.1 abc x\r\n\r\n";
    length = static_cast<std::uint32_t>(std::strlen(badStatus));
    std::memcpy(buffer, badStatus, length);
    Check(Dl_ParseResponseHead(buffer, &length, &head)
            == DlResponseEvent::StatusError,
        "head-bad-status");

    // Empty head (immediate terminator).
    static const char *const empty = "\n\n";
    length = static_cast<std::uint32_t>(std::strlen(empty));
    std::memcpy(buffer, empty, length);
    Check(Dl_ParseResponseHead(buffer, &length, &head)
            == DlResponseEvent::StatusError,
        "head-empty");

    // Truncated escape of a status line is still a parse failure, never a
    // hang; and an unterminated head under the bound is NeedMoreData.
    static const char *const partial = "HTTP/1.1 200 OK\r\nContent-Len";
    length = static_cast<std::uint32_t>(std::strlen(partial));
    std::memcpy(buffer, partial, length);
    Check(Dl_ParseResponseHead(buffer, &length, &head)
            == DlResponseEvent::NeedMoreData,
        "head-partial");

    // Head overflow beyond the declared maximum fails closed.
    char oversized[DlResponseHeadMaxLength + 64];
    std::memset(oversized, 'x', sizeof(oversized));
    oversized[0] = '\n';
    length = sizeof(oversized);
    Check(Dl_ParseResponseHead(oversized, &length, &head)
            == DlResponseEvent::StatusError,
        "head-overflow");
}

} // namespace

int main()
{
    StageUrlParseBasics();
    StageUrlParseCredentials();
    StageUrlParseFailures();
    StageUrlParseBounds();
    StageRequestFormat();
    StageHeadParseComplete();
    StageHeadParseIncremental();
    StageHeadParseHeaders();
    StageHeadParseFailures();

    std::printf("dl_http: all stages passed\n");
    return 0;
}
