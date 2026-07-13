#include <Windows.h>

#include <qcommon/sys_filesystem.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
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
