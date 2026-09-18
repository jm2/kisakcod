// SPDX-License-Identifier: GPL-3.0-only
//
// Download request formatting (the HTTP/1.1 client half's GET framing
// over a decomposed URL). See dl_http.h for the contract; URL parsing
// lives in dl_http_url.cpp and response head parsing in
// dl_http_parse.cpp. This translation unit is deliberately
// dependency-free: it compiles into the engine, into the headless service
// composition, and unchanged into the protocol unit tests, so the exact
// bytes a download transport emits are pinned by tests on any host.

#include <qcommon/dl_http.h>

#include <qcommon/dl_http_internal.h>

#include <cstdint>
#include <cstdio>

namespace
{
// RFC 4648 standard base64 alphabet, shared by the quantum emitter and
// the encoder below.
constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

constexpr std::uint32_t DlBase64EncodedLength(const std::uint32_t rawLength) noexcept
{
    // 3 raw bytes -> 4 characters, padded with '=' to the full quantum.
    return ((rawLength + 2u) / 3u) * 4u;
}

// Emits the top `sextets` (1..4) 6-bit groups of a packed 3-byte quantum
// as alphabet characters, most significant first. The caller pads the
// unused tail of the quantum with '='.
void EmitBase64Sextets(const std::uint32_t group,
    const std::uint32_t sextets,
    char *const out,
    std::uint32_t *const written) noexcept
{
    std::uint32_t shift = 18;
    for (std::uint32_t emitted = 0; emitted < sextets; ++emitted)
    {
        out[(*written)++] = kBase64Alphabet[(group >> shift) & 0x3FU];
        shift -= 6;
    }
}

// Encodes `raw` (length `rawLength`) as base64 into `out` (capacity
// `capacity`) with standard padding. False on overflow; on success
// *outEncodedLength holds the encoded length (terminator excluded).
bool Base64Encode(const char *const raw,
    const std::uint32_t rawLength,
    char *const out,
    const std::uint32_t capacity,
    std::uint32_t *const outEncodedLength) noexcept
{
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
        EmitBase64Sextets(group, 4, out, &written);
        index += 3;
    }
    const std::uint32_t remaining = rawLength - index;
    if (remaining == 1)
    {
        const auto first = static_cast<unsigned char>(raw[index]);
        const std::uint32_t group = static_cast<std::uint32_t>(first) << 16;
        EmitBase64Sextets(group, 2, out, &written);
        out[written++] = '=';
        out[written++] = '=';
    }
    else if (remaining == 2)
    {
        const auto first = static_cast<unsigned char>(raw[index]);
        const auto second = static_cast<unsigned char>(raw[index + 1]);
        const std::uint32_t group = (static_cast<std::uint32_t>(first) << 16)
            | (static_cast<std::uint32_t>(second) << 8);
        EmitBase64Sextets(group, 3, out, &written);
        out[written++] = '=';
    }
    out[written] = '\0';
    *outEncodedLength = written;
    return true;
}

// Appends to a bounded request buffer. False (with a terminated prefix)
// once the capacity would be exceeded. The copy is an explicit bounded
// loop: the append guard precedes every written byte.
bool AppendText(char *const buffer,
    const std::uint32_t capacity,
    std::uint32_t *const length,
    const char *const text,
    const std::uint32_t textLength) noexcept
{
    if (*length + textLength + 1 > capacity)
        return false;
    for (std::uint32_t index = 0; index < textLength; ++index)
        buffer[*length + index] = text[index];
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

// Appends "Authorization: Basic <base64(user:pass)>" for the URL's
// credential pair. Returns InvalidArgument for an over-long pair or an
// encoding failure, TooLong on an output overflow, Ok on success.
DlRequestStatus AppendBasicAuthHeader(const DlRedirectUrl &url,
    char *const buffer,
    const std::uint32_t capacity,
    std::uint32_t *const length) noexcept
{
    // user:pass, the HTTP Basic credential pair. The combined form is
    // bounded by the URL field sizes, so a fixed 160-byte scratch can
    // never overflow; the bounded appends still guard every byte.
    char credentials[160];
    std::uint32_t credentialsLength = 0;
    const std::uint32_t userLength = Dl_UrlUserLength(url);
    const std::uint32_t passwordLength = Dl_UrlPasswordLength(url);
    if (userLength + 1 + passwordLength + 1 > sizeof(credentials))
        return DlRequestStatus::InvalidArgument;
    if (!AppendText(credentials, sizeof(credentials), &credentialsLength,
            Dl_UrlUser(url), userLength)
        || !AppendText(credentials, sizeof(credentials), &credentialsLength,
            ":", 1)
        || !AppendText(credentials, sizeof(credentials), &credentialsLength,
            Dl_UrlPassword(url), passwordLength))
        return DlRequestStatus::InvalidArgument;

    char encoded[DlBase64EncodedLength(sizeof(credentials)) + 1];
    std::uint32_t encodedLength = 0;
    if (!Base64Encode(credentials, credentialsLength, encoded,
            sizeof(encoded), &encodedLength))
        return DlRequestStatus::InvalidArgument;
    if (!AppendText(buffer, capacity, length, "Authorization: Basic ", 21)
        || !AppendText(buffer, capacity, length, encoded, encodedLength)
        || !AppendCRLF(buffer, capacity, length))
        return DlRequestStatus::TooLong;
    return DlRequestStatus::Ok;
}

// Appends the request line "GET <path> HTTP/1.1".
DlRequestStatus AppendRequestLine(const DlRedirectUrl &url,
    char *const buffer,
    const std::uint32_t capacity,
    std::uint32_t *const length) noexcept
{
    if (!AppendText(buffer, capacity, length, "GET ", 4)
        || !AppendText(buffer, capacity, length, Dl_UrlPath(url),
            Dl_UrlPathLength(url))
        || !AppendText(buffer, capacity, length, " HTTP/1.1", 9)
        || !AppendCRLF(buffer, capacity, length))
        return DlRequestStatus::TooLong;
    return DlRequestStatus::Ok;
}

// Appends the Host header, carrying an explicit port only when it differs
// from the default 80.
DlRequestStatus AppendHostHeader(const DlRedirectUrl &url,
    char *const buffer,
    const std::uint32_t capacity,
    std::uint32_t *const length) noexcept
{
    if (!AppendText(buffer, capacity, length, "Host: ", 6)
        || !AppendText(buffer, capacity, length, Dl_UrlHost(url),
            Dl_UrlHostLength(url)))
        return DlRequestStatus::TooLong;
    if (!Dl_UrlIsDefaultPort(url))
    {
        char portText[8];
        // 5 digits + NUL can never exceed the buffer.
        const int written = std::snprintf(portText, sizeof(portText), ":%u",
            static_cast<unsigned>(Dl_UrlPort(url)));
        if (written <= 0)
            return DlRequestStatus::InvalidArgument;
        if (!AppendText(buffer, capacity, length, portText,
                static_cast<std::uint32_t>(written)))
            return DlRequestStatus::TooLong;
    }
    if (!AppendCRLF(buffer, capacity, length))
        return DlRequestStatus::TooLong;
    return DlRequestStatus::Ok;
}

// Appends the fixed identity headers shared by every download request.
DlRequestStatus AppendCommonHeaders(char *const buffer,
    const std::uint32_t capacity,
    std::uint32_t *const length) noexcept
{
    if (!AppendText(buffer, capacity, length,
            "User-Agent: ID_DOWNLOAD/1.0", 27)
        || !AppendCRLF(buffer, capacity, length)
        || !AppendText(buffer, capacity, length, "Accept: */*", 11)
        || !AppendCRLF(buffer, capacity, length))
        return DlRequestStatus::TooLong;
    return DlRequestStatus::Ok;
}

// Appends the Connection: close header and the terminating blank line.
DlRequestStatus AppendConnectionClose(char *const buffer,
    const std::uint32_t capacity,
    std::uint32_t *const length) noexcept
{
    if (!AppendText(buffer, capacity, length, "Connection: close", 17)
        || !AppendCRLF(buffer, capacity, length)
        || !AppendCRLF(buffer, capacity, length))
        return DlRequestStatus::TooLong;
    return DlRequestStatus::Ok;
}

// A formatted request needs a parsed host and a path-absolute target.
bool RequestTargetValid(const DlRedirectUrl &url) noexcept
{
    return Dl_UrlHost(url)[0] != '\0' && Dl_UrlPath(url)[0] == '/';
}
} // namespace

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
    if (!RequestTargetValid(url))
        return DlRequestStatus::InvalidArgument;

    std::uint32_t length = 0;
    const DlRequestStatus lineStatus =
        AppendRequestLine(url, buffer, capacity, &length);
    if (lineStatus != DlRequestStatus::Ok)
        return lineStatus;
    const DlRequestStatus hostStatus =
        AppendHostHeader(url, buffer, capacity, &length);
    if (hostStatus != DlRequestStatus::Ok)
        return hostStatus;
    const DlRequestStatus commonStatus =
        AppendCommonHeaders(buffer, capacity, &length);
    if (commonStatus != DlRequestStatus::Ok)
        return commonStatus;

    if (Dl_UrlHasCredentials(url))
    {
        const DlRequestStatus authStatus = AppendBasicAuthHeader(url, buffer,
            capacity, &length);
        if (authStatus != DlRequestStatus::Ok)
            return authStatus;
    }

    const DlRequestStatus trailerStatus =
        AppendConnectionClose(buffer, capacity, &length);
    if (trailerStatus != DlRequestStatus::Ok)
        return trailerStatus;

    *outLength = length;
    return DlRequestStatus::Ok;
}
