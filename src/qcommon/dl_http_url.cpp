// SPDX-License-Identifier: GPL-3.0-only
//
// Download URL parsing (one of the pure protocol helpers; see dl_http.h
// for the contract). Split out of dl_http.cpp so every function stays
// small, bounded, and reviewable. No engine dependencies: this unit is
// compiled unchanged into the protocol unit tests on any host.

#include <qcommon/dl_http.h>

#include <qcommon/dl_http_internal.h>

#include <cstdint>
#include <cstring>

namespace
{
using DlHttpInternal::CopyRaw;
using DlHttpInternal::EqualsIgnoreCase;

bool IsSchemeHttp(const char *const scheme,
    const std::uint32_t length) noexcept
{
    return EqualsIgnoreCase(scheme, length, "http");
}

// Value of one hex digit, or -1 when the character is not hexadecimal.
int DlHexDigitValue(const char value) noexcept
{
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}

// Decodes one %XX escape (bounded scan). Returns false unless both hex
// digits are present; *outLength advances by 3 so the caller can report
// where scanning must resume.
bool DecodePercentEscape(const char *const source,
    const std::uint32_t remaining,
    std::uint32_t *const cursor,
    char *const out) noexcept
{
    if (remaining - *cursor < 3)
        return false;
    const int high = DlHexDigitValue(source[*cursor + 1]);
    const int low = DlHexDigitValue(source[*cursor + 2]);
    if (high < 0 || low < 0)
        return false;
    *out = static_cast<char>((high << 4) | low);
    *cursor += 3;
    return true;
}

// Percent-decodes `length` bytes of `source` into `out` (capacity
// `capacity`), reproducing the retail library's HTUnEscape of the
// userinfo before Basic-credential encoding. False when the escapes are
// malformed or the output would overflow; on success *outLength holds
// the decoded length.
bool PercentDecode(const char *const source,
    const std::uint32_t length,
    char *const out,
    const std::uint32_t capacity,
    std::uint32_t *const outLength) noexcept
{
    std::uint32_t written = 0;
    std::uint32_t cursor = 0;
    while (cursor < length)
    {
        char decoded = source[cursor];
        if (decoded == '%')
        {
            if (!DecodePercentEscape(source, length, &cursor, &decoded))
                return false;
        }
        else
        {
            ++cursor;
        }
        if (written + 1 >= capacity)
            return false;
        out[written++] = decoded;
    }
    out[written] = '\0';
    *outLength = written;
    return true;
}

// The authority split into userinfo and host spans (byte ranges within
// the URL, decoded only at storage time).
struct DlUrlPieces
{
    const char *hostBegin;
    std::uint32_t hostLength;
    const char *userBegin; // null when the URL carried no userinfo
    std::uint32_t userLength;
    const char *passBegin; // null when the userinfo carried no ':' part
    std::uint32_t passLength;
};

// Splits the optional userinfo at the LAST '@' before the host (RFC 3986)
// from the authority span. Returns false when the '@' leaves an empty
// host ("http://user@/" parses with an empty authority overall and is
// rejected by the empty-authority guard; an '@' past a nonempty host
// cannot occur because the userinfo precedes the authority's end).
bool SplitUserInfo(const char *const authority,
    const std::uint32_t authorityLength,
    DlUrlPieces *const pieces) noexcept
{
    pieces->hostBegin = authority;
    pieces->hostLength = authorityLength;
    pieces->userBegin = nullptr;
    pieces->userLength = 0;
    pieces->passBegin = nullptr;
    pieces->passLength = 0;

    const char *atSign = nullptr;
    for (std::uint32_t index = 0; index < authorityLength; ++index)
    {
        if (authority[index] == '@')
            atSign = authority + index;
    }
    if (!atSign)
        return true;

    const std::uint32_t infoLength =
        static_cast<std::uint32_t>(atSign - authority);
    pieces->hostBegin = atSign + 1;
    pieces->hostLength = authorityLength - infoLength - 1;
    if (pieces->hostLength == 0)
        return false;

    // The userinfo splits at the first ':'; both halves are stored
    // percent-decoded, matching the retail library's behavior.
    const char *colon = nullptr;
    for (std::uint32_t index = 0; index < infoLength; ++index)
    {
        if (authority[index] == ':')
        {
            colon = authority + index;
            break;
        }
    }
    pieces->userBegin = authority;
    if (colon)
    {
        pieces->userLength = static_cast<std::uint32_t>(colon - authority);
        pieces->passBegin = colon + 1;
        pieces->passLength = infoLength - pieces->userLength - 1;
    }
    else
    {
        pieces->userLength = infoLength;
    }
    return true;
}

// Parses the 1..5 decimal digits following `colon` into a 16-bit port.
// False on a non-digit, an out-of-range digit count, or a value above
// 65535.
bool ParsePortDigits(const char *const colon,
    const std::uint32_t digits,
    std::uint16_t *const outPort) noexcept
{
    if (digits == 0 || digits > 5)
        return false;
    unsigned value = 0;
    for (std::uint32_t index = 1; index <= digits; ++index)
    {
        const char digit = colon[index];
        if (digit < '0' || digit > '9')
            return false;
        value = value * 10u + static_cast<unsigned>(digit - '0');
    }
    if (value > 65535u)
        return false;
    *outPort = static_cast<std::uint16_t>(value);
    return true;
}

// Parses an optional trailing ':port' (decimal, 1..5 digits) and trims it
// from the host span. False on a malformed or out-of-range port or an
// empty host.
bool SplitHostPort(DlUrlPieces *const pieces,
    std::uint16_t *const outPort) noexcept
{
    std::uint16_t port = 80;
    const char *colon = nullptr;
    for (std::uint32_t index = 0; index < pieces->hostLength; ++index)
    {
        if (pieces->hostBegin[index] == ':')
        {
            colon = pieces->hostBegin + index;
            break;
        }
    }
    if (colon)
    {
        const std::uint32_t digits = pieces->hostLength
            - static_cast<std::uint32_t>(colon - pieces->hostBegin) - 1;
        if (!ParsePortDigits(colon, digits, &port))
            return false;
        pieces->hostLength =
            static_cast<std::uint32_t>(colon - pieces->hostBegin);
    }
    if (pieces->hostLength == 0)
        return false;
    *outPort = port;
    return true;
}

// Stores the percent-decoded credential halves and their lengths. A URL
// without userinfo stores empty credentials; the caller still gates the
// Basic header on Dl_UrlHasCredentials.
bool StoreCredentials(const DlUrlPieces &pieces,
    DlRedirectUrl *const out) noexcept
{
    out->userLength = 0;
    out->passwordLength = 0;
    if (!pieces.userBegin)
        return true;

    std::uint32_t storedLength = 0;
    if (!PercentDecode(pieces.userBegin, pieces.userLength, out->user,
            sizeof(out->user), &storedLength))
        return false;
    out->userLength = static_cast<std::uint16_t>(storedLength);
    // Userinfo without a password (token@host) skips the password decode;
    // reset the scratch length and store it only inside the guarded branch
    // so the stale USER decode length can never leak into passwordLength
    // and over-read the empty password buffer in the Basic header.
    storedLength = 0;
    if (pieces.passBegin)
    {
        if (!PercentDecode(pieces.passBegin, pieces.passLength, out->password,
                sizeof(out->password), &storedLength))
            return false;
        out->passwordLength = static_cast<std::uint16_t>(storedLength);
    }
    out->hasBasicAuth = true;
    return true;
}

// Stores the request path: everything from the first '/' up to (not
// including) any fragment, or the '/' root when the URL ends at the
// authority. False on overflow.
bool StorePath(const char *const authority,
    const std::uint32_t authorityLength,
    DlRedirectUrl *const out) noexcept
{
    const char *afterAuthority = authority + authorityLength;
    std::uint32_t pathLength = 0;
    if (*afterAuthority == '/')
    {
        while (afterAuthority[pathLength] != '\0'
            && afterAuthority[pathLength] != '#')
            ++pathLength;
    }
    else
    {
        afterAuthority = "/";
        pathLength = 1;
    }
    std::uint32_t storedLength = 0;
    if (!CopyRaw(afterAuthority, pathLength, out->path, sizeof(out->path),
            &storedLength))
        return false;
    out->pathLength = static_cast<std::uint16_t>(storedLength);
    return true;
}

// Outcome of consuming the scheme and locating the authority span.
enum class DlPrologueStatus : std::uint8_t
{
    Ok,
    Invalid, // null/empty URL or empty authority
    Unsupported, // scheme missing or not http
};

// True for the characters that terminate an authority span: the path,
// query, or fragment separator.
constexpr bool IsAuthorityTerminator(const char character) noexcept
{
    return character == '/' || character == '?' || character == '#';
}

DlPrologueStatus SplitPrologue(const char *const url,
    const char **const outAuthority,
    std::uint32_t *const outAuthorityLength) noexcept
{
    if (!url || url[0] == '\0')
        return DlPrologueStatus::Invalid;
    const char *const schemeEnd = std::strstr(url, "://");
    if (!schemeEnd || schemeEnd == url)
        return DlPrologueStatus::Unsupported;
    if (!IsSchemeHttp(url, static_cast<std::uint32_t>(schemeEnd - url)))
        return DlPrologueStatus::Unsupported;

    const char *authority = schemeEnd + 3;
    // The authority ends at the first path, query, or fragment separator.
    std::uint32_t authorityLength = 0;
    while (authority[authorityLength] != '\0'
        && !IsAuthorityTerminator(authority[authorityLength]))
        ++authorityLength;
    *outAuthority = authority;
    *outAuthorityLength = authorityLength;
    return authorityLength != 0 ? DlPrologueStatus::Ok
                                : DlPrologueStatus::Invalid;
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

    const char *authority = nullptr;
    std::uint32_t authorityLength = 0;
    switch (SplitPrologue(url, &authority, &authorityLength))
    {
        case DlPrologueStatus::Invalid:
            return DlUrlStatus::InvalidArgument;
        case DlPrologueStatus::Unsupported:
            return DlUrlStatus::UnsupportedScheme;
        case DlPrologueStatus::Ok:
            break;
    }

    DlUrlPieces pieces{};
    std::uint16_t port = 80;
    if (!SplitUserInfo(authority, authorityLength, &pieces)
        || !SplitHostPort(&pieces, &port))
        return DlUrlStatus::InvalidArgument;

    std::uint32_t storedLength = 0;
    if (!CopyRaw(pieces.hostBegin, pieces.hostLength, out->host,
            sizeof(out->host), &storedLength))
        return DlUrlStatus::TooLong;
    out->hostLength = static_cast<std::uint16_t>(storedLength);

    if (!StoreCredentials(pieces, out) || !StorePath(authority,
            authorityLength, out))
        return DlUrlStatus::TooLong;

    out->port = port;
    return DlUrlStatus::Ok;
}
