#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <universal/platform_compat.h>

// Creates one directory without traversing symbolic-link/reparse ancestors.
// Existing real directories are accepted. Empty paths, parent components,
// invalid encodings, and symbolic-link/reparse targets are rejected.
bool KISAK_CDECL Sys_FileSystemCreateDirectory(const char *utf8Path);

// Recursively deletes one real directory tree without following POSIX
// symbolic links or Win32 reparse points. The leaf and every descent use
// handle-relative operations so a racing rename cannot escape the boundary.
// A leaf itself that is a symbolic link or reparse point is rejected.
// During descent any symbolic link or reparse point encountered is left in
// place and not traversed. Real regular files and real directories are
// removed; special files (FIFOs, sockets, device nodes) are rejected.
// Descent is bounded by kSysFileSystemMaximumRecursionDepth so a
// pathological tree cannot exhaust the stack. Returns false on the first
// error and stops; partial state may remain.
bool KISAK_CDECL Sys_FileSystemRemoveTree(const char *utf8Path);

// Hard upper bound on directory levels the deletion service will descend
// into from a single leaf. Defends against pathologically deep trees whose
// recursion would otherwise overflow the call stack.
constexpr std::size_t kSysFileSystemMaximumRecursionDepth = 256;

// Returns the human-readable stage at which the most recent call to
// Sys_FileSystemRemoveTree failed. Stages identify the suboperation that
// returned the underlying error: "open-parent", "open-leaf",
// "enumerate", "stat-child", "unlink-file", "remove-directory",
// "unlink-symlink", "validate-handle", "depth-exceeded", or
// "invalid-arguments". Returns an empty string when no failure has been
// recorded (or after a successful call).
const char *Sys_FileSystemRemoveTreeLastStage();

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

using SysFileSystemEntryFilter = bool (KISAK_CDECL *)(
    const char *name,
    SysFileSystemEntryKind kind,
    const void *context);

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

// Applies filter before maximumEntries is enforced, so irrelevant directory
// children cannot consume the caller's result budget. A null filter includes
// every eligible child and is equivalent to Sys_FileSystemListDirectory.
SysFileSystemListStatus KISAK_CDECL Sys_FileSystemListDirectoryFiltered(
    const char *utf8Path,
    std::size_t maximumEntries,
    SysFileSystemEntryFilter filter,
    const void *filterContext,
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

inline int Sys_FileSystemCompareEnginePaths(
    const char *left,
    const char *right)
{
    if (left == right)
        return 0;
    if (!left)
        return -1;
    if (!right)
        return 1;

    for (;;)
    {
        const unsigned char leftCharacter =
            kisakcod_filesystem_detail::NormalizeFilterCharacter(
                static_cast<unsigned char>(*left++));
        const unsigned char rightCharacter =
            kisakcod_filesystem_detail::NormalizeFilterCharacter(
                static_cast<unsigned char>(*right++));
        if (leftCharacter < rightCharacter)
            return -1;
        if (leftCharacter > rightCharacter)
            return 1;
        if (leftCharacter == '\0')
            return 0;
    }
}

inline bool Sys_FileSystemEnginePathsEqual(
    const char *const left,
    const char *const right)
{
    return Sys_FileSystemCompareEnginePaths(left, right) == 0;
}

inline void Sys_FileSystemSortPathPointers(
    const char **const paths,
    const std::size_t pathCount)
{
    if (!paths || pathCount < 2)
        return;
    std::stable_sort(
        paths,
        paths + pathCount,
        [](const char *const left, const char *const right) {
            return Sys_FileSystemCompareEnginePaths(left, right) < 0;
        });
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
