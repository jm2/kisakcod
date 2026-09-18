// SPDX-License-Identifier: GPL-3.0-only
//
// Download redirect protocol helpers (HTTP/1.1 client half). See
// dl_http.h for the contract. This translation unit is deliberately
// dependency-free: it compiles into the engine, into the headless service
// composition, and unchanged into the protocol unit tests, so the exact
// bytes a download transport emits and accepts are pinned by tests on any
// host.

#include <qcommon/dl_http.h>

#include <cctype>
#include <cstdio>
#include <cstring>

namespace
{
constexpr std::uint32_t DlBase64EncodedLength(const std::uint32_t rawLength) noexcept
{
    // 3 raw bytes -> 4 characters, padded with '=' to the full quantum.
    return ((rawLength + 2u) / 3u) * 4u;
}

// Case-insensitive ASCII equality; locale-independent by construction.
bool EqualsIgnoreCase(const char *const left,
    const std::uint32_t leftLength,
    const char *const right) noexcept
{
    const std::size_t rightLength = std::strlen(right);
    if (static_cast<std::size_t>(leftLength) != rightLength)
        return false;
    for (std::uint32_t index = 0; index < leftLength; ++index)
    {
        if (std::tolower(static_cast<unsigned char>(left[index]))
            != std::tolower(static_cast<unsigned char>(right[index])))
            return false;
    }
    return true;
}

bool IsSchemeHttp(const char *const scheme, const std::uint32_t length) noexcept
{
    return EqualsIgnoreCase(scheme, length, "http");
}

// Decodes %XX escapes in place semantics without allocation: reads `source`
// (length `length`, not necessarily NUL-terminated within `length`) into
// `out` (capacity `capacity`). Returns false on a truncated/invalid escape
// or an output overflow, leaving `out` holding a terminated prefix either
// way.
bool PercentDecode(const char *const source,
    const std::uint32_t length,
    char *const out,
    const std::uint32_t capacity) noexcept
{
    std::uint32_t written = 0;
    std::uint32_t index = 0;
    while (index < length)
    {
        const char current = source[index];
        if (current != '%')
        {
            if (written + 1 >= capacity)
                return false;
            out[written++] = current;
            ++index;
            continue;
        }
        if (index + 2 >= length)
            return false;
        const char hexHigh = source[index + 1];
        const char hexLow = source[index + 2];
        const auto digit = [](const char value) -> int {
            if (value >= '0' && value <= '9')
                return value - '0';
            if (value >= 'a' && value <= 'f')
                return value - 'a' + 10;
            if (value >= 'A' && value <= 'F')
                return value - 'A' + 10;
            return -1;
        };
        const int high = digit(hexHigh);
        const int low = digit(hexLow);
        if (high < 0 || low < 0)
            return false;
        if (written + 1 >= capacity)
            return false;
        out[written++] = static_cast<char>((high << 4) | low);
        index += 3;
    }
    out[written] = '\0';
    return true;
}

// Copies `length` raw bytes of `source` into `out` (capacity `capacity`)
// and terminates. False on overflow, leaving a terminated prefix.
bool CopyRaw(const char *const source,
    const std::uint32_t length,
    char *const out,
    const std::uint32_t capacity) noexcept
{
    if (length + 1 > capacity)
    {
        out[0] = '\0';
        return false;
    }
    std::memcpy(out, source, length);
    out[length] = '\0';
    return true;
}

// Encodes `raw` (length `rawLength`) as base64 into `out` (capacity
// `capacity`) with standard padding. False on overflow.
bool Base64Encode(const char *const raw,
    const std::uint32_t rawLength,
    char *const out,
    const std::uint32_t capacity) noexcept
{
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const std::uint32_t encodedLength = DlBase64EncodedLength(rawLength);
    if (encodedLength + 1 > capacity)
        return false;

    std::uint32_t written = 0;
    std::uint32_t index = 0;
    while (index + 2 < rawLength)
    {
        const auto first = static_cast<unsigned char>(raw[index]);
        const auto second = static_cast<unsigned char>(raw[index + 1]);
        const auto third = static_cast<unsigned char>(raw[index + 2]);
        const std::uint32_t group = (static_cast<std::uint32_t>(first) << 16)
            | (static_cast<std::uint32_t>(second) << 8)
            | static_cast<std::uint32_t>(third);
        out[written++] = kAlphabet[(group >> 18) & 0x3FU];
        out[written++] = kAlphabet[(group >> 12) & 0x3FU];
        out[written++] = kAlphabet[(group >> 6) & 0x3FU];
        out[written++] = kAlphabet[group & 0x3FU];
        index += 3;
    }
    const std::uint32_t remaining = rawLength - index;
    if (remaining == 1)
    {
        const auto first = static_cast<unsigned char>(raw[index]);
        const std::uint32_t group = static_cast<std::uint32_t>(first) << 16;
        out[written++] = kAlphabet[(group >> 18) & 0x3FU];
        out[written++] = kAlphabet[(group >> 12) & 0x3FU];
        out[written++] = '=';
        out[written++] = '=';
    }
    else if (remaining == 2)
    {
        const auto first = static_cast<unsigned char>(raw[index]);
        const auto second = static_cast<unsigned char>(raw[index + 1]);
        const std::uint32_t group = (static_cast<std::uint32_t>(first) << 16)
            | (static_cast<std::uint32_t>(second) << 8);
        out[written++] = kAlphabet[(group >> 18) & 0x3FU];
        out[written++] = kAlphabet[(group >> 12) & 0x3FU];
        out[written++] = kAlphabet[(group >> 6) & 0x3FU];
        out[written++] = '=';
    }
    out[written] = '\0';
    return true;
}

// Appends to a bounded request buffer. False (with a terminated prefix)
// once the capacity would be exceeded.
bool AppendText(char *const buffer,
    const std::uint32_t capacity,
    std::uint32_t *const length,
    const char *const text,
    const std::uint32_t textLength) noexcept
{
    if (*length + textLength + 1 > capacity)
        return false;
    std::memcpy(buffer + *length, text, textLength);
    *length += textLength;
    buffer[*length] = '\0';
    return true;
}

bool AppendCRLF(char *const buffer,
    const std::uint32_t capacity,
    std::uint32_t *const length) noexcept
{
    return AppendText(buffer, capacity, length, "\r\n", 2);
}

// Case-insensitive scan for `token` inside a header value fragment.
bool ContainsIgnoreCase(const char *const haystack,
    const std::uint32_t length,
    const char *const needle) noexcept
{
    const std::size_t needleLength = std::strlen(needle);
    if (needleLength == 0 || needleLength > length)
        return false;
    for (std::uint32_t start = 0; start + needleLength <= length; ++start)
    {
        if (EqualsIgnoreCase(haystack + start,
                static_cast<std::uint32_t>(needleLength), needle))
            return true;
    }
    return false;
}

// Trims optional whitespace from both ends of a header value fragment and
// copies it into `out` (capacity `capacity`). False on overflow.
bool TrimCopy(const char *const source,
    const std::uint32_t length,
    char *const out,
    const std::uint32_t capacity) noexcept
{
    std::uint32_t begin = 0;
    std::uint32_t end = length;
    const auto isSpace = [](const char value) {
        return value == ' ' || value == '\t';
    };
    while (begin < end && isSpace(source[begin]))
        ++begin;
    while (end > begin && isSpace(source[end - 1]))
        --end;
    return CopyRaw(source + begin, end - begin, out, capacity);
}

// Finds the earliest head terminator (CRLF CRLF, lenient LF LF) inside
// `length` bytes. Returns true with *outEnd = first byte after the
// terminator, false when no terminator is present.
bool FindHeadEnd(const char *const buffer,
    const std::uint32_t length,
    std::uint32_t *const outEnd) noexcept
{
    for (std::uint32_t index = 0; index < length; ++index)
    {
        if (buffer[index] != '\n')
            continue;
        // LF LF: terminator complete at this second newline.
        if (index > 0 && buffer[index - 1] == '\n')
        {
            *outEnd = index + 1;
            return true;
        }
        // CRLF CRLF: the byte before this LF must itself end a CRLF.
        if (index >= 3 && buffer[index - 1] == '\r'
            && buffer[index - 2] == '\n' && buffer[index - 3] == '\r')
        {
            *outEnd = index + 1;
            return true;
        }
    }
    return false;
}

// Parses the status line "HTTP/1.x NNN ..." into statusCode. False when
// the line is not HTTP at all.
bool ParseStatusCode(const char *const head,
    const std::uint32_t length,
    int *const outStatus) noexcept
{
    if (length < 12 || std::memcmp(head, "HTTP/", 5) != 0)
        return false;
    // Find the end of the first line to bound the digits.
    std::uint32_t lineEnd = 0;
    while (lineEnd < length && head[lineEnd] != '\n')
        ++lineEnd;
    if (lineEnd < 9 || head[8] != ' ')
        return false;
    int value = 0;
    for (std::uint32_t index = 9; index < 12; ++index)
    {
        const char digit = head[index];
        if (digit < '0' || digit > '9')
            return false;
        value = value * 10 + (digit - '0');
    }
    *outStatus = value;
    return true;
}
} // namespace

DlUrlStatus KISAK_CDECL Dl_ParseRedirectUrl(
    const char *const url,
    DlRedirectUrl *const out)
{
    if (!out)
        return DlUrlStatus::InvalidArgument;
    // Fail closed: clear the output so no stale caller state can be
    // mistaken for a parse result on any failure path.
    *out = DlRedirectUrl{};
    if (!url || url[0] == '\0')
        return DlUrlStatus::InvalidArgument;

    // Scheme runs to the first "://".
    const char *const schemeEnd = std::strstr(url, "://");
    if (!schemeEnd || schemeEnd == url)
        return DlUrlStatus::UnsupportedScheme;
    const std::uint32_t schemeLength =
        static_cast<std::uint32_t>(schemeEnd - url);
    if (!IsSchemeHttp(url, schemeLength))
        return DlUrlStatus::UnsupportedScheme;

    const char *authority = schemeEnd + 3;
    // The authority ends at the first path, query, or fragment separator.
    std::uint32_t authorityLength = 0;
    while (authority[authorityLength] != '\0'
        && authority[authorityLength] != '/'
        && authority[authorityLength] != '?'
        && authority[authorityLength] != '#')
        ++authorityLength;
    if (authorityLength == 0)
        return DlUrlStatus::InvalidArgument;

    // Split optional userinfo at the LAST '@' (RFC 3986) from the host.
    const char *atSign = nullptr;
    for (std::uint32_t index = 0; index < authorityLength; ++index)
    {
        if (authority[index] == '@')
            atSign = authority + index;
    }

    const char *hostBegin = authority;
    std::uint32_t hostLength = authorityLength;
    const char *userBegin = nullptr;
    std::uint32_t userLength = 0;
    const char *passBegin = nullptr;
    std::uint32_t passLength = 0;
    if (atSign)
    {
        const std::uint32_t infoLength =
            static_cast<std::uint32_t>(atSign - authority);
        hostBegin = atSign + 1;
        hostLength = authorityLength - infoLength - 1;
        if (hostLength == 0)
            return DlUrlStatus::InvalidArgument;

        // Userinfo splits at the first ':'. Both halves are stored
        // percent-decoded, as the retail library did before encoding the
        // Basic credentials.
        const char *colon = nullptr;
        for (std::uint32_t index = 0; index < infoLength; ++index)
        {
            if (authority[index] == ':')
            {
                colon = authority + index;
                break;
            }
        }
        if (colon)
        {
            userBegin = authority;
            userLength = static_cast<std::uint32_t>(colon - authority);
            passBegin = colon + 1;
            passLength = infoLength - userLength - 1;
        }
        else
        {
            userBegin = authority;
            userLength = infoLength;
        }
    }

    // Host carries an optional trailing ':port' (decimal, 1..5 digits).
    std::uint16_t port = 80;
    const char *colon = nullptr;
    for (std::uint32_t index = 0; index < hostLength; ++index)
    {
        if (hostBegin[index] == ':')
        {
            colon = hostBegin + index;
            break;
        }
    }
    if (colon)
    {
        const std::uint32_t digits =
            static_cast<std::uint32_t>(hostLength
                - static_cast<std::uint32_t>(colon - hostBegin) - 1);
        if (digits == 0 || digits > 5)
            return DlUrlStatus::InvalidArgument;
        unsigned value = 0;
        for (std::uint32_t index = 1; index <= digits; ++index)
        {
            const char digit = colon[index];
            if (digit < '0' || digit > '9')
                return DlUrlStatus::InvalidArgument;
            value = value * 10u + static_cast<unsigned>(digit - '0');
        }
        if (value > 65535u)
            return DlUrlStatus::InvalidArgument;
        port = static_cast<std::uint16_t>(value);
        hostLength = static_cast<std::uint32_t>(colon - hostBegin);
    }
    if (hostLength == 0)
        return DlUrlStatus::InvalidArgument;
    if (!CopyRaw(hostBegin, hostLength, out->host, sizeof(out->host)))
        return DlUrlStatus::TooLong;

    if (userBegin)
    {
        if (!PercentDecode(userBegin, userLength, out->user, sizeof(out->user)))
            return DlUrlStatus::TooLong;
        if (passBegin
            && !PercentDecode(passBegin, passLength, out->password,
                sizeof(out->password)))
            return DlUrlStatus::TooLong;
        out->hasBasicAuth = true;
    }

    // Path: from the first '/', or '/' alone when the URL ends at the
    // authority (query/fragment directly after the authority still yields
    // the '/' root). The fragment is not part of an HTTP request target.
    const char *afterAuthority = authority + authorityLength;
    if (*afterAuthority == '/')
    {
        std::uint32_t pathLength = 0;
        while (afterAuthority[pathLength] != '\0'
            && afterAuthority[pathLength] != '#')
            ++pathLength;
        if (!CopyRaw(afterAuthority, pathLength, out->path, sizeof(out->path)))
            return DlUrlStatus::TooLong;
    }
    else
    {
        if (CopyRaw("/", 1, out->path, sizeof(out->path)) != true)
            return DlUrlStatus::TooLong;
    }

    out->port = port;
    return DlUrlStatus::Ok;
}

DlRequestStatus KISAK_CDECL Dl_FormatGetRequest(
    const DlRedirectUrl &url,
    char *const buffer,
    const std::uint32_t capacity,
    std::uint32_t *const outLength)
{
    if (!buffer || !outLength)
        return DlRequestStatus::InvalidArgument;
    buffer[0] = '\0';
    *outLength = 0;
    if (url.host[0] == '\0' || url.path[0] != '/')
        return DlRequestStatus::InvalidArgument;

    std::uint32_t length = 0;
    if (!AppendText(buffer, capacity, &length, "GET ", 4)
        || !AppendText(buffer, capacity, &length, url.path,
            static_cast<std::uint32_t>(std::strlen(url.path)))
        || !AppendText(buffer, capacity, &length, " HTTP/1.1", 9)
        || !AppendCRLF(buffer, capacity, &length)
        || !AppendText(buffer, capacity, &length, "Host: ", 6)
        || !AppendText(buffer, capacity, &length, url.host,
            static_cast<std::uint32_t>(std::strlen(url.host))))
        return DlRequestStatus::TooLong;
    if (url.port != 80)
    {
        char portText[8];
        // 5 digits + NUL can never exceed the buffer.
        const int written = std::snprintf(portText, sizeof(portText), ":%u",
            static_cast<unsigned>(url.port));
        if (written <= 0)
            return DlRequestStatus::InvalidArgument;
        if (!AppendText(buffer, capacity, &length, portText,
                static_cast<std::uint32_t>(written)))
            return DlRequestStatus::TooLong;
    }
    if (!AppendCRLF(buffer, capacity, &length)
        || !AppendText(buffer, capacity, &length,
            "User-Agent: ID_DOWNLOAD/1.0", 27)
        || !AppendCRLF(buffer, capacity, &length)
        || !AppendText(buffer, capacity, &length, "Accept: */*", 11)
        || !AppendCRLF(buffer, capacity, &length))
        return DlRequestStatus::TooLong;

    if (url.hasBasicAuth)
    {
        // user:pass, the HTTP Basic credential pair. The combined form is
        // bounded by the URL field sizes, so a fixed 160-byte scratch can
        // never overflow.
        char credentials[160];
        const std::uint32_t userLength =
            static_cast<std::uint32_t>(std::strlen(url.user));
        const std::uint32_t passwordLength =
            static_cast<std::uint32_t>(std::strlen(url.password));
        if (userLength + 1 + passwordLength + 1 > sizeof(credentials))
            return DlRequestStatus::InvalidArgument;
        std::memcpy(credentials, url.user, userLength);
        credentials[userLength] = ':';
        std::memcpy(credentials + userLength + 1, url.password,
            passwordLength);
        credentials[userLength + 1 + passwordLength] = '\0';

        char encoded[DlBase64EncodedLength(sizeof(credentials)) + 1];
        if (!Base64Encode(credentials, userLength + 1 + passwordLength,
                encoded, sizeof(encoded)))
            return DlRequestStatus::InvalidArgument;
        if (!AppendText(buffer, capacity, &length, "Authorization: Basic ", 21)
            || !AppendText(buffer, capacity, &length, encoded,
                static_cast<std::uint32_t>(std::strlen(encoded)))
            || !AppendCRLF(buffer, capacity, &length))
            return DlRequestStatus::TooLong;
    }

    if (!AppendText(buffer, capacity, &length, "Connection: close", 17)
        || !AppendCRLF(buffer, capacity, &length)
        || !AppendCRLF(buffer, capacity, &length))
        return DlRequestStatus::TooLong;

    *outLength = length;
    return DlRequestStatus::Ok;
}

DlResponseEvent KISAK_CDECL Dl_ParseResponseHead(
    char *const buffer,
    std::uint32_t *const inOutLength,
    DlResponseHead *const out)
{
    if (!buffer || !inOutLength || !out)
        return DlResponseEvent::StatusError;
    *out = DlResponseHead{};
    out->statusCode = 0;

    const std::uint32_t length = *inOutLength;
    std::uint32_t headEnd = 0;
    if (!FindHeadEnd(buffer, length, &headEnd))
    {
        // A head that outruns the accumulator is not speaking HTTP.
        if (length > DlResponseHeadMaxLength)
            return DlResponseEvent::StatusError;
        return DlResponseEvent::NeedMoreData;
    }
    if (headEnd > DlResponseHeadMaxLength)
        return DlResponseEvent::StatusError;

    if (!ParseStatusCode(buffer, headEnd, &out->statusCode))
        return DlResponseEvent::StatusError;

    // Header lines start after the status line and run to the head end.
    std::uint32_t cursor = 0;
    while (cursor < headEnd && buffer[cursor] != '\n')
        ++cursor;
    ++cursor; // skip the status line's newline
    while (cursor < headEnd)
    {
        std::uint32_t lineEnd = cursor;
        while (lineEnd < headEnd && buffer[lineEnd] != '\n')
            ++lineEnd;
        std::uint32_t lineLength = lineEnd - cursor;
        // Strip the CR of a CRLF ending, if present.
        if (lineLength > 0 && buffer[cursor + lineLength - 1] == '\r')
            --lineLength;

        const char *line = buffer + cursor;
        if (lineLength > 0 && line[0] != ' ' && line[0] != '\t')
        {
            const char *colon = nullptr;
            for (std::uint32_t index = 0; index < lineLength; ++index)
            {
                if (line[index] == ':')
                {
                    colon = line + index;
                    break;
                }
            }
            if (colon)
            {
                const std::uint32_t nameLength =
                    static_cast<std::uint32_t>(colon - line);
                const std::uint32_t valueLength = lineLength - nameLength - 1;
                // First occurrence wins for the fields we consume.
                if (EqualsIgnoreCase(line, nameLength, "Content-Length")
                    && !out->hasContentLength)
                {
                    char value[32];
                    if (TrimCopy(colon + 1, valueLength, value, sizeof(value)))
                    {
                        // Decimal digits only; an empty or non-decimal
                        // length is simply absent.
                        bool digitsOnly = value[0] != '\0';
                        std::uint64_t parsed = 0;
                        for (std::uint32_t index = 0; value[index] != '\0';
                             ++index)
                        {
                            if (value[index] < '0' || value[index] > '9')
                            {
                                digitsOnly = false;
                                break;
                            }
                            parsed = parsed * 10u
                                + static_cast<std::uint64_t>(value[index]
                                    - '0');
                        }
                        if (digitsOnly)
                        {
                            out->hasContentLength = true;
                            out->contentLength = parsed;
                        }
                    }
                }
                else if (EqualsIgnoreCase(line, nameLength, "Location")
                    && !out->hasLocation)
                {
                    char value[sizeof(out->location)];
                    if (TrimCopy(colon + 1, valueLength, value, sizeof(value)))
                    {
                        std::memcpy(out->location, value, sizeof(value));
                        out->hasLocation = true;
                    }
                }
                else if (EqualsIgnoreCase(line, nameLength,
                             "Transfer-Encoding")
                    && ContainsIgnoreCase(colon + 1, valueLength, "chunked"))
                {
                    out->chunked = true;
                }
            }
        }

        cursor = lineEnd + 1;
    }

    // Consume the head: keep only body bytes at the front of the buffer.
    const std::uint32_t bodyLength = length - headEnd;
    if (bodyLength != 0)
        std::memmove(buffer, buffer + headEnd, bodyLength);
    *inOutLength = bodyLength;
    return DlResponseEvent::HeadComplete;
}
