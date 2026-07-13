#include <qcommon/sys_filesystem.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
#if defined(_WIN32)
std::wstring ExtendedPath(const std::string &path);
#endif

bool Check(const bool condition)
{
    return condition;
}

std::string Join(const std::string &left, const std::string &right)
{
#if defined(_WIN32)
    return left + "\\" + right;
#else
    return left + "/" + right;
#endif
}

bool WriteFile(const std::string &path)
{
#if defined(_WIN32)
    const std::wstring extended = ExtendedPath(path);
    FILE *file = nullptr;
    if (!extended.empty()
        && _wfopen_s(&file, extended.c_str(), L"wb") != 0)
    {
        file = nullptr;
    }
#else
    FILE *const file = std::fopen(path.c_str(), "wb");
#endif
    if (!file)
        return false;
    const bool written = std::fwrite("x", 1, 1, file) == 1;
    return std::fclose(file) == 0 && written;
}

#if defined(_WIN32)
bool Utf8ToWide(const std::string &input, std::wstring *const output)
{
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, input.c_str(), -1, nullptr, 0);
    if (required <= 0 || !output)
        return false;
    output->assign(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            input.c_str(),
            -1,
            output->data(),
            required) != required)
    {
        return false;
    }
    output->resize(static_cast<std::size_t>(required - 1));
    return true;
}

std::wstring ExtendedPath(const std::string &path)
{
    std::wstring wide;
    if (!Utf8ToWide(path, &wide))
        return {};
    if (wide.rfind(L"\\\\?\\", 0) == 0)
        return wide;
    if (wide.rfind(L"\\\\", 0) == 0)
        return L"\\\\?\\UNC\\" + wide.substr(2);
    return L"\\\\?\\" + wide;
}

bool RemoveDirectoryNative(const std::string &path)
{
    const std::wstring extended = ExtendedPath(path);
    return !extended.empty() && RemoveDirectoryW(extended.c_str());
}

bool RemoveFileNative(const std::string &path)
{
    const std::wstring extended = ExtendedPath(path);
    return !extended.empty() && DeleteFileW(extended.c_str());
}

bool SetCurrentDirectoryNative(const std::string &path)
{
    const std::wstring extended = ExtendedPath(path);
    return !extended.empty() && SetCurrentDirectoryW(extended.c_str());
}

std::uint64_t ProcessId()
{
    return GetCurrentProcessId();
}
#else
bool RemoveDirectoryNative(const std::string &path)
{
    return rmdir(path.c_str()) == 0;
}

bool RemoveFileNative(const std::string &path)
{
    return unlink(path.c_str()) == 0;
}

bool SetCurrentDirectoryNative(const std::string &path)
{
    return chdir(path.c_str()) == 0;
}

std::uint64_t ProcessId()
{
    return static_cast<std::uint64_t>(getpid());
}
#endif

std::string MakeUniquePath(const std::string &workingDirectory)
{
    const auto tick = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    return Join(
        workingDirectory,
        "kisakcod-fs-test-" + std::to_string(tick)
            + "-" + std::to_string(ProcessId()));
}

bool ReadPaths(std::string *const workingDirectory)
{
    std::array<char, 2> tooSmall{'x', 'x'};
    if (!Check(!Sys_FileSystemGetCurrentDirectory(nullptr, 0))
        || !Check(!Sys_FileSystemGetExecutablePath(nullptr, 0))
        || !Check(!Sys_FileSystemGetCurrentDirectory(
            tooSmall.data(), tooSmall.size()))
        || !Check(tooSmall[0] == '\0'))
    {
        return false;
    }
    tooSmall = {'x', 'x'};
    if (!Check(!Sys_FileSystemGetExecutablePath(
            tooSmall.data(), tooSmall.size()))
        || !Check(tooSmall[0] == '\0'))
    {
        return false;
    }

    std::array<char, 4096> current{};
    std::array<char, 4096> executable{};
    if (!Check(Sys_FileSystemGetCurrentDirectory(current.data(), current.size()))
        || !Check(Sys_FileSystemGetExecutablePath(
            executable.data(), executable.size()))
        || !Check(current[0] != '\0')
        || !Check(executable[0] != '\0'))
    {
        return false;
    }
    *workingDirectory = current.data();
    return true;
}

bool TestRootParentClassification()
{
    const auto hasParent = [](const char *const path, const char *const expected) {
        return std::string(
            path,
            Sys_FileSystemParentPathLength(path)) == expected;
    };
    return Check(hasParent("/app", "/"))
        && Check(hasParent("C:\\app.exe", "C:\\"))
        && Check(hasParent("\\\\?\\C:\\app.exe", "\\\\?\\C:\\"))
        && Check(hasParent(
            "\\\\server\\share\\app.exe",
            "\\\\server\\share\\"))
        && Check(hasParent(
            "\\\\?\\unc\\server\\share\\app.exe",
            "\\\\?\\unc\\server\\share\\"));
}

bool TestClassificationAndDepth(const std::string &workingDirectory)
{
    const std::string root = MakeUniquePath(workingDirectory);
    if (!Check(Sys_FileSystemCreateDirectory(root.c_str()))
        || !Check(Sys_FileSystemCreateDirectory(root.c_str())))
    {
        return false;
    }

    const std::string trailing = root
#if defined(_WIN32)
        + "\\";
#else
        + "/";
#endif
    const std::string file = Join(root, "readonly-file");
    const std::string unicodeDirectory = Join(root, "unicode-\xe2\x98\x83");
    if (!Check(Sys_FileSystemCreateDirectory(trailing.c_str()))
        || !Check(Sys_FileSystemCreateDirectory(unicodeDirectory.c_str()))
        || !Check(WriteFile(file)))
    {
        return false;
    }
#if defined(_WIN32)
    const std::wstring wideFile = ExtendedPath(file);
    if (!Check(!wideFile.empty())
        || !Check(SetFileAttributesW(wideFile.c_str(), FILE_ATTRIBUTE_READONLY)))
    {
        return false;
    }
#else
    if (!Check(chmod(file.c_str(), 0444) == 0))
        return false;
#endif
    if (!Check(!Sys_FileSystemCreateDirectory(file.c_str()))
        || !Check(!Sys_FileSystemCreateDirectory(nullptr))
        || !Check(!Sys_FileSystemCreateDirectory(""))
        || !Check(!Sys_FileSystemCreateDirectory("\xff")))
    {
        return false;
    }
#if defined(_WIN32)
    if (!Check(!Sys_FileSystemCreateDirectory("CON"))
        || !Check(!Sys_FileSystemCreateDirectory("nul.txt")))
    {
        return false;
    }
#endif

    std::string tooDeep = root;
    for (std::size_t index = 0; index != 257; ++index)
        tooDeep = Join(tooDeep, "d");
    if (!Check(!Sys_FileSystemCreateDirectory(tooDeep.c_str())))
        return false;

#if defined(_WIN32)
    if (!Check(SetFileAttributesW(wideFile.c_str(), FILE_ATTRIBUTE_NORMAL)))
        return false;
#else
    if (geteuid() != 0)
    {
        const std::string restricted = Join(root, "restricted");
        const std::string restrictedChild = Join(restricted, "child");
        if (!Check(Sys_FileSystemCreateDirectory(restricted.c_str()))
            || !Check(chmod(restricted.c_str(), 0300) == 0)
            || !Check(Sys_FileSystemCreateDirectory(restrictedChild.c_str()))
            || !Check(chmod(restricted.c_str(), 0700) == 0)
            || !Check(RemoveDirectoryNative(restrictedChild))
            || !Check(RemoveDirectoryNative(restricted)))
        {
            return false;
        }
    }
    if (!Check(chmod(file.c_str(), 0644) == 0))
        return false;
#endif
    return Check(RemoveFileNative(file))
        && Check(RemoveDirectoryNative(unicodeDirectory))
        && Check(RemoveDirectoryNative(root));
}

bool TestAncestorLinks(const std::string &workingDirectory)
{
    const std::string root = MakeUniquePath(workingDirectory) + "-links";
    const std::string outside = MakeUniquePath(workingDirectory) + "-outside";
    const std::string link = Join(root, "outside-link");
    const std::string escaped = Join(link, "escaped");
    if (!Check(Sys_FileSystemCreateDirectory(root.c_str()))
        || !Check(Sys_FileSystemCreateDirectory(outside.c_str())))
    {
        return false;
    }

#if defined(_WIN32)
    const std::wstring wideLink = ExtendedPath(link);
    const std::wstring wideOutside = ExtendedPath(outside);
    if (!Check(!wideLink.empty()) || !Check(!wideOutside.empty()))
    {
        return false;
    }
    constexpr DWORD directoryLink = 0x1;
    constexpr DWORD allowUnprivilegedCreate = 0x2;
    if (!CreateSymbolicLinkW(
            wideLink.c_str(),
            wideOutside.c_str(),
            directoryLink | allowUnprivilegedCreate))
    {
        std::fputs(
            "SKIP: Windows host cannot create an unprivileged directory symlink\n",
            stderr);
        (void)RemoveDirectoryNative(root);
        (void)RemoveDirectoryNative(outside);
        return true;
    }
#else
    if (!Check(symlink(outside.c_str(), link.c_str()) == 0))
        return false;
#endif

    const std::string trailingLink = link
#if defined(_WIN32)
        + "\\";
#else
        + "/";
#endif
    if (!Check(!Sys_FileSystemCreateDirectory(link.c_str()))
        || !Check(!Sys_FileSystemCreateDirectory(trailingLink.c_str()))
        || !Check(!Sys_FileSystemCreateDirectory(escaped.c_str())))
    {
        return false;
    }

#if defined(_WIN32)
    if (!Check(RemoveDirectoryW(wideLink.c_str())))
        return false;
#else
    if (!Check(unlink(link.c_str()) == 0))
        return false;
#endif
    return Check(RemoveDirectoryNative(root))
        && Check(RemoveDirectoryNative(outside));
}

bool TestLongCurrentDirectory(const std::string &workingDirectory)
{
    const std::string root = MakeUniquePath(workingDirectory) + "-long";
    if (!Check(Sys_FileSystemCreateDirectory(root.c_str())))
        return false;

    std::vector<std::string> directories{root};
    std::string current = root;
    while (current.size() <= 320)
    {
        current = Join(current, "long-directory-component");
        if (!Check(Sys_FileSystemCreateDirectory(current.c_str())))
            return false;
        directories.push_back(current);
    }
    if (!Check(SetCurrentDirectoryNative(current)))
        return false;

    std::array<char, 256> truncated{};
    std::array<char, 4096> complete{};
    const bool pathChecks =
        Check(!Sys_FileSystemGetCurrentDirectory(truncated.data(), truncated.size()))
        && Check(truncated[0] == '\0')
        && Check(Sys_FileSystemGetCurrentDirectory(complete.data(), complete.size()))
        && Check(std::strlen(complete.data()) > 256);

    const bool restored = SetCurrentDirectoryNative(workingDirectory);
    bool removed = true;
    for (auto directory = directories.rbegin(); directory != directories.rend(); ++directory)
        removed = RemoveDirectoryNative(*directory) && removed;
    return pathChecks && Check(restored) && Check(removed);
}

bool TestBoundedDirectoryEnumeration(const std::string &workingDirectory)
{
    const std::string root = MakeUniquePath(workingDirectory) + "-enumerate";
    const std::string alphaDirectory = Join(root, "alpha-dir");
    const std::string nestedDirectory = Join(alphaDirectory, "nested");
    const std::string zuluDirectory = Join(root, "Zulu-dir");
    const std::string alphaFile = Join(root, "a.TXT");
    const std::string zuluFile = Join(root, "Z.txt");
    const std::string middleFile = Join(root, "middle.bin");
    const std::string unicodeName = "unicode-\xe2\x98\x83";
    const std::string unicodeFile = Join(root, unicodeName);
    const std::string longName = std::string(220, 'l') + ".dat";
    const std::string longFile = Join(root, longName);
    const std::string link = Join(root, "directory-link");

    if (!Check(Sys_FileSystemCreateDirectory(root.c_str()))
        || !Check(Sys_FileSystemCreateDirectory(alphaDirectory.c_str()))
        || !Check(Sys_FileSystemCreateDirectory(nestedDirectory.c_str()))
        || !Check(Sys_FileSystemCreateDirectory(zuluDirectory.c_str()))
        || !Check(WriteFile(alphaFile))
        || !Check(WriteFile(zuluFile))
        || !Check(WriteFile(middleFile))
        || !Check(WriteFile(unicodeFile))
        || !Check(WriteFile(longFile)))
    {
        return false;
    }

    bool linkCreated = false;
#if defined(_WIN32)
    const std::wstring wideLink = ExtendedPath(link);
    const std::wstring wideTarget = ExtendedPath(alphaDirectory);
    constexpr DWORD directoryLink = 0x1;
    constexpr DWORD allowUnprivilegedCreate = 0x2;
    linkCreated = !wideLink.empty()
        && !wideTarget.empty()
        && CreateSymbolicLinkW(
            wideLink.c_str(),
            wideTarget.c_str(),
            directoryLink | allowUnprivilegedCreate);
#else
    linkCreated = symlink(alphaDirectory.c_str(), link.c_str()) == 0;
    const std::string fifo = Join(root, "named-pipe");
    if (!Check(mkfifo(fifo.c_str(), 0600) == 0))
        return false;
#endif

    std::vector<SysFileSystemDirectoryEntry> entries{{
        "must-be-cleared", SysFileSystemEntryKind::RegularFile}};
    if (!Check(Sys_FileSystemListDirectory(
            root.c_str(), 32, &entries)
            == SysFileSystemListStatus::Complete)
        || !Check(entries.size() == 7))
    {
        return false;
    }
    const std::vector<std::string> expectedNames{
        "a.TXT",
        "alpha-dir",
        longName,
        "middle.bin",
        unicodeName,
        "Z.txt",
        "Zulu-dir",
    };
    for (std::size_t index = 0; index < expectedNames.size(); ++index)
    {
        if (!Check(entries[index].name == expectedNames[index]))
            return false;
    }
    if (!Check(entries[0].kind == SysFileSystemEntryKind::RegularFile)
        || !Check(entries[1].kind == SysFileSystemEntryKind::Directory)
        || !Check(entries[6].kind == SysFileSystemEntryKind::Directory))
    {
        return false;
    }

    if (!Check(Sys_FileSystemListDirectory(root.c_str(), 3, &entries)
            == SysFileSystemListStatus::Truncated)
        || !Check(entries.size() == 3)
        || !Check(entries[0].name == "a.TXT")
        || !Check(entries[1].name == "alpha-dir")
        || !Check(entries[2].name == longName)
        || !Check(Sys_FileSystemListDirectory(root.c_str(), 0, &entries)
            == SysFileSystemListStatus::Truncated)
        || !Check(entries.empty()))
    {
        return false;
    }

    entries.push_back({"must-be-cleared", SysFileSystemEntryKind::RegularFile});
    const std::string missing = Join(root, "missing");
    if (!Check(Sys_FileSystemListDirectory(missing.c_str(), 4, &entries)
            == SysFileSystemListStatus::Error)
        || !Check(entries.empty())
        || !Check(Sys_FileSystemListDirectory(alphaFile.c_str(), 4, &entries)
            == SysFileSystemListStatus::Error)
        || !Check(entries.empty())
        || (linkCreated
            && !Check(Sys_FileSystemListDirectory(link.c_str(), 4, &entries)
                == SysFileSystemListStatus::Error))
        || !Check(entries.empty())
        || (linkCreated
            && !Check(Sys_FileSystemListDirectory(
                Join(link, "nested").c_str(), 4, &entries)
                == SysFileSystemListStatus::Error))
        || !Check(entries.empty())
        || !Check(Sys_FileSystemListDirectory("\xff", 4, &entries)
            == SysFileSystemListStatus::Error)
        || !Check(Sys_FileSystemListDirectory(root.c_str(), 4, nullptr)
            == SysFileSystemListStatus::Error)
        || !Check(Sys_FileSystemHasExtension("archive.IWD", "iwd"))
        || !Check(Sys_FileSystemHasExtension("archive.tar.IwD", "iwd"))
        || !Check(!Sys_FileSystemHasExtension("archiveiwd", "iwd"))
        || !Check(!Sys_FileSystemHasExtension("archive.iwd.bak", "iwd"))
        || !Check(!Sys_FileSystemHasExtension("archive.iwd", ""))
        || !Check(!Sys_FileSystemHasExtension(nullptr, "iwd"))
        || !Check(Sys_FileSystemMatchesPathFilter(
            "sub\\*.cfg", "SUB/file.CFG"))
        || !Check(Sys_FileSystemMatchesPathFilter(
            "maps/*/script?.[g-h]sc", "maps/mp/script1.gsc"))
        || !Check(Sys_FileSystemMatchesPathFilter(
            "literal[[]name", "literal[name"))
        || !Check(Sys_FileSystemMatchesPathFilter("", "any/path"))
        || !Check(Sys_FileSystemMatchesPathFilter("prefix", "prefix-tail"))
        || !Check(!Sys_FileSystemMatchesPathFilter(
            "maps/*/script?.[g-h]sc", "maps/mp/script.gsc"))
        || !Check(Sys_FileSystemMatchesPathFilter(
            "*.dat", longName.c_str()))
        || !Check(!Sys_FileSystemMatchesPathFilter(
            "*.txt", longName.c_str()))
        || !Check(!Sys_FileSystemMatchesPathFilter(nullptr, "file.cfg")))
    {
        return false;
    }

#if defined(_WIN32)
    bool removedLink = true;
    if (linkCreated)
        removedLink = RemoveDirectoryW(ExtendedPath(link).c_str());
#else
    const bool removedLink = !linkCreated || unlink(link.c_str()) == 0;
    const bool removedFifo = RemoveFileNative(fifo);
#endif
    const bool removed = RemoveFileNative(longFile)
        && RemoveFileNative(unicodeFile)
        && RemoveFileNative(middleFile)
        && RemoveFileNative(zuluFile)
        && RemoveFileNative(alphaFile)
        && RemoveDirectoryNative(zuluDirectory)
        && RemoveDirectoryNative(nestedDirectory)
        && RemoveDirectoryNative(alphaDirectory)
        && RemoveDirectoryNative(root);
    return Check(removedLink)
#if !defined(_WIN32)
        && Check(removedFifo)
#endif
        && Check(removed);
}
}

int main()
{
    std::string workingDirectory;
    return ReadPaths(&workingDirectory)
        && TestRootParentClassification()
        && TestClassificationAndDepth(workingDirectory)
        && TestAncestorLinks(workingDirectory)
        && TestLongCurrentDirectory(workingDirectory)
        && TestBoundedDirectoryEnumeration(workingDirectory)
        ? 0
        : 1;
}
