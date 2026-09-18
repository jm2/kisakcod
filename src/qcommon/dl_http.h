#pragma once

#include <cstddef>
#include <cstdint>

#include <universal/platform_compat.h>

// Download redirect protocol helpers (HTTP/1.1 client half).
//
// These helpers are pure: fixed buffers, no allocation, no engine
// dependencies, and no sockets -- a transport (dl_main.cpp, over the
// portable Sys_Socket* stream extension) feeds bytes in and pulls requests
// out. Keeping the protocol logic independent of the transport is what makes
// it unit-testable on any host and on both platforms.
//
// Retail compatibility notes: the retail client drove libwww with the agent
// identity "ID_DOWNLOAD"/"1.0"; request framing below reproduces the
// observable subset (GET, Host, agent, Accept, optional Basic credentials
// taken from the URL userinfo, Connection: close). Only the http:// scheme
// is supported: https and ftp (both present in the retail URL parser) have
// no portable TLS/FTP transport here, and a redirect carrying them fails the
// download, which the client reports to the server as "wwwdl fail" and the
// server answers by switching to the in-band UDP download -- the same
// recovery the retail client performs for an unreachable redirector.

// Decomposed download URL. All strings are NUL-terminated raw text; the
// path keeps its percent-escapes and query exactly as received, while the
// userinfo components are percent-decoded once, matching the retail
// library's HTUnEscape before Basic-credential encoding.
struct DlRedirectUrl
{
    char host[256];
    char path[1024]; // always begins with '/'
    char user[64];
    char password[64];
    std::uint16_t port; // host byte order; defaults to 80
    bool hasBasicAuth;  // true when the URL carried a user:pass userinfo
};

enum class DlUrlStatus : std::uint8_t
{
    Ok,
    InvalidArgument, // null/empty input, no authority, malformed pieces
    UnsupportedScheme, // any scheme other than http (https, ftp, ...)
    TooLong, // a component exceeded its fixed buffer
};

// Parses an absolute http:// URL. `out` is written only on Ok: a failure
// never leaves a partially populated URL behind. The userinfo (if any)
// splits at the first ':' and is percent-decoded; decoded credentials that
// exceed their buffers are TooLong.
DlUrlStatus KISAK_CDECL Dl_ParseRedirectUrl(
    const char *url,
    DlRedirectUrl *out);

enum class DlRequestStatus : std::uint8_t
{
    Ok,
    InvalidArgument, // null buffer, null out pointer, unparsed URL
    TooLong, // the formatted request exceeded `capacity`
};

// Formats the GET request for `url` into `buffer`. The request always
// carries Host (with an explicit port only when it differs from 80), the
// retail agent identity, Accept: */*, Connection: close, and an
// Authorization: Basic header exactly when the URL carried credentials.
// On Ok, *outLength holds the request size excluding the NUL terminator.
DlRequestStatus KISAK_CDECL Dl_FormatGetRequest(
    const DlRedirectUrl &url,
    char *buffer,
    std::uint32_t capacity,
    std::uint32_t *outLength);

// Upper bound the caller must give the accumulator before the response
// head is declared malformed instead of merely incomplete. A server that
// streams more than this without finishing the head is not speaking HTTP.
inline constexpr std::uint32_t DlResponseHeadMaxLength = UINT32_C(16384);

enum class DlResponseEvent : std::uint8_t
{
    NeedMoreData, // the head terminator has not arrived yet
    HeadComplete, // *inOutLength advanced past the head; body bytes remain
    StatusError, // malformed head; the download must fail
};

struct DlResponseHead
{
    int statusCode; // as received (200, 301, 404, ...); 0 when unparseable
    bool hasLocation;
    char location[1024];
    bool hasContentLength;
    std::uint64_t contentLength;
    bool chunked; // Transfer-Encoding: chunked -- unsupported by the
                  // transport; the download fails to the in-band fallback
};

// Scans the `*inOutLength` accumulated bytes at `buffer` for the end of
// the response head (CRLF CRLF, with the lenient LF LF form accepted, as
// the retail library did). On HeadComplete the status line is parsed, the
// interesting headers are extracted (case-insensitive names, first
// occurrence wins), the head bytes are removed from the front of the
// buffer with memmove, and *inOutLength is reduced so only body bytes
// remain. StatusError with statusCode 0 reports a status line that is not
// HTTP at all; NeedMoreData is the ordinary incomplete-head outcome and
// turns into StatusError only once the accumulator exceeds
// DlResponseHeadMaxLength.
DlResponseEvent KISAK_CDECL Dl_ParseResponseHead(
    char *buffer,
    std::uint32_t *inOutLength,
    DlResponseHead *out);
