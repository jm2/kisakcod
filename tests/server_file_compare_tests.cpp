#include "qcommon/server_file_compare.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>

namespace
{
using server_file_compare::Callbacks;
using server_file_compare::FastFileReferences;
using server_file_compare::IwdReferences;
using server_file_compare::Result;

struct SizeRule
{
    const char *name = nullptr;
    bool gameDirectory = false;
    int fileSize = 0;
};

struct TestContext
{
    std::array<int, 4> localIwdChecksums{};
    std::size_t localIwdChecksumCount = 0;
    std::array<SizeRule, 8> sizeRules{};
    std::size_t sizeRuleCount = 0;
    std::size_t sizeLookupCount = 0;
    std::size_t unexpectedSizeLookups = 0;
    bool localIwdFileExists = false;
};

int gFailures;

template <std::size_t Capacity, std::size_t Length>
void SetText(
    std::array<char, Capacity> &destination,
    const char (&text)[Length])
{
    static_assert(Length <= Capacity, "test text must fit its destination");
    std::memcpy(destination.data(), text, Length);
}

void Expect(const bool condition, const char *const message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++gFailures;
    }
}

bool HasIwdChecksum(void *const rawContext, const int checksum)
{
    auto &context = *static_cast<TestContext *>(rawContext);
    for (std::size_t index = 0;
         index < context.localIwdChecksumCount;
         ++index)
    {
        if (context.localIwdChecksums[index] == checksum)
            return true;
    }
    return false;
}

bool IsServerPak(void *, const char *const name)
{
    return server_file_compare::IsServerOnlyIwdName(name);
}

bool IsOfficialMainIwd(void *, const char *const name)
{
    return std::strncmp(name, "main/iw_", 8) == 0;
}

bool LocalIwdFileExists(void *const rawContext, const char *)
{
    return static_cast<TestContext *>(rawContext)->localIwdFileExists;
}

int FastFileSize(
    void *const rawContext,
    const char *const name,
    const bool gameDirectory)
{
    auto &context = *static_cast<TestContext *>(rawContext);
    ++context.sizeLookupCount;
    for (std::size_t index = 0; index < context.sizeRuleCount; ++index)
    {
        const SizeRule &rule = context.sizeRules[index];
        if (rule.name
            && rule.gameDirectory == gameDirectory
            && std::strcmp(rule.name, name) == 0)
        {
            return rule.fileSize;
        }
    }
    ++context.unexpectedSizeLookups;
    return 0;
}

Callbacks MakeCallbacks(TestContext &context)
{
    return {
        &context,
        HasIwdChecksum,
        IsServerPak,
        IsOfficialMainIwd,
        LocalIwdFileExists,
        FastFileSize,
    };
}

void AddSizeRule(
    TestContext &context,
    const char *const name,
    const bool gameDirectory,
    const int fileSize)
{
    if (context.sizeRuleCount >= context.sizeRules.size())
    {
        Expect(false, "test fixture size-rule capacity must be sufficient");
        return;
    }
    context.sizeRules[context.sizeRuleCount++] = {
        name, gameDirectory, fileSize};
}

void TestExactGameDirectoryMembership()
{
    const char *const suffix = server_file_compare::GameDirectorySuffix(
        "MODS/Outer/Inner/Zones/MP_TEST",
        "mods/outer/inner");
    Expect(
        suffix && std::strcmp(suffix, "Zones/MP_TEST") == 0,
        "uppercase nested mod children must retain the suffix after the entire gameDir");
    Expect(
        server_file_compare::GameDirectorySuffix(
            "mods/example2/zone", "mods/example") == nullptr,
        "a gameDir prefix collision must not be a member");
    Expect(
        server_file_compare::GameDirectorySuffix(
            "custom/example/zone", "custom/example") == nullptr,
        "an unexpected non-mod dvar domain must not select mod storage");
    Expect(
        server_file_compare::GameDirectorySuffix(
            "mods/example/zone", "mods") == nullptr,
        "the mods root without a child directory must not be a gameDir");
}

void TestDownloadRequestAdmission()
{
    constexpr char iwdNames[] =
        "main/iw_00 mods/example/pak1 mods/example/sub/pak2";
    constexpr char fastFileNames[] =
        "common_mp mods/example/zone1 mods/example/sub/zone2";
    const auto isPermitted =
        [=](const char *const request)
        {
            const server_file_compare::DownloadKind kind =
                server_file_compare::ClassifyServerDownloadRequest(request);
            const char *const names =
                kind == server_file_compare::DownloadKind::Iwd
                ? iwdNames
                : fastFileNames;
            return server_file_compare::IsPermittedServerDownloadRequest(
                request, "mods/example", names, kind);
        };

    Expect(
        isPermitted("mods/example/pak1.iwd"),
        "an exact advertised mod IWD request must be admitted");
    Expect(
        isPermitted("mods/example/sub/zone2.ff"),
        "an exact advertised nested mod FF request must be admitted");

    struct RejectedRequest
    {
        const char *request;
        const char *description;
    };
    constexpr RejectedRequest rejected[] = {
        {nullptr, "a null request must fail"},
        {"", "an empty request must fail"},
        {"../../server.cfg", "a traversal request must fail"},
        {"main/server.cfg", "a safe-looking unadvertised server file must fail"},
        {"mods/example/server.cfg", "an unadvertised mod file must fail"},
        {"mods/example/pak1", "a request without an extension must fail"},
        {"mods/example/pak1.IWD", "a noncanonical extension must fail"},
        {"mods/example/pak1.iwd/extra", "an extension before the end must fail"},
        {"mods/example2/pak1.iwd", "a mod prefix collision must fail"},
        {"mods/example/pak1.ff", "an IWD token cannot authorize an FF request"},
        {"mods/example/pak1.iwd ", "trailing whitespace must fail"},
        {"mods/example/CON.iwd", "a Windows device alias must fail"},
        {"mods/example/pak_SvR_1.iwd", "a server-only IWD must fail"},
        {"updates/patch.exe", "the unauthenticated legacy update namespace must fail closed"},
    };
    for (const RejectedRequest &testCase : rejected)
    {
        Expect(!isPermitted(testCase.request), testCase.description);
    }

    Expect(
        !isPermitted("MODS/EXAMPLE/pak1.iwd"),
        "request identity must exactly match the advertised token even when the mod prefix aliases");
    Expect(
        !server_file_compare::IsPermittedServerDownloadRequest(
            "mods/example/pak1.iwd",
            "",
            iwdNames,
            server_file_compare::DownloadKind::Iwd),
        "an empty active mod must fail closed");
    Expect(
        !server_file_compare::IsPermittedServerDownloadRequest(
            "mods/example/pak1.iwd",
            "mods/example",
            nullptr,
            server_file_compare::DownloadKind::Iwd),
        "a missing IWD advertisement list must fail closed");
    Expect(
        !server_file_compare::IsPermittedServerDownloadRequest(
            "mods/example/pak_SvR_1.iwd",
            "mods/example",
            "mods/example/pak_SvR_1",
            server_file_compare::DownloadKind::Iwd),
        "an exactly advertised mixed-case server-only IWD must fail closed");

    constexpr char boundedNames[] =
        "mods/example/pak1 mods/example/not-in-view";
    Expect(
        !server_file_compare::IsPermittedServerDownloadRequest(
            "mods/example/not-in-view.iwd",
            "mods/example",
            boundedNames,
            std::strlen("mods/example/pak1"),
            server_file_compare::DownloadKind::Iwd),
        "authorization must not scan beyond the bounded SYSTEMINFO value");

    std::array<char, server_file_compare::kServerDownloadNameCapacity> exact{};
    exact.fill('x');
    constexpr char prefix[] = "mods/example/";
    std::memcpy(exact.data(), prefix, sizeof(prefix) - 1);
    constexpr char extension[] = ".ff";
    std::memcpy(
        exact.data() + exact.size() - sizeof(extension),
        extension,
        sizeof(extension));
    std::array<char, server_file_compare::kServerDownloadNameCapacity>
        exactStem{};
    std::memcpy(
        exactStem.data(),
        exact.data(),
        exact.size() - sizeof(extension));
    Expect(
        std::strlen(exact.data())
            == server_file_compare::kServerDownloadNameCapacity - 1,
        "the boundary request fixture must occupy 63 bytes plus NUL");
    Expect(
        server_file_compare::IsPermittedServerDownloadRequest(
            exact.data(),
            "mods/example",
            exactStem.data(),
            server_file_compare::DownloadKind::FastFile),
        "a complete 63-byte advertised request must fit the protocol field");

    std::array<char, server_file_compare::kServerDownloadNameCapacity + 1>
        tooLong{};
    tooLong.fill('x');
    std::memcpy(tooLong.data(), prefix, sizeof(prefix) - 1);
    std::memcpy(
        tooLong.data() + tooLong.size() - sizeof(extension),
        extension,
        sizeof(extension));
    Expect(
        !server_file_compare::IsPermittedServerDownloadRequest(
            tooLong.data(),
            "mods/example",
            tooLong.data(),
            server_file_compare::DownloadKind::FastFile),
        "a 64-byte request must be rejected instead of truncated");

    std::array<char, 61> stem{};
    stem.fill('x');
    stem.back() = '\0';
    Expect(
        !server_file_compare::CanStoreServerDownloadName(
            stem.data(),
            ".iwd"),
        "a 60-byte IWD stem must not be truncated into the protocol field");
    stem[59] = '\0';
    Expect(
        server_file_compare::CanStoreServerDownloadName(
            stem.data(),
            ".iwd"),
        "a 59-byte IWD stem plus extension and NUL must fit");
    stem[59] = 'x';
    Expect(
        server_file_compare::CanStoreServerDownloadName(
            stem.data(),
            ".ff"),
        "a 60-byte FF stem plus extension and NUL must fit");
    stem[60] = 'x';
    std::array<char, 62> oversizedFastFileStem{};
    oversizedFastFileStem.fill('x');
    oversizedFastFileStem.back() = '\0';
    Expect(
        !server_file_compare::CanStoreServerDownloadName(
            oversizedFastFileStem.data(),
            ".ff"),
        "a 61-byte FF stem must not be truncated into the protocol field");
}

void TestEmptyGameDirectoryIsNotDownloadable()
{
    std::array<char, 128> output{};
    TestContext context;
    const Callbacks callbacks = MakeCallbacks(context);

    const char *ordinaryNames[] = {"custom/pak1"};
    const int ordinaryChecksums[] = {11};
    Result result = server_file_compare::CompareAll(
        output.data(),
        output.size(),
        true,
        "",
        {ordinaryNames, ordinaryChecksums, 1},
        {},
        callbacks);
    Expect(
        result == Result::NotDownloadable,
        "an ordinary missing IWD cannot be downloaded without fs_game");
    Expect(
        std::strcmp(output.data(), "custom/pak1.iwd") == 0,
        "a representable ordinary IWD culprit must be complete");

    const char *serverPakNames[] = {"custom/pak_svr_1"};
    const int serverPakChecksums[] = {12};
    SetText(output, "before");
    result = server_file_compare::CompareAll(
        output.data(),
        output.size(),
        true,
        "",
        {serverPakNames, serverPakChecksums, 1},
        {},
        callbacks);
    Expect(
        result == Result::NotDownloadable,
        "a missing _svr_ IWD cannot be silently skipped when fs_game is empty");
    Expect(
        std::strcmp(output.data(), "custom/pak_svr_1.iwd") == 0,
        "the missing _svr_ IWD culprit must be complete");

    const char *fastFileNames[] = {"common_mp"};
    const int fastFileSizes[] = {1234};
    AddSizeRule(context, "common_mp", false, 0);
    result = server_file_compare::CompareAll(
        output.data(),
        output.size(),
        true,
        "",
        {},
        {fastFileNames, fastFileSizes, 1},
        callbacks);
    Expect(
        result == Result::NotDownloadable,
        "a missing FF cannot be downloaded without fs_game");
    Expect(
        std::strcmp(output.data(), "common_mp.ff") == 0,
        "the missing FF culprit must be complete");
}

void TestMatchingLocalFiles()
{
    std::array<char, 128> output{};
    SetText(output, "stale");
    TestContext context;
    context.localIwdChecksums[0] = 77;
    context.localIwdChecksumCount = 1;
    AddSizeRule(context, "zone1", true, 4096);

    const char *iwdNames[] = {"mods/example/pak1"};
    const int iwdChecksums[] = {77};
    const char *fastFileNames[] = {"mods/example/zone1"};
    const int fastFileSizes[] = {4096};
    const Result result = server_file_compare::CompareAll(
        output.data(),
        output.size(),
        true,
        "mods/example",
        {iwdNames, iwdChecksums, 1},
        {fastFileNames, fastFileSizes, 1},
        MakeCallbacks(context));
    Expect(
        result == Result::Match,
        "matching local IWD checksum and FF size must match");
    Expect(output[0] == '\0', "a complete match must publish an empty list");
    Expect(
        context.unexpectedSizeLookups == 0,
        "matching mod FFs must use the expected game-directory lookup");
}

void TestServerOnlyIwdIsNotAdvertised()
{
    std::array<char, 128> output{};
    const char *names[] = {"mods/example/pak_SvR_1"};
    const int checksums[] = {77};
    TestContext context;
    const Result result = server_file_compare::CompareAll(
        output.data(),
        output.size(),
        true,
        "mods/example",
        {names, checksums, 1},
        {},
        MakeCallbacks(context));
    Expect(
        result == Result::Match,
        "a server-only IWD must not be advertised to a client");
    Expect(
        output[0] == '\0',
        "skipping a server-only IWD must leave the download list empty");
}

void TestExactModDownloadsAndAggregate()
{
    std::array<char, 256> output{};
    TestContext context;
    AddSizeRule(context, "zone1", true, 0);

    const char *iwdNames[] = {"mods/example/pak1"};
    const int iwdChecksums[] = {13};
    const char *fastFileNames[] = {"mods/example/zone1"};
    const int fastFileSizes[] = {2048};
    const Result result = server_file_compare::CompareAll(
        output.data(),
        output.size(),
        true,
        "mods/example",
        {iwdNames, iwdChecksums, 1},
        {fastFileNames, fastFileSizes, 1},
        MakeCallbacks(context));
    Expect(
        result == Result::NeedDownload,
        "exact mod children missing locally must be downloadable");
    Expect(
        std::strcmp(
            output.data(),
            "@mods/example/pak1.iwd@mods/example/pak1.iwd"
            "@mods/example/zone1.ff@mods/example/zone1.ff") == 0,
        "IWD and FF pairs must form one complete aggregate");
}

void TestPrefixCollisionIsNotDownloadable()
{
    std::array<char, 128> output{};
    TestContext context;
    AddSizeRule(context, "mods/example2/zone", false, 0);

    const char *names[] = {"mods/example2/zone"};
    const int sizes[] = {100};
    const Result result = server_file_compare::CompareAll(
        output.data(),
        output.size(),
        true,
        "mods/example",
        {},
        {names, sizes, 1},
        MakeCallbacks(context));
    Expect(
        result == Result::NotDownloadable,
        "a gameDir prefix collision must not become a download");
    Expect(
        std::strcmp(output.data(), "mods/example2/zone.ff") == 0,
        "a prefix-collision culprit must remain complete");
    Expect(
        context.unexpectedSizeLookups == 0,
        "a prefix collision must use the ordinary full-name DB lookup");
}

void TestUppercaseNestedModLookup()
{
    std::array<char, 256> output{};
    TestContext context;
    AddSizeRule(context, "Zones/MP_TEST", true, 0);

    const char *names[] = {"MODS/Outer/Inner/Zones/MP_TEST"};
    const int sizes[] = {9000};
    const Result result = server_file_compare::CompareAll(
        output.data(),
        output.size(),
        true,
        "mods/outer/inner",
        {},
        {names, sizes, 1},
        MakeCallbacks(context));
    Expect(
        result == Result::NeedDownload,
        "uppercase nested mod children must remain downloadable");
    Expect(
        context.sizeLookupCount == 1
            && context.unexpectedSizeLookups == 0,
        "DB lookup must receive the suffix after the entire nested gameDir");
}

void TestMultiplePairs()
{
    std::array<char, 256> output{};
    TestContext context;
    AddSizeRule(context, "zone1", true, 0);
    AddSizeRule(context, "sub/zone2", true, 0);

    const char *names[] = {
        "mods/example/zone1", "mods/example/sub/zone2"};
    const int sizes[] = {1, 2};
    const Result result = server_file_compare::CompareAll(
        output.data(),
        output.size(),
        true,
        "mods/example",
        {},
        {names, sizes, 2},
        MakeCallbacks(context));
    Expect(result == Result::NeedDownload, "multiple missing FFs must download");
    Expect(
        std::strcmp(
            output.data(),
            "@mods/example/zone1.ff@mods/example/zone1.ff"
            "@mods/example/sub/zone2.ff@mods/example/sub/zone2.ff") == 0,
        "multiple download pairs must append without separators or truncation");
}

void TestExactCapacityBoundary()
{
    std::array<char, 508> remote{};
    std::array<char, 509> local{};
    remote.fill('r');
    local.fill('l');
    remote.back() = '\0';
    local.back() = '\0';

    std::array<char, 1024> exact{};
    Expect(
        server_file_compare::AppendDownloadPair(
            exact.data(),
            exact.size(),
            remote.data(),
            local.data(),
            ".ff"),
        "1023 bytes of pair data plus NUL must fit a 1024-byte output");
    Expect(
        std::strlen(exact.data()) == 1023 && exact.back() == '\0',
        "the exact-capacity pair must be terminated at byte 1023");

    std::array<char, 1023> oneByteShort{};
    const auto before = oneByteShort;
    Expect(
        !server_file_compare::AppendDownloadPair(
            oneByteShort.data(),
            oneByteShort.size(),
            remote.data(),
            local.data(),
            ".ff"),
        "a 1023-byte capacity must reject 1023 bytes without NUL room");
    Expect(
        oneByteShort == before,
        "an exact-boundary append failure must not alter output");
}

void TestAggregateRollbackOnLaterOverflow()
{
    std::array<char, 64> output{};
    SetText(output, "unchanged");
    const auto before = output;
    TestContext context;
    AddSizeRule(
        context,
        "abcdefghijklmnopqrstuvwxyz0123456789",
        true,
        0);

    const char *iwdNames[] = {"mods/x/p"};
    const int iwdChecksums[] = {1};
    const char *fastFileNames[] = {
        "mods/x/abcdefghijklmnopqrstuvwxyz0123456789"};
    const int fastFileSizes[] = {2};
    const Result result = server_file_compare::CompareAll(
        output.data(),
        output.size(),
        true,
        "mods/x",
        {iwdNames, iwdChecksums, 1},
        {fastFileNames, fastFileSizes, 1},
        MakeCallbacks(context));
    Expect(
        result == Result::NotDownloadable,
        "a later FF capacity failure must produce a safe result");
    Expect(
        output == before,
        "a later FF overflow must not publish the earlier IWD pair");
}

void TestHumanListAtomicity()
{
    std::array<char, 96> output{};
    SetText(output, "first.iwd\n");
    Expect(
        server_file_compare::AppendMissingLine(
            output.data(),
            output.size(),
            "mods/example/pak2",
            ".iwd",
            " (local file exists with wrong checksum)"),
        "a complete human-readable missing line must append");
    Expect(
        std::strcmp(
            output.data(),
            "first.iwd\nmods/example/pak2.iwd"
            " (local file exists with wrong checksum)\n") == 0,
        "the human-readable line must include its annotation and newline");

    std::array<char, 32> tooSmall{};
    SetText(tooSmall, "existing\n");
    const auto before = tooSmall;
    Expect(
        !server_file_compare::AppendMissingLine(
            tooSmall.data(),
            tooSmall.size(),
            "mods/example/a-very-long-pak-name",
            ".iwd",
            " (local file exists with wrong checksum)"),
        "an oversized human-readable line must fail");
    Expect(
        tooSmall == before,
        "a human-readable line failure must leave the aggregate unchanged");
}

void TestUnrepresentableSemanticCulpritIsAtomic()
{
    std::array<char, 16> output{};
    SetText(output, "unchanged");
    const auto before = output;
    TestContext context;
    const char *names[] = {"ordinary/path/that/cannot/fit"};
    const int checksums[] = {9};
    const Result result = server_file_compare::CompareAll(
        output.data(),
        output.size(),
        true,
        "",
        {names, checksums, 1},
        {},
        MakeCallbacks(context));
    Expect(
        result == Result::NotDownloadable,
        "an unrepresentable semantic culprit must remain non-downloadable");
    Expect(
        output == before,
        "an unrepresentable semantic culprit must not publish truncation");
}

void TestUnstorableDownloadNameIsNotAdvertised()
{
    std::array<char, 65> longName{};
    longName.fill('x');
    constexpr char prefix[] = "mods/x/";
    std::memcpy(longName.data(), prefix, sizeof(prefix) - 1);
    longName.back() = '\0';

    std::array<char, 128> output{};
    TestContext context;
    const char *names[] = {longName.data()};
    const int checksums[] = {123};
    const Result result = server_file_compare::CompareAll(
        output.data(),
        output.size(),
        true,
        "mods/x",
        {names, checksums, 1},
        {},
        MakeCallbacks(context));
    Expect(
        result == Result::NotDownloadable,
        "a missing name that cannot fit the server field must not be advertised");
    Expect(
        std::strlen(output.data()) == std::strlen(longName.data()) + 4
            && std::memcmp(
                output.data(),
                longName.data(),
                std::strlen(longName.data())) == 0
            && std::strcmp(
                output.data() + std::strlen(longName.data()),
                ".iwd") == 0,
        "an unstorable request must publish only a complete diagnostic filename");

    output.fill('\0');
    TestContext fastFileContext;
    AddSizeRule(
        fastFileContext,
        longName.data() + sizeof(prefix) - 1,
        true,
        0);
    const int fileSizes[] = {456};
    const Result fastFileResult = server_file_compare::CompareAll(
        output.data(),
        output.size(),
        true,
        "mods/x",
        {},
        {names, fileSizes, 1},
        MakeCallbacks(fastFileContext));
    Expect(
        fastFileResult == Result::NotDownloadable,
        "an unstorable missing FF name must not be advertised");
    Expect(
        std::strlen(output.data()) == std::strlen(longName.data()) + 3
            && std::memcmp(
                output.data(),
                longName.data(),
                std::strlen(longName.data())) == 0
            && std::strcmp(
                output.data() + std::strlen(longName.data()),
                ".ff") == 0,
        "an unstorable FF request must publish a complete diagnostic filename");
}
}

int main()
{
    static_assert(sizeof(void *) == sizeof(std::size_t));
    static_assert(
        static_cast<int>(Result::Match) == 0
        && static_cast<int>(Result::NeedDownload) == 1
        && static_cast<int>(Result::NotDownloadable) == 2);

    TestExactGameDirectoryMembership();
    TestDownloadRequestAdmission();
    TestEmptyGameDirectoryIsNotDownloadable();
    TestMatchingLocalFiles();
    TestServerOnlyIwdIsNotAdvertised();
    TestExactModDownloadsAndAggregate();
    TestPrefixCollisionIsNotDownloadable();
    TestUppercaseNestedModLookup();
    TestMultiplePairs();
    TestExactCapacityBoundary();
    TestAggregateRollbackOnLaterOverflow();
    TestHumanListAtomicity();
    TestUnrepresentableSemanticCulpritIsAtomic();
    TestUnstorableDownloadNameIsNotAdvertised();

    if (gFailures != 0)
    {
        std::fprintf(stderr, "%d server-file comparison test(s) failed\n", gFailures);
        return 1;
    }
    return 0;
}
