// SPDX-License-Identifier: GPL-3.0-only
//
// dl_http_head_parse_tests.cpp -- pins the response head parsing half of
// the pure download-redirect protocol helpers (status, header lines,
// Content-Length overflow rejection). Linked with dl_http_tests.cpp into
// one binary sharing the check harness in dl_http_test_harness.h; each
// stage names its checks and a failing check reports the stage that
// owned it. No sockets, no engine dependencies: the exact bytes a
// download transport accepts are pinned here on any host.

#include <qcommon/dl_http.h>

#include "dl_http_test_harness.h"

#include <cstdint>
#include <cstring>

void StageHeadParseComplete()
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Server: redirector\r\n"
        "Content-Length: 11\r\n"
        "Content-Type: application/octet-stream\r\n"
        "\r\n"
        "hello world";

    char buffer[512];
    std::uint32_t length =
        static_cast<std::uint32_t>(sizeof(response) - 1);
    CopyBounded(buffer, sizeof(buffer), response, length);

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
    static const char response[] =
        "HTTP/1.0 301 Moved\r\nLocation: http://origin/f.map\r\n\r\nBODY";
    const std::uint32_t total =
        static_cast<std::uint32_t>(sizeof(response) - 1);
    for (std::uint32_t feed = 1; feed < total; feed += 7)
    {
        length = feed;
        CopyBounded(buffer, sizeof(buffer), response, feed);
        Check(Dl_ParseResponseHead(buffer, &length, &head)
                == DlResponseEvent::NeedMoreData,
            "head-incremental");
        Check(length == feed, "head-incremental");
    }

    length = total;
    CopyBounded(buffer, sizeof(buffer), response, total);
    Check(Dl_ParseResponseHead(buffer, &length, &head)
            == DlResponseEvent::HeadComplete,
        "head-incremental-final");
    Check(head.statusCode == 301, "head-incremental-final");
    Check(head.hasLocation, "head-incremental-final");
    CheckString(head.location, "http://origin/f.map",
        "head-incremental-final");
    Check(head.locationLength == 19, "head-incremental-final");
    Check(length == 4 && std::memcmp(buffer, "BODY", 4) == 0,
        "head-incremental-final");
}

void StageHeadParseHeaders()
{
    // Case-insensitive names, first occurrence wins, continuation lines
    // skipped, lenient LF LF terminator accepted.
    static const char response[] =
        "HTTP/1.1 404 Not Found\r\n"
        "content-length: 5\r\n"
        "CONTENT-LENGTH: 999\r\n"
        "Location: /other.map\r\n"
        " location continued\r\n"
        "Transfer-Encoding: gzip, chunked\r\n"
        "\n\r\nrest";

    char buffer[512];
    std::uint32_t length =
        static_cast<std::uint32_t>(sizeof(response) - 1);
    CopyBounded(buffer, sizeof(buffer), response, length);

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
    static const char garbage[] = "NOT HTTP\r\n\r\nbody";
    std::uint32_t length =
        static_cast<std::uint32_t>(sizeof(garbage) - 1);
    CopyBounded(buffer, sizeof(buffer), garbage, length);
    Check(Dl_ParseResponseHead(buffer, &length, &head)
            == DlResponseEvent::StatusError,
        "head-garbage");
    Check(head.statusCode == 0, "head-garbage");

    // Non-decimal status.
    static const char badStatus[] = "HTTP/1.1 abc x\r\n\r\n";
    length = static_cast<std::uint32_t>(sizeof(badStatus) - 1);
    CopyBounded(buffer, sizeof(buffer), badStatus, length);
    Check(Dl_ParseResponseHead(buffer, &length, &head)
            == DlResponseEvent::StatusError,
        "head-bad-status");

    // Empty head (immediate terminator).
    static const char empty[] = "\n\n";
    length = static_cast<std::uint32_t>(sizeof(empty) - 1);
    CopyBounded(buffer, sizeof(buffer), empty, length);
    Check(Dl_ParseResponseHead(buffer, &length, &head)
            == DlResponseEvent::StatusError,
        "head-empty");

    // Truncated escape of a status line is still a parse failure, never a
    // hang; and an unterminated head under the bound is NeedMoreData.
    static const char partial[] = "HTTP/1.1 200 OK\r\nContent-Len";
    length = static_cast<std::uint32_t>(sizeof(partial) - 1);
    CopyBounded(buffer, sizeof(buffer), partial, length);
    Check(Dl_ParseResponseHead(buffer, &length, &head)
            == DlResponseEvent::NeedMoreData,
        "head-partial");

    // Head overflow beyond the declared maximum fails closed.
    char oversized[DlResponseHeadMaxLength + 64];
    std::memset(oversized, 'x', sizeof(oversized));
    oversized[0] = '\n';
    length = static_cast<std::uint32_t>(sizeof(oversized));
    Check(Dl_ParseResponseHead(oversized, &length, &head)
            == DlResponseEvent::StatusError,
        "head-overflow");
}

// Regression: a Content-Length that overflows 64 bits must reject the
// response head. The old parser wrapped 18446744073709551616 (2^64) to a
// length of 0, declared the body complete immediately, and a zero-byte
// file could be renamed into place; the head now fails into the pump's
// StatusError path, which aborts the transfer -- the temp file handle is
// closed by DlAbort and never renamed into place.
void StageHeadParseContentLengthOverflowAtMax()
{
    DlResponseHead head{};
    char buffer[256];

    // 2^64: decimal digits, but one past the representable maximum.
    static const char overflowing[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 18446744073709551616\r\n"
        "\r\n"
        "0123456789";
    std::uint32_t length =
        static_cast<std::uint32_t>(sizeof(overflowing) - 1);
    CopyBounded(buffer, sizeof(buffer), overflowing, length);
    Check(Dl_ParseResponseHead(buffer, &length, &head)
            == DlResponseEvent::StatusError,
        "head-content-length-overflow");
    Check(!head.hasContentLength, "head-content-length-overflow");
}

// 40 decimal digits overflow the parser's scratch buffer before the
// arithmetic guard runs; an all-decimal span still rejects the head
// instead of silently falling back to connection-close framing,
// where a short body followed by the peer's close would pass as
// complete.
void StageHeadParseContentLengthOverflowBeyond()
{
    DlResponseHead head{};
    char buffer[256];

    static const char fortyDigits[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 9999999999999999999999999999999999999999\r\n"
        "\r\n"
        "0123456789";
    std::uint32_t length =
        static_cast<std::uint32_t>(sizeof(fortyDigits) - 1);
    CopyBounded(buffer, sizeof(buffer), fortyDigits, length);
    Check(Dl_ParseResponseHead(buffer, &length, &head)
            == DlResponseEvent::StatusError,
        "head-content-length-overlong");
    Check(!head.hasContentLength, "head-content-length-overlong");
}

// An over-long non-decimal value keeps the lenient absent-length
// behavior: the head completes with no declared length.
void StageHeadParseContentLengthOverlongLenient()
{
    DlResponseHead head{};
    char buffer[256];

    static const char overlongNonDecimal[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: "
        "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqr\r\n"
        "\r\n"
        "body";
    std::uint32_t length =
        static_cast<std::uint32_t>(sizeof(overlongNonDecimal) - 1);
    CopyBounded(buffer, sizeof(buffer), overlongNonDecimal, length);
    Check(Dl_ParseResponseHead(buffer, &length, &head)
            == DlResponseEvent::HeadComplete,
        "head-content-length-overlong-lenient");
    Check(!head.hasContentLength, "head-content-length-overlong-lenient");
}

// 2^64 - 1, the largest representable length, still parses.
void StageHeadParseContentLengthMax()
{
    DlResponseHead head{};
    char buffer[256];

    static const char maxLegal[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 18446744073709551615\r\n"
        "\r\n";
    std::uint32_t length =
        static_cast<std::uint32_t>(sizeof(maxLegal) - 1);
    CopyBounded(buffer, sizeof(buffer), maxLegal, length);
    Check(Dl_ParseResponseHead(buffer, &length, &head)
            == DlResponseEvent::HeadComplete,
        "head-content-length-max");
    Check(head.hasContentLength
            && head.contentLength == 18446744073709551615ULL,
        "head-content-length-max");
}

// First occurrence wins: an overflow in a later, ignored
// Content-Length does not reject a head whose first value parsed.
void StageHeadParseContentLengthFirstWins()
{
    DlResponseHead head{};
    char buffer[256];

    static const char overflowSecond[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "Content-Length: 18446744073709551616\r\n"
        "\r\nhello";
    std::uint32_t length =
        static_cast<std::uint32_t>(sizeof(overflowSecond) - 1);
    CopyBounded(buffer, sizeof(buffer), overflowSecond, length);
    Check(Dl_ParseResponseHead(buffer, &length, &head)
            == DlResponseEvent::HeadComplete,
        "head-content-length-first-wins");
    Check(head.hasContentLength && head.contentLength == 5,
        "head-content-length-first-wins");
}
