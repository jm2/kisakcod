#pragma once

#include <charconv>
#include <cstddef>
#include <cstring>
#include <limits>
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
            || *cursor == static_cast<unsigned char>(0x7f)
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

namespace detail
{
constexpr unsigned char FoldAsciiCase(const unsigned char value) noexcept
{
    return value >= static_cast<unsigned char>('A')
            && value <= static_cast<unsigned char>('Z')
        ? static_cast<unsigned char>(
            value + static_cast<unsigned char>('a' - 'A'))
        : value;
}

inline bool ComponentStemEquals(
    const char *const begin,
    const char *const end,
    const char *const reserved,
    const std::size_t reservedLength) noexcept
{
    if (static_cast<std::size_t>(end - begin) != reservedLength)
        return false;

    for (std::size_t index = 0; index < reservedLength; ++index)
    {
        if (FoldAsciiCase(static_cast<unsigned char>(begin[index]))
            != FoldAsciiCase(static_cast<unsigned char>(reserved[index])))
        {
            return false;
        }
    }
    return true;
}

// Windows resolves these names as devices even when a filename extension is
// present. Check each slash-delimited path component without allocating or
// applying locale-sensitive case conversion.
inline bool IsWindowsDosDevicePathComponent(
    const char *const begin,
    const char *const end) noexcept
{
    const char *baseEnd = begin;
    while (baseEnd != end && *baseEnd != '.')
        ++baseEnd;

    const std::size_t baseLength =
        static_cast<std::size_t>(baseEnd - begin);
    if (ComponentStemEquals(begin, baseEnd, "CON", 3)
        || ComponentStemEquals(begin, baseEnd, "PRN", 3)
        || ComponentStemEquals(begin, baseEnd, "AUX", 3)
        || ComponentStemEquals(begin, baseEnd, "NUL", 3)
        || ComponentStemEquals(begin, baseEnd, "CONIN$", 6)
        || ComponentStemEquals(begin, baseEnd, "CONOUT$", 7)
        || ComponentStemEquals(begin, baseEnd, "CLOCK$", 6))
    {
        return true;
    }

    if (baseLength != 4 && baseLength != 5)
        return false;

    const unsigned char first = FoldAsciiCase(
        static_cast<unsigned char>(begin[0]));
    const unsigned char second = FoldAsciiCase(
        static_cast<unsigned char>(begin[1]));
    const unsigned char third = FoldAsciiCase(
        static_cast<unsigned char>(begin[2]));

    if (!((first == 'c' && second == 'o' && third == 'm')
            || (first == 'l' && second == 'p' && third == 't')))
        return false;

    const unsigned char suffix = static_cast<unsigned char>(begin[3]);
    if (baseLength == 4)
    {
        return (suffix >= static_cast<unsigned char>('1')
                && suffix <= static_cast<unsigned char>('9'))
            || suffix == 0xB9u
            || suffix == 0xB2u
            || suffix == 0xB3u;
    }

    const unsigned char utf8Suffix =
        static_cast<unsigned char>(begin[4]);
    return suffix == 0xC2u
        && (utf8Suffix == 0xB9u
            || utf8Suffix == 0xB2u
            || utf8Suffix == 0xB3u);
}
} // namespace detail

// Path-like token components may contain ordinary forward slashes, but not a
// leading/trailing separator, either tokenizer comment introducer, the
// download-list field delimiter, a Windows namespace/metacharacter, a DOS
// device component, or traversal spellings rejected by the filesystem domain.
// Dots remain valid within filenames, but an exact-dot component and a
// trailing dot are rejected because Windows normalizes them. This also makes
// adjoining two independently validated components with '/' safe.
inline bool IsSafeUnquotedPathTokenComponent(
    const char *const value) noexcept
{
    if (!IsSafeUnquotedValueComponent(value))
        return false;

    if (std::strchr(value, '@')
        || std::strpbrk(value, ":<>|?*")
        || std::strstr(value, "..")
        || std::strstr(value, "::"))
        return false;

    const std::size_t length = std::strlen(value);
    if (length == 0)
        return true;
    if (value[0] == '/' || value[length - 1] == '/')
        return false;

    const char *component = value;
    for (const char *cursor = value;; ++cursor)
    {
        if (*cursor != '/' && *cursor != '\0')
            continue;

        const std::size_t componentLength =
            static_cast<std::size_t>(cursor - component);
        if (componentLength == 0
            || (componentLength == 1 && component[0] == '.')
            || component[componentLength - 1] == '.'
            || detail::IsWindowsDosDevicePathComponent(component, cursor))
        {
            return false;
        }
        if (*cursor == '\0')
            break;
        component = cursor + 1;
    }

    return true;
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

// Validates the complete key/value grammar without copying or using shared
// engine buffers. A leading delimiter is optional and values may be empty;
// keys must be nonempty and a trailing delimiter cannot introduce an orphan
// key.
inline bool IsWellFormed(const char *info) noexcept
{
    if (!info)
        return false;
    if (*info == '\\')
    {
        ++info;
        if (!*info)
            return false;
    }

    while (*info)
    {
        const char *const separator = std::strchr(info, '\\');
        if (!separator || separator == info)
            return false;

        const char *const value = separator + 1;
        const char *const valueEnd = std::strchr(value, '\\');
        if (!valueEnd)
            return true;
        if (valueEnd[1] == '\0')
            return false;
        info = valueEnd + 1;
    }
    return true;
}

// Returns a stable view into one exact value without using the engine's shared
// rotating Info_ValueForKey buffers. The complete info string is validated
// before publication, and duplicate keys are rejected. Outputs remain
// unchanged on malformed input, a missing/duplicate key, or invalid arguments.
// The value may be present-empty.
inline bool TryGetExactValueView(
    const char *info,
    const char *const key,
    const char **const value,
    std::size_t *const valueLength) noexcept
{
    if (!info || !key || !*key || !value || !valueLength)
        return false;

    const std::size_t keyLength = std::strlen(key);
    if (*info == '\\')
        ++info;

    const char *matchedValue = nullptr;
    std::size_t matchedValueLength = 0;
    while (*info)
    {
        const char *const separator = std::strchr(info, '\\');
        if (!separator)
            return false;

        const std::size_t candidateKeyLength =
            static_cast<std::size_t>(separator - info);
        if (candidateKeyLength == 0)
            return false;

        const char *const candidateValue = separator + 1;
        const char *candidateEnd = std::strchr(candidateValue, '\\');
        if (!candidateEnd)
            candidateEnd = candidateValue + std::strlen(candidateValue);

        if (candidateKeyLength == keyLength
            && std::memcmp(info, key, keyLength) == 0)
        {
            if (matchedValue)
                return false;

            matchedValue = candidateValue;
            matchedValueLength =
                static_cast<std::size_t>(candidateEnd - candidateValue);
        }

        if (*candidateEnd == '\0')
            break;
        if (candidateEnd[1] == '\0')
            return false;
        info = candidateEnd + 1;
    }

    if (!matchedValue)
        return false;

    *value = matchedValue;
    *valueLength = matchedValueLength;
    return true;
}

// Info_SetValueForKey_Big canonically omits empty values. Treat that one
// representation as equivalent only when the complete grammar is valid and
// the key is genuinely absent; malformed and duplicate-key input fails.
inline bool ValueMatchesExactOrAbsentEmpty(
    const char *const info,
    const char *const key,
    const char *const expected) noexcept
{
    if (!info || !key || !*key || !expected)
        return false;

    const char *value = nullptr;
    std::size_t valueLength = 0;
    if (!TryGetExactValueView(
            info, key, &value, &valueLength))
    {
        return !*expected
            && IsWellFormed(info)
            && !HasExactKey(info, key);
    }

    const std::size_t expectedLength = std::strlen(expected);
    return valueLength == expectedLength
        && std::memcmp(value, expected, expectedLength) == 0;
}

// Replaces the first exact key in a bounded regular info string and appends
// the sanitized nonempty value at the end. The source is copied to separate
// scratch storage and committed only after every grammar and capacity check,
// so failure leaves the published string unchanged. This retains the legacy
// delimiter-stripping and first-match replacement behavior.
inline bool TrySetValueForKey(
    char *const current,
    const std::size_t capacity,
    char *const scratch,
    const std::size_t scratchCapacity,
    const char *const key,
    const char *const value) noexcept
{
    if (!current
        || !scratch
        || current == scratch
        || !key
        || !value
        || capacity == 0
        || scratchCapacity < capacity)
    {
        return false;
    }

    const char *const currentEnd = static_cast<const char *>(
        std::memchr(current, '\0', capacity));
    if (!currentEnd || !IsWellFormed(current))
        return false;

    std::size_t keyLength = 0;
    while (keyLength < capacity && key[keyLength])
    {
        if (key[keyLength] == '\\'
            || key[keyLength] == ';'
            || key[keyLength] == '"')
        {
            return false;
        }
        ++keyLength;
    }
    if (keyLength == 0 || keyLength == capacity)
        return false;

    std::size_t rawValueLength = 0;
    std::size_t cleanValueLength = 0;
    while (rawValueLength < capacity && value[rawValueLength])
    {
        const char character = value[rawValueLength++];
        if (character != '\\' && character != ';' && character != '"')
            ++cleanValueLength;
    }
    if (rawValueLength == capacity)
        return false;

    std::size_t outputLength = 0;
    bool removedFirstMatch = false;
    const bool hadLeadingDelimiter = *current == '\\';
    const char *cursor = current;
    if (hadLeadingDelimiter)
        ++cursor;
    bool firstInputPair = true;
    bool hasRetainedPair = false;
    bool removedFirstInputPair = false;
    while (cursor != currentEnd)
    {
        const char *const separator = std::strchr(cursor, '\\');
        if (!separator)
            return false;
        const char *const componentValue = separator + 1;
        const char *componentEnd = std::strchr(componentValue, '\\');
        if (!componentEnd)
            componentEnd = currentEnd;

        const std::size_t componentKeyLength =
            static_cast<std::size_t>(separator - cursor);
        const std::size_t componentValueLength =
            static_cast<std::size_t>(componentEnd - componentValue);
        const bool matches = componentKeyLength == keyLength
            && std::memcmp(cursor, key, keyLength) == 0;
        if (matches && !removedFirstMatch)
        {
            removedFirstMatch = true;
            removedFirstInputPair = firstInputPair;
        }
        else
        {
            const std::size_t delimiterCount =
                hasRetainedPair
                    || hadLeadingDelimiter
                    || removedFirstInputPair
                ? 2
                : 1;
            const std::size_t pairLength =
                componentKeyLength
                + componentValueLength
                + delimiterCount;
            if (outputLength >= capacity
                || pairLength >= capacity - outputLength)
            {
                return false;
            }
            outputLength += pairLength;
            hasRetainedPair = true;
        }

        firstInputPair = false;
        cursor = componentEnd == currentEnd
            ? currentEnd
            : componentEnd + 1;
    }

    if (cleanValueLength != 0)
    {
        const std::size_t maximum =
            (std::numeric_limits<std::size_t>::max)();
        if (cleanValueLength > maximum - 2
            || keyLength > maximum - cleanValueLength - 2)
        {
            return false;
        }
        const std::size_t pairLength = keyLength + cleanValueLength + 2;
        if (outputLength >= capacity
            || pairLength >= capacity - outputLength)
        {
            return false;
        }
        outputLength += pairLength;
    }

    char *output = scratch;
    cursor = current;
    if (hadLeadingDelimiter)
        ++cursor;
    removedFirstMatch = false;
    firstInputPair = true;
    hasRetainedPair = false;
    removedFirstInputPair = false;
    while (cursor != currentEnd)
    {
        const char *const separator = std::strchr(cursor, '\\');
        const char *const componentValue = separator + 1;
        const char *componentEnd = std::strchr(componentValue, '\\');
        if (!componentEnd)
            componentEnd = currentEnd;

        const std::size_t componentKeyLength =
            static_cast<std::size_t>(separator - cursor);
        const std::size_t componentValueLength =
            static_cast<std::size_t>(componentEnd - componentValue);
        const bool matches = componentKeyLength == keyLength
            && std::memcmp(cursor, key, keyLength) == 0;
        if (matches && !removedFirstMatch)
        {
            removedFirstMatch = true;
            removedFirstInputPair = firstInputPair;
        }
        else
        {
            if (hasRetainedPair
                || hadLeadingDelimiter
                || removedFirstInputPair)
            {
                *output++ = '\\';
            }
            std::memcpy(output, cursor, componentKeyLength);
            output += componentKeyLength;
            *output++ = '\\';
            std::memcpy(output, componentValue, componentValueLength);
            output += componentValueLength;
            hasRetainedPair = true;
        }

        firstInputPair = false;
        cursor = componentEnd == currentEnd
            ? currentEnd
            : componentEnd + 1;
    }

    if (cleanValueLength != 0)
    {
        *output++ = '\\';
        std::memcpy(output, key, keyLength);
        output += keyLength;
        *output++ = '\\';
        for (std::size_t index = 0; index < rawValueLength; ++index)
        {
            const char character = value[index];
            if (character != '\\' && character != ';' && character != '"')
                *output++ = character;
        }
    }

    *output = '\0';
    if (static_cast<std::size_t>(output - scratch) != outputLength)
        return false;

    std::memmove(current, scratch, outputLength + 1);
    return true;
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
