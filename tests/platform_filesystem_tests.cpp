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
#include <winioctl.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
#if defined(_WIN32)
std::wstring ExtendedPath(const std::string &path);
#endif

const char *gCheckStage = "startup";

void SetCheckStage(const char *const stage)
{
    gCheckStage = stage ? stage : "unknown";
}

bool Check(const bool condition)
{
    if (!condition)
    {
#if defined(_WIN32)
        std::fprintf(
            stderr,
            "FAIL: platform filesystem stage: %s (Win32 error %lu)\n",
            gCheckStage,
            static_cast<unsigned long>(GetLastError()));
#else
        std::fprintf(stderr, "FAIL: platform filesystem stage: %s\n", gCheckStage);
#endif
    }
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
    const HANDLE file = extended.empty()
        ? INVALID_HANDLE_VALUE
        : CreateFileW(
            extended.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD byteCount = 0;
    const bool written = ::WriteFile(
        file, "x", 1, &byteCount, nullptr)
        && byteCount == 1;
    return CloseHandle(file) && written;
#else
    FILE *const file = std::fopen(path.c_str(), "wb");
    if (!file)
        return false;
    const bool written = std::fwrite("x", 1, 1, file) == 1;
    return std::fclose(file) == 0 && written;
#endif
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

bool KISAK_CDECL SelectTextFile(
    const char *const name,
    const SysFileSystemEntryKind kind,
    const void *)
{
    return kind == SysFileSystemEntryKind::RegularFile
        && Sys_FileSystemHasExtension(name, "txt");
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
    SetCheckStage("long-current-directory/create-root");
    const std::string root = MakeUniquePath(workingDirectory) + "-long";
    if (!Check(Sys_FileSystemCreateDirectory(root.c_str())))
        return false;

    std::vector<std::string> directories{root};
    std::string current = root;
#if defined(_WIN32)
    // SetCurrentDirectoryW remains subject to the process-wide MAX_PATH
    // policy unless both the host and executable opt into long-path behavior.
    // Stay below that boundary while still forcing a dynamically sized query.
    constexpr std::size_t currentDirectoryTarget = 220;
#else
    constexpr std::size_t currentDirectoryTarget = 320;
#endif
    while (current.size() <= currentDirectoryTarget)
    {
        SetCheckStage("long-current-directory/create-component");
        current = Join(current, "long-directory-component");
        if (!Check(Sys_FileSystemCreateDirectory(current.c_str())))
            return false;
        directories.push_back(current);
    }
    SetCheckStage("long-current-directory/set-current");
    if (!Check(SetCurrentDirectoryNative(current)))
        return false;

    std::array<char, 128> truncated{};
    std::array<char, 4096> complete{};
    SetCheckStage("long-current-directory/query");
    const bool pathChecks =
        Check(!Sys_FileSystemGetCurrentDirectory(truncated.data(), truncated.size()))
        && Check(truncated[0] == '\0')
        && Check(Sys_FileSystemGetCurrentDirectory(complete.data(), complete.size()))
        && Check(std::strlen(complete.data()) >= truncated.size());

    SetCheckStage("long-current-directory/restore");
    const bool restored = SetCurrentDirectoryNative(workingDirectory);
    SetCheckStage("long-current-directory/cleanup");
    bool removed = true;
    for (auto directory = directories.rbegin(); directory != directories.rend(); ++directory)
        removed = RemoveDirectoryNative(*directory) && removed;
    return pathChecks && Check(restored) && Check(removed);
}

bool TestBoundedDirectoryEnumeration(const std::string &workingDirectory)
{
    SetCheckStage("bounded-enumeration/setup");
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
    SetCheckStage("bounded-enumeration/link-and-special-entry-setup");
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
    const std::string literalBackslash = Join(root, "literal\\child");
    const std::string literalColon = Join(root, "literal:child");
    if (!Check(mkfifo(fifo.c_str(), 0600) == 0)
        || !Check(WriteFile(literalBackslash))
        || !Check(WriteFile(literalColon)))
        return false;
#endif

    std::vector<SysFileSystemDirectoryEntry> entries{{
        "must-be-cleared", SysFileSystemEntryKind::RegularFile}};
    SetCheckStage("bounded-enumeration/complete-snapshot");
    const SysFileSystemListStatus snapshotStatus =
        Sys_FileSystemListDirectory(root.c_str(), 32, &entries);
    if (snapshotStatus != SysFileSystemListStatus::Complete
        || entries.size() != 7)
    {
        std::fprintf(
            stderr,
            "filesystem snapshot: status=%u count=%zu link=%s\n",
            static_cast<unsigned int>(snapshotStatus),
            entries.size(),
            linkCreated ? "created" : "not-created");
        for (const SysFileSystemDirectoryEntry &entry : entries)
        {
            std::fprintf(
                stderr,
                "  %s (%s)\n",
                entry.name.c_str(),
                entry.kind == SysFileSystemEntryKind::Directory
                    ? "directory"
                    : "file");
        }
    }
    if (!Check(snapshotStatus == SysFileSystemListStatus::Complete)
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

    // FS_BuildOSPath historically emits engine-style backslashes on every
    // host.  POSIX services must interpret both slash forms as separators.
    std::string engineStyleRoot = root;
    SetCheckStage("bounded-enumeration/engine-style-path");
    for (char &character : engineStyleRoot)
    {
        if (character == '/')
            character = '\\';
    }
    if (!Check(Sys_FileSystemListDirectory(
            engineStyleRoot.c_str(), 32, &entries)
            == SysFileSystemListStatus::Complete)
        || !Check(entries.size() == expectedNames.size()))
    {
        return false;
    }

    SetCheckStage("bounded-enumeration/truncation-contract");
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
    SetCheckStage("bounded-enumeration/error-filter-extension-contracts");
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

    SetCheckStage("bounded-enumeration/cleanup");
#if defined(_WIN32)
    bool removedLink = true;
    if (linkCreated)
        removedLink = RemoveDirectoryW(ExtendedPath(link).c_str());
#else
    const bool removedLink = !linkCreated || unlink(link.c_str()) == 0;
    const bool removedFifo = RemoveFileNative(fifo);
    const bool removedAmbiguousNames = RemoveFileNative(literalBackslash)
        && RemoveFileNative(literalColon);
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
        && Check(removedAmbiguousNames)
#endif
        && Check(removed);
}

bool TestFilteredCollectionAndPathHelpers(
    const std::string &workingDirectory)
{
    SetCheckStage("filtered-collection/setup");
    const std::string root = MakeUniquePath(workingDirectory) + "-filtered";
    if (!Check(Sys_FileSystemCreateDirectory(root.c_str())))
        return false;

    std::vector<std::string> nonmatchingFiles;
    for (int index = 0; index < 12; ++index)
    {
        const std::string file = Join(
            root,
            "a" + std::to_string(index) + ".bin");
        if (!Check(WriteFile(file)))
            return false;
        nonmatchingFiles.push_back(file);
    }
    const std::string lastEligible = Join(root, "z-last.txt");
    if (!Check(WriteFile(lastEligible)))
        return false;

    std::vector<SysFileSystemDirectoryEntry> entries;
    SetCheckStage("filtered-collection/nonmatches-before-eligible");
    if (!Check(Sys_FileSystemListDirectoryFiltered(
            root.c_str(), 1, SelectTextFile, nullptr, &entries)
            == SysFileSystemListStatus::Complete)
        || !Check(entries.size() == 1)
        || !Check(entries[0].name == "z-last.txt"))
    {
        return false;
    }

    const std::string firstEligible = Join(root, "y-first.txt");
    SetCheckStage("filtered-collection/eligible-truncation");
    if (!Check(WriteFile(firstEligible))
        || !Check(Sys_FileSystemListDirectoryFiltered(
            root.c_str(), 1, SelectTextFile, nullptr, &entries)
            == SysFileSystemListStatus::Truncated)
        || !Check(entries.size() == 1)
        || !Check(entries[0].name == "y-first.txt"))
    {
        return false;
    }

    const char *paths[]{
        "zeta",
        "Beta\\item",
        "alpha",
        "beta/item",
    };
    SetCheckStage("filtered-collection/native-pointer-sort-and-dedupe");
    Sys_FileSystemSortPathPointers(paths, std::size(paths));
    if (!Check(std::string(paths[0]) == "alpha")
        || !Check(std::string(paths[1]) == "Beta\\item")
        || !Check(std::string(paths[2]) == "beta/item")
        || !Check(std::string(paths[3]) == "zeta")
        || !Check(Sys_FileSystemEnginePathsEqual(
            "folder\\FILE.cfg", "FOLDER/file.cfg"))
        || !Check(Sys_FileSystemEnginePathsEqual(
            "folder:FILE.cfg", "FOLDER/file.cfg"))
        || !Check(!Sys_FileSystemEnginePathsEqual(
            "folder/file.cfg", "folder/file.bin")))
    {
        return false;
    }

    SetCheckStage("filtered-collection/cleanup");
    bool removed = RemoveFileNative(firstEligible)
        && RemoveFileNative(lastEligible);
    for (const std::string &file : nonmatchingFiles)
        removed = RemoveFileNative(file) && removed;
    return Check(removed) && Check(RemoveDirectoryNative(root));
}
bool WriteBytesNative(const std::string &path, const std::vector<unsigned char> &bytes)
{
#if defined(_WIN32)
    const std::wstring extended = ExtendedPath(path);
    if (extended.empty())
        return false;
    const HANDLE file = CreateFileW(
        extended.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    bool written = true;
    std::size_t total = 0;
    while (total < bytes.size())
    {
        DWORD chunk = 0;
        const DWORD request = static_cast<DWORD>(
            (std::min)(bytes.size() - total, static_cast<std::size_t>(1u << 30)));
        if (!::WriteFile(file, bytes.data() + total, request, &chunk, nullptr)
            || chunk == 0)
        {
            written = false;
            break;
        }
        total += chunk;
    }
    return CloseHandle(file) && written;
#else
    FILE *const file = std::fopen(path.c_str(), "wb");
    if (!file)
        return false;
    const bool written = bytes.empty()
        || std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    return std::fclose(file) == 0 && written;
#endif
}

bool TestReadFileNoFollow(const std::string &workingDirectory)
{
    const std::string root = MakeUniquePath(workingDirectory) + "-readfile";
    const std::string nested = Join(root, "nested");
    const std::string payloadPath = Join(nested, "payload.bin");
    const std::string emptyPath = Join(nested, "empty.bin");
    const std::string outside = MakeUniquePath(workingDirectory) + "-readfile-outside";
    const std::string outsidePath = Join(outside, "secret.txt");

    SetCheckStage("read-file/setup");
    std::vector<unsigned char> payload(300u * 1024u);
    for (std::size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<unsigned char>(i * 7u + (i >> 8));
    const std::vector<unsigned char> secret{'s', 'e', 'c', 'r', 'e', 't'};
    if (!Check(Sys_FileSystemCreateDirectory(root.c_str()))
        || !Check(Sys_FileSystemCreateDirectory(nested.c_str()))
        || !Check(Sys_FileSystemCreateDirectory(outside.c_str()))
        || !Check(WriteBytesNative(payloadPath, payload))
        || !Check(WriteBytesNative(emptyPath, {}))
        || !Check(WriteBytesNative(outsidePath, secret)))

    {
        return false;
    }

    // Round-trip: the complete regular file is readable through nested
    // real directories and the output is byte-identical.
    SetCheckStage("read-file/round-trip");
    {
        std::vector<unsigned char> contents{'x'};
        if (!Check(Sys_FileSystemReadFile(
                payloadPath.c_str(),
                payload.size(),
                &contents))
            || !Check(contents.size() == payload.size())
            || !Check(std::memcmp(
                    contents.data(),
                    payload.data(),
                    payload.size()) == 0))
        {
            return false;
        }
    }

    SetCheckStage("read-file/empty-file");
    {
        std::vector<unsigned char> contents{'x'};
        if (!Check(Sys_FileSystemReadFile(emptyPath.c_str(), 0, &contents))
            || !Check(contents.empty()))
        {
            return false;
        }
        if (!Check(Sys_FileSystemReadFile(emptyPath.c_str(), 16, &contents))
            || !Check(contents.empty()))
        {
            return false;
        }
    }

    // Size cap: a file above maximumBytes is rejected and the output
    // stays empty (failure-atomic, not partial).
    SetCheckStage("read-file/size-cap");
    {
        std::vector<unsigned char> contents{'x'};
        if (!Check(!Sys_FileSystemReadFile(
                payloadPath.c_str(),
                payload.size() - 1,
                &contents))
            || !Check(contents.empty()))
        {
            return false;
        }
    }

    // Rejections must clear any prior output (failure-atomic contract).
    SetCheckStage("read-file/rejection-clears-output");
    {
        std::vector<unsigned char> contents{'x'};
        const bool cleared = Sys_FileSystemReadFile(
            payloadPath.c_str(),
            payload.size() - 1,
            &contents);
        if (!Check(!cleared) || !Check(contents.empty()))
            return false;
    }

    SetCheckStage("read-file/invalid-inputs");
    {
        std::vector<unsigned char> contents{'x'};
        const std::string missingNested = Join(root, "no-such-file.bin");
        const std::string traversal = Join(Join(root, ".."), "escaped.bin");
        const std::string invalidUtf8 = Join(nested, "\xFF\xFE.bin");
        const char *const invalidPaths[] = {
            "",
            "no-such-file-kisakcod.bin",
            missingNested.c_str(),
            traversal.c_str(),
            invalidUtf8.c_str(),
            root.c_str(),
        };
        bool rejected = true;
        for (const char *const path : invalidPaths)
            rejected = Sys_FileSystemReadFile(path, 1024, &contents) == false && rejected;
        // A null output vector is a hard rejection, never a crash.
        rejected = Sys_FileSystemReadFile(payloadPath.c_str(), 1024, nullptr) == false
            && rejected;
        if (!Check(rejected) || !Check(contents.empty()))
            return false;
    }

#if !defined(_WIN32)
    // Special files are not regular files and must be rejected.
    SetCheckStage("read-file/special-file-rejection");
    {
        std::vector<unsigned char> contents{'x'};
        if (!Check(!Sys_FileSystemReadFile("/dev/null", 1024, &contents))
            || !Check(contents.empty()))
        {
            return false;
        }
    }
#endif

    // Link leaves and link ancestors must be rejected without reading
    // the outside target. Windows skips when the host cannot create
    // unprivileged symlinks.
    SetCheckStage("read-file/link-rejection");
    {
#if defined(_WIN32)
        const std::wstring wideLeafLink = ExtendedPath(Join(nested, "leaf-link.txt"));
        const std::wstring wideOutsidePath = ExtendedPath(outsidePath);
        const std::wstring wideDirLink = ExtendedPath(Join(nested, "dir-link"));
        const std::wstring wideOutside = ExtendedPath(outside);
        if (!Check(!wideLeafLink.empty()) || !Check(!wideOutsidePath.empty()))
            return false;
        if (!CreateSymbolicLinkW(
                wideLeafLink.c_str(),
                wideOutsidePath.c_str(),
                0x2 /* SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE */)
            || !CreateSymbolicLinkW(
                wideDirLink.c_str(),
                wideOutside.c_str(),
                0x1 | 0x2))
        {
            std::fputs(
                "SKIP: Windows host cannot create an unprivileged symlink\n",
                stderr);
            (void)RemoveFileNative(Join(nested, "leaf-link.txt"));
            (void)RemoveDirectoryNative(Join(nested, "dir-link"));
        }
        else
        {
            std::vector<unsigned char> contents{'x'};
            const std::string leafLink = Join(nested, "leaf-link.txt");
            const std::string dirLinkFile = Join(Join(nested, "dir-link"), "secret.txt");
            if (!Check(!Sys_FileSystemReadFile(leafLink.c_str(), 64, &contents))
                || !Check(!Sys_FileSystemReadFile(dirLinkFile.c_str(), 64, &contents))
                || !Check(contents.empty()))
            {
                return false;
            }
            (void)RemoveFileNative(leafLink);
            (void)RemoveDirectoryNative(Join(nested, "dir-link"));
        }
#else
        const std::string leafLink = Join(nested, "leaf-link.txt");
        const std::string dirLink = Join(nested, "dir-link");
        if (!Check(symlink(outsidePath.c_str(), leafLink.c_str()) == 0)
            || !Check(symlink(outside.c_str(), dirLink.c_str()) == 0))
        {
            return false;
        }
        std::vector<unsigned char> contents{'x'};
        const std::string dirLinkFile = Join(dirLink, "secret.txt");
        if (!Check(!Sys_FileSystemReadFile(leafLink.c_str(), 64, &contents))
            || !Check(!Sys_FileSystemReadFile(dirLinkFile.c_str(), 64, &contents))
            || !Check(contents.empty()))
        {
            return false;
        }
        (void)unlink(leafLink.c_str());
        (void)unlink(dirLink.c_str());
#endif
    }

    SetCheckStage("read-file/cleanup");
    bool removed = RemoveFileNative(payloadPath) && RemoveFileNative(emptyPath);
    removed = RemoveDirectoryNative(nested) && removed;
    removed = RemoveFileNative(outsidePath) && removed;
    removed = RemoveDirectoryNative(outside) && removed;
    return Check(removed) && Check(RemoveDirectoryNative(root));

}


// Path fixture for the recursive-deletion contract scenarios. Building the
// names is separated from creating the layout so each scenario helper stays
// within the complexity budget.
struct RemoveTreeContractPaths
{
    std::string root;
    std::string nestedDirectory;
    std::string deeperDirectory;
    std::string file;
    std::string nestedFile;
    std::string siblingFile;
    std::string internalLink;
    std::string externalTarget;
    std::string externalLink;
};

RemoveTreeContractPaths MakeRemoveTreeContractPaths(
    const std::string &workingDirectory)
{
    RemoveTreeContractPaths paths;
    paths.root = MakeUniquePath(workingDirectory) + "-remove";
    paths.nestedDirectory = Join(paths.root, "nested");
    paths.deeperDirectory = Join(paths.nestedDirectory, "deeper");
    paths.file = Join(paths.root, "keep-file.dat");
    paths.nestedFile = Join(paths.deeperDirectory, "inside.bin");
    paths.siblingFile = Join(paths.root, "sibling.txt");
    paths.internalLink = Join(paths.root, "internal-symlink");
    paths.externalTarget = MakeUniquePath(workingDirectory)
        + "-external-target";
    paths.externalLink = Join(paths.root, "external-symlink");
    return paths;
}

bool CreateRemoveTreeFixture(const RemoveTreeContractPaths &paths)
{
    SetCheckStage("remove-tree/setup");
    if (!Check(Sys_FileSystemCreateDirectory(paths.root.c_str()))
        || !Check(Sys_FileSystemCreateDirectory(paths.nestedDirectory.c_str()))
        || !Check(Sys_FileSystemCreateDirectory(paths.deeperDirectory.c_str()))
        || !Check(Sys_FileSystemCreateDirectory(paths.externalTarget.c_str()))
        || !Check(WriteFile(paths.file))
        || !Check(WriteFile(paths.nestedFile))
        || !Check(WriteFile(paths.siblingFile))
        || !Check(WriteFile(Join(paths.externalTarget, "outside.bin"))))
    {
        return false;
    }
    return true;
}

bool CreateRemoveTreeContractLinks(const RemoveTreeContractPaths &paths)
{
#if defined(_WIN32)
    constexpr DWORD directoryLink = 0x1;
    constexpr DWORD allowUnprivilegedCreate = 0x2;
    const std::wstring wideInternal = ExtendedPath(paths.internalLink);
    const std::wstring wideExternal = ExtendedPath(paths.externalLink);
    const std::wstring wideExternalTarget = ExtendedPath(paths.externalTarget);
    if (!Check(!wideInternal.empty())
        || !Check(!wideExternal.empty())
        || !Check(!wideExternalTarget.empty()))
    {
        return false;
    }
    const bool internalCreated = CreateSymbolicLinkW(
        wideInternal.c_str(),
        ExtendedPath(paths.nestedDirectory).c_str(),
        directoryLink | allowUnprivilegedCreate) != 0;
    const bool externalCreated = CreateSymbolicLinkW(
        wideExternal.c_str(),
        wideExternalTarget.c_str(),
        directoryLink | allowUnprivilegedCreate) != 0;
#else
    const bool internalCreated =
        symlink("nested", paths.internalLink.c_str()) == 0;
    const bool externalCreated =
        symlink(paths.externalTarget.c_str(), paths.externalLink.c_str()) == 0;
#endif
    if (!Check(internalCreated) || !Check(externalCreated))
        return false;
    return true;
}

// A symlink pointing out of the tree must be refused as the requested root
// without touching its target.
bool TestRemoveTreeRefusesLinkLeaf(
    const std::string &workingDirectory,
    const std::string &root)
{
    SetCheckStage("remove-tree/refuse-link-leaf");
    const std::string linkLeaf = MakeUniquePath(workingDirectory)
        + "-remove-link";
#if defined(_WIN32)
    constexpr DWORD directoryLink = 0x1;
    constexpr DWORD allowUnprivilegedCreate = 0x2;
    const std::wstring wideLinkLeaf = ExtendedPath(linkLeaf);
    const bool created = CreateSymbolicLinkW(
        wideLinkLeaf.c_str(),
        ExtendedPath(root).c_str(),
        directoryLink | allowUnprivilegedCreate) != 0;
#else
    const bool created =
        symlink(root.c_str(), linkLeaf.c_str()) == 0;
#endif
    if (!Check(created))
        return false;
    if (!Check(!Sys_FileSystemRemoveTree(linkLeaf.c_str())))
        return false;
#if defined(_WIN32)
    const std::wstring wideCleanupLeaf = ExtendedPath(linkLeaf);
    (void)RemoveDirectoryW(wideCleanupLeaf.c_str());
#else
    (void)unlink(linkLeaf.c_str());
#endif
    return true;
}

bool TestRemoveTreeRejectsInvalidArguments(const std::string &root)
{
    SetCheckStage("remove-tree/invalid-arguments");
    if (!Check(!Sys_FileSystemRemoveTree(nullptr))
        || !Check(!Sys_FileSystemRemoveTree(""))
        || !Check(!Sys_FileSystemRemoveTree("\xff"))
        || !Check(!Sys_FileSystemRemoveTree("../escape"))
        || !Check(!Sys_FileSystemRemoveTree(Join(root, "missing").c_str())))
    {
        return false;
    }
    return true;
}

#if defined(_WIN32)
bool VerifyRemoveTreeWin32Results(
    const std::string &root,
    const std::string &externalTarget)
{
    const std::wstring wideRoot = ExtendedPath(root);
    const std::wstring wideExternalTargetCheck = ExtendedPath(externalTarget);
    const std::wstring wideOutsideCheck =
        ExtendedPath(Join(externalTarget, "outside.bin"));
    if (!Check(GetFileAttributesW(wideRoot.c_str()) == INVALID_FILE_ATTRIBUTES)
        || !Check(GetFileAttributesW(wideExternalTargetCheck.c_str())
            != INVALID_FILE_ATTRIBUTES)
        || !Check(GetFileAttributesW(wideOutsideCheck.c_str())
            != INVALID_FILE_ATTRIBUTES))
    {
        return false;
    }
    if (!Check(RemoveFileW(wideOutsideCheck.c_str())))
        return false;
    SetCheckStage("remove-tree/external-cleanup");
    std::vector<std::wstring> leftover;
    WIN32_FIND_DATAW findData{};
    std::wstring searchExt = wideExternalTargetCheck + L"\\*";
    HANDLE findHandle = FindFirstFileExW(
        searchExt.c_str(),
        FindExInfoBasic,
        &findData,
        FindExSearchNameMatch,
        nullptr,
        0);
    if (findHandle != INVALID_HANDLE_VALUE)
    {
        for (;;)
        {
            const wchar_t *const name = findData.cFileName;
            const bool dot = name[0] == L'.' && name[1] == L'\0';
            const bool dotDot = name[0] == L'.'
                && name[1] == L'.'
                && name[2] == L'\0';
            if (!dot && !dotDot)
                leftover.emplace_back(name);
            if (!FindNextFileW(findHandle, &findData))
                break;
        }
        FindClose(findHandle);
    }
    if (!leftover.empty())
        return false;
    return Check(RemoveDirectoryW(wideExternalTargetCheck.c_str()));
}
#else
bool VerifyRemoveTreePosixResults(
    const std::string &root,
    const std::string &externalTarget)
{
    // Existence checks only: the tree must be gone and the external
    // symlink must have survived. faccessat keeps these checks free of
    // stat's 32-bit time_t surface and needs no output buffer; the
    // no-follow flag keeps the symlink itself the object being tested.
    if (!Check(faccessat(AT_FDCWD, root.c_str(), F_OK, 0) != 0)
        || !Check(faccessat(
                AT_FDCWD,
                externalTarget.c_str(),
                F_OK,
                AT_SYMLINK_NOFOLLOW)
            == 0))
    {
        return false;
    }
    SetCheckStage("remove-tree/external-cleanup");
    if (!Check(RemoveFileNative(Join(externalTarget, "outside.bin"))))
        return false;
    return Check(rmdir(externalTarget.c_str()) == 0);
}
#endif

bool TestRemoveTreeContract(const std::string &workingDirectory)
{
    const RemoveTreeContractPaths paths =
        MakeRemoveTreeContractPaths(workingDirectory);
    if (!CreateRemoveTreeFixture(paths))
        return false;
    if (!CreateRemoveTreeContractLinks(paths))
        return false;
    if (!TestRemoveTreeRefusesLinkLeaf(workingDirectory, paths.root))
        return false;
    if (!TestRemoveTreeRejectsInvalidArguments(paths.root))
        return false;
    SetCheckStage("remove-tree/executes");
    if (!Check(Sys_FileSystemRemoveTree(paths.root.c_str())))
        return false;
#if defined(_WIN32)
    return VerifyRemoveTreeWin32Results(paths.root, paths.externalTarget);
#else
    return VerifyRemoveTreePosixResults(paths.root, paths.externalTarget);
#endif
}

#if defined(_WIN32)
namespace
{
#pragma pack(push, 4)
struct KisakTestMountPointBuffer
{
    std::uint32_t ReparseTag;
    std::uint16_t ReparseDataLength;
    std::uint16_t Reserved;
    std::uint16_t SubstituteNameOffset;
    std::uint16_t SubstituteNameLength;
    std::uint16_t PrintNameOffset;
    std::uint16_t PrintNameLength;
    wchar_t PathBuffer[1];
};
#pragma pack(pop)

// Resolves the substitute name — the absolute native target path prefixed
// with the \??\ device namespace, without the \\?\ extended prefix — and
// fills a mount-point reparse buffer sized to the real path. Returns false
// if the target cannot be resolved or would not fit the 16-bit reparse
// name-length fields.
bool BuildMountPointReparseBuffer(
    const std::string &targetPath,
    std::vector<unsigned char> *buffer)
{
    std::wstring wideRaw;
    if (!Utf8ToWide(targetPath, &wideRaw))
        return false;
    const DWORD required = GetFullPathNameW(
        wideRaw.c_str(), 0, nullptr, nullptr);
    if (required == 0)
        return false;
    std::vector<wchar_t> absolute(required, L'\0');
    if (GetFullPathNameW(
            wideRaw.c_str(),
            required,
            absolute.data(),
            nullptr)
        == 0)
    {
        return false;
    }
    const std::wstring wideTarget = L"\\??\\" + std::wstring(absolute.data());

    const std::size_t substituteBytes = wideTarget.size() * sizeof(wchar_t);
    if (substituteBytes == 0 || substituteBytes > 0xFFFFu)
        return false;

    const std::size_t pathBufferOffset =
        offsetof(KisakTestMountPointBuffer, PathBuffer);
    const std::size_t copyBytes = substituteBytes + sizeof(wchar_t);
    buffer->assign(pathBufferOffset + copyBytes + 16u, 0u);
    auto *const reparse =
        reinterpret_cast<KisakTestMountPointBuffer *>(buffer->data());
    reparse->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
    reparse->Reserved = 0;
    reparse->SubstituteNameOffset = 0;
    reparse->SubstituteNameLength = static_cast<std::uint16_t>(substituteBytes);
    reparse->PrintNameOffset =
        static_cast<std::uint16_t>(substituteBytes + sizeof(wchar_t));
    reparse->PrintNameLength = 0;
    // ReparseDataLength counts the mount-point fields only: the four
    // USHORT offsets/lengths plus the substitute name and its print-name
    // terminator slot. The generic header (tag/length/reserved) is not
    // part of it.
    reparse->ReparseDataLength = static_cast<std::uint16_t>(
        offsetof(KisakTestMountPointBuffer, PathBuffer)
        - offsetof(KisakTestMountPointBuffer, SubstituteNameOffset)
        + substituteBytes + sizeof(wchar_t));
    // Copy through the heap allocation rather than the PathBuffer[1] tail
    // anchor, with the bound spelled out: PathBuffer is the flexible-array
    // idiom's anchor, not a real one-element array.
    if (copyBytes > buffer->size() - pathBufferOffset)
        return false;
    std::memcpy(
        buffer->data() + pathBufferOffset,
        wideTarget.c_str(),
        copyBytes);
    return true;
}

// Creates a true NTFS junction (IO_REPARSE_TAG_MOUNT_POINT) at linkPath
// pointing at targetPath. Junctions require no privilege, unlike symbolic
// links, so this is the deterministic way to exercise reparse-point
// handling on CI hosts.
bool CreateJunctionNative(
    const std::string &linkPath,
    const std::string &targetPath)
{
    const std::wstring wideLink = ExtendedPath(linkPath);
    if (wideLink.empty())
        return false;
    if (!CreateDirectoryW(wideLink.c_str(), nullptr)
        && GetLastError() != ERROR_ALREADY_EXISTS)
    {
        return false;
    }

    std::vector<unsigned char> buffer;
    if (!BuildMountPointReparseBuffer(targetPath, &buffer))
        return false;
    const auto *const reparse =
        reinterpret_cast<const KisakTestMountPointBuffer *>(buffer.data());

    const HANDLE handle = CreateFileW(
        wideLink.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    DWORD returned = 0;
    const bool set = DeviceIoControl(
        handle,
        FSCTL_SET_REPARSE_POINT,
        buffer.data(),
        static_cast<DWORD>(
            offsetof(KisakTestMountPointBuffer, PathBuffer)
            + reparse->ReparseDataLength),
        nullptr,
        0,
        &returned,
        nullptr);
    CloseHandle(handle);
    return set;
}
}
#endif // defined(_WIN32)

// Native Win32 junction contracts for the recursive deletion service:
// a junction leaf is refused without touching its target, and a junction
// occupying a name inside the tree — the deterministic end state of a
// rename/reparse substitution race — is removed as itself while the target
// it references survives untouched. POSIX junction equivalents (directory
// symbolic links) are covered by TestRemoveTreeContract's internal/external
// link cases, so the whole contract is compiled only where junctions exist.
#if defined(_WIN32)
namespace
{
bool CreateJunctionContractFixture(
    const std::string &root,
    const std::string &nested,
    const std::string &outside,
    const std::string &victimPath)
{
    SetCheckStage("junction/setup");
    if (!Check(Sys_FileSystemCreateDirectory(root.c_str()))
        || !Check(Sys_FileSystemCreateDirectory(nested.c_str()))
        || !Check(Sys_FileSystemCreateDirectory(outside.c_str()))
        || !Check(WriteFile(Join(root, "keep-file.dat")))
        || !Check(WriteFile(victimPath)))
    {
        return false;
    }
    return true;
}

// A junction as the requested tree must be refused: opening it follows
// nothing, tag verification rejects it, and the target survives.
bool VerifyJunctionLeafRefused(
    const std::string &junctionLeaf,
    const std::string &victimPath)
{
    SetCheckStage("junction/leaf-refused");
    if (!Check(!Sys_FileSystemRemoveTree(junctionLeaf.c_str())))
        return false;
    return Check(GetFileAttributesW(ExtendedPath(junctionLeaf).c_str())
            != INVALID_FILE_ATTRIBUTES)
        && Check(GetFileAttributesW(ExtendedPath(victimPath).c_str())
            != INVALID_FILE_ATTRIBUTES);
}

// A junction occupying a name inside the tree is deleted as itself, never
// traversed. The victim file behind it proves the target was never followed.
bool VerifyJunctionInTreeRemovedAsItself(
    const std::string &root,
    const std::string &victimPath)
{
    SetCheckStage("junction/in-tree-removed-as-itself");
    if (!Check(Sys_FileSystemRemoveTree(root.c_str())))
        return false;
    return Check(GetFileAttributesW(ExtendedPath(root).c_str())
            == INVALID_FILE_ATTRIBUTES)
        && Check(GetFileAttributesW(ExtendedPath(victimPath).c_str())
            != INVALID_FILE_ATTRIBUTES);
}
}

bool TestRemoveTreeJunctionContract(const std::string &workingDirectory)
{
    const std::string root = MakeUniquePath(workingDirectory) + "-junc";
    const std::string nested = Join(root, "nested");
    const std::string outside = MakeUniquePath(workingDirectory)
        + "-junc-target";
    const std::string victimPath = Join(outside, "victim.bin");
    const std::string junctionLeaf = Join(root, "junction-leaf");
    const std::string junctionInside = Join(nested, "junction-inside");

    if (!CreateJunctionContractFixture(root, nested, outside, victimPath))
        return false;
    if (!Check(CreateJunctionNative(junctionLeaf, outside))
        || !Check(CreateJunctionNative(junctionInside, outside)))
    {
        return false;
    }
    if (!VerifyJunctionLeafRefused(junctionLeaf, victimPath))
        return false;
    if (!VerifyJunctionInTreeRemovedAsItself(root, victimPath))
        return false;

    SetCheckStage("junction/cleanup");
    if (!Check(RemoveFileNative(victimPath)))
        return false;
    return Check(RemoveDirectoryNative(outside));
}
#endif // defined(_WIN32)

// Deterministic race-interference contract: a file held open without
// FILE_SHARE_DELETE must make the deletion service fail fast (the
// disposition conflicts immediately, under both POSIX-semantics and
// fallback deletion), must not silently remove the conflicting file, and
// must succeed on a retry after the interfering handle is released. No
// timing or scheduling is involved.
bool TestRemoveTreeOpenHandleRace(const std::string &workingDirectory)
{
#if !defined(_WIN32)
    // POSIX unlink succeeds regardless of open handles; the deterministic
    // sharing-conflict path is Win32-specific.
    (void)workingDirectory;
    return true;
#else
    const std::string root = MakeUniquePath(workingDirectory) + "-race";
    const std::string sub = Join(root, "sub");
    const std::string blockerPath = Join(root, "blocker.dat");
    const std::string deepPath = Join(sub, "deep.txt");

    SetCheckStage("open-handle-race/setup");
    if (!Check(Sys_FileSystemCreateDirectory(root.c_str()))
        || !Check(Sys_FileSystemCreateDirectory(sub.c_str()))
        || !Check(WriteFile(blockerPath))
        || !Check(WriteFile(deepPath)))
    {
        return false;
    }

    const HANDLE blocker = CreateFileW(
        ExtendedPath(blockerPath).c_str(),
        GENERIC_READ,
        0, // no sharing at all: the strongest deterministic interference
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (!Check(blocker != INVALID_HANDLE_VALUE))
        return false;

    SetCheckStage("open-handle-race/deletion-refused");
    if (!Check(!Sys_FileSystemRemoveTree(root.c_str())))
    {
        CloseHandle(blocker);
        return false;
    }
    if (!Check(GetFileAttributesW(ExtendedPath(blockerPath).c_str())
            != INVALID_FILE_ATTRIBUTES))
    {
        CloseHandle(blocker);
        return false;
    }

    SetCheckStage("open-handle-race/retry-after-release");
    CloseHandle(blocker);
    if (!Check(Sys_FileSystemRemoveTree(root.c_str())))
        return false;
    return Check(GetFileAttributesW(ExtendedPath(root).c_str())
        == INVALID_FILE_ATTRIBUTES);
#endif
}
}

int main()
{
    std::string workingDirectory;
    SetCheckStage("path-queries");
    if (!ReadPaths(&workingDirectory))
        return 1;
    SetCheckStage("root-parent-classification");
    if (!TestRootParentClassification())
        return 1;
    SetCheckStage("mkdir-classification-and-depth");
    if (!TestClassificationAndDepth(workingDirectory))
        return 1;
    SetCheckStage("ancestor-link-rejection");
    if (!TestAncestorLinks(workingDirectory))
        return 1;
    SetCheckStage("long-current-directory");
    if (!TestLongCurrentDirectory(workingDirectory))
        return 1;
    if (!TestBoundedDirectoryEnumeration(workingDirectory))
        return 1;
    if (!TestFilteredCollectionAndPathHelpers(workingDirectory))
        return 1;
    SetCheckStage("read-file-no-follow");
    if (!TestReadFileNoFollow(workingDirectory))
        return 1;
    SetCheckStage("handle-relative-recursive-deletion");
    if (!TestRemoveTreeContract(workingDirectory))
        return 1;
#if defined(_WIN32)
    SetCheckStage("junction-reparse-contracts");
    if (!TestRemoveTreeJunctionContract(workingDirectory))
        return 1;
#endif
    SetCheckStage("deterministic-open-handle-race");
    if (!TestRemoveTreeOpenHandleRace(workingDirectory))
        return 1;
    return 0;
}
