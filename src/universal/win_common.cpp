#include <qcommon/sys_filesystem.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <direct.h>
#include <io.h>
#endif
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
#include "com_memory.h"

// *(_DWORD *)(*(_DWORD *)(*((_DWORD *)NtCurrentTeb()->ThreadLocalStoragePointer + _tls_index) + 4)

void __cdecl Sys_Mkdir(const char *path)
{
    (void)Sys_FileSystemCreateDirectory(path);
}

bool __cdecl Sys_RemoveDirTree(const char *path)
{
    if (!path || !path[0])
        return false;

#if defined(_WIN32)
    bool v2; // [esp+8h] [ebp-250h]
    std::intptr_t handle; // [esp+1Ch] [ebp-23Ch]
    char childPath[256]; // [esp+20h] [ebp-238h] BYREF
    _finddata64i32_t find; // [esp+120h] [ebp-138h] BYREF
    bool hasError; // [esp+252h] [ebp-6h]
    bool hasTrailingSeparater; // [esp+253h] [ebp-5h]
    int length; // [esp+254h] [ebp-4h]

    length = strlen(path);
    v2 = path[length - 1] == 92 || path[length - 1] == 47;
    hasTrailingSeparater = v2;
    if (v2)
        Com_sprintf(childPath, 0x100u, "%s*", path);
    else
        Com_sprintf(childPath, 0x100u, "%s\\*", path);
    handle = _findfirst64i32(childPath, &find);
    if (handle == -1)
        return _rmdir(path) != -1;
    hasError = 0;
    do
    {
        if (find.name[0] != 46 || find.name[1] && (find.name[1] != 46 || find.name[2]))
        {
            if (hasTrailingSeparater)
                Com_sprintf(childPath, 0x100u, "%s%s", path, find.name);
            else
                Com_sprintf(childPath, 0x100u, "%s\\%s", path, find.name);
            if ((find.attrib & 0x10) != 0)
                hasError = !Sys_RemoveDirTree(childPath);
            else
                hasError = remove(childPath) == -1;
        }
    } while (!hasError && _findnext64i32(handle, &find) != -1);
    _findclose(handle);
    return !hasError && _rmdir(path) != -1;
#else
    // Recursive deletion remains a separate handle-relative platform-service
    // task. Do not emulate it with path-following POSIX traversal.
    return false;
#endif
}

namespace
{
constexpr std::size_t kMaximumListedFiles = 0x1fff;
constexpr std::size_t kMaximumFilteredTraversalEntries =
    kMaximumListedFiles * 8;
constexpr std::size_t kMaximumFilteredDepth = 256;

std::string JoinPath(
    const std::string &left,
    const std::string &right)
{
    if (left.empty())
        return right;
    if (right.empty())
        return left;
    if (left.back() == '/' || left.back() == '\\')
        return left + right;
#if defined(_WIN32)
    return left + "\\" + right;
#else
    return left + "/" + right;
#endif
}

std::string JoinRelativePath(
    const std::string &left,
    const std::string &right)
{
    if (left.empty())
        return right;
#if defined(_WIN32)
    return left + "\\" + right;
#else
    return left + "/" + right;
#endif
}

bool IsExcludedDirectory(
    const char *const name,
    const SysFileSystemEntryKind kind)
{
    return kind == SysFileSystemEntryKind::Directory
        && Sys_FileSystemEnginePathsEqual(name, "CVS");
}

bool IsExcludedDirectory(const SysFileSystemDirectoryEntry &entry)
{
    return IsExcludedDirectory(entry.name.c_str(), entry.kind);
}

struct DirectListFilterContext
{
    const char *extension;
    bool wantDirectories;
};

bool KISAK_CDECL SelectDirectListEntry(
    const char *const name,
    const SysFileSystemEntryKind kind,
    const void *const opaqueContext)
{
    const auto *const context =
        static_cast<const DirectListFilterContext *>(opaqueContext);
    if (!name || !context)
        return false;
    const bool isDirectory = kind == SysFileSystemEntryKind::Directory;
    return isDirectory == context->wantDirectories
        && !IsExcludedDirectory(name, kind)
        && (context->extension[0] == '\0'
            || Sys_FileSystemHasExtension(name, context->extension));
}

char *CopyHunkString(
    HunkUser *const user,
    const std::string &value)
{
    if (!user
        || value.size() >= (std::numeric_limits<std::uint32_t>::max)())
    {
        return nullptr;
    }
    const std::size_t byteCount = value.size() + 1;
    char *const copy = static_cast<char *>(Hunk_UserAlloc(
        user,
        static_cast<std::uint32_t>(byteCount),
        alignof(char)));
    std::memcpy(copy, value.c_str(), byteCount);
    return copy;
}

bool ListFilteredFilesRecursive(
    HunkUser *const user,
    const std::string &baseDirectory,
    const std::string &subdirectories,
    const char *const filter,
    char **const list,
    int *const numberOfFiles,
    std::size_t *const visitedEntries,
    const std::size_t depth)
{
    if (!user
        || !filter
        || !list
        || !numberOfFiles
        || !visitedEntries
        || *numberOfFiles < 0)
    {
        return false;
    }
    if (static_cast<std::size_t>(*numberOfFiles) >= kMaximumListedFiles)
        return false;
    if (*visitedEntries >= kMaximumFilteredTraversalEntries
        || depth > kMaximumFilteredDepth)
        return false;

    std::vector<SysFileSystemDirectoryEntry> entries;
    const std::size_t remainingTraversal =
        kMaximumFilteredTraversalEntries - *visitedEntries;
    const SysFileSystemListStatus status = Sys_FileSystemListDirectory(
        JoinPath(baseDirectory, subdirectories).c_str(),
        remainingTraversal,
        &entries);
    if (status == SysFileSystemListStatus::Error)
        return false;
    bool complete = status == SysFileSystemListStatus::Complete;

    for (const SysFileSystemDirectoryEntry &entry : entries)
    {
        if (*visitedEntries == kMaximumFilteredTraversalEntries
            || static_cast<std::size_t>(*numberOfFiles)
                == kMaximumListedFiles)
        {
            return false;
        }
        ++*visitedEntries;
        if (IsExcludedDirectory(entry))
            continue;

        const std::string relativePath =
            JoinRelativePath(subdirectories, entry.name);
        if (entry.kind == SysFileSystemEntryKind::Directory)
        {
            const bool childComplete = ListFilteredFilesRecursive(
                user,
                baseDirectory,
                relativePath,
                filter,
                list,
                numberOfFiles,
                visitedEntries,
                depth + 1);
            complete = childComplete && complete;
        }
        if (static_cast<std::size_t>(*numberOfFiles)
            == kMaximumListedFiles)
        {
            return false;
        }
        if (Sys_FileSystemMatchesPathFilter(
                filter, relativePath.c_str()))
        {
            char *const copy = CopyHunkString(user, relativePath);
            if (!copy)
                return false;
            list[(*numberOfFiles)++] = copy;
        }
    }
    return complete;
}

char **FinalizeFileList(
    HunkUser *const user,
    char **const temporaryList,
    const int numberOfFiles)
{
    if (!user || !temporaryList || numberOfFiles <= 0)
    {
        if (user)
            Hunk_UserDestroy(user);
        return nullptr;
    }

    const std::size_t pointerCount =
        static_cast<std::size_t>(numberOfFiles) + 2;
    if (pointerCount
        > (std::numeric_limits<std::uint32_t>::max)() / sizeof(char *))
    {
        Hunk_UserDestroy(user);
        return nullptr;
    }
    const std::size_t byteCount = pointerCount * sizeof(char *);
    char **const allocation = static_cast<char **>(Hunk_UserAlloc(
        user,
        static_cast<std::uint32_t>(byteCount),
        alignof(char *)));
    allocation[0] = reinterpret_cast<char *>(user);
    char **const result = allocation + 1;
    for (int index = 0; index < numberOfFiles; ++index)
        result[index] = temporaryList[index];
    result[numberOfFiles] = nullptr;
    return result;
}
}

void __cdecl Sys_ListFilteredFiles(
    HunkUser *const user,
    const char *const basedir,
    const char *const subdirs,
    const char *const filter,
    char **const list,
    int *const numfiles)
{
    if (!user || !basedir || !filter || !list || !numfiles)
        return;
    std::size_t visitedEntries = 0;
    const bool complete = ListFilteredFilesRecursive(
        user,
        basedir,
        subdirs ? subdirs : "",
        filter,
        list,
        numfiles,
        &visitedEntries,
        0);
    if (!complete)
    {
        std::fprintf(
            stderr,
            "WARNING: Sys_ListFilteredFiles traversal incomplete for '%s' "
            "(directory error, depth limit, or %zu-entry safety cap)\n",
            basedir,
            kMaximumFilteredTraversalEntries);
    }
}

bool __cdecl HasFileExtension(const char *name, const char *extension)
{
    return Sys_FileSystemHasExtension(name, extension);
}

int __cdecl Sys_CountFileList(char **list)
{
    int i; // [esp+0h] [ebp-4h]

    i = 0;
    if (list)
    {
        while (*list)
        {
            ++list;
            ++i;
        }
    }
    return i;
}

char **__cdecl Sys_ListFiles(
    const char *directory,
    const char *extension,
    const char *filter,
    int *numfiles,
    int wantsubs)
{
    if (!numfiles)
        return nullptr;
    *numfiles = 0;
    if (!directory || directory[0] == '\0')
        return nullptr;

    std::vector<char *> temporaryList(kMaximumListedFiles + 1, nullptr);
    HunkUser *const user =
        Hunk_UserCreate(0x20000, "Sys_ListFiles", 0, 0, 3);
    int nfiles = 0;
    if (filter)
    {
        Sys_ListFilteredFiles(
            user,
            directory,
            "",
            filter,
            temporaryList.data(),
            &nfiles);
        *numfiles = nfiles;
        return FinalizeFileList(user, temporaryList.data(), nfiles);
    }

    if (!extension)
        extension = "";
    const bool extensionRequestsDirectories =
        extension[0] == '/' && extension[1] == '\0';
    if (extensionRequestsDirectories)
        extension = "";
    const bool wantDirectories = wantsubs || extensionRequestsDirectories;

    std::vector<SysFileSystemDirectoryEntry> entries;
    const DirectListFilterContext directFilter{extension, wantDirectories};
    const SysFileSystemListStatus status =
        Sys_FileSystemListDirectoryFiltered(
        directory,
        kMaximumListedFiles,
        SelectDirectListEntry,
        &directFilter,
        &entries);
    if (status == SysFileSystemListStatus::Error)
    {
        Hunk_UserDestroy(user);
        return nullptr;
    }
    if (status == SysFileSystemListStatus::Truncated)
    {
        std::fprintf(
            stderr,
            "WARNING: Sys_ListFiles limited '%s' to %zu eligible entries\n",
            directory,
            kMaximumListedFiles);
    }

    for (const SysFileSystemDirectoryEntry &entry : entries)
    {
        if (static_cast<std::size_t>(nfiles) == kMaximumListedFiles)
            break;
        char *const copy = CopyHunkString(user, entry.name);
        if (!copy)
            break;
        temporaryList[nfiles++] = copy;
    }
    *numfiles = nfiles;
    return FinalizeFileList(user, temporaryList.data(), nfiles);
}


namespace
{
using PathQuery = bool (KISAK_CDECL *)(char *, std::size_t);

bool ReadDynamicPath(const PathQuery query, std::vector<char> *const output)
{
    if (!query || !output)
        return false;
    for (std::size_t capacity = 256; capacity <= 1024 * 1024; capacity *= 2)
    {
        output->assign(capacity, '\0');
        if (query(output->data(), output->size()))
        {
            output->resize(strlen(output->data()) + 1);
            return true;
        }
    }
    output->assign(1, '\0');
    return false;
}

}

char *__cdecl Sys_Cwd()
{
    thread_local std::vector<char> cwd(1, '\0');
    (void)ReadDynamicPath(Sys_FileSystemGetCurrentDirectory, &cwd);
    return cwd.data();
}

const char *__cdecl Sys_DefaultCDPath()
{
    return "";
}

char *__cdecl Sys_DefaultInstallPath()
{
    static std::vector<char> exePath = [] {
        std::vector<char> path;
#if defined(_WIN32)
        if (IsDebuggerPresent())
        {
            if (!ReadDynamicPath(Sys_FileSystemGetCurrentDirectory, &path))
                path.assign(1, '\0');
        }
        else
#endif
        {
            if (!ReadDynamicPath(Sys_FileSystemGetExecutablePath, &path))
                path.assign(1, '\0');
            const std::string fullPath(path.data());
            const std::size_t parentLength =
                Sys_FileSystemParentPathLength(fullPath.c_str());
            path.assign(fullPath.begin(), fullPath.begin() + parentLength);
            path.push_back('\0');
        }
        return path;
    }();
    return exePath.data();
}
