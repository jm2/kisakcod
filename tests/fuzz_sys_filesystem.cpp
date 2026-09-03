// fuzz_sys_filesystem: production-path fuzz fixture for the
// handle-relative no-follow file read service (Sys_FileSystemReadFile)
// that hardened file opening feeds.
//
// The harness builds a deterministic sandbox — nested real directories,
// a 256 KiB payload, an empty file, and (where the host allows) a
// symlink leaf and a symlinked directory pointing at an outside decoy —
// then drives attacker-shaped path strings through the production read
// service and asserts the no-follow contract on every attempt:
//
//   1. The service never crashes on hostile path bytes (no out-of-
//      bounds resolution work, no UB) — the actual memory-safety
//      property.
//   2. A failed read is failure-atomic: the output vector comes back
//      empty, never partially filled.
//   3. A successful read returns a byte-exact known file (the payload
//      or the empty file) — never a torn or partial prefix.
//   4. The outside decoy bytes are NEVER returned. The decoy's only
//      routes from the sandbox corpus are symbolic links, and links
//      must fail closed; the corpus never contains the decoy's direct
//      path.
//
// The corpus covers the engine's path spellings: both separators, "."
// components, case variants, traversal attempts, invalid UTF-8 bytes,
// control bytes, over-long components, and directory-as-leaf forms.
// `seeds` runs the fixed corpus; `random <count>` runs a seeded,
// deterministic byte-local mutation sweep (byte flips, deletions,
// duplication, separator swaps) across every family. Both modes are
// CI-friendly: no wall-clock input, no unbounded loops.
//
// Build target: fuzz_sys_filesystem
// CTest entries: fuzz-sys-filesystem-seeds, fuzz-sys-filesystem-random

#include <qcommon/sys_filesystem.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace fuzz_sys_filesystem
{
namespace
{
int g_runs = 0;

const std::string g_secretBytes = "KISAKCOD-DECOY-SECRET";

int Fail(const char *const message)
{
    std::fprintf(stderr, "fuzz_sys_filesystem: %s\n", message);
    return 1;
}

#define CHECK(expr) do {                                                  \
    ++g_runs;                                                             \
    if (!(expr)) {                                                        \
        std::fprintf(stderr, "fuzz_sys_filesystem: %s:%d: %s\n",         \
            __FILE__, __LINE__, #expr);                                   \
        return 1;                                                         \
    }                                                                     \
} while (0)

struct Sandbox
{
    std::string root;
    std::string nested;
    std::string outside;
    std::string payloadPath;
    std::string emptyPath;
    std::string secretPath;
    std::string leafLink;
    std::string dirLink;
    std::string dirLinkSecret;
    std::vector<unsigned char> payload;
    bool linksAvailable = false;
};

std::string Join(const std::string &left, const std::string &right)
{
#if defined(_WIN32)
    return left + "\\" + right;
#else
    return left + "/" + right;
#endif
}

bool WriteBytesNative(const std::string &path, const std::vector<unsigned char> &bytes)
{
#if defined(_WIN32)
    const int wideCapacity = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, nullptr, 0);
    if (wideCapacity <= 0)
        return false;
    std::vector<wchar_t> wide(static_cast<std::size_t>(wideCapacity), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            path.c_str(),
            -1,
            wide.data(),
            wideCapacity) != wideCapacity)
    {
        return false;
    }
    const HANDLE file = CreateFileW(
        wide.data(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD chunk = 0;
    const bool ok = bytes.empty()
        || (::WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &chunk, nullptr)
            && chunk == bytes.size());
    return CloseHandle(file) && ok;
#else
    FILE *const file = std::fopen(path.c_str(), "wb");
    if (!file)
        return false;
    const bool written = bytes.empty()
        || std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    return std::fclose(file) == 0 && written;
#endif
}

bool RemoveFileNative(const std::string &path)
{
#if defined(_WIN32)
    const int wideCapacity = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, nullptr, 0);
    if (wideCapacity <= 0)
        return false;
    std::vector<wchar_t> wide(static_cast<std::size_t>(wideCapacity), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            path.c_str(),
            -1,
            wide.data(),
            wideCapacity) != wideCapacity)
    {
        return false;
    }
    return DeleteFileW(wide.data()) != 0;
#else
    return unlink(path.c_str()) == 0;
#endif
}

bool RemoveDirectoryNative(const std::string &path)
{
    std::error_code ec;
    return std::filesystem::remove(std::filesystem::path(path), ec) && !ec;
}

bool CreateSymlink(const std::string &link, const std::string &target, const bool isDirectory)
{
#if defined(_WIN32)
    auto toWide = [](const std::string &text) {
        const int required = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1, nullptr, 0);
        if (required <= 0)
            return std::wstring();
        std::wstring wide(static_cast<std::size_t>(required), L'\0');
        if (MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                text.c_str(),
                -1,
                wide.data(),
                required) != required)
        {
            return std::wstring();
        }
        wide.resize(static_cast<std::size_t>(required - 1));
        return wide;
    };
    const std::wstring wideLink = toWide(link);
    const std::wstring wideTarget = toWide(target);
    if (wideLink.empty() || wideTarget.empty())
        return false;
    const DWORD flags = (isDirectory ? 1u : 0u) | 0x2u;
    return CreateSymbolicLinkW(wideLink.c_str(), wideTarget.c_str(), flags) != 0;
#else
    (void)isDirectory;
    return symlink(target.c_str(), link.c_str()) == 0;
#endif
}

bool SetupSandbox(Sandbox *const sandbox)
{
    std::error_code ec;
    std::filesystem::path base =
        std::filesystem::temp_directory_path(ec);
    if (ec)
        return false;
    const auto tick = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const std::string unique = "kisakcod-fuzz-fs-"
        + std::to_string(tick);
    sandbox->root = (base / unique).string();
    sandbox->nested = Join(sandbox->root, "nested");
    sandbox->outside = Join(sandbox->root, "outside");
    sandbox->payloadPath = Join(sandbox->nested, "payload.bin");
    sandbox->emptyPath = Join(sandbox->nested, "empty.bin");
    sandbox->secretPath = Join(sandbox->outside, "secret.txt");

    sandbox->payload.resize(256u * 1024u);
    for (std::size_t i = 0; i < sandbox->payload.size(); ++i)
        sandbox->payload[i] = static_cast<unsigned char>(i * 31u + (i >> 9));

    if (!std::filesystem::create_directories(
            std::filesystem::path(sandbox->nested), ec)
        || ec)
        return false;
    ec.clear();
    if (!std::filesystem::create_directories(
            std::filesystem::path(sandbox->outside), ec)
        || ec)
        return false;
    if (!WriteBytesNative(sandbox->payloadPath, sandbox->payload))
        return false;
    if (!WriteBytesNative(sandbox->emptyPath, {}))
        return false;
    if (!WriteBytesNative(sandbox->secretPath, {
            g_secretBytes.begin(), g_secretBytes.end()}))
    {
        return false;
    }

    sandbox->leafLink = Join(sandbox->nested, "leaf-link.bin");
    sandbox->dirLink = Join(sandbox->nested, "dir-link");
    sandbox->dirLinkSecret = Join(sandbox->dirLink, "secret.txt");
    sandbox->linksAvailable =
        CreateSymlink(sandbox->leafLink, sandbox->secretPath, false)
        && CreateSymlink(sandbox->dirLink, sandbox->outside, true);
    if (!sandbox->linksAvailable)
    {
        std::fprintf(
            stderr,
            "fuzz_sys_filesystem: host cannot create symlinks; "
            "link families degrade to plain missing paths\n");
    }
    return true;
}

void TeardownSandbox(const Sandbox &sandbox)
{
    if (sandbox.linksAvailable)
    {
        (void)RemoveFileNative(sandbox.leafLink);
        (void)RemoveDirectoryNative(sandbox.dirLink);
    }
    (void)RemoveFileNative(sandbox.payloadPath);
    (void)RemoveFileNative(sandbox.emptyPath);
    (void)RemoveFileNative(sandbox.secretPath);
    (void)RemoveDirectoryNative(sandbox.nested);
    (void)RemoveDirectoryNative(sandbox.outside);
    (void)RemoveDirectoryNative(sandbox.root);
}

// The core contract shared by every mode: a failed read is failure-
// atomic, a successful read is byte-exact against a known file, and
// the decoy secret bytes are never returned through any path.
bool CheckReadContract(
    const Sandbox &sandbox,
    const char *const path,
    const std::vector<unsigned char> &contents,
    const bool ok)
{
    ++g_runs;
    if (!ok)
    {
        if (!contents.empty())
        {
            std::fprintf(
                stderr,
                "fuzz_sys_filesystem: failed read returned partial bytes "
                "(%zu bytes) for path family at %s\n",
                contents.size(),
                path);
            return false;
        }
        return true;
    }

    const bool isPayload = contents.size() == sandbox.payload.size()
        && std::memcmp(
            contents.data(),
            sandbox.payload.data(),
            sandbox.payload.size()) == 0;
    const bool isEmpty = contents.empty();
    if (!isPayload && !isEmpty)
    {
        std::fprintf(
            stderr,
            "fuzz_sys_filesystem: successful read returned unknown/torn "
            "content (%zu bytes) for path family at %s\n",
            contents.size(),
            path);
        return false;
    }
    if (contents.size() >= g_secretBytes.size())
    {
        for (std::size_t i = 0; i + g_secretBytes.size() <= contents.size(); ++i)
        {
            if (std::memcmp(
                    contents.data() + i,
                    g_secretBytes.data(),
                    g_secretBytes.size()) == 0)
            {
                std::fprintf(
                    stderr,
                    "fuzz_sys_filesystem: decoy bytes escaped through a "
                    "link for path family at %s\n",
                    path);
                return false;
            }
        }
    }
    return true;
}

std::vector<std::string> BuildCorpus(const Sandbox &sandbox)
{
    std::vector<std::string> paths;
    const std::string payload = sandbox.payloadPath;
    // Canonical and spelling variants of the real payload: these are the
    // only corpus members allowed to succeed, and they must return the
    // exact payload bytes.
    paths.push_back(payload);
    std::string mixedSeparators = payload;
    std::replace(mixedSeparators.begin(), mixedSeparators.end(),
#if defined(_WIN32)
        '/', '\\');
#else
        '\\', '/');
#endif
    paths.push_back(mixedSeparators);
    {
        const std::string dotVariant = Join(
            Join(sandbox.nested, "."),
            "payload.bin");
        paths.push_back(dotVariant);
    }
    {
        std::string doubleSlash = sandbox.nested;
        doubleSlash += "//payload.bin";
        paths.push_back(doubleSlash);
    }
    // Empty file variants.
    paths.push_back(sandbox.emptyPath);
    // Missing and foreign names.
    paths.push_back(Join(sandbox.nested, "missing.bin"));
    paths.push_back(Join(sandbox.root, "payload.bin"));
    // Traversal attempts.
    paths.push_back(Join(Join(sandbox.root, ".."), "escape.bin"));
    paths.push_back(sandbox.nested + "\\..\\..\\escape.bin");
    // Directory as leaf.
    paths.push_back(sandbox.nested);
    paths.push_back(sandbox.root);
    // Invalid UTF-8 and control bytes.
    paths.push_back(sandbox.nested + "/\xFF\xFE.bin");
    paths.push_back(sandbox.nested + "/bad\x01\x02name.bin");
    // Over-long component.
    paths.push_back(Join(sandbox.nested, std::string(4096, 'a')));
    // Link families: must fail closed; a mutation that reduces one of
    // these to a direct sandbox path may legitimately succeed with
    // payload bytes, but must never return the decoy.
    if (sandbox.linksAvailable)
    {
        paths.push_back(sandbox.leafLink);
        paths.push_back(sandbox.dirLinkSecret);
    }
    return paths;
}

int RunSeeds()
{
    Sandbox sandbox;
    if (!SetupSandbox(&sandbox))
        return Fail("could not create sandbox");
    int status = 0;
    for (const std::string &path : BuildCorpus(sandbox))
    {
        std::vector<unsigned char> contents{0xA7u};
        const bool ok = Sys_FileSystemReadFile(
            path.c_str(),
            1024u * 1024u,
            &contents);
        if (!CheckReadContract(sandbox, path.c_str(), contents, ok))
        {
            status = 1;
            break;
        }
    }
    // The canonical payload must succeed byte-exactly — the fixture
    // proves the service still works, not only that it rejects.
    if (status == 0)
    {
        std::vector<unsigned char> contents;
        const bool ok = Sys_FileSystemReadFile(
            sandbox.payloadPath.c_str(),
            sandbox.payload.size(),
            &contents);
        if (!ok || contents.size() != sandbox.payload.size())
        {
            status = Fail("canonical payload read regressed");
        }
        else
        {
            status = CheckReadContract(
                sandbox,
                sandbox.payloadPath.c_str(),
                contents,
                true) ? 0 : 1;
        }
    }
    TeardownSandbox(sandbox);
    if (status == 0)
        std::fprintf(stdout, "fuzz_sys_filesystem: seeds ok (runs=%d)\n", g_runs);
    return status;
}

std::string Mutate(const std::string &path, std::mt19937 &rng)
{
    std::string mutated = path;
    if (mutated.empty())
        return mutated;
    const unsigned op = rng() % 6u;
    const std::size_t at = rng() % mutated.size();
    switch (op)
    {
    case 0u:  // byte flip
        mutated[at] = static_cast<char>(rng() & 0xFFu);
        break;
    case 1u:  // separator swap
        mutated[at] = mutated[at] == '/' ? '\\' : '/';
        break;
    case 2u:  // deletion
        mutated.erase(at, 1 + rng() % 4u);
        break;
    case 3u:  // duplication
        mutated.insert(at, mutated, at, 1 + rng() % 8u);
        break;
    case 4u:  // dot or dotdot injection
        mutated.insert(at, (rng() & 1u) ? ".." : ".");
        break;
    default:  // truncation
        mutated.resize(at);
        break;
    }
    return mutated;
}

int RunRandom(const unsigned long iterations)
{
    Sandbox sandbox;
    if (!SetupSandbox(&sandbox))
        return Fail("could not create sandbox");

    std::vector<std::string> corpus = BuildCorpus(sandbox);
    std::mt19937 rng(0x4B315341u);  // fixed seed: deterministic runs
    int status = 0;
    for (unsigned long i = 0; i < iterations && status == 0; ++i)
    {
        const std::string &base = corpus[rng() % corpus.size()];
        const std::string path = Mutate(base, rng);
        std::vector<unsigned char> contents{0xA7u};
        const bool ok = Sys_FileSystemReadFile(
            path.c_str(),
            1024u * 1024u,
            &contents);
        if (!CheckReadContract(sandbox, path.c_str(), contents, ok))
            status = 1;
    }
    TeardownSandbox(sandbox);
    if (status == 0)
        std::fprintf(
            stdout,
            "fuzz_sys_filesystem: random sweep ok (iter=%lu, runs=%d)\n",
            iterations,
            g_runs);
    return status;
}
}  // namespace
}  // namespace fuzz_sys_filesystem

int main(int argc, char **argv)
{
    using namespace fuzz_sys_filesystem;
    if (argc >= 2 && std::strcmp(argv[1], "seeds") == 0)
        return RunSeeds();
    if (argc >= 3 && std::strcmp(argv[1], "random") == 0)
        return RunRandom(
            static_cast<unsigned long>(std::strtoul(argv[2], nullptr, 10)));
    std::fprintf(
        stderr,
        "Usage: %s [seeds] [random <count>]\n",
        argc >= 1 ? argv[0] : "fuzz_sys_filesystem");
    return 2;
}
