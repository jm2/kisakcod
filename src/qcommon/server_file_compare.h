#pragma once

#include <array>
#include <cstddef>
#include <cstring>

namespace server_file_compare
{
enum class Result : int
{
    Match = 0,
    NeedDownload = 1,
    NotDownloadable = 2,
};

enum class Failure
{
    None,
    Semantic,
    OutputCapacity,
    InvalidInput,
};

struct Outcome
{
    Result result = Result::Match;
    Failure failure = Failure::None;
};

inline constexpr std::size_t kAggregateCapacity = 1024;

struct IwdReferences
{
    const char *const *names = nullptr;
    const int *checksums = nullptr;
    std::size_t count = 0;
};

struct FastFileReferences
{
    const char *const *names = nullptr;
    const int *fileSizes = nullptr;
    std::size_t count = 0;
};

struct Callbacks
{
    void *context = nullptr;
    bool (*hasIwdChecksum)(void *context, int checksum) = nullptr;
    bool (*isServerPak)(void *context, const char *name) = nullptr;
    bool (*isOfficialMainIwd)(void *context, const char *name) = nullptr;
    bool (*localIwdFileExists)(void *context, const char *name) = nullptr;
    int (*fastFileSize)(void *context, const char *name, bool gameDirectory) = nullptr;
};

inline unsigned char FoldAscii(const unsigned char value) noexcept
{
    if (value >= static_cast<unsigned char>('A')
        && value <= static_cast<unsigned char>('Z'))
    {
        return static_cast<unsigned char>(
            value + (static_cast<unsigned char>('a')
                - static_cast<unsigned char>('A')));
    }
    return value;
}

inline bool IsModGameDirectory(const char *const gameDirectory) noexcept
{
    if (!gameDirectory || !*gameDirectory)
        return false;

    constexpr char prefix[] = "mods/";
    if (std::strlen(gameDirectory) <= sizeof(prefix) - 1)
        return false;
    for (std::size_t index = 0; index < sizeof(prefix) - 1; ++index)
    {
        if (FoldAscii(static_cast<unsigned char>(gameDirectory[index]))
                != FoldAscii(static_cast<unsigned char>(prefix[index])))
        {
            return false;
        }
    }
    return true;
}

// Returns the nonempty path below gameDirectory only when name is an exact,
// case-insensitive child. A prefix collision (for example mods/foo2 for
// mods/foo) is deliberately not a member.
inline const char *GameDirectorySuffix(
    const char *const name,
    const char *const gameDirectory) noexcept
{
    if (!name || !IsModGameDirectory(gameDirectory))
        return nullptr;

    const std::size_t gameDirectoryLength = std::strlen(gameDirectory);
    const std::size_t nameLength = std::strlen(name);
    if (nameLength <= gameDirectoryLength
        || name[gameDirectoryLength] != '/'
        || name[gameDirectoryLength + 1] == '\0')
    {
        return nullptr;
    }

    for (std::size_t index = 0; index < gameDirectoryLength; ++index)
    {
        if (FoldAscii(static_cast<unsigned char>(name[index]))
            != FoldAscii(static_cast<unsigned char>(gameDirectory[index])))
        {
            return nullptr;
        }
    }

    return name + gameDirectoryLength + 1;
}

inline std::size_t BoundedLength(
    const char *const value,
    const std::size_t capacity) noexcept
{
    if (!value)
        return capacity;

    std::size_t length = 0;
    while (length < capacity && value[length] != '\0')
        ++length;
    return length;
}

// Preflights the complete append, including the final NUL, before changing
// output. Parts must not alias output. On failure output remains byte-for-byte
// unchanged.
inline bool AppendParts(
    char *const output,
    const std::size_t capacity,
    const char *const *const parts,
    const std::size_t partCount) noexcept
{
    if (!output || capacity == 0 || (partCount != 0 && !parts))
        return false;

    const std::size_t outputLength = BoundedLength(output, capacity);
    if (outputLength == capacity)
        return false;

    std::size_t finalLength = outputLength;
    for (std::size_t index = 0; index < partCount; ++index)
    {
        if (!parts[index])
            return false;
        const std::size_t partLength = std::strlen(parts[index]);
        const std::size_t remaining = capacity - finalLength - 1;
        if (partLength > remaining)
            return false;
        finalLength += partLength;
    }

    char *cursor = output + outputLength;
    for (std::size_t index = 0; index < partCount; ++index)
    {
        const std::size_t partLength = std::strlen(parts[index]);
        std::memcpy(cursor, parts[index], partLength);
        cursor += partLength;
    }
    *cursor = '\0';
    return true;
}

inline bool AppendFileName(
    char *const output,
    const std::size_t capacity,
    const char *const name,
    const char *const extension) noexcept
{
    const char *const parts[] = {name, extension};
    return AppendParts(output, capacity, parts, 2);
}

inline bool AppendDownloadPair(
    char *const output,
    const std::size_t capacity,
    const char *const remoteName,
    const char *const localName,
    const char *const extension) noexcept
{
    const char *const parts[] = {
        "@", remoteName, extension, "@", localName, extension};
    return AppendParts(output, capacity, parts, 6);
}

inline bool AppendMissingLine(
    char *const output,
    const std::size_t capacity,
    const char *const name,
    const char *const extension,
    const char *const annotation) noexcept
{
    const char *const parts[] = {
        name, extension, annotation ? annotation : "", "\n"};
    return AppendParts(output, capacity, parts, 4);
}

inline Outcome CompareIwds(
    char *const output,
    const std::size_t capacity,
    const bool downloadList,
    const char *const gameDirectory,
    const IwdReferences references,
    const Callbacks &callbacks) noexcept
{
    if (!output || capacity == 0
        || (references.count != 0
            && (!references.names || !references.checksums)))
    {
        return {Result::NotDownloadable, Failure::InvalidInput};
    }

    bool needsDownload = false;
    for (std::size_t index = 0; index < references.count; ++index)
    {
        const char *const name = references.names[index];
        if (!name || !*name)
            continue;

        const bool hasGameDirectory = IsModGameDirectory(gameDirectory);
        if (hasGameDirectory
            && callbacks.isServerPak
            && callbacks.isServerPak(callbacks.context, name))
        {
            continue;
        }

        if (callbacks.hasIwdChecksum
            && callbacks.hasIwdChecksum(
                callbacks.context, references.checksums[index]))
        {
            continue;
        }

        const char *const suffix =
            GameDirectorySuffix(name, gameDirectory);
        const bool officialMainIwd = callbacks.isOfficialMainIwd
            && callbacks.isOfficialMainIwd(callbacks.context, name);
        if (!suffix || officialMainIwd)
        {
            // A complete diagnostic is useful when it fits. The safe result is
            // still non-downloadable when even the diagnostic cannot fit.
            if (!AppendFileName(output, capacity, name, ".iwd"))
                return {Result::NotDownloadable, Failure::OutputCapacity};
            return {Result::NotDownloadable, Failure::Semantic};
        }

        bool appended = false;
        if (downloadList)
        {
            appended = AppendDownloadPair(
                output, capacity, name, name, ".iwd");
        }
        else
        {
            const bool localFileExists = callbacks.localIwdFileExists
                && callbacks.localIwdFileExists(callbacks.context, name);
            appended = AppendMissingLine(
                output,
                capacity,
                name,
                ".iwd",
                localFileExists
                    ? " (local file exists with wrong checksum)"
                    : "");
        }

        if (!appended)
            return {Result::NotDownloadable, Failure::OutputCapacity};
        needsDownload = true;
    }

    return {
        needsDownload ? Result::NeedDownload : Result::Match,
        Failure::None};
}

inline Outcome CompareFastFiles(
    char *const output,
    const std::size_t capacity,
    const bool downloadList,
    const char *const gameDirectory,
    const FastFileReferences references,
    const Callbacks &callbacks) noexcept
{
    if (!output || capacity == 0
        || (references.count != 0
            && (!references.names || !references.fileSizes)))
    {
        return {Result::NotDownloadable, Failure::InvalidInput};
    }

    bool needsDownload = false;
    for (std::size_t index = 0; index < references.count; ++index)
    {
        const char *const name = references.names[index];
        if (!name || !*name)
            continue;

        const char *const suffix =
            GameDirectorySuffix(name, gameDirectory);
        const char *const lookupName = suffix ? suffix : name;
        const int fileSize = callbacks.fastFileSize
            ? callbacks.fastFileSize(
                callbacks.context, lookupName, suffix != nullptr)
            : 0;
        if (fileSize == references.fileSizes[index])
            continue;

        if (!suffix)
        {
            if (!AppendFileName(output, capacity, name, ".ff"))
                return {Result::NotDownloadable, Failure::OutputCapacity};
            return {Result::NotDownloadable, Failure::Semantic};
        }

        bool appended = false;
        if (downloadList)
        {
            appended = AppendDownloadPair(
                output, capacity, name, name, ".ff");
        }
        else
        {
            appended = AppendMissingLine(
                output,
                capacity,
                name,
                ".ff",
                fileSize != 0
                    ? " (local file exists with wrong filesize)"
                    : "");
        }

        if (!appended)
            return {Result::NotDownloadable, Failure::OutputCapacity};
        needsDownload = true;
    }

    return {
        needsDownload ? Result::NeedDownload : Result::Match,
        Failure::None};
}

inline Result CompareAll(
    char *const output,
    const std::size_t capacity,
    const bool downloadList,
    const char *const gameDirectory,
    const IwdReferences iwds,
    const FastFileReferences fastFiles,
    const Callbacks &callbacks) noexcept
{
    if (!output || capacity == 0)
        return Result::NotDownloadable;

    const std::size_t stagingCapacity = capacity < kAggregateCapacity
        ? capacity
        : kAggregateCapacity;
    std::array<char, kAggregateCapacity> staging{};
    const Outcome iwdOutcome = CompareIwds(
        staging.data(),
        stagingCapacity,
        downloadList,
        gameDirectory,
        iwds,
        callbacks);
    Outcome finalOutcome = iwdOutcome;
    if (iwdOutcome.result != Result::NotDownloadable)
    {
        const Outcome fastFileOutcome = CompareFastFiles(
            staging.data(),
            stagingCapacity,
            downloadList,
            gameDirectory,
            fastFiles,
            callbacks);
        finalOutcome = fastFileOutcome;
        if (fastFileOutcome.result != Result::NotDownloadable)
        {
            finalOutcome.result =
                iwdOutcome.result == Result::NeedDownload
                    || fastFileOutcome.result == Result::NeedDownload
                ? Result::NeedDownload
                : Result::Match;
        }
    }

    if (finalOutcome.failure == Failure::OutputCapacity
        || finalOutcome.failure == Failure::InvalidInput)
    {
        return Result::NotDownloadable;
    }

    const std::size_t stagedLength =
        BoundedLength(staging.data(), stagingCapacity);
    if (stagedLength == stagingCapacity)
        return Result::NotDownloadable;
    std::memcpy(output, staging.data(), stagedLength + 1);
    return finalOutcome.result;
}
}
