#include <qcommon/sys_filesystem.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace
{
constexpr std::size_t kMaximumPathComponents = 256;

int DirectoryOpenFlags()
{
#if defined(__linux__)
    return O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW;
#elif defined(O_SEARCH)
    return O_SEARCH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW;
#else
    // Older macOS SDKs lack a search-only open mode. Those targets can reject
    // a write/search-only ancestor that has no read permission.
    return O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW;
#endif
}

bool IsValidUtf8(const char *const text)
{
    if (!text)
        return false;
    const auto *cursor = reinterpret_cast<const unsigned char *>(text);
    while (*cursor != 0)
    {
        if (*cursor <= 0x7f)
        {
            ++cursor;
            continue;
        }

        std::size_t continuationCount = 0;
        std::uint32_t codePoint = 0;
        if (*cursor >= 0xc2 && *cursor <= 0xdf)
        {
            continuationCount = 1;
            codePoint = *cursor & 0x1fU;
        }
        else if (*cursor >= 0xe0 && *cursor <= 0xef)
        {
            continuationCount = 2;
            codePoint = *cursor & 0x0fU;
        }
        else if (*cursor >= 0xf0 && *cursor <= 0xf4)
        {
            continuationCount = 3;
            codePoint = *cursor & 0x07U;
        }
        else
        {
            return false;
        }
        ++cursor;
        for (std::size_t index = 0; index < continuationCount; ++index)
        {
            if ((cursor[index] & 0xc0U) != 0x80U)
                return false;
            codePoint = (codePoint << 6U) | (cursor[index] & 0x3fU);
        }
        cursor += continuationCount;
        if ((continuationCount == 2 && codePoint < 0x800U)
            || (continuationCount == 3 && codePoint < 0x10000U)
            || (codePoint >= 0xd800U && codePoint <= 0xdfffU)
            || codePoint > 0x10ffffU)
        {
            return false;
        }
    }
    return true;
}

void ResetOutput(char *const output, const std::size_t outputCapacity)
{
    if (output && outputCapacity != 0)
        output[0] = '\0';
}

bool IsEnginePathSeparator(const char character)
{
    return character == '/' || character == '\\';
}

bool SplitSafePath(
    const char *const path,
    std::vector<std::string> *const components)
{
    if (!path || path[0] == '\0' || !components || !IsValidUtf8(path))
        return false;
    components->clear();

    const char *cursor = path;
    while (*cursor != '\0')
    {
        while (IsEnginePathSeparator(*cursor))
            ++cursor;
        const char *const begin = cursor;
        while (*cursor != '\0' && !IsEnginePathSeparator(*cursor))
            ++cursor;
        if (cursor == begin)
            continue;

        std::string component(begin, cursor);
        if (component == ".")
            continue;
        if (component == ".." || components->size() == kMaximumPathComponents)
            return false;
        components->push_back(std::move(component));
    }
    return true;
}

bool IsDirectoryNoFollow(const int parentFd, const char *const name)
{
    struct stat status{};
    return fstatat(parentFd, name, &status, AT_SYMLINK_NOFOLLOW) == 0
        && S_ISDIR(status.st_mode);
}

int OpenDirectoryForEnumeration(const char *const path)
{
    std::vector<std::string> components;
    if (!SplitSafePath(path, &components))
        return -1;

    constexpr int enumerationFlags =
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW;
    int parentFd = open(
        IsEnginePathSeparator(path[0]) ? "/" : ".",
        components.empty() ? enumerationFlags : DirectoryOpenFlags());
    if (parentFd < 0)
        return -1;

    for (std::size_t index = 0; index < components.size(); ++index)
    {
        const bool leaf = index + 1 == components.size();
        const int nextFd = openat(
            parentFd,
            components[index].c_str(),
            leaf ? enumerationFlags : DirectoryOpenFlags());
        const bool parentClosed = close(parentFd) == 0;
        if (nextFd < 0 || !parentClosed)
        {
            if (nextFd >= 0)
                close(nextFd);
            return -1;
        }
        parentFd = nextFd;
    }
    return parentFd;
}

unsigned char AsciiLower(const unsigned char character)
{
    return character >= 'A' && character <= 'Z'
        ? static_cast<unsigned char>(character + ('a' - 'A'))
        : character;
}

bool DirectoryEntryLess(
    const SysFileSystemDirectoryEntry &left,
    const SysFileSystemDirectoryEntry &right)
{
    const std::size_t commonLength =
        (std::min)(left.name.size(), right.name.size());
    for (std::size_t index = 0; index < commonLength; ++index)
    {
        const unsigned char leftCharacter =
            AsciiLower(static_cast<unsigned char>(left.name[index]));
        const unsigned char rightCharacter =
            AsciiLower(static_cast<unsigned char>(right.name[index]));
        if (leftCharacter != rightCharacter)
            return leftCharacter < rightCharacter;
    }
    if (left.name.size() != right.name.size())
        return left.name.size() < right.name.size();
    return left.name < right.name;
}

void InsertBoundedEntry(
    SysFileSystemDirectoryEntry entry,
    const std::size_t maximumEntries,
    std::vector<SysFileSystemDirectoryEntry> *const entries,
    bool *const truncated)
{
    if (entries->size() < maximumEntries)
    {
        entries->push_back(std::move(entry));
        std::push_heap(
            entries->begin(), entries->end(), DirectoryEntryLess);
        return;
    }

    *truncated = true;
    if (maximumEntries != 0
        && DirectoryEntryLess(entry, entries->front()))
    {
        std::pop_heap(
            entries->begin(), entries->end(), DirectoryEntryLess);
        entries->back() = std::move(entry);
        std::push_heap(
            entries->begin(), entries->end(), DirectoryEntryLess);
    }
}
}

bool KISAK_CDECL Sys_FileSystemCreateDirectory(const char *const path)
{
    std::vector<std::string> components;
    if (!SplitSafePath(path, &components))
        return false;

    int parentFd = open(
        IsEnginePathSeparator(path[0]) ? "/" : ".",
        DirectoryOpenFlags());
    if (parentFd < 0)
        return false;

    if (components.empty())
        return close(parentFd) == 0;

    for (std::size_t index = 0; index + 1 < components.size(); ++index)
    {
        const int nextFd = openat(
            parentFd,
            components[index].c_str(),
            DirectoryOpenFlags());
        const bool parentClosed = close(parentFd) == 0;
        if (nextFd < 0 || !parentClosed)
        {
            if (nextFd >= 0)
                close(nextFd);
            return false;
        }
        parentFd = nextFd;
    }

    const char *const leaf = components.back().c_str();
    bool created = mkdirat(parentFd, leaf, 0777) == 0;
    if (!created && errno == EEXIST)
        created = IsDirectoryNoFollow(parentFd, leaf);
    const bool parentClosed = close(parentFd) == 0;
    return created && parentClosed;
}

bool KISAK_CDECL Sys_FileSystemGetCurrentDirectory(
    char *const output,
    const std::size_t outputCapacity)
{
    ResetOutput(output, outputCapacity);
    if (!output || outputCapacity == 0)
        return false;
    if (!getcwd(output, outputCapacity))
    {
        ResetOutput(output, outputCapacity);
        return false;
    }
    if (!IsValidUtf8(output))
    {
        ResetOutput(output, outputCapacity);
        return false;
    }
    return true;
}

bool KISAK_CDECL Sys_FileSystemGetExecutablePath(
    char *const output,
    const std::size_t outputCapacity)
{
    ResetOutput(output, outputCapacity);
    if (!output || outputCapacity == 0)
        return false;

#if defined(__APPLE__)
    if (outputCapacity > std::numeric_limits<std::uint32_t>::max())
        return false;
    std::uint32_t capacity = static_cast<std::uint32_t>(outputCapacity);
    if (_NSGetExecutablePath(output, &capacity) != 0)
    {
        ResetOutput(output, outputCapacity);
        return false;
    }
    char *const resolved = realpath(output, nullptr);
    if (!resolved)
    {
        ResetOutput(output, outputCapacity);
        return false;
    }
    const std::size_t resolvedLength = std::strlen(resolved);
    if (resolvedLength >= outputCapacity)
    {
        std::free(resolved);
        ResetOutput(output, outputCapacity);
        return false;
    }
    std::memcpy(output, resolved, resolvedLength + 1);
    std::free(resolved);
    if (!IsValidUtf8(output))
    {
        ResetOutput(output, outputCapacity);
        return false;
    }
    return true;
#elif defined(__linux__)
    if (outputCapacity > static_cast<std::size_t>(
            (std::numeric_limits<ssize_t>::max)()))
    {
        return false;
    }

    const ssize_t length = readlink("/proc/self/exe", output, outputCapacity);
    if (length < 0 || static_cast<std::size_t>(length) >= outputCapacity)
    {
        ResetOutput(output, outputCapacity);
        return false;
    }
    output[length] = '\0';
    if (!IsValidUtf8(output))
    {
        ResetOutput(output, outputCapacity);
        return false;
    }
    return true;
#else
#error Unsupported POSIX executable-path implementation
#endif
}

SysFileSystemListStatus KISAK_CDECL Sys_FileSystemListDirectoryFiltered(
    const char *const utf8Path,
    const std::size_t maximumEntries,
    const SysFileSystemEntryFilter filter,
    const void *const filterContext,
    std::vector<SysFileSystemDirectoryEntry> *const entries)
{
    if (!entries)
        return SysFileSystemListStatus::Error;
    entries->clear();
    if (!utf8Path || utf8Path[0] == '\0' || !IsValidUtf8(utf8Path))
        return SysFileSystemListStatus::Error;

    const int directoryFd = OpenDirectoryForEnumeration(utf8Path);
    if (directoryFd < 0)
        return SysFileSystemListStatus::Error;
    DIR *const directory = fdopendir(directoryFd);
    if (!directory)
    {
        close(directoryFd);
        return SysFileSystemListStatus::Error;
    }

    bool truncated = false;
    bool failed = false;
    errno = 0;
    while (const dirent *const directoryEntry = readdir(directory))
    {
        const char *const name = directoryEntry->d_name;
        if ((name[0] == '.' && name[1] == '\0')
            || (name[0] == '.' && name[1] == '.' && name[2] == '\0'))
        {
            errno = 0;
            continue;
        }
        if (!IsValidUtf8(name))
        {
            failed = true;
            break;
        }
        // Engine paths treat both bytes as separators. A literal POSIX child
        // containing either byte cannot be represented and safely reopened.
        if (std::strchr(name, '\\') || std::strchr(name, ':'))
        {
            errno = 0;
            continue;
        }

        struct stat status{};
        if (fstatat(directoryFd, name, &status, AT_SYMLINK_NOFOLLOW) != 0)
        {
            failed = true;
            break;
        }

        SysFileSystemEntryKind kind;
        if (S_ISREG(status.st_mode))
            kind = SysFileSystemEntryKind::RegularFile;
        else if (S_ISDIR(status.st_mode))
            kind = SysFileSystemEntryKind::Directory;
        else
        {
            errno = 0;
            continue;
        }

        if (filter && !filter(name, kind, filterContext))
        {
            errno = 0;
            continue;
        }

        try
        {
            InsertBoundedEntry(
                SysFileSystemDirectoryEntry{std::string(name), kind},
                maximumEntries,
                entries,
                &truncated);
        }
        catch (const std::bad_alloc &)
        {
            failed = true;
            break;
        }
        errno = 0;
    }
    if (errno != 0)
        failed = true;
    if (closedir(directory) != 0)
        failed = true;
    if (failed)
    {
        entries->clear();
        return SysFileSystemListStatus::Error;
    }
    std::sort(entries->begin(), entries->end(), DirectoryEntryLess);
    return truncated
        ? SysFileSystemListStatus::Truncated
        : SysFileSystemListStatus::Complete;
}

SysFileSystemListStatus KISAK_CDECL Sys_FileSystemListDirectory(
    const char *const utf8Path,
    const std::size_t maximumEntries,
    std::vector<SysFileSystemDirectoryEntry> *const entries)
{
    return Sys_FileSystemListDirectoryFiltered(
        utf8Path, maximumEntries, nullptr, nullptr, entries);
}
