#pragma once

#include <cstddef>

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
