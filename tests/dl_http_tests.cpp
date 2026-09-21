// SPDX-License-Identifier: GPL-3.0-only
//
// dl_http_tests.cpp -- pins the pure download-redirect protocol helpers
// for URL parsing and GET request formatting; response head parsing
// lives in dl_http_head_parse_tests.cpp. Both translation units form one
// binary sharing the check harness in dl_http_test_harness.h, and the
// binary runs the full suite with no arguments; each stage names its
// checks and a failing check reports the stage that owned it. No
// sockets, no engine dependencies: the exact bytes a download transport
// emits and accepts are pinned here on any host.

#include <qcommon/dl_http.h>

#include "dl_http_test_harness.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
void StageUrlParseBasics()
{
    DlRedirectUrl url{};
    Check(Dl_ParseRedirectUrl("http://cdn.example.com/maps/mp_ship.map",
              &url)
            == DlUrlStatus::Ok,
        "url-basic");
    CheckString(url.host, "cdn.example.com", "url-basic");
    CheckString(url.path, "/maps/mp_ship.map", "url-basic");
    Check(url.hostLength == 15, "url-basic");
    Check(url.pathLength == 17, "url-basic");
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

    // Regression: a query-only reference keeps its query. The path store
    // used to replace every reference without a leading '/' with the
    // bare root, silently requesting the wrong resource and dropping
    // authorization parameters carried in the query.
    Check(Dl_ParseRedirectUrl("http://cdn.example.com?token=abc", &url)
            == DlUrlStatus::Ok,
        "url-query-only");
    CheckString(url.path, "/?token=abc", "url-query-only");
    Check(url.pathLength == 11, "url-query-only");
    CheckString(url.host, "cdn.example.com", "url-query-only");

    // A fragment after a query-only reference is not part of the path.
    Check(Dl_ParseRedirectUrl("http://cdn.example.com?a=1#frag", &url)
            == DlUrlStatus::Ok,
        "url-query-only-fragment");
    CheckString(url.path, "/?a=1", "url-query-only-fragment");
    Check(url.pathLength == 5, "url-query-only-fragment");

    // A lone '?' stores the root plus the empty query span, exactly as
    // the reference expressed it.
    Check(Dl_ParseRedirectUrl("http://cdn.example.com?#frag", &url)
            == DlUrlStatus::Ok,
        "url-query-only-empty");
    CheckString(url.path, "/?", "url-query-only-empty");
    Check(url.pathLength == 2, "url-query-only-empty");
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
    Check(url.userLength == 5, "url-user-only");
    // Regression: the password decode is skipped for token@host URLs, so
    // the stale USER decode length must not leak into passwordLength (it
    // over-read the empty password buffer into the Basic auth header).
    Check(url.passwordLength == 0, "url-user-only");
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
    // Bounded fill equivalent to the previous strncat loop: append the
    // 11-character fragment until one more would not fit, leaving room
    // for the terminator. sizeof-1 bound keeps the copy explicit.
    std::uint32_t longPathLength = 0;
    while (longPathLength + 11 < sizeof(longPath))
    {
        CopyBounded(longPath + longPathLength, sizeof(longPath)
                - longPathLength - 1,
            "/aaaaaaaaaa", 11);
        longPathLength += 11;
    }
    longPath[longPathLength] = '\0';
    char longPathUrl[1600];
    std::snprintf(longPathUrl, sizeof(longPathUrl),
        "http://cdn.example.com%s", longPath);
    Check(Dl_ParseRedirectUrl(longPathUrl, &url) == DlUrlStatus::TooLong,
        "url-path-too-long");

    // A query-only reference whose span overflows the path buffer is
    // rejected too, and fails closed (no partial path survives).
    char longQuery[1200];
    longQuery[0] = '?';
    for (int index = 1; index < 1100; ++index)
        longQuery[index] = 'q';
    longQuery[1100] = '\0';
    char longQueryUrl[1400];
    std::snprintf(longQueryUrl, sizeof(longQueryUrl),
        "http://cdn.example.com%s", longQuery);
    Check(Dl_ParseRedirectUrl(longQueryUrl, &url) == DlUrlStatus::TooLong,
        "url-query-too-long");
    Check(url.path[0] == '\0', "url-query-too-long");

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
    static const char expectedSimple[] =
        "GET /maps/mp_ship.map HTTP/1.1\r\n"
        "Host: cdn.example.com\r\n"
        "User-Agent: ID_DOWNLOAD/1.0\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n";
    Check(length == sizeof(expectedSimple) - 1, "request-simple");
    Check(std::memcmp(request, expectedSimple, length) == 0,
        "request-simple");

    Check(Dl_ParseRedirectUrl("http://cdn.example.com:8080/f.map", &url)
            == DlUrlStatus::Ok,
        "request-parse");
    Check(Dl_FormatGetRequest(url, request, sizeof(request), &length)
            == DlRequestStatus::Ok,
        "request-port");
    static const char expectedPort[] =
        "GET /f.map HTTP/1.1\r\n"
        "Host: cdn.example.com:8080\r\n"
        "User-Agent: ID_DOWNLOAD/1.0\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n";
    Check(length == sizeof(expectedPort) - 1, "request-port");
    Check(std::memcmp(request, expectedPort, length) == 0, "request-port");
}

// Regression: a query-only reference must survive into the request
// line as "GET /?token=abc" -- the dropped-query bug requested "/"
// and lost the authorization token.
void StageRequestQueryOnlyFormat()
{
    char request[2048];
    std::uint32_t length = 0;

    DlRedirectUrl url{};
    Check(Dl_ParseRedirectUrl("http://cdn.example.com?token=abc", &url)
            == DlUrlStatus::Ok,
        "request-parse");
    Check(Dl_FormatGetRequest(url, request, sizeof(request), &length)
            == DlRequestStatus::Ok,
        "request-query-only");
    static const char expectedQueryOnly[] =
        "GET /?token=abc HTTP/1.1\r\n"
        "Host: cdn.example.com\r\n"
        "User-Agent: ID_DOWNLOAD/1.0\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n";
    Check(length == sizeof(expectedQueryOnly) - 1, "request-query-only");
    Check(std::memcmp(request, expectedQueryOnly, length) == 0,
        "request-query-only");
}

void StageRequestAuthFormat()
{
    char request[2048];
    std::uint32_t length = 0;

    DlRedirectUrl url{};
    Check(Dl_ParseRedirectUrl("http://user:pass@cdn.example.com/f.map",
              &url)
            == DlUrlStatus::Ok,
        "request-parse");
    Check(Dl_FormatGetRequest(url, request, sizeof(request), &length)
            == DlRequestStatus::Ok,
        "request-auth");
    static const char expectedAuth[] =
        "GET /f.map HTTP/1.1\r\n"
        "Host: cdn.example.com\r\n"
        "User-Agent: ID_DOWNLOAD/1.0\r\n"
        "Accept: */*\r\n"
        "Authorization: Basic dXNlcjpwYXNz\r\n"
        "Connection: close\r\n"
        "\r\n";
    Check(length == sizeof(expectedAuth) - 1, "request-auth");
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

// Regression: userinfo without a password encodes the Basic pair as
// base64("token:") with nothing after it -- the skipped password decode
// must not leak the stale USER length into the header bytes. The stale
// state lives in the reused DlRedirectUrl, so the user:pass parse below
// recreates the carryover precondition before the token-only parse.
void StageRequestAuthUserOnlyFormat()
{
    char request[2048];
    std::uint32_t length = 0;

    DlRedirectUrl url{};
    Check(Dl_ParseRedirectUrl("http://user:pass@cdn.example.com/f.map",
              &url)
            == DlUrlStatus::Ok,
        "request-auth-user-only-setup");
    Check(Dl_ParseRedirectUrl("http://token@cdn.example.com/f.map", &url)
            == DlUrlStatus::Ok,
        "request-auth-user-only");
    Check(Dl_FormatGetRequest(url, request, sizeof(request), &length)
            == DlRequestStatus::Ok,
        "request-auth-user-only");
    static const char expectedAuthUserOnly[] =
        "GET /f.map HTTP/1.1\r\n"
        "Host: cdn.example.com\r\n"
        "User-Agent: ID_DOWNLOAD/1.0\r\n"
        "Accept: */*\r\n"
        "Authorization: Basic dG9rZW46\r\n"
        "Connection: close\r\n"
        "\r\n";
    Check(length == sizeof(expectedAuthUserOnly) - 1,
        "request-auth-user-only");
    Check(std::memcmp(request, expectedAuthUserOnly, length) == 0,
        "request-auth-user-only");
}
} // namespace

int main()
{
    StageUrlParseBasics();
    StageUrlParseCredentials();
    StageUrlParseFailures();
    StageUrlParseBounds();
    StageRequestFormat();
    StageRequestQueryOnlyFormat();
    StageRequestAuthFormat();
    StageRequestAuthUserOnlyFormat();
    StageHeadParseComplete();
    StageHeadParseIncremental();
    StageHeadParseHeaders();
    StageHeadParseFailures();
    StageHeadParseContentLengthOverflowAtMax();
    StageHeadParseContentLengthOverflowBeyond();
    StageHeadParseContentLengthOverlongLenient();
    StageHeadParseContentLengthMax();
    StageHeadParseContentLengthFirstWins();
    StageResponseStatusAcceptance();

    if (checkFailed)
    {
        std::printf("dl_http: stage '%s' failed\n", checkStage);
        return 1;
    }
    std::printf("dl_http: all stages passed\n");
    return 0;
}
