#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <qcommon/sys_filesystem.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr std::size_t kMaximumPathComponents = 256;
constexpr std::size_t kMaximumWidePath = 32768;

void ResetOutput(char *const output, const std::size_t outputCapacity)
{
    if (output && outputCapacity != 0)
        output[0] = '\0';
}

bool Utf8ToWide(const char *const input, std::wstring *const output)
{
    if (!input || input[0] == '\0' || !output)
        return false;
    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        input,
        -1,
        nullptr,
        0);
    if (required <= 0)
        return false;
    output->assign(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            input,
            -1,
            output->data(),
            required) != required)
    {
        output->clear();
        return false;
    }
    output->resize(static_cast<std::size_t>(required - 1));
    return true;
}

bool WideToUtf8(
    const wchar_t *const input,
    char *const output,
    const std::size_t outputCapacity)
{
    ResetOutput(output, outputCapacity);
    if (!input || !output || outputCapacity == 0
        || outputCapacity > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    {
        return false;
    }
    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        input,
        -1,
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0
        || static_cast<std::size_t>(required) > outputCapacity)
    {
        return false;
    }
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            input,
            -1,
            output,
            static_cast<int>(outputCapacity),
            nullptr,
            nullptr) != required)
    {
        ResetOutput(output, outputCapacity);
        return false;
    }
    return true;
}

bool WideToUtf8String(
    const wchar_t *const input,
    std::string *const output)
{
    if (!input || !output)
        return false;
    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        input,
        -1,
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0)
        return false;
    std::vector<char> utf8(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            input,
            -1,
            utf8.data(),
            required,
            nullptr,
            nullptr) != required)
    {
        return false;
    }
    output->assign(utf8.data(), static_cast<std::size_t>(required - 1));
    return true;
}

bool HasUnsafeRawComponent(const std::wstring &path)
{
    std::size_t componentCount = 0;
    std::size_t cursor = 0;
    while (cursor < path.size())
    {
        while (cursor < path.size()
            && (path[cursor] == L'\\' || path[cursor] == L'/'))
        {
            ++cursor;
        }
        const std::size_t begin = cursor;
        while (cursor < path.size()
            && path[cursor] != L'\\'
            && path[cursor] != L'/')
        {
            ++cursor;
        }
        if (cursor == begin)
            continue;
        const std::wstring component = path.substr(begin, cursor - begin);
        if (component == L".")
            continue;
        const bool extendedMarker = component == L"?"
            && begin == 2
            && path.size() > 3
            && path[0] == L'\\'
            && path[1] == L'\\'
            && path[3] == L'\\';
        const bool driveDesignator = component.size() == 2
            && ((component[0] >= L'A' && component[0] <= L'Z')
                || (component[0] >= L'a' && component[0] <= L'z'))
            && component[1] == L':'
            && (begin == 0 || begin == 4);
        bool invalidCharacter = false;
        for (const wchar_t character : component)
        {
            invalidCharacter = invalidCharacter
                || character < 0x20
                || character == L'<'
                || character == L'>'
                || character == L'"'
                || character == L'|'
                || character == L'*'
                || (character == L'?' && !extendedMarker)
                || (character == L':' && !driveDesignator);
        }
        const std::size_t extension = component.find(L'.');
        std::wstring baseName = component.substr(0, extension);
        for (wchar_t &character : baseName)
        {
            if (character >= L'a' && character <= L'z')
                character = static_cast<wchar_t>(character - (L'a' - L'A'));
        }
        const bool reservedName =
            baseName == L"CON"
            || baseName == L"PRN"
            || baseName == L"AUX"
            || baseName == L"NUL"
            || baseName == L"CONIN$"
            || baseName == L"CONOUT$"
            || (baseName.size() == 4
                && (baseName.rfind(L"COM", 0) == 0
                    || baseName.rfind(L"LPT", 0) == 0)
                && baseName[3] >= L'1'
                && baseName[3] <= L'9');
        if (component == L".."
            || component.back() == L'.'
            || component.back() == L' '
            || reservedName
            || invalidCharacter
            || ++componentCount > kMaximumPathComponents)
        {
            return true;
        }
    }
    return false;
}

bool GetAbsolutePath(const std::wstring &input, std::wstring *const output)
{
    if (!output)
        return false;
    const DWORD required = GetFullPathNameW(input.c_str(), 0, nullptr, nullptr);
    if (required == 0 || required > kMaximumWidePath)
        return false;
    output->assign(required, L'\0');
    const DWORD length = GetFullPathNameW(
        input.c_str(),
        required,
        output->data(),
        nullptr);
    if (length == 0 || length >= required)
    {
        output->clear();
        return false;
    }
    output->resize(length);
    return true;
}

std::wstring AddExtendedPrefix(const std::wstring &path)
{
    if (path.rfind(L"\\\\?\\", 0) == 0)
        return path;
    if (path.rfind(L"\\\\", 0) == 0)
        return L"\\\\?\\UNC\\" + path.substr(2);
    return L"\\\\?\\" + path;
}

std::size_t ExtendedRootLength(const std::wstring &path)
{
    const auto asciiUpper = [](const wchar_t character) {
        return character >= L'a' && character <= L'z'
            ? static_cast<wchar_t>(character - (L'a' - L'A'))
            : character;
    };
    const bool isExtendedUnc = path.size() >= 8
        && path[0] == L'\\'
        && path[1] == L'\\'
        && path[2] == L'?'
        && path[3] == L'\\'
        && asciiUpper(path[4]) == L'U'
        && asciiUpper(path[5]) == L'N'
        && asciiUpper(path[6]) == L'C'
        && path[7] == L'\\';
    if (isExtendedUnc)
    {
        const std::size_t serverEnd = path.find(L'\\', 8);
        if (serverEnd == std::wstring::npos)
            return 0;
        const std::size_t shareEnd = path.find(L'\\', serverEnd + 1);
        return shareEnd == std::wstring::npos ? path.size() : shareEnd + 1;
    }
    if (path.size() >= 7
        && path.rfind(L"\\\\?\\", 0) == 0
        && path[5] == L':'
        && (path[6] == L'\\' || path[6] == L'/'))
    {
        return 7;
    }
    return 0;
}

bool IsRealDirectoryHandle(const HANDLE handle)
{
    FILE_ATTRIBUTE_TAG_INFO info{};
    return GetFileInformationByHandleEx(
            handle,
            FileAttributeTagInfo,
            &info,
            sizeof(info))
        && (info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0
        && (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

HANDLE OpenHeldDirectory(const std::wstring &path)
{
    const HANDLE handle = CreateFileW(
        path.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;
    if (!IsRealDirectoryHandle(handle))
    {
        CloseHandle(handle);
        return INVALID_HANDLE_VALUE;
    }
    return handle;
}

void CloseHeldDirectories(std::vector<HANDLE> *const handles)
{
    if (!handles)
        return;
    for (const HANDLE handle : *handles)
        CloseHandle(handle);
    handles->clear();
}

bool HoldRealAncestors(
    const std::wstring &extendedPath,
    std::vector<HANDLE> *const handles,
    std::size_t *const leafBegin)
{
    if (!handles || !leafBegin)
        return false;
    const std::size_t rootLength = ExtendedRootLength(extendedPath);
    if (rootLength == 0)
        return false;

    const HANDLE root = OpenHeldDirectory(extendedPath.substr(0, rootLength));
    if (root == INVALID_HANDLE_VALUE)
        return false;
    handles->push_back(root);

    std::size_t cursor = rootLength;
    std::size_t previousBegin = rootLength;
    while (cursor < extendedPath.size())
    {
        while (cursor < extendedPath.size()
            && (extendedPath[cursor] == L'\\' || extendedPath[cursor] == L'/'))
        {
            ++cursor;
        }
        if (cursor == extendedPath.size())
            break;
        const std::size_t begin = cursor;
        while (cursor < extendedPath.size()
            && extendedPath[cursor] != L'\\'
            && extendedPath[cursor] != L'/')
        {
            ++cursor;
        }
        previousBegin = begin;
        if (cursor == extendedPath.size())
            break;

        const HANDLE ancestor = OpenHeldDirectory(extendedPath.substr(0, cursor));
        if (ancestor == INVALID_HANDLE_VALUE)
        {
            CloseHeldDirectories(handles);
            return false;
        }
        handles->push_back(ancestor);
    }
    *leafBegin = previousBegin;
    return true;
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

bool KISAK_CDECL Sys_FileSystemCreateDirectory(const char *const utf8Path)
{
    std::wstring path;
    if (!Utf8ToWide(utf8Path, &path) || HasUnsafeRawComponent(path))
        return false;

    std::wstring absolutePath;
    if (!GetAbsolutePath(path, &absolutePath))
        return false;
    std::wstring extendedPath = AddExtendedPrefix(absolutePath);
    const std::size_t rootLength = ExtendedRootLength(extendedPath);
    if (rootLength == 0)
        return false;
    while (extendedPath.size() > rootLength
        && (extendedPath.back() == L'\\' || extendedPath.back() == L'/'))
    {
        extendedPath.pop_back();
    }

    std::vector<HANDLE> heldDirectories;
    std::size_t leafBegin = 0;
    if (!HoldRealAncestors(extendedPath, &heldDirectories, &leafBegin))
        return false;

    bool created = false;
    if (leafBegin >= extendedPath.size())
    {
        created = true;
    }
    else if (CreateDirectoryW(extendedPath.c_str(), nullptr))
    {
        created = true;
    }
    else if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        const HANDLE existing = OpenHeldDirectory(extendedPath);
        created = existing != INVALID_HANDLE_VALUE;
        if (existing != INVALID_HANDLE_VALUE)
            CloseHandle(existing);
    }

    CloseHeldDirectories(&heldDirectories);
    return created;
}

bool KISAK_CDECL Sys_FileSystemGetCurrentDirectory(
    char *const output,
    const std::size_t outputCapacity)
{
    ResetOutput(output, outputCapacity);
    const DWORD required = GetCurrentDirectoryW(0, nullptr);
    if (required == 0 || required > kMaximumWidePath)
        return false;
    std::vector<wchar_t> path(required, L'\0');
    const DWORD length = GetCurrentDirectoryW(required, path.data());
    if (length == 0 || length >= required)
        return false;
    return WideToUtf8(path.data(), output, outputCapacity);
}

bool KISAK_CDECL Sys_FileSystemGetExecutablePath(
    char *const output,
    const std::size_t outputCapacity)
{
    ResetOutput(output, outputCapacity);
    std::vector<wchar_t> path(260, L'\0');
    while (path.size() <= kMaximumWidePath)
    {
        const DWORD length = GetModuleFileNameW(
            nullptr,
            path.data(),
            static_cast<DWORD>(path.size()));
        if (length == 0)
            return false;
        if (length < path.size() && path[length] == L'\0')
            return WideToUtf8(path.data(), output, outputCapacity);
        if (path.size() == kMaximumWidePath)
            break;
        path.assign(
            (std::min)(path.size() * 2, kMaximumWidePath),
            L'\0');
    }
    return false;
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

    std::wstring path;
    if (!Utf8ToWide(utf8Path, &path) || HasUnsafeRawComponent(path))
        return SysFileSystemListStatus::Error;
    std::wstring absolutePath;
    if (!GetAbsolutePath(path, &absolutePath))
        return SysFileSystemListStatus::Error;
    std::wstring extendedPath = AddExtendedPrefix(absolutePath);

    std::vector<HANDLE> heldAncestors;
    std::size_t leafBegin = 0;
    if (!HoldRealAncestors(extendedPath, &heldAncestors, &leafBegin))
        return SysFileSystemListStatus::Error;
    const HANDLE heldDirectory = OpenHeldDirectory(extendedPath);
    if (heldDirectory == INVALID_HANDLE_VALUE)
    {
        CloseHeldDirectories(&heldAncestors);
        return SysFileSystemListStatus::Error;
    }

    if (!extendedPath.empty()
        && extendedPath.back() != L'\\'
        && extendedPath.back() != L'/')
    {
        extendedPath.push_back(L'\\');
    }
    extendedPath.push_back(L'*');

    WIN32_FIND_DATAW findData{};
    HANDLE findHandle = FindFirstFileExW(
        extendedPath.c_str(),
        FindExInfoBasic,
        &findData,
        FindExSearchNameMatch,
        nullptr,
        0);
    if (findHandle == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        CloseHandle(heldDirectory);
        CloseHeldDirectories(&heldAncestors);
        return error == ERROR_FILE_NOT_FOUND
            ? SysFileSystemListStatus::Complete
            : SysFileSystemListStatus::Error;
    }

    bool truncated = false;
    bool failed = false;
    for (;;)
    {
        const wchar_t *const wideName = findData.cFileName;
        const bool dot = wideName[0] == L'.' && wideName[1] == L'\0';
        const bool dotDot = wideName[0] == L'.'
            && wideName[1] == L'.'
            && wideName[2] == L'\0';
        const DWORD attributes = findData.dwFileAttributes;
        if (!dot
            && !dotDot
            && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
        {
            const bool directory =
                (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            std::string name;
            try
            {
                if (!WideToUtf8String(wideName, &name))
                {
                    failed = true;
                    break;
                }
                const SysFileSystemEntryKind kind = directory
                    ? SysFileSystemEntryKind::Directory
                    : SysFileSystemEntryKind::RegularFile;
                if (!filter || filter(name.c_str(), kind, filterContext))
                {
                    InsertBoundedEntry(
                        SysFileSystemDirectoryEntry{
                            std::move(name), kind},
                        maximumEntries,
                        entries,
                        &truncated);
                }
            }
            catch (const std::bad_alloc &)
            {
                failed = true;
                break;
            }
        }

        if (!FindNextFileW(findHandle, &findData))
        {
            if (GetLastError() != ERROR_NO_MORE_FILES)
                failed = true;
            break;
        }
    }

    if (!FindClose(findHandle))
        failed = true;
    if (!CloseHandle(heldDirectory))
        failed = true;
    CloseHeldDirectories(&heldAncestors);
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
thread_local const char *gRemoveTreeLastStage = "";

void SetRemoveTreeStage(const char *const stage)
{
    gRemoveTreeLastStage = stage ? stage : "";
}

// Recursively removes one real directory opened on heldDirectory. Symbolic
// links (Win32 reparse points) are removed, but never traversed. Real
// regular files are deleted, and real subdirectories are recursed into and
// removed after their contents are gone. depthRemaining bounds the
// recursion so a pathological tree cannot exhaust the stack.
bool RemoveHeldTree(
    const HANDLE heldDirectory,
    const std::size_t depthRemaining)
{
    if (depthRemaining == 0)
    {
        SetRemoveTreeStage("depth-exceeded");
        return false;
    }
    WIN32_FIND_DATAW findData{};
    std::wstring search;
    const DWORD nameLength = GetFinalPathNameByHandleW(
        heldDirectory, nullptr, 0, FILE_NAME_NORMALIZED);
    if (nameLength == 0)
    {
        SetRemoveTreeStage("resolve-handle");
        return false;
    }
    search.assign(nameLength, L'\0');
    const DWORD written = GetFinalPathNameByHandleW(
        heldDirectory, search.data(), nameLength, FILE_NAME_NORMALIZED);
    if (written == 0 || written >= nameLength)
    {
        SetRemoveTreeStage("resolve-handle");
        return false;
    }
    search.resize(written);
    if (search.rfind(L"\\\\?\\", 0) != 0)
    {
        if (search.rfind(L"\\\\", 0) == 0)
            search = L"\\\\?\\UNC\\" + search.substr(2);
        else
            search = L"\\\\?\\" + search;
    }
    if (!search.empty() && search.back() != L'\\' && search.back() != L'/')
        search.push_back(L'\\');
    search.push_back(L'*');

    HANDLE findHandle = FindFirstFileExW(
        search.c_str(),
        FindExInfoBasic,
        &findData,
        FindExSearchNameMatch,
        nullptr,
        0);
    if (findHandle == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND)
            return true;
        SetRemoveTreeStage("enumerate");
        return false;
    }

    std::vector<std::wstring> subdirectories;
    std::vector<std::wstring> files;
    std::vector<std::wstring> reparseNames;
    bool failed = false;
    for (;;)
    {
        const wchar_t *const wideName = findData.cFileName;
        const bool dot = wideName[0] == L'.' && wideName[1] == L'\0';
        const bool dotDot = wideName[0] == L'.'
            && wideName[1] == L'.'
            && wideName[2] == L'\0';
        const DWORD attributes = findData.dwFileAttributes;
        if (!dot && !dotDot)
        {
            // Reparse points are not traversed. File symbolic links and
            // directory junctions are removed as themselves via DeleteFileW /
            // RemoveDirectoryW without ever opening their target.
            if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                try
                {
                    reparseNames.emplace_back(wideName);
                }
                catch (const std::bad_alloc &)
                {
                    failed = true;
                    break;
                }
            }
            else if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                try
                {
                    subdirectories.emplace_back(wideName);
                }
                catch (const std::bad_alloc &)
                {
                    failed = true;
                    break;
                }
            }
            else
            {
                try
                {
                    files.emplace_back(wideName);
                }
                catch (const std::bad_alloc &)
                {
                    failed = true;
                    break;
                }
            }
        }

        if (!FindNextFileW(findHandle, &findData))
        {
            if (GetLastError() != ERROR_NO_MORE_FILES)
            {
                SetRemoveTreeStage("enumerate-next");
                failed = true;
            }
            break;
        }
    }
    if (!FindClose(findHandle))
    {
        SetRemoveTreeStage("close-find");
        failed = true;
    }
    if (failed)
        return false;

    const std::wstring directoryPath = search.substr(0, search.size() - 1);

    for (const std::wstring &file : files)
    {
        const std::wstring filePath = directoryPath + file;
        if (!DeleteFileW(filePath.c_str()))
        {
            SetRemoveTreeStage("delete-file");
            return false;
        }
    }

    for (const std::wstring &subdirectory : subdirectories)
    {
        const std::wstring subdirectoryPath = directoryPath + subdirectory;
        const HANDLE childDirectory = OpenHeldDirectory(subdirectoryPath);
        if (childDirectory == INVALID_HANDLE_VALUE)
        {
            SetRemoveTreeStage("open-subdirectory");
            return false;
        }
        const bool recursed =
            RemoveHeldTree(childDirectory, depthRemaining - 1);
        CloseHandle(childDirectory);
        if (!recursed)
            return false;
        if (!RemoveDirectoryW(subdirectoryPath.c_str()))
        {
            SetRemoveTreeStage("remove-directory");
            return false;
        }
    }

    for (const std::wstring &reparseName : reparseNames)
    {
        const std::wstring reparsePath = directoryPath + reparseName;
        // Symbolic-link files delete via DeleteFileW regardless of target.
        // Junctions and directory symlinks delete via RemoveDirectoryW with
        // FILE_FLAG_OPEN_REPARSE_POINT semantics implied; RemoveDirectoryW
        // works for both. If we cannot tell the reparse kind, prefer the
        // file deletion first and fall back to the directory delete.
        if (!DeleteFileW(reparsePath.c_str())
            && (!RemoveDirectoryW(reparsePath.c_str())))
        {
            SetRemoveTreeStage("delete-reparse");
            return false;
        }
    }
    return true;
}
}

bool KISAK_CDECL Sys_FileSystemRemoveTree(const char *const utf8Path)
{
    SetRemoveTreeStage("");
    std::wstring path;
    if (!Utf8ToWide(utf8Path, &path) || HasUnsafeRawComponent(path))
    {
        SetRemoveTreeStage("invalid-arguments");
        return false;
    }
    std::wstring absolutePath;
    if (!GetAbsolutePath(path, &absolutePath))
    {
        SetRemoveTreeStage("resolve-absolute");
        return false;
    }
    std::wstring extendedPath = AddExtendedPrefix(absolutePath);
    while (extendedPath.size() > 0
        && (extendedPath.back() == L'\\' || extendedPath.back() == L'/'))
    {
        extendedPath.pop_back();
    }

    // Open the leaf as a handle-relative directory, refusing reparse points
    // and symbolic links. The held handle is owned by us and stays alive
    // through the recursion so a racing rename cannot redirect the tree.
    const HANDLE held = OpenHeldDirectory(extendedPath);
    if (held == INVALID_HANDLE_VALUE)
    {
        SetRemoveTreeStage("open-leaf");
        return false;
    }

    const bool removed = RemoveHeldTree(held, kSysFileSystemMaximumRecursionDepth);
    CloseHandle(held);
    if (!removed)
        return false;
    if (!RemoveDirectoryW(extendedPath.c_str()))
    {
        SetRemoveTreeStage("remove-directory");
        return false;
    }
    SetRemoveTreeStage("");
    return true;
}

const char *Sys_FileSystemRemoveTreeLastStage()
{
    return gRemoveTreeLastStage;
}
