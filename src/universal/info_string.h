#pragma once

#include <charconv>
#include <cstddef>
#include <cstring>
#include <system_error>

namespace info_string
{
// Values used as unquoted, whitespace-tokenized info-string components must
// not contain either token separators or info-string delimiters. Empty text is
// representable here; callers that require a nonempty component enforce that
// separately.
inline bool IsSafeUnquotedValueComponent(const char *const value) noexcept
{
    if (!value)
        return false;

    for (const unsigned char *cursor =
             reinterpret_cast<const unsigned char *>(value);
         *cursor;
         ++cursor)
    {
        if (*cursor <= static_cast<unsigned char>(' ')
            || *cursor == static_cast<unsigned char>('\\')
            || *cursor == static_cast<unsigned char>(';')
            || *cursor == static_cast<unsigned char>('"')
            || (*cursor == static_cast<unsigned char>('/')
                && (cursor[1] == static_cast<unsigned char>('/')
                    || cursor[1] == static_cast<unsigned char>('*'))))
        {
            return false;
        }
    }

    return true;
}

// Path-like token components may contain ordinary forward slashes, but not a
// leading/trailing separator, either tokenizer comment introducer, or the
// traversal/namespace spellings rejected by the filesystem domain. Single
// dots remain valid filename characters. This also makes adjoining two
// independently validated components with '/' safe.
inline bool IsSafeUnquotedPathTokenComponent(
    const char *const value) noexcept
{
    if (!IsSafeUnquotedValueComponent(value))
        return false;

    if (std::strstr(value, "..") || std::strstr(value, "::"))
        return false;

    const std::size_t length = std::strlen(value);
    return length == 0
        || (value[0] != '/'
            && value[0] != '*'
            && value[length - 1] != '/');
}

// Parse one complete signed-decimal token. The destination remains
// unchanged on null, empty, non-decimal, trailing-junk, or range failure.
inline bool TryParseSignedDecimalToken(
    const char *const value,
    int *const output) noexcept
{
    if (!value || !*value || !output)
        return false;

    int parsed = 0;
    const char *const end = value + std::strlen(value);
    const std::from_chars_result result =
        std::from_chars(value, end, parsed, 10);
    if (result.ec != std::errc{} || result.ptr != end)
        return false;

    *output = parsed;
    return true;
}

// Info_ValueForKey cannot distinguish an absent key from a present empty
// value. This bounded-by-NUL parser supplies that distinction for exact keys.
inline bool HasExactKey(
    const char *info,
    const char *const key) noexcept
{
    if (!info || !key || !*key)
        return false;

    const std::size_t keyLength = std::strlen(key);
    if (*info == '\\')
        ++info;

    while (*info)
    {
        const char *const separator = std::strchr(info, '\\');
        if (!separator)
            return false;

        const std::size_t candidateLength =
            static_cast<std::size_t>(separator - info);
        if (candidateLength == keyLength
            && std::memcmp(info, key, keyLength) == 0)
        {
            return true;
        }

        const char *const nextPair = std::strchr(separator + 1, '\\');
        if (!nextPair)
            return false;
        info = nextPair + 1;
    }

    return false;
}

// This subtraction form proves that currentLength + suffixLength is strictly
// below capacity without ever evaluating an overflowing addition.
constexpr bool CanAppendPreformattedSuffix(
    const std::size_t currentLength,
    const std::size_t suffixLength,
    const std::size_t capacity) noexcept
{
    return capacity != 0
        && currentLength < capacity
        && suffixLength < capacity - currentLength;
}

// The current string and suffix must be NUL-terminated. Failure never writes
// the destination, and success always copies the suffix terminator as well.
inline bool AppendPreformattedSuffix(
    char *const current,
    const std::size_t capacity,
    const char *const suffix) noexcept
{
    if (!current || !suffix || capacity == 0)
        return false;

    const void *const terminator = std::memchr(current, '\0', capacity);
    if (!terminator)
        return false;

    const std::size_t currentLength =
        static_cast<std::size_t>(
            static_cast<const char *>(terminator) - current);
    const std::size_t suffixLength = std::strlen(suffix);
    if (!CanAppendPreformattedSuffix(
            currentLength, suffixLength, capacity))
    {
        return false;
    }

    std::memmove(
        current + currentLength,
        suffix,
        suffixLength + 1);
    return true;
}
} // namespace info_string
