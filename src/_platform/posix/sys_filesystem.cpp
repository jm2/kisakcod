#include <qcommon/sys_filesystem.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
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

bool KISAK_CDECL Sys_FileSystemReadFile(
    const char *const utf8Path,
    const std::size_t maximumBytes,
    std::vector<unsigned char> *const contents)
{
    if (contents)
        contents->clear();
    if (!contents || !utf8Path || utf8Path[0] == '\0')
        return false;

    std::vector<std::string> components;
    if (!SplitSafePath(utf8Path, &components))
        return false;
    if (components.empty())
        return false;

    // Walk and validate every directory ancestor without following
    // symbolic links, exactly like the directory services above.
    int parentFd = open(
        IsEnginePathSeparator(utf8Path[0]) ? "/" : ".",
        DirectoryOpenFlags());
    if (parentFd < 0)
        return false;

    const std::size_t ancestorCount = components.size() - 1;
    for (std::size_t index = 0; index < ancestorCount; ++index)
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

    // O_NOFOLLOW on the leaf opens a symbolic link's error instead of
    // its target, so link leaves fail closed here.
    const int fileFd = openat(
        parentFd,
        components.back().c_str(),
        O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    const bool parentClosed = close(parentFd) == 0;
    if (fileFd < 0 || !parentClosed)
    {
        if (fileFd >= 0)
            close(fileFd);
        return false;
    }

    struct stat status{};
    if (fstat(fileFd, &status) != 0 || !S_ISREG(status.st_mode))
    {
        close(fileFd);
        return false;
    }
    if (status.st_size < 0
        || static_cast<std::uintmax_t>(status.st_size)
            > static_cast<std::uintmax_t>(maximumBytes))
    {
        close(fileFd);
        return false;
    }

    std::vector<unsigned char> bytes;
    try
    {
        bytes.resize(static_cast<std::size_t>(status.st_size));
    }
    catch (const std::bad_alloc &)
    {
        close(fileFd);
        return false;
    }

    std::size_t total = 0;
    bool failed = false;
    while (total < bytes.size())
    {
        const ssize_t chunk = read(
            fileFd,
            bytes.data() + total,
            bytes.size() - total);
        if (chunk < 0)
        {
            if (errno == EINTR)
                continue;
            failed = true;
            break;
        }
        if (chunk == 0)
        {
            // The file shrank between fstat and read; the content would
            // be a torn prefix, so reject it wholesale.
            failed = true;
            break;
        }
        total += static_cast<std::size_t>(chunk);
    }

    struct stat after{};
    if (!failed)
    {
        failed = fstat(fileFd, &after) != 0
            || after.st_size != status.st_size;
    }
    if (close(fileFd) != 0)
        failed = true;
    if (failed)
        return false;

    contents->swap(bytes);
    return true;
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

namespace
{
// "." and ".." are never deletion candidates.
bool IsRelativeDirectoryName(const char *const name)
{
    return (name[0] == '.' && name[1] == '\0')
        || (name[0] == '.' && name[1] == '.' && name[2] == '\0');
}

// Classification of one directory entry for the deletion walk. kStop
// covers both a failed classification probe and a special file: FIFOs,
// sockets, device nodes, and other specials refuse to be removed through
// the deletion service — they were not part of the engine's intended
// layout and a top-level caller should not erase them silently.
enum class RemoveEntryKind
{
    kFile,
    kDirectory,
    kSymlink,
    kStop
};

RemoveEntryKind ClassifyEntryForRemoval(
    const int directoryFd,
    const char *const name)
{
    struct stat status{};
    if (fstatat(directoryFd, name, &status, AT_SYMLINK_NOFOLLOW) != 0)
        return RemoveEntryKind::kStop;
    // Symbolic links are never traversed. They are removed only when
    // the path the test follows leads through the deletion service
    // itself; otherwise deletion of their target would depend on
    // contents outside the engine's filesystem tree.
    if (S_ISLNK(status.st_mode))
        return RemoveEntryKind::kSymlink;
    if (S_ISREG(status.st_mode))
        return RemoveEntryKind::kFile;
    if (S_ISDIR(status.st_mode))
        return RemoveEntryKind::kDirectory;
    return RemoveEntryKind::kStop;
}

bool AppendEntryName(
    std::vector<std::string> *entries,
    const char *const name)
{
    try
    {
        entries->emplace_back(name);
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
    return true;
}

bool AppendClassifiedEntry(
    const RemoveEntryKind kind,
    const char *const name,
    std::vector<std::string> *files,
    std::vector<std::string> *subdirectories,
    std::vector<std::string> *symlinks)
{
    switch (kind)
    {
        case RemoveEntryKind::kFile:
            return AppendEntryName(files, name);
        case RemoveEntryKind::kDirectory:
            return AppendEntryName(subdirectories, name);
        case RemoveEntryKind::kSymlink:
            return AppendEntryName(symlinks, name);
        case RemoveEntryKind::kStop:
        default:
            return false;
    }
}

// One readdir pass that classifies every entry into its removal bucket.
// Fails closed on unreadable directories, invalid UTF-8 names, specials,
// and allocation failures. Names containing ':' or '\' are ordinary POSIX
// names and are removed like any other: every removal below acts on the
// held directory descriptor (unlinkat/openat never re-resolve a name
// through a path), so the engine's separator rules do not apply — skipping
// such names here stranded them and left the tree partially deleted
// (operator-audit defect).
bool CollectDirectoryEntries(
    const int directoryFd,
    DIR *const directory,
    std::vector<std::string> *files,
    std::vector<std::string> *subdirectories,
    std::vector<std::string> *symlinks)
{
    errno = 0;
    for (;;)
    {
        const dirent *const entry = readdir(directory);
        if (!entry)
            break;
        const char *const name = entry->d_name;
        if (IsRelativeDirectoryName(name))
        {
            errno = 0;
            continue;
        }
        if (!IsValidUtf8(name))
            return false;
        if (!AppendClassifiedEntry(
                ClassifyEntryForRemoval(directoryFd, name),
                name,
                files,
                subdirectories,
                symlinks))
        {
            return false;
        }
        errno = 0;
    }
    return errno == 0;
}

bool UnlinkEntriesAt(
    const int directoryFd,
    const std::vector<std::string> &names,
    const int unlinkFlags)
{
    for (const std::string &name : names)
    {
        if (unlinkat(directoryFd, name.c_str(), unlinkFlags) != 0)
            return false;
    }
    return true;
}

// One pending directory in the explicit-stack removal walk. The mutual
// recursion this replaces (RemoveTreeAt <-> DescendIntoSubdirectories)
// carried the same state in call frames; MISRA 17.2 forbids recursion, so
// the frames are now explicit. Field semantics mirror the recursive
// version exactly: files are unlinked during the frame's first phase, each
// subdirectory is opened and emptied before the frame removes its name,
// and symlink entries are unlinked last.
struct RemoveTreeFrame
{
    int directoryFd = -1;                  // open descriptor for this directory
    std::vector<std::string> files;
    std::vector<std::string> subdirectories;
    std::vector<std::string> symlinks;
    std::string nameInParent;              // unlinkat(AT_REMOVEDIR) name in the parent
    std::size_t nextSubdirectory = 0;      // cursor into subdirectories
    bool enumerated = false;               // entries collected and files unlinked?
    bool ownsFd = false;                   // root fd stays caller-owned
};

// Collects the frame's entries and unlinks its regular files — the first
// phase the recursive RemoveTreeAt performed on entry. The directory
// descriptor itself is only duplicated for readdir (fdopendir would
// consume it) and is never closed here: the frame, or the walk's caller,
// owns it.
bool EnumerateRemovalFrame(RemoveTreeFrame *const frame)
{
    const int ownedFd = dup(frame->directoryFd);
    if (ownedFd < 0)
        return false;
    DIR *const directory = fdopendir(ownedFd);
    if (!directory)
    {
        close(ownedFd);
        return false;
    }
    if (!CollectDirectoryEntries(
            frame->directoryFd,
            directory,
            &frame->files,
            &frame->subdirectories,
            &frame->symlinks))
    {
        closedir(directory);
        return false;
    }
    if (closedir(directory) != 0)
        return false;
    return UnlinkEntriesAt(frame->directoryFd, frame->files, 0);
}

// Opens the frame's next real subdirectory relative to its descriptor with
// O_NOFOLLOW so a substituted link cannot be followed, and pushes an empty
// frame that owns the child descriptor. The push happens before the open
// so an allocation failure cannot strand a descriptor; the frame sets
// ownsFd before the name copy so a throwing copy is still drained. The
// parent removes the child's name only after the child frame completes.
bool DescendToNextChild(
    RemoveTreeFrame *const frame,
    std::deque<RemoveTreeFrame> *const stack)
{
    constexpr int subdirectoryFlags =
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW;
    stack->emplace_back();
    RemoveTreeFrame &child = stack->back();
    const std::string &name =
        frame->subdirectories[frame->nextSubdirectory];
    const int childFd = openat(
        frame->directoryFd, name.c_str(), subdirectoryFlags);
    if (childFd < 0)
    {
        stack->pop_back();
        return false;
    }
    child.directoryFd = childFd;
    child.ownsFd = true;
    child.nameInParent = name;
    ++frame->nextSubdirectory;
    return true;
}

// Pops a completed frame: its symlink entries are already gone (the final
// phase), so closing a child descriptor is what lets the parent remove the
// child's now-empty directory by name. The root frame's descriptor stays
// open — the walk's caller owns it and removes the leaf name itself,
// exactly as the recursive version did.
bool CompleteRemovalFrame(std::deque<RemoveTreeFrame> *const stack)
{
    RemoveTreeFrame completed(std::move(stack->back()));
    stack->pop_back();
    if (!completed.ownsFd)
        return true;
    close(completed.directoryFd);
    RemoveTreeFrame &parent = stack->back();
    return unlinkat(
        parent.directoryFd,
        completed.nameInParent.c_str(),
        AT_REMOVEDIR) == 0;
}

// Empties one real directory opened on directoryFd without following
// symbolic links: removes every real regular file, then descends into
// every real subdirectory (also without following links) so each child is
// empty before its name is removed, then removes the surviving
// symbolic-link entries. Once directoryFd is empty the walk returns with
// the descriptor still open — the caller removes the leaf name itself.
//
// The descent is an explicit stack of RemoveTreeFrame entries instead of
// the previous RemoveTreeAt <-> DescendIntoSubdirectories mutual
// recursion (MISRA 17.2). Every operation, the order between them, and
// every failure rule are unchanged: any failure stops the walk with the
// remaining entries untouched, every descriptor the walk opened is closed
// on the way out, and directoryFd itself is left open for the caller.
bool RemoveTreeAt(const int directoryFd)
{
    std::deque<RemoveTreeFrame> stack;
    bool ok = true;
    try
    {
        stack.emplace_back();
        stack.back().directoryFd = directoryFd;
        while (ok && !stack.empty())
        {
            RemoveTreeFrame &frame = stack.back();
            if (!frame.enumerated)
            {
                frame.enumerated = true;
                ok = EnumerateRemovalFrame(&frame);
            }
            else if (frame.nextSubdirectory < frame.subdirectories.size())
            {
                ok = DescendToNextChild(&frame, &stack);
            }
            else if (
                !UnlinkEntriesAt(frame.directoryFd, frame.symlinks, 0))
            {
                ok = false;
            }
            else
            {
                ok = CompleteRemovalFrame(&stack);
            }
        }
    }
    catch (const std::bad_alloc &)
    {
        ok = false;
    }
    for (RemoveTreeFrame &frame : stack)
    {
        if (frame.ownsFd)
            close(frame.directoryFd);
    }
    return ok;
}
}

bool ParseRemoveTreePath(
    const char *const utf8Path,
    std::vector<std::string> *components)
{
    return utf8Path
        && utf8Path[0] != '\0'
        && IsValidUtf8(utf8Path)
        && SplitSafePath(utf8Path, components)
        && !components->empty();
}

// Opens every component except the leaf, each relative to the previously
// held descriptor and releasing the parent as soon as its child name has
// resolved — the ancestor chain never re-resolves a name from the process
// root and never follows a symbolic link component. On failure the
// caller's *parentFd has already been released by the walk, mirroring the
// original close-as-you-descend contract, so the caller must not close it
// again.
bool OpenAncestorOfLeaf(
    const std::vector<std::string> &components,
    int *parentFd)
{
    for (std::size_t index = 0; index + 1 < components.size(); ++index)
    {
        const int nextFd = openat(
            *parentFd, components[index].c_str(), DirectoryOpenFlags());
        const bool parentClosed = close(*parentFd) == 0;
        if (nextFd < 0 || !parentClosed)
        {
            if (nextFd >= 0)
                close(nextFd);
            return false;
        }
        *parentFd = nextFd;
    }
    return true;
}

bool KISAK_CDECL Sys_FileSystemRemoveTree(const char *const utf8Path)
{
    std::vector<std::string> components;
    if (!ParseRemoveTreePath(utf8Path, &components))
        return false;

    // Open the parent of the leaf handle-relative, refusing any symbolic
    // link or "."-only traversal. Then open the leaf as its own descriptor
    // while keeping the parent open so we can call unlinkat(...,leaf, AT_REMOVEDIR).
    constexpr int parentFlags =
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW;
    int parentFd = open(
        IsEnginePathSeparator(utf8Path[0]) ? "/" : ".", parentFlags);
    if (parentFd < 0)
        return false;
    if (!OpenAncestorOfLeaf(components, &parentFd))
        return false;

    const std::string &leaf = components.back();
    const int leafFd = openat(parentFd, leaf.c_str(), parentFlags);
    if (leafFd < 0)
    {
        close(parentFd);
        return false;
    }
    bool removed = RemoveTreeAt(leafFd);
    if (removed
        && unlinkat(parentFd, leaf.c_str(), AT_REMOVEDIR) != 0)
    {
        removed = false;
    }
    close(leafFd);
    close(parentFd);
    return removed;
}
