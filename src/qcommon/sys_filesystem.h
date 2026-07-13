#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <universal/platform_compat.h>

// Creates one directory without traversing symbolic-link/reparse ancestors.
// Existing real directories are accepted. Empty paths, parent components,
// invalid encodings, and symbolic-link/reparse targets are rejected.
bool KISAK_CDECL Sys_FileSystemCreateDirectory(const char *utf8Path);

// These queries return absolute UTF-8 paths without truncation. On failure,
// including insufficient capacity, output is reset to an empty string when
// possible. Callers may retry with a larger buffer.
bool KISAK_CDECL Sys_FileSystemGetCurrentDirectory(
    char *output,
    std::size_t outputCapacity);
bool KISAK_CDECL Sys_FileSystemGetExecutablePath(
    char *output,
    std::size_t outputCapacity);

enum class SysFileSystemEntryKind : std::uint8_t
{
    RegularFile,
    Directory,
};

struct SysFileSystemDirectoryEntry
{
    std::string name;
    SysFileSystemEntryKind kind;
};

enum class SysFileSystemListStatus : std::uint8_t
{
    Complete,
    Truncated,
    Error,
};

// Enumerates immediate children of one real directory. Results contain only
// regular files and real directories: symbolic links, reparse points, and
// special files are omitted. Names are valid UTF-8 and sorted by a stable,
// locale-independent ASCII case-insensitive ordering with a bytewise tie
// break. At most maximumEntries are retained; Truncated reports that eligible
// entries were omitted. Error always clears entries.
SysFileSystemListStatus KISAK_CDECL Sys_FileSystemListDirectory(
    const char *utf8Path,
    std::size_t maximumEntries,
    std::vector<SysFileSystemDirectoryEntry> *entries);

// Engine filesystem extensions do not include the dot. Matching is an exact,
// ASCII case-insensitive suffix match and does not interpret wildcard bytes.
inline bool Sys_FileSystemHasExtension(
    const char *const name,
    const char *const extension)
{
    if (!name || !extension || extension[0] == '\0')
        return false;

    std::size_t nameLength = 0;
    while (name[nameLength] != '\0')
        ++nameLength;
    std::size_t extensionLength = 0;
    while (extension[extensionLength] != '\0')
        ++extensionLength;
    if (nameLength <= extensionLength
        || name[nameLength - extensionLength - 1] != '.')
    {
        return false;
    }

    const char *const suffix = name + nameLength - extensionLength;
    const auto asciiLower = [](const unsigned char character) {
        return character >= 'A' && character <= 'Z'
            ? static_cast<unsigned char>(character + ('a' - 'A'))
            : character;
    };
    for (std::size_t index = 0; index < extensionLength; ++index)
    {
        if (asciiLower(static_cast<unsigned char>(suffix[index]))
            != asciiLower(static_cast<unsigned char>(extension[index])))
        {
            return false;
        }
    }
    return true;
}

namespace kisakcod_filesystem_detail
{
inline unsigned char NormalizeFilterCharacter(
    const unsigned char character)
{
    const unsigned char separator =
        character == '\\' || character == ':' ? '/' : character;
    return separator >= 'A' && separator <= 'Z'
        ? static_cast<unsigned char>(separator + ('a' - 'A'))
        : separator;
}

inline bool MatchFilterToken(
    const char *const pattern,
    const unsigned char nameCharacter,
    const char **const nextPattern)
{
    if (*pattern == '?')
    {
        *nextPattern = pattern + 1;
        return true;
    }
    if (*pattern != '[')
    {
        *nextPattern = pattern + 1;
        return NormalizeFilterCharacter(
                static_cast<unsigned char>(*pattern))
            == NormalizeFilterCharacter(nameCharacter);
    }

    bool matched = false;
    const unsigned char normalizedName =
        NormalizeFilterCharacter(nameCharacter);
    const char *cursor = pattern + 1;
    while (*cursor != '\0' && *cursor != ']')
    {
        const unsigned char rangeBegin = NormalizeFilterCharacter(
            static_cast<unsigned char>(*cursor));
        if (cursor[1] == '-'
            && cursor[2] != '\0'
            && cursor[2] != ']')
        {
            const unsigned char rangeEnd = NormalizeFilterCharacter(
                static_cast<unsigned char>(cursor[2]));
            const unsigned char lower =
                rangeBegin < rangeEnd ? rangeBegin : rangeEnd;
            const unsigned char upper =
                rangeBegin < rangeEnd ? rangeEnd : rangeBegin;
            matched = matched
                || (normalizedName >= lower && normalizedName <= upper);
            cursor += 3;
        }
        else
        {
            matched = matched || normalizedName == rangeBegin;
            ++cursor;
        }
    }
    if (*cursor == ']')
    {
        *nextPattern = cursor + 1;
        return matched;
    }

    // An unterminated class is a literal opening bracket.
    *nextPattern = pattern + 1;
    return normalizedName == static_cast<unsigned char>('[');
}
}

// Full-length, locale-independent form of the engine's path glob. Both slash
// spellings (and the historical ':' spelling) compare as '/', matching is
// ASCII case-insensitive, '*' spans zero or more bytes, '?' spans one byte,
// and bracket ranges are supported.
inline bool Sys_FileSystemMatchesPathFilter(
    const char *const filter,
    const char *const name)
{
    if (!filter || !name)
        return false;

    const char *pattern = filter;
    const char *cursor = name;
    const char *starPattern = nullptr;
    const char *starCursor = nullptr;
    while (*cursor != '\0')
    {
        // Preserve Com_Filter's historical prefix behavior: exhausting the
        // filter succeeds even when name bytes remain.
        if (*pattern == '\0')
            return true;
        if (*pattern == '*')
        {
            do
            {
                ++pattern;
            } while (*pattern == '*');
            starPattern = pattern;
            starCursor = cursor;
            if (*pattern == '\0')
                return true;
            continue;
        }

        const char *nextPattern = pattern;
        if (*pattern != '\0'
            && kisakcod_filesystem_detail::MatchFilterToken(
                pattern,
                static_cast<unsigned char>(*cursor),
                &nextPattern))
        {
            pattern = nextPattern;
            ++cursor;
            continue;
        }

        if (!starPattern)
            return false;
        pattern = starPattern;
        cursor = ++starCursor;
    }
    while (*pattern == '*')
        ++pattern;
    return *pattern == '\0';
}

// Returns the byte length of the parent portion of an absolute UTF-8 path.
// Files directly beneath POSIX, drive, extended-drive, and UNC roots retain
// the root separator. A zero result means no parent could be identified.
inline std::size_t Sys_FileSystemParentPathLength(const char *const path)
{
    if (!path || path[0] == '\0')
        return 0;
    const auto isSeparator = [](const char character) {
        return character == '\\' || character == '/';
    };
    const auto asciiUpper = [](const char character) {
        return character >= 'a' && character <= 'z'
            ? static_cast<char>(character - ('a' - 'A'))
            : character;
    };

    std::size_t length = 0;
    std::size_t lastSeparator = static_cast<std::size_t>(-1);
    while (path[length] != '\0')
    {
        if (isSeparator(path[length]))
            lastSeparator = length;
        ++length;
    }
    if (lastSeparator == static_cast<std::size_t>(-1))
        return 0;
    if (lastSeparator == 0)
        return 1;
    if (lastSeparator == 2 && length >= 3 && path[1] == ':')
        return 3;

    std::size_t rootStart = static_cast<std::size_t>(-1);
    if (length >= 2 && isSeparator(path[0]) && isSeparator(path[1]))
    {
        rootStart = 2;
        if (length >= 8
            && path[2] == '?'
            && isSeparator(path[3])
            && asciiUpper(path[4]) == 'U'
            && asciiUpper(path[5]) == 'N'
            && asciiUpper(path[6]) == 'C'
            && isSeparator(path[7]))
        {
            rootStart = 8;
        }
    }
    if (rootStart != static_cast<std::size_t>(-1))
    {
        std::size_t serverEnd = rootStart;
        while (serverEnd < length && !isSeparator(path[serverEnd]))
            ++serverEnd;
        std::size_t shareEnd = serverEnd < length ? serverEnd + 1 : length;
        while (shareEnd < length && !isSeparator(path[shareEnd]))
            ++shareEnd;
        if (shareEnd == lastSeparator)
            return lastSeparator + 1;
    }
    return lastSeparator;
}
