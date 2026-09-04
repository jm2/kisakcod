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

bool ReadRegularFileHandle(
    const HANDLE file,
    const std::size_t maximumBytes,
    std::vector<unsigned char> *const bytes)
{
    // Opened with FILE_FLAG_OPEN_REPARSE_POINT, so a link or reparse
    // point opened as itself and is detectable here instead of being
    // silently followed.
    FILE_ATTRIBUTE_TAG_INFO tagInfo{};
    if (!GetFileInformationByHandleEx(
            file,
            FileAttributeTagInfo,
            &tagInfo,
            sizeof(tagInfo)))
    {
        return false;
    }
    if ((tagInfo.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0
        || (tagInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0)
        return false;
    const std::uint64_t fileSize64 = static_cast<std::uint64_t>(size.QuadPart);
    if (fileSize64 > static_cast<std::uint64_t>(maximumBytes))
        return false;

    const std::size_t fileSize = static_cast<std::size_t>(fileSize64);
    try
    {
        bytes->assign(fileSize, 0);
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }

    std::size_t total = 0;
    while (total < fileSize)
    {
        // Call std::min inside the cast: written as
        // static_cast<DWORD>((std::min))(...) the arguments hang outside
        // the cast and MSVC tries to convert the overloaded function
        // itself to DWORD (error C2440/C2737).
        const DWORD chunkSize = static_cast<DWORD>((std::min)(
            fileSize - total,
            static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        DWORD bytesRead = 0;
        if (!ReadFile(file, bytes->data() + total, chunkSize, &bytesRead, nullptr)
            || bytesRead == 0)
        {
            // A short read means the file shrank or the handle broke;
            // either way the content would be a torn prefix.
            return false;
        }
        total += bytesRead;
    }
    return true;
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

bool KISAK_CDECL Sys_FileSystemReadFile(
    const char *const utf8Path,
    const std::size_t maximumBytes,
    std::vector<unsigned char> *const contents)
{
    if (contents)
        contents->clear();
    if (!contents || !utf8Path || utf8Path[0] == '\0')
        return false;

    std::wstring path;
    if (!Utf8ToWide(utf8Path, &path) || HasUnsafeRawComponent(path))
        return false;

    std::wstring absolutePath;
    if (!GetAbsolutePath(path, &absolutePath))
        return false;
    std::wstring extendedPath = AddExtendedPrefix(absolutePath);
    if (ExtendedRootLength(extendedPath) == 0)
        return false;
    while (!extendedPath.empty()
        && (extendedPath.back() == L'\\' || extendedPath.back() == L'/'))
    {
        extendedPath.pop_back();
    }
    if (extendedPath.empty())
        return false;

    // Validate every directory ancestor as a real directory, rejecting
    // reparse points, before opening the leaf.
    std::vector<HANDLE> heldDirectories;
    std::size_t leafBegin = 0;
    if (!HoldRealAncestors(extendedPath, &heldDirectories, &leafBegin))
        return false;

    bool ok = false;
    std::vector<unsigned char> bytes;
    if (leafBegin < extendedPath.size())
    {
        const HANDLE file = CreateFileW(
            extendedPath.c_str(),
            FILE_GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT,
            nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            ok = ReadRegularFileHandle(file, maximumBytes, &bytes);
            if (!CloseHandle(file))
                ok = false;
        }
    }

    CloseHeldDirectories(&heldDirectories);
    if (!ok)
        return false;

    contents->swap(bytes);
    return true;
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

// The Windows SDK gained POSIX-semantics deletion in 10.0.1709; the guards
// keep alternative SDKs that predate it compiling with the fallback path
// intact.
#ifndef FILE_DISPOSITION_INFO_EX
typedef struct _FILE_DISPOSITION_INFO_EX
{
    DWORD FileDispositionFlags;
} FILE_DISPOSITION_INFO_EX;
#endif
#ifndef FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE
#define FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE 0x00000001
#endif
#ifndef FILE_DISPOSITION_FLAG_POSIX_SEMANTICS
#define FILE_DISPOSITION_FLAG_POSIX_SEMANTICS 0x00000002
#endif

namespace
{
// ---------------------------------------------------------------------------
// NT runtime surface used for handle-relative recursive deletion.
//
// The removal service must keep parent-handle identity through every descent
// and must never convert a held handle back into a pathname: a pathname is
// resolved from the process root on every use, so a racing rename or reparse
// substitution could redirect the deletion outside the intended tree. The NT
// APIs accept a RootDirectory object-attribute that pins name resolution to
// a held parent handle, and NtQueryDirectoryFile enumerates a handle
// directly. They are resolved from ntdll at run time so this translation
// unit stays free of winternl.h and of a hard ntdll link dependency.
// ---------------------------------------------------------------------------

using KisakNtStatus = std::int32_t;

constexpr KisakNtStatus kKisakStatusSuccess = 0;
constexpr KisakNtStatus kKisakStatusNoMoreFiles =
    static_cast<KisakNtStatus>(0x80000006u);
constexpr KisakNtStatus kKisakStatusNoMoreEntries =
    static_cast<KisakNtStatus>(0x8000001Bu);

// DesiredAccess values (the subset used here).
constexpr std::uint32_t kKisakFileListDirectory = 0x00000001u;
constexpr std::uint32_t kKisakFileReadAttributes = 0x00000080u;
constexpr std::uint32_t kKisakDelete = 0x00010000u;
constexpr std::uint32_t kKisakSynchronize = 0x00100000u;

// ShareAccess: full sharing on every open. Sharing conflicts surface at
// disposition time instead of blocking the open, which keeps failure
// reporting in one deterministic place.
constexpr std::uint32_t kKisakFileShareAll = 0x00000007u;

// CreateDisposition.
constexpr std::uint32_t kKisakFileOpen = 0x00000001u;

// CreateOptions.
constexpr std::uint32_t kKisakFileDirectoryFile = 0x00000001u;
constexpr std::uint32_t kKisakFileSynchronousIoNonAlert = 0x00000020u;
constexpr std::uint32_t kKisakFileNonDirectoryFile = 0x00000040u;
constexpr std::uint32_t kKisakFileOpenReparsePoint = 0x00200000u;

// OBJECT_ATTRIBUTES Attributes.
constexpr std::uint32_t kKisakObjCaseInsensitive = 0x00000040u;

// FileInformationClass.
constexpr std::uint32_t kKisakFileDirectoryInformation = 1u;

struct KisakUnicodeString
{
    std::uint16_t Length;
    std::uint16_t MaximumLength;
    wchar_t *Buffer;
};

struct KisakIoStatusBlock
{
    union
    {
        KisakNtStatus Status;
        void *Pointer;
    };
    std::uintptr_t Information;
};

struct KisakObjectAttributes
{
    std::uint32_t Length;
    void *RootDirectory;
    KisakUnicodeString *ObjectName;
    std::uint32_t Attributes;
    void *SecurityDescriptor;
    void *SecurityQualityOfService;
};

struct KisakFileDirectoryInformation
{
    std::uint32_t NextEntryOffset;
    std::uint32_t FileIndex;
    std::int64_t CreationTime;
    std::int64_t LastAccessTime;
    std::int64_t LastWriteTime;
    std::int64_t ChangeTime;
    std::int64_t EndOfFile;
    std::int64_t AllocationSize;
    std::uint32_t FileAttributes;
    std::uint32_t FileNameLength;
    wchar_t FileName[1];
};

// Layout pins for the NT ABI mirrors above. Several members are never
// dereferenced, but they must exist at their documented offsets so the
// members that follow them land where the kernel expects; these
// assertions turn any layout drift into a compile-time failure.
static_assert(
    offsetof(KisakIoStatusBlock, Status) == 0
        && offsetof(KisakIoStatusBlock, Pointer) == 0,
    "IO_STATUS_BLOCK union members must share offset 0");
static_assert(
    offsetof(KisakIoStatusBlock, Information) == sizeof(void *),
    "IO_STATUS_BLOCK Information must follow the Status/Pointer union");
static_assert(
    offsetof(KisakObjectAttributes, SecurityQualityOfService)
            - offsetof(KisakObjectAttributes, SecurityDescriptor)
        == sizeof(void *),
    "OBJECT_ATTRIBUTES security members must be pointer-sized apart");
static_assert(
    offsetof(KisakFileDirectoryInformation, CreationTime)
            - offsetof(KisakFileDirectoryInformation, FileIndex)
        == sizeof(KisakFileDirectoryInformation::NextEntryOffset),
    "FILE_DIRECTORY_INFORMATION timestamps must follow FileIndex");
static_assert(
    offsetof(KisakFileDirectoryInformation, LastAccessTime)
            - offsetof(KisakFileDirectoryInformation, CreationTime)
        == sizeof(std::int64_t)
        && offsetof(KisakFileDirectoryInformation, LastWriteTime)
                - offsetof(KisakFileDirectoryInformation, LastAccessTime)
            == sizeof(std::int64_t)
        && offsetof(KisakFileDirectoryInformation, ChangeTime)
                - offsetof(KisakFileDirectoryInformation, LastWriteTime)
            == sizeof(std::int64_t)
        && offsetof(KisakFileDirectoryInformation, EndOfFile)
                - offsetof(KisakFileDirectoryInformation, ChangeTime)
            == sizeof(std::int64_t)
        && offsetof(KisakFileDirectoryInformation, AllocationSize)
                - offsetof(KisakFileDirectoryInformation, EndOfFile)
            == sizeof(std::int64_t),
    "FILE_DIRECTORY_INFORMATION timestamp/size chain must be contiguous");
static_assert(
    offsetof(KisakFileDirectoryInformation, FileName)
            - offsetof(KisakFileDirectoryInformation, FileAttributes)
        == 2u * sizeof(std::uint32_t),
    "FILE_DIRECTORY_INFORMATION FileName must follow the attribute fields");

using KisakNtCreateFileFn = KisakNtStatus (__stdcall *)(
    HANDLE *fileHandle,
    std::uint32_t desiredAccess,
    KisakObjectAttributes *objectAttributes,
    KisakIoStatusBlock *ioStatusBlock,
    std::int64_t *allocationSize,
    std::uint32_t fileAttributes,
    std::uint32_t shareAccess,
    std::uint32_t createDisposition,
    std::uint32_t createOptions,
    void *eaBuffer,
    std::uint32_t eaLength);

using KisakNtQueryDirectoryFileFn = KisakNtStatus (__stdcall *)(
    HANDLE fileHandle,
    HANDLE event,
    void *apcRoutine,
    void *apcContext,
    KisakIoStatusBlock *ioStatusBlock,
    void *fileInformation,
    std::uint32_t length,
    std::uint32_t fileInformationClass,
    std::uint32_t returnSingleEntry,
    KisakUnicodeString *fileName,
    std::uint32_t restartScan);

struct KisakNtProcedures
{
    KisakNtCreateFileFn createFile;
    KisakNtQueryDirectoryFileFn queryDirectoryFile;
};

const KisakNtProcedures *NtProcedures()
{
    static const KisakNtProcedures procedures = [] {
        KisakNtProcedures resolved{};
        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll != nullptr)
        {
            resolved.createFile =
                reinterpret_cast<KisakNtCreateFileFn>(reinterpret_cast<void *>(
                    GetProcAddress(ntdll, "NtCreateFile")));
            resolved.queryDirectoryFile =
                reinterpret_cast<KisakNtQueryDirectoryFileFn>(
                    reinterpret_cast<void *>(
                        GetProcAddress(ntdll, "NtQueryDirectoryFile")));
        }
        return resolved;
    }();
    return &procedures;
}

// Opens one child of a held parent by name. Name resolution happens against
// the parent handle, so whatever object answers is inside the subtree the
// parent anchors. FILE_OPEN_REPARSE_POINT keeps reparse points untraversed
// for every classification: a junction or symbolic link opens as itself and
// is later verified and deleted as itself.
HANDLE OpenChildRelativeToParent(
    const HANDLE parent,
    const wchar_t *const name,
    const std::size_t nameLength,
    const std::uint32_t desiredAccess,
    const std::uint32_t createOptions)
{
    const KisakNtProcedures *const nt = NtProcedures();
    if (!nt->createFile)
        return INVALID_HANDLE_VALUE;

    // A single component this long cannot exist on NTFS; refuse up front.
    if (nameLength == 0 || nameLength > 32767)
        return INVALID_HANDLE_VALUE;

    KisakUnicodeString unicodeName{};
    unicodeName.Length =
        static_cast<std::uint16_t>(nameLength * sizeof(wchar_t));
    unicodeName.MaximumLength = unicodeName.Length;
    unicodeName.Buffer = const_cast<wchar_t *>(name);

    KisakObjectAttributes attributes{};
    attributes.Length = sizeof(attributes);
    attributes.RootDirectory = parent;
    attributes.ObjectName = &unicodeName;
    attributes.Attributes = kKisakObjCaseInsensitive;

    HANDLE child = INVALID_HANDLE_VALUE;
    KisakIoStatusBlock ioStatus{};
    const KisakNtStatus status = nt->createFile(
        &child,
        desiredAccess,
        &attributes,
        &ioStatus,
        nullptr,
        FILE_ATTRIBUTE_NORMAL,
        kKisakFileShareAll,
        kKisakFileOpen,
        createOptions,
        nullptr,
        0);
    if (status != kKisakStatusSuccess
        || child == INVALID_HANDLE_VALUE
        || child == nullptr)
    {
        if (child != INVALID_HANDLE_VALUE && child != nullptr)
            CloseHandle(child);
        return INVALID_HANDLE_VALUE;
    }
    return child;
}

// Verifies an already-open handle is what the enumeration said it was. Every
// open happens with FILE_OPEN_REPARSE_POINT, so reparse points answer with
// their tag set and real objects answer with their own attributes. A
// mismatch means the name changed hands between enumeration and open — the
// deterministic signature of a rename/reparse substitution race — and the
// operation fails instead of deleting the wrong object.
bool VerifyHandleKind(
    const HANDLE handle,
    const bool expectedReparse,
    const bool expectedDirectory)
{
    FILE_ATTRIBUTE_TAG_INFO info{};
    if (!GetFileInformationByHandleEx(
            handle,
            FileAttributeTagInfo,
            &info,
            sizeof(info)))
    {
        return false;
    }
    const bool isReparse =
        (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    if (isReparse != expectedReparse)
        return false;
    if (expectedReparse)
        return true;
    return ((info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        == expectedDirectory;
}

// Marks an open object for deletion. POSIX semantics is preferred: the name
// disappears once our handle closes even if unrelated handles exist, and an
// incompatible existing handle fails the disposition immediately instead of
// deferring a surprise. Filesystems without POSIX-semantics support fall
// back to plain delete disposition.
bool SetDeletionDisposition(const HANDLE handle)
{
    FILE_DISPOSITION_INFO_EX dispositionEx{};
    dispositionEx.FileDispositionFlags =
        FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE
        | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS;
    if (SetFileInformationByHandle(
            handle,
            FileDispositionInfoEx,
            &dispositionEx,
            sizeof(dispositionEx)))
    {
        return true;
    }

    const DWORD exError = GetLastError();
    if (exError != ERROR_INVALID_PARAMETER
        && exError != ERROR_CALL_NOT_IMPLEMENTED
        && exError != ERROR_NOT_SUPPORTED)
    {
        return false;
    }

    FILE_DISPOSITION_INFO disposition{};
    disposition.DeleteFile = TRUE;
    return SetFileInformationByHandle(
        handle,
        FileDispositionInfo,
        &disposition,
        sizeof(disposition)) != 0;
}

bool RemoveHeldTree(const HANDLE heldDirectory)
{
    // Enumerate one real directory by handle. Names are collected with the
    // classification the enumeration reported; afterwards every name is
    // re-opened relative to heldDirectory and every reopened object is
    // verified against that classification before any deletion happens.
    // 64KiB dwarfs the largest legal NTFS directory entry.
    std::vector<std::uint64_t> enumerationBuffer(8192u);
    void *const buffer = enumerationBuffer.data();
    const std::uint32_t bufferBytes = static_cast<std::uint32_t>(
        enumerationBuffer.size() * sizeof(std::uint64_t));

    const KisakNtProcedures *const nt = NtProcedures();
    if (!nt->queryDirectoryFile)
        return false;

    std::vector<std::wstring> files;
    std::vector<std::wstring> directories;
    std::vector<std::wstring> reparseChildren;
    bool failed = false;
    bool restartScan = true;
    for (;;)
    {
        KisakIoStatusBlock ioStatus{};
        const KisakNtStatus status = nt->queryDirectoryFile(
            heldDirectory,
            nullptr,
            nullptr,
            nullptr,
            &ioStatus,
            buffer,
            bufferBytes,
            kKisakFileDirectoryInformation,
            0u,
            nullptr,
            restartScan ? 1u : 0u);
        restartScan = false;
        if (status == kKisakStatusNoMoreFiles
            || status == kKisakStatusNoMoreEntries)
        {
            break;
        }
        if (status != kKisakStatusSuccess)
        {
            failed = true;
            break;
        }

        std::uint32_t offset = 0;
        for (;;)
        {
            const auto *const entry =
                reinterpret_cast<const KisakFileDirectoryInformation *>(
                    static_cast<const unsigned char *>(buffer) + offset);
            const std::size_t nameCharacters =
                entry->FileNameLength / sizeof(wchar_t);
            const bool dot =
                nameCharacters == 1 && entry->FileName[0] == L'.';
            const bool dotDot = nameCharacters == 2
                && entry->FileName[0] == L'.'
                && entry->FileName[1] == L'.';
            if (!dot && !dotDot)
            {
                try
                {
                    if ((entry->FileAttributes
                            & FILE_ATTRIBUTE_REPARSE_POINT)
                        != 0)
                    {
                        reparseChildren.emplace_back(
                            entry->FileName,
                            nameCharacters);
                    }
                    else if (
                        (entry->FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                        != 0)
                    {
                        directories.emplace_back(
                            entry->FileName,
                            nameCharacters);
                    }
                    else
                    {
                        files.emplace_back(entry->FileName, nameCharacters);
                    }
                }
                catch (const std::bad_alloc &)
                {
                    failed = true;
                }
                if (failed)
                    break;
            }

            offset = entry->NextEntryOffset;
            if (offset == 0)
                break;
        }
        if (failed)
            break;
    }
    if (failed)
        return false;

    // Enumeration is done; heldDirectory stays open as the deletion anchor.
    // Real files: open by name relative to the anchor, verify the object
    // still matches the enumeration, mark for deletion, close.
    constexpr std::uint32_t fileAccess =
        kKisakDelete | kKisakFileReadAttributes | kKisakSynchronize;
    constexpr std::uint32_t fileOptions =
        kKisakFileNonDirectoryFile
        | kKisakFileOpenReparsePoint
        | kKisakFileSynchronousIoNonAlert;
    for (const std::wstring &name : files)
    {
        const HANDLE child = OpenChildRelativeToParent(
            heldDirectory, name.c_str(), name.size(), fileAccess, fileOptions);
        if (child == INVALID_HANDLE_VALUE)
            return false;
        const bool verified = VerifyHandleKind(child, false, false);
        const bool marked = verified && SetDeletionDisposition(child);
        CloseHandle(child);
        if (!marked)
            return false;
    }

    // Reparse points: open the link itself (either file or directory
    // shape), verify it is still a reparse point, and delete it as itself.
    // The target is never opened, so its contents survive untouched.
    constexpr std::uint32_t reparseOptions =
        kKisakFileOpenReparsePoint | kKisakFileSynchronousIoNonAlert;
    for (const std::wstring &name : reparseChildren)
    {
        const HANDLE child = OpenChildRelativeToParent(
            heldDirectory,
            name.c_str(),
            name.size(),
            fileAccess,
            reparseOptions);
        if (child == INVALID_HANDLE_VALUE)
            return false;
        const bool verified = VerifyHandleKind(child, true, false);
        const bool marked = verified && SetDeletionDisposition(child);
        CloseHandle(child);
        if (!marked)
            return false;
    }

    // Real directories: open by name relative to the anchor, verify, empty
    // the child recursively, then delete the child by its own handle. The
    // child's disposition can only succeed once it is empty, which the
    // recursion guarantees before returning true.
    constexpr std::uint32_t directoryAccess =
        kKisakDelete
        | kKisakFileListDirectory
        | kKisakFileReadAttributes
        | kKisakSynchronize;
    constexpr std::uint32_t directoryOptions =
        kKisakFileDirectoryFile
        | kKisakFileOpenReparsePoint
        | kKisakFileSynchronousIoNonAlert;
    for (const std::wstring &name : directories)
    {
        const HANDLE child = OpenChildRelativeToParent(
            heldDirectory,
            name.c_str(),
            name.size(),
            directoryAccess,
            directoryOptions);
        if (child == INVALID_HANDLE_VALUE)
            return false;
        const bool verified = VerifyHandleKind(child, false, true);
        const bool emptied = verified && RemoveHeldTree(child);
        const bool marked = emptied && SetDeletionDisposition(child);
        CloseHandle(child);
        if (!marked)
            return false;
    }

    // The anchor itself is empty now; delete it by handle. For the leaf
    // call this removes the requested tree root without ever holding a
    // pathname for it; at every other level it removes the recursed
    // subdirectory after its contents are gone.
    return SetDeletionDisposition(heldDirectory);
}
}

bool KISAK_CDECL Sys_FileSystemRemoveTree(const char *const utf8Path)
{
    std::wstring path;
    if (!Utf8ToWide(utf8Path, &path) || HasUnsafeRawComponent(path))
        return false;
    std::wstring absolutePath;
    if (!GetAbsolutePath(path, &absolutePath))
        return false;
    std::wstring extendedPath = AddExtendedPrefix(absolutePath);
    while (!extendedPath.empty()
        && (extendedPath.back() == L'\\' || extendedPath.back() == L'/'))
    {
        extendedPath.pop_back();
    }

    const std::size_t rootLength = ExtendedRootLength(extendedPath);
    if (rootLength == 0 || extendedPath.size() <= rootLength)
        return false;

    // Open the filesystem root (drive or UNC share) by pathname exactly
    // once. A volume root cannot be a reparse point, but the same
    // open-reparse-then-verify discipline is applied so the entry sequence
    // has no exceptions.
    HANDLE held = CreateFileW(
        extendedPath.c_str(),
        FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        kKisakFileShareAll,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (held == INVALID_HANDLE_VALUE)
        return false;
    if (!VerifyHandleKind(held, false, true))
    {
        CloseHandle(held);
        return false;
    }

    // Walk every component — ancestors and leaf — opening each one relative
    // to the previously held handle and releasing the parent as soon as the
    // child is verified. Parent-handle identity is preserved the whole way
    // down: nothing after the root open resolves names from the process
    // root, and FILE_OPEN_REPARSE_POINT plus tag verification refuses any
    // junction or symbolic-link ancestor or leaf before descent.
    constexpr std::uint32_t walkAccess =
        kKisakDelete
        | kKisakFileListDirectory
        | kKisakFileReadAttributes
        | kKisakSynchronize;
    constexpr std::uint32_t walkOptions =
        kKisakFileDirectoryFile
        | kKisakFileOpenReparsePoint
        | kKisakFileSynchronousIoNonAlert;
    const KisakNtProcedures *const nt = NtProcedures();
    bool walked = nt->createFile != nullptr;
    std::size_t cursor = rootLength;
    while (walked && cursor < extendedPath.size())
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

        const HANDLE child = OpenChildRelativeToParent(
            held,
            extendedPath.c_str() + begin,
            cursor - begin,
            walkAccess,
            walkOptions);
        if (child == INVALID_HANDLE_VALUE
            || !VerifyHandleKind(child, false, true))
        {
            if (child != INVALID_HANDLE_VALUE)
                CloseHandle(child);
            walked = false;
            break;
        }
        CloseHandle(held);
        held = child;
    }

    // The last held handle anchors the tree. Empty it recursively; the
    // anchor's own disposition inside RemoveHeldTree deletes the tree root
    // itself, so no pathname removal ever happens.
    const bool removed = walked && RemoveHeldTree(held);
    CloseHandle(held);
    return removed;
}
