// SPDX-License-Identifier: GPL-3.0-only
//
// Response head parsing for the download protocol (one of the pure
// protocol helpers; see dl_http.h for the contract). Split out of
// dl_http.cpp so every function stays small, bounded, and reviewable.
// No engine dependencies: this unit compiles unchanged into the protocol
// unit tests on any host.

#include <qcommon/dl_http.h>

#include <qcommon/dl_http_internal.h>

#include <cstdint>
#include <cstring>

namespace
{
using DlHttpInternal::CopyRaw;
using DlHttpInternal::EqualsIgnoreCase;

// Case-insensitive scan for `needle` (a string literal) inside a bounded
// header value fragment.
template <std::size_t N>
bool ContainsIgnoreCase(const char *const haystack,
    const std::uint32_t length,
    const char (&needle)[N]) noexcept
{
    static_assert(N > 0, "needle must be a string literal");
    const std::uint32_t needleLength = static_cast<std::uint32_t>(N - 1);
    if (needleLength == 0 || needleLength > length)
        return false;
    for (std::uint32_t start = 0; start + needleLength <= length; ++start)
    {
        if (EqualsIgnoreCase(haystack + start, needleLength, needle))
            return true;
    }
    return false;
}

bool IsHeaderSpace(const char value) noexcept
{
    return value == ' ' || value == '\t';
}

// Trims optional whitespace from both ends of a header value fragment and
// copies the interior into `out` (capacity `capacity`). False on overflow;
// on success *outLength holds the trimmed length.
bool TrimCopy(const char *const source,
    const std::uint32_t length,
    char *const out,
    const std::uint32_t capacity,
    std::uint32_t *const outLength) noexcept
{
    std::uint32_t begin = 0;
    std::uint32_t end = length;
    while (begin < end && IsHeaderSpace(source[begin]))
        ++begin;
    while (end > begin && IsHeaderSpace(source[end - 1]))
        --end;
    return CopyRaw(source + begin, end - begin, out, capacity, outLength);
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

// Header name/value split at the first ':'.
struct DlHeaderLine
{
    const char *name;
    std::uint32_t nameLength;
    const char *value;
    std::uint32_t valueLength;
};

bool SplitHeaderLine(const char *const line,
    const std::uint32_t lineLength,
    DlHeaderLine *const out) noexcept
{
    for (std::uint32_t index = 0; index < lineLength; ++index)
    {
        if (line[index] == ':')
        {
            out->name = line;
            out->nameLength = index;
            out->value = line + index + 1;
            out->valueLength = lineLength - index - 1;
            return true;
        }
    }
    return false;
}

// Parses a Content-Length value: trimmed decimal digits only, as the
// retail library accepted. False when the value is empty, non-decimal,
// or too long for the parser -- an absent length, exactly as before.
// *outOverflow is set when the digits are decimal but exceed 64 bits:
// that is a malformed response rather than an absent length, and the
// caller rejects the head instead of letting the value wrap (2^64 would
// silently parse as a length of 0 and mark a truncated body complete).
bool ParseContentLength(const char *const value,
    const std::uint32_t valueLength,
    std::uint64_t *const outLength,
    bool *const outOverflow) noexcept
{
    *outOverflow = false;
    char digits[32];
    std::uint32_t digitsLength = 0;
    if (!TrimCopy(value, valueLength, digits, sizeof(digits), &digitsLength))
        return false;
    if (digitsLength == 0)
        return false;
    std::uint64_t parsed = 0;
    for (std::uint32_t index = 0; index < digitsLength; ++index)
    {
        const char digit = digits[index];
        if (digit < '0' || digit > '9')
            return false;
        const std::uint64_t magnitude =
            static_cast<std::uint64_t>(digit - '0');
        // Guard every step against overflow: parsed * 10 + magnitude
        // must stay representable, so the value fails into the
        // response-rejection path instead of wrapping.
        if (parsed > (UINT64_MAX - magnitude) / 10u)
        {
            *outOverflow = true;
            return false;
        }
        parsed = parsed * 10u + magnitude;
    }
    *outLength = parsed;
    return true;
}

// Location: trim directly into the head's own buffer so the stored value
// and its length can never disagree. Overflow leaves the previous state.
void ApplyLocationHeader(DlResponseHead *const out,
    const char *const value,
    const std::uint32_t valueLength) noexcept
{
    std::uint32_t storedLength = 0;
    if (!TrimCopy(value, valueLength, out->location, sizeof(out->location),
            &storedLength))
        return;
    out->locationLength = static_cast<std::uint16_t>(storedLength);
    out->hasLocation = true;
}

// Transfer-Encoding: the transport cannot dechunk, so only the chunked
// token matters; anything else is ignored.
void ApplyTransferEncodingHeader(DlResponseHead *const out,
    const char *const value,
    const std::uint32_t valueLength) noexcept
{
    if (ContainsIgnoreCase(value, valueLength, "chunked"))
        out->chunked = true;
}

// Applies one header line; the first occurrence of each consumed field
// wins, matching the retail library's parsing. False when the head must
// be rejected: a present Content-Length whose decimal value overflows
// 64 bits is malformed, not absent, and reaches the response-rejection
// path through this return.
bool ApplyHeader(DlResponseHead *const out, const DlHeaderLine &header) noexcept
{
    if (EqualsIgnoreCase(header.name, header.nameLength, "Content-Length"))
    {
        std::uint64_t parsedLength = 0;
        bool overflow = false;
        if (!out->hasContentLength)
        {
            if (!ParseContentLength(header.value, header.valueLength,
                    &parsedLength, &overflow))
            {
                if (overflow)
                    return false;
                // Empty/non-decimal keeps the lenient absent-length
                // behavior; a later header may still supply a value.
                return true;
            }
            out->hasContentLength = true;
            out->contentLength = parsedLength;
        }
    }
    else if (EqualsIgnoreCase(header.name, header.nameLength, "Location"))
    {
        if (!out->hasLocation)
            ApplyLocationHeader(out, header.value, header.valueLength);
    }
    else if (EqualsIgnoreCase(header.name, header.nameLength,
                 "Transfer-Encoding"))
    {
        ApplyTransferEncodingHeader(out, header.value, header.valueLength);
    }
    return true;
}

// Header lines start after the status line and run to the head end.
std::uint32_t DlHeaderBlockStart(const char *const buffer,
    const std::uint32_t headEnd) noexcept
{
    std::uint32_t cursor = 0;
    while (cursor < headEnd && buffer[cursor] != '\n')
        ++cursor;
    return cursor + 1; // skip the status line's newline
}

// Parses every complete header line in [start, headEnd). Continuation
// lines (leading SP/HT, RFC 7230 obs-fold) are skipped, as before.
// False when a header rejects the head (an overflowing Content-Length).
bool ParseHeaderLines(DlResponseHead *const out,
    const char *const buffer,
    const std::uint32_t start,
    const std::uint32_t headEnd) noexcept
{
    std::uint32_t cursor = start;
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
            DlHeaderLine header{};
            if (SplitHeaderLine(line, lineLength, &header)
                && !ApplyHeader(out, header))
                return false;
        }
        cursor = lineEnd + 1;
    }
    return true;
}

bool HeadArgumentsValid(const char *const buffer,
    const std::uint32_t *const inOutLength,
    const DlResponseHead *const out) noexcept
{
    return buffer && inOutLength && out;
}
} // namespace

DlResponseEvent KISAK_CDECL Dl_ParseResponseHead(
    char *const buffer,
    std::uint32_t *const inOutLength,
    DlResponseHead *const out)
{
    if (!HeadArgumentsValid(buffer, inOutLength, out))
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

    // A header can reject the head outright: an overflowing
    // Content-Length aborts the transfer before any body byte is
    // written, so a truncated body can never be renamed into place.
    if (!ParseHeaderLines(out, buffer, DlHeaderBlockStart(buffer, headEnd),
            headEnd))
        return DlResponseEvent::StatusError;

    // Consume the head: keep only body bytes at the front of the buffer.
    const std::uint32_t bodyLength = length - headEnd;
    if (bodyLength != 0)
        std::memmove(buffer, buffer + headEnd, bodyLength);
    *inOutLength = bodyLength;
    return DlResponseEvent::HeadComplete;
}
