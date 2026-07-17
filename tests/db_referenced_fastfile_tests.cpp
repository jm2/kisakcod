#include "database/db_referenced_fastfile.h"
#include "database/db_zone_slots.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>

namespace
{
struct ZoneFixture
{
    const char *name = "";
    bool modZone = false;
    std::int32_t fileSize = 0;
};

int g_failures;

void Expect(const bool condition, const char *const message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++g_failures;
    }
}

bool IsLocalized(const char *const name)
{
    return std::string_view(name).starts_with("localized_");
}

void TestSlotWalk()
{
    using namespace db::referenced_fastfile;

    ZoneFixture zones[db::zone_slots::kPhysicalZoneSlotCount]{};
    zones[db::zone_slots::kDefaultZoneSlot] = {
        "slot0-default", false, -100};
    zones[db::zone_slots::kFirstUsableZoneSlot] = {
        "slot1", false, -1};
    zones[db::zone_slots::kFirstUsableZoneSlot + 1] = {
        "localized_slot2", false, -2};
    zones[db::zone_slots::kPhysicalZoneSlotCount - 2] = {
        "slot31", false, 31};
    zones[db::zone_slots::kPhysicalZoneSlotCount - 1] = {
        "slot32", false, 32};

    std::array<std::size_t, 3> visited{};
    std::array<std::int32_t, 3> visitedFileSizes{};
    std::size_t visitedCount = 0;
    ForEachReferencedFastFile(
        zones,
        IsLocalized,
        [&](const std::size_t slot, const ZoneFixture &zone)
        {
            if (visitedCount < visited.size())
            {
                visited[visitedCount] = slot;
                visitedFileSizes[visitedCount] = zone.fileSize;
            }
            ++visitedCount;
        });

    Expect(visitedCount == visited.size(), "only slots 1, 31, and 32 must match");
    Expect(visited[0] == 1, "slot 1 must be visited first");
    Expect(visited[1] == 31, "slot 31 must retain table order");
    Expect(visited[2] == 32, "the highest usable slot must be visited last");
    Expect(
        visitedFileSizes == std::array<std::int32_t, 3>{-1, 31, 32},
        "checksum inputs must have the same filtering and slot order as names");
}

void TestZoneSlotConstants()
{
    using namespace db::zone_slots;

    static_assert(kDefaultZoneSlot == 0);
    static_assert(kFirstUsableZoneSlot == 1);
    static_assert(kUsableZoneSlotCount == 32);
    static_assert(kPhysicalZoneSlotCount == 33);
    static_assert(!IsUsableZoneSlot(kDefaultZoneSlot));
    static_assert(IsUsableZoneSlot(kFirstUsableZoneSlot));
    static_assert(IsUsableZoneSlot(kPhysicalZoneSlotCount - 1));
    static_assert(!IsUsableZoneSlot(kPhysicalZoneSlotCount));
    static_assert(
        !IsUsableZoneSlot((std::numeric_limits<std::size_t>::max)()));

    Expect(
        !IsUsableZoneSlot(kDefaultZoneSlot),
        "the reserved/default slot must not be usable");
    Expect(
        IsUsableZoneSlot(kFirstUsableZoneSlot),
        "slot one must be the first usable fast-file slot");
    Expect(
        IsUsableZoneSlot(kPhysicalZoneSlotCount - 1),
        "slot 32 must be the highest usable fast-file slot");
    Expect(
        !IsUsableZoneSlot(kPhysicalZoneSlotCount),
        "the first out-of-range physical slot must not be usable");
}

void TestSignedDecimalFormatting()
{
    using db::referenced_fastfile::FormatSignedDecimal;

    std::array<char, 12> exact{};
    Expect(
        FormatSignedDecimal(
            (std::numeric_limits<std::int32_t>::min)(),
            exact.data(),
            exact.size()),
        "INT32_MIN must fit its exact signed-decimal buffer");
    Expect(
        std::strcmp(exact.data(), "-2147483648") == 0,
        "signed decimal formatting must preserve the negative sign");
    Expect(exact.back() == '\0', "signed decimal output must be explicitly terminated");

    std::array<char, 11> tooSmall{};
    Expect(
        !FormatSignedDecimal(
            (std::numeric_limits<std::int32_t>::min)(),
            tooSmall.data(),
            tooSmall.size()),
        "signed decimal formatting must reject a buffer without NUL capacity");
}

void TestInfoStringPredicates()
{
    using info_string::IsSafeUnquotedPathTokenComponent;
    using info_string::IsSafeUnquotedValueComponent;

    static constexpr char controlValue[] = {
        'o', 'k', static_cast<char>(0x1f), '\0'};
    struct PredicateCase
    {
        const char *value;
        const char *description;
    };
    constexpr PredicateCase unsafeValues[] = {
        {"two words", "whitespace"},
        {"tab\tvalue", "tab whitespace"},
        {"bad\\value", "backslash"},
        {"bad;value", "semicolon"},
        {"bad\"value", "quote"},
        {controlValue, "control byte"},
        {"bad//value", "line-comment introducer"},
        {"bad/*value", "block-comment introducer"},
    };

    Expect(
        !IsSafeUnquotedValueComponent(nullptr),
        "a null value component must be rejected");
    Expect(
        IsSafeUnquotedValueComponent(""),
        "the base value predicate must permit an explicitly empty value");
    Expect(
        IsSafeUnquotedValueComponent("mods/example-zone_1"),
        "ordinary portable token characters must be accepted");
    Expect(
        IsSafeUnquotedValueComponent("zone@name"),
        "the generic value policy must not inherit download-list framing");
    for (const PredicateCase &testCase : unsafeValues)
    {
        Expect(
            !IsSafeUnquotedValueComponent(testCase.value),
            testCase.description);
        Expect(
            !IsSafeUnquotedPathTokenComponent(testCase.value),
            testCase.description);
    }

    Expect(
        !IsSafeUnquotedPathTokenComponent(nullptr),
        "a null path component must be rejected");
    Expect(
        IsSafeUnquotedPathTokenComponent(""),
        "the path primitive must leave nonempty policy to its callers");
    Expect(
        IsSafeUnquotedPathTokenComponent("mods/example-zone_1"),
        "a safe relative path component must be accepted");
    Expect(
        IsSafeUnquotedPathTokenComponent("mods/example.v1/zone.v2"),
        "single dots must remain valid filename characters");
    constexpr PredicateCase unsafePortablePaths[] = {
        {"zone.ff:stream", "an NTFS alternate-data-stream name must fail"},
        {"C:/zone.ff", "a drive/colon namespace path must fail"},
        {"mods/zone<copy", "a less-than metacharacter must fail"},
        {"mods/zone>copy", "a greater-than metacharacter must fail"},
        {"mods/zone|copy", "a pipe metacharacter must fail"},
        {"mods/zone?copy", "a question-mark wildcard must fail"},
        {"mods/zone*copy", "a non-leading star wildcard must fail"},
        {"CON", "a root CON device component must fail"},
        {"nul.ff", "a root NUL device name before an extension must fail"},
        {"mods/PrN.cfg", "a case-insensitive PRN component must fail"},
        {"mods/AUX/zone", "an intermediate AUX component must fail"},
        {"mods/COM1.ff", "a COM1 device name before an extension must fail"},
        {"mods/lPt9/zone", "a case-insensitive LPT9 component must fail"},
    };
    for (const PredicateCase &testCase : unsafePortablePaths)
    {
        Expect(
            !IsSafeUnquotedPathTokenComponent(testCase.value),
            testCase.description);
    }

    constexpr PredicateCase safePortableNearMisses[] = {
        {"console", "CON must not reject a longer ordinary name"},
        {"mods/printer/zone", "PRN must not reject an ordinary prefix"},
        {"auxiliary.ff", "AUX must not reject an ordinary prefix"},
        {"null.ff", "NUL must not reject an ordinary prefix"},
        {"mods/COM0.ff", "COM0 is not a reserved DOS device"},
        {"mods/com10.ff", "COM10 is not a reserved DOS device"},
        {"mods/LPT0/zone", "LPT0 is not a reserved DOS device"},
        {"mods/lpt10/zone", "LPT10 is not a reserved DOS device"},
        {"mods/xcom1.cfg", "an embedded device spelling is ordinary text"},
    };
    for (const PredicateCase &testCase : safePortableNearMisses)
    {
        Expect(
            IsSafeUnquotedPathTokenComponent(testCase.value),
            testCase.description);
    }

    Expect(
        !IsSafeUnquotedPathTokenComponent("/absolute"),
        "a leading slash must be rejected for path components");
    Expect(
        !IsSafeUnquotedPathTokenComponent("*wildcard"),
        "a leading star must be rejected for path components");
    Expect(
        !IsSafeUnquotedPathTokenComponent("mods/example/"),
        "a trailing slash must be rejected for path components");
    Expect(
        !IsSafeUnquotedPathTokenComponent("mods/../example"),
        "a traversal spelling must be rejected for path components");
    Expect(
        !IsSafeUnquotedPathTokenComponent("mods::example"),
        "a namespace spelling must be rejected for path components");
    Expect(
        !IsSafeUnquotedPathTokenComponent("mods@example"),
        "the download-list field delimiter must be rejected for paths");
}

void TestSignedDecimalTokenParsing()
{
    using info_string::TryParseSignedDecimalToken;

    int parsed = 17;
    Expect(
        TryParseSignedDecimalToken("2147483647", &parsed)
            && parsed == (std::numeric_limits<int>::max)(),
        "INT_MAX must parse as one complete signed-decimal token");
    Expect(
        TryParseSignedDecimalToken("-2147483648", &parsed)
            && parsed == (std::numeric_limits<int>::min)(),
        "INT_MIN must parse without intermediate signed overflow");
    Expect(
        TryParseSignedDecimalToken("0", &parsed) && parsed == 0,
        "zero must parse as a signed-decimal token");

    struct RejectedCase
    {
        const char *value;
        const char *description;
    };
    constexpr RejectedCase rejected[] = {
        {nullptr, "a null checksum token must fail"},
        {"", "an empty checksum token must fail"},
        {"+1", "a leading plus must fail"},
        {" 1", "leading whitespace must fail"},
        {"1 ", "trailing whitespace must fail"},
        {"1junk", "trailing junk must fail"},
        {"--1", "multiple signs must fail"},
        {"2147483648", "positive signed-int overflow must fail"},
        {"-2147483649", "negative signed-int overflow must fail"},
    };
    for (const RejectedCase &testCase : rejected)
    {
        parsed = 17;
        Expect(
            !TryParseSignedDecimalToken(testCase.value, &parsed),
            testCase.description);
        Expect(parsed == 17, "failed checksum parsing must be output-atomic");
    }

    Expect(
        !TryParseSignedDecimalToken("1", nullptr),
        "a null checksum destination must fail");
}

void ExpectRejectedNameFormatting(
    const char *const zoneName,
    const bool modZone,
    const char *const modDirectory,
    const char *const description)
{
    using namespace db::referenced_fastfile;

    ZoneFixture zones[db::zone_slots::kPhysicalZoneSlotCount]{};
    zones[db::zone_slots::kFirstUsableZoneSlot] = {zoneName, modZone};
    std::array<char, 96> output{};
    output.fill('#');
    const auto unchanged = output;
    Expect(
        !FormatReferencedFastFileNames(
            zones,
            modDirectory,
            IsLocalized,
            output.data(),
            output.size()),
        description);
    Expect(
        output == unchanged,
        "invalid referenced-name input must leave output unchanged");
}

void TestRejectedNameComponents()
{
    static constexpr char controlName[] = {
        'z', 'o', 'n', 'e', static_cast<char>(0x1f), '\0'};
    static constexpr char controlDirectory[] = {
        'm', 'o', 'd', 's', static_cast<char>(0x1f), '\0'};
    struct RejectedCase
    {
        const char *value;
        const char *description;
    };
    constexpr RejectedCase rejectedNames[] = {
        {"zone name", "a zone name containing whitespace must fail"},
        {"zone\\name", "a zone name containing backslash must fail"},
        {"zone;name", "a zone name containing semicolon must fail"},
        {"zone\"name", "a zone name containing quote must fail"},
        {controlName, "a zone name containing a control byte must fail"},
        {"zone//name", "a zone name containing // must fail"},
        {"zone/*name", "a zone name containing /* must fail"},
        {"zone..name", "a zone name containing .. must fail"},
        {"zone::name", "a zone name containing :: must fail"},
        {"zone@name", "a zone name containing @ must fail"},
        {"zone:stream", "a zone name containing an ADS colon must fail"},
        {"zone?name", "a zone name containing a wildcard must fail"},
        {"NUL.ff", "a zone name resolving to a DOS device must fail"},
        {"/zone", "a zone name beginning with slash must fail"},
        {"*zone", "a zone name beginning with star must fail"},
        {"zone/", "a zone name ending with slash must fail"},
    };
    for (const RejectedCase &testCase : rejectedNames)
    {
        ExpectRejectedNameFormatting(
            testCase.value,
            false,
            "mods/example",
            testCase.description);
    }

    constexpr RejectedCase rejectedDirectories[] = {
        {nullptr, "a mod zone requires a non-null mod directory"},
        {"", "a mod zone requires a nonempty mod directory"},
        {"mods example", "a mod directory containing whitespace must fail"},
        {"mods\\example", "a mod directory containing backslash must fail"},
        {"mods;example", "a mod directory containing semicolon must fail"},
        {"mods\"example", "a mod directory containing quote must fail"},
        {controlDirectory, "a mod directory containing a control byte must fail"},
        {"mods//example", "a mod directory containing // must fail"},
        {"mods/*example", "a mod directory containing /* must fail"},
        {"mods/../example", "a mod directory containing .. must fail"},
        {"mods::example", "a mod directory containing :: must fail"},
        {"mods@example", "a mod directory containing @ must fail"},
        {"mods/example:stream", "a mod directory containing an ADS colon must fail"},
        {"mods/example|copy", "a mod directory containing a metacharacter must fail"},
        {"mods/COM3", "a mod directory containing a DOS device must fail"},
        {"/mods", "a mod directory beginning with slash must fail"},
        {"*mods", "a mod directory beginning with star must fail"},
        {"mods/example/", "a mod directory ending with slash must fail"},
    };
    for (const RejectedCase &testCase : rejectedDirectories)
    {
        ExpectRejectedNameFormatting(
            "zone",
            true,
            testCase.value,
            testCase.description);
    }
}

void TestInfoStringAppend()
{
    using info_string::AppendPreformattedSuffix;
    using info_string::CanAppendPreformattedSuffix;

    std::array<char, 6> exact = {'a', 'b', '\0', '#', '#', '#'};
    Expect(
        AppendPreformattedSuffix(exact.data(), exact.size(), "cde"),
        "an append ending at capacity minus one must succeed");
    Expect(
        std::strcmp(exact.data(), "abcde") == 0,
        "an exact representable append must copy its terminator");

    std::array<char, 6> full = {'a', 'b', '\0', '#', '#', '#'};
    const auto unchangedFull = full;
    Expect(
        !AppendPreformattedSuffix(full.data(), full.size(), "cdef"),
        "an append whose content length equals capacity must fail");
    Expect(full == unchangedFull, "capacity failure must be output-atomic");

    Expect(
        !AppendPreformattedSuffix(nullptr, 6, "suffix"),
        "a null append destination must fail");
    std::array<char, 6> nullSuffix = {'a', '\0', '#', '#', '#', '#'};
    const auto unchangedNullSuffix = nullSuffix;
    Expect(
        !AppendPreformattedSuffix(
            nullSuffix.data(), nullSuffix.size(), nullptr),
        "a null suffix must fail");
    Expect(
        nullSuffix == unchangedNullSuffix,
        "a null suffix must not change the destination");

    std::array<char, 2> zeroCapacity = {'a', '\0'};
    const auto unchangedZeroCapacity = zeroCapacity;
    Expect(
        !AppendPreformattedSuffix(zeroCapacity.data(), 0, "b"),
        "zero append capacity must fail");
    Expect(
        zeroCapacity == unchangedZeroCapacity,
        "zero capacity must not change the destination");

    std::array<char, 1> emptyOnly = {'\0'};
    Expect(
        AppendPreformattedSuffix(
            emptyOnly.data(), emptyOnly.size(), ""),
        "capacity one must represent an empty string and its terminator");
    const auto unchangedEmptyOnly = emptyOnly;
    Expect(
        !AppendPreformattedSuffix(
            emptyOnly.data(), emptyOnly.size(), "x"),
        "capacity one must reject every nonempty suffix");
    Expect(
        emptyOnly == unchangedEmptyOnly,
        "capacity-one failure must leave the empty string unchanged");

    std::array<char, 4> unterminated = {'a', 'b', 'c', 'd'};
    const auto unchangedUnterminated = unterminated;
    Expect(
        !AppendPreformattedSuffix(
            unterminated.data(), unterminated.size(), "e"),
        "an unterminated current value must fail");
    Expect(
        unterminated == unchangedUnterminated,
        "unterminated-input failure must be output-atomic");

    constexpr std::size_t maximum = (std::numeric_limits<std::size_t>::max)();
    static_assert(!CanAppendPreformattedSuffix(0, 0, 0));
    static_assert(!CanAppendPreformattedSuffix(maximum, 0, maximum));
    static_assert(!CanAppendPreformattedSuffix(maximum - 1, 1, maximum));
    static_assert(CanAppendPreformattedSuffix(maximum - 1, 0, maximum));
    static_assert(CanAppendPreformattedSuffix(0, maximum - 1, maximum));
    static_assert(!CanAppendPreformattedSuffix(1, maximum, maximum));
}

void TestExactKeyDetection()
{
    using info_string::HasExactKey;

    constexpr char info[] =
        "\\alpha\\one\\explicit_empty\\\\last\\value";
    Expect(HasExactKey(info, "alpha"), "the first exact key must be found");
    Expect(
        HasExactKey(info, "explicit_empty"),
        "an explicitly present empty value must retain key presence");
    Expect(HasExactKey(info, "last"), "the final exact key must be found");
    Expect(
        !HasExactKey("\\other\\target", "target"),
        "text appearing only as a value must not be reported as a key");
    Expect(
        !HasExactKey("\\alpha_extra\\one", "alpha"),
        "a key prefix must not produce a false positive");
    Expect(!HasExactKey(nullptr, "alpha"), "a null info string must fail");
    Expect(!HasExactKey(info, nullptr), "a null key must fail");
    Expect(!HasExactKey(info, ""), "an empty key must fail");
}

void TestNameFormatting()
{
    using namespace db::referenced_fastfile;

    ZoneFixture zones[db::zone_slots::kPhysicalZoneSlotCount]{};
    zones[db::zone_slots::kDefaultZoneSlot] = {"slot0-default", true};
    zones[db::zone_slots::kPhysicalZoneSlotCount - 2] = {
        "slot31", true};
    zones[db::zone_slots::kPhysicalZoneSlotCount - 1] = {
        "slot32", false};

    constexpr char expected[] = "mods/example/slot31 slot32";
    std::array<char, sizeof(expected)> exact{};
    Expect(
        FormatReferencedFastFileNames(
            zones,
            "mods/example",
            IsLocalized,
            exact.data(),
            exact.size()),
        "an exact-fit name list must succeed");
    Expect(
        std::strcmp(exact.data(), expected) == 0,
        "mod zones must use the requested directory prefix and preserve slot order");

    std::array<char, sizeof(expected) - 1> tooSmall{};
    tooSmall.fill('#');
    const auto unchanged = tooSmall;
    Expect(
        !FormatReferencedFastFileNames(
            zones,
            "mods/example",
            IsLocalized,
            tooSmall.data(),
            tooSmall.size()),
        "a name list without NUL capacity must fail");
    Expect(tooSmall == unchanged, "a failed name list must leave output unchanged");

    std::array<char, sizeof(expected) - 1> emptyOutput{};
    Expect(
        !FormatReferencedFastFileNames(
            zones,
            "mods/example",
            IsLocalized,
            emptyOutput.data(),
            emptyOutput.size()),
        "the production-equivalent empty destination must reject overflow");
    Expect(emptyOutput[0] == '\0', "failed production output must remain empty");

    zones[db::zone_slots::kPhysicalZoneSlotCount - 1].name =
        "localized_slot32";
    std::array<char, 128> localizedOutput{};
    Expect(
        FormatReferencedFastFileNames(
            zones,
            "mods/example",
            IsLocalized,
            localizedOutput.data(),
            localizedOutput.size()),
        "a filtered name list must fit");
    Expect(
        std::strcmp(localizedOutput.data(), "mods/example/slot31") == 0,
        "a localized fast file in slot 32 must be excluded");

    zones[db::zone_slots::kPhysicalZoneSlotCount - 1].name =
        "localized_unsafe name";
    Expect(
        FormatReferencedFastFileNames(
            zones,
            "mods/example",
            IsLocalized,
            localizedOutput.data(),
            localizedOutput.size()),
        "an excluded localized name need not be token-safe");
    Expect(
        std::strcmp(localizedOutput.data(), "mods/example/slot31") == 0,
        "unsafe excluded names must not enter the formatted output");
}

void TestExpandedSystemInfoCapacity()
{
    using namespace db::referenced_fastfile;

    constexpr std::size_t kLegacyCapacity = 2080;
    constexpr std::size_t kSystemInfoCapacity = 8192;
    constexpr std::size_t kZoneNameLength = 63;
    constexpr std::size_t kModPrefixLength = sizeof("mods/example/") - 1;
    constexpr std::size_t kExpectedLength =
        db::zone_slots::kUsableZoneSlotCount
            * (kModPrefixLength + kZoneNameLength)
        + (db::zone_slots::kUsableZoneSlotCount - 1);
    static_assert(kExpectedLength >= kLegacyCapacity);
    static_assert(kExpectedLength < kSystemInfoCapacity);

    ZoneFixture zones[db::zone_slots::kPhysicalZoneSlotCount]{};
    std::array<
        std::array<char, kZoneNameLength + 1>,
        db::zone_slots::kUsableZoneSlotCount>
        zoneNames{};
    for (std::size_t index = 0; index < zoneNames.size(); ++index)
    {
        zoneNames[index].fill(static_cast<char>('a' + index % 26));
        zoneNames[index].back() = '\0';
        zones[index + db::zone_slots::kFirstUsableZoneSlot] = {
            zoneNames[index].data(),
            true,
            static_cast<std::int32_t>(index),
        };
    }

    std::array<char, kLegacyCapacity> legacyOutput{};
    legacyOutput.fill('#');
    const auto unchangedLegacyOutput = legacyOutput;
    Expect(
        !FormatReferencedFastFileNames(
            zones,
            "mods/example",
            IsLocalized,
            legacyOutput.data(),
            legacyOutput.size()),
        "the complete 32-zone mod list must exceed the legacy 2080-byte buffer");
    Expect(
        legacyOutput == unchangedLegacyOutput,
        "legacy-capacity failure must not publish a partial list");

    std::array<char, kSystemInfoCapacity> systemInfoOutput{};
    Expect(
        FormatReferencedFastFileNames(
            zones,
            "mods/example",
            IsLocalized,
            systemInfoOutput.data(),
            systemInfoOutput.size()),
        "the complete 32-zone mod list must fit the SYSTEMINFO value buffer");
    Expect(
        std::strlen(systemInfoOutput.data()) == kExpectedLength,
        "the complete 32-zone mod list must retain every byte");
    Expect(
        std::strncmp(
            systemInfoOutput.data(),
            "mods/example/",
            kModPrefixLength) == 0,
        "the expanded list must begin with the requested mod prefix");
    Expect(
        systemInfoOutput[kExpectedLength] == '\0',
        "the expanded list must be explicitly terminated");
}

void TestSystemInfoSuffixAssembly()
{
    using namespace db::referenced_fastfile;
    using info_string::AppendPreformattedSuffix;
    using info_string::HasExactKey;

    ZoneFixture zones[db::zone_slots::kPhysicalZoneSlotCount]{};
    zones[db::zone_slots::kFirstUsableZoneSlot] = {"zone1", true};
    zones[db::zone_slots::kFirstUsableZoneSlot + 1] = {"zone2", false};

    constexpr char expectedNames[] = "mods/example/zone1 zone2";
    std::array<char, sizeof(expectedNames)> names{};
    Expect(
        FormatReferencedFastFileNames(
            zones,
            "mods/example",
            IsLocalized,
            names.data(),
            names.size()),
        "a representable referenced-name value must format successfully");
    Expect(
        std::strcmp(names.data(), expectedNames) == 0,
        "the representable referenced-name value must round-trip exactly");

    std::array<char, 256> systemInfo{};
    Expect(
        AppendPreformattedSuffix(
            systemInfo.data(),
            systemInfo.size(),
            "\\sv_referencedFFCheckSums\\-1 2"),
        "the referenced-checksum pair must fit the test SYSTEMINFO");
    Expect(
        AppendPreformattedSuffix(
            systemInfo.data(),
            systemInfo.size(),
            "\\sv_referencedFFNames\\"),
        "the referenced-name key suffix must fit the test SYSTEMINFO");
    Expect(
        AppendPreformattedSuffix(
            systemInfo.data(), systemInfo.size(), names.data()),
        "the referenced-name value suffix must fit the test SYSTEMINFO");

    constexpr char expectedSystemInfo[] =
        "\\sv_referencedFFCheckSums\\-1 2"
        "\\sv_referencedFFNames\\mods/example/zone1 zone2";
    Expect(
        std::strcmp(systemInfo.data(), expectedSystemInfo) == 0,
        "representable referenced pairs must assemble without alteration");
    Expect(
        HasExactKey(systemInfo.data(), "sv_referencedFFCheckSums"),
        "the assembled checksum key must survive an exact-key round trip");
    Expect(
        HasExactKey(systemInfo.data(), "sv_referencedFFNames"),
        "the assembled names key must survive an exact-key round trip");

    constexpr std::size_t kSystemInfoCapacity = 8192;
    constexpr std::size_t kZoneNameLength = 63;
    ZoneFixture longZones[db::zone_slots::kPhysicalZoneSlotCount]{};
    std::array<
        std::array<char, kZoneNameLength + 1>,
        db::zone_slots::kUsableZoneSlotCount>
        zoneNames{};
    for (std::size_t index = 0; index < zoneNames.size(); ++index)
    {
        zoneNames[index].fill(static_cast<char>('a' + index % 26));
        zoneNames[index].back() = '\0';
        longZones[index + db::zone_slots::kFirstUsableZoneSlot] = {
            zoneNames[index].data(),
            true,
            static_cast<std::int32_t>(index),
        };
    }

    std::array<char, kSystemInfoCapacity> longNames{};
    Expect(
        FormatReferencedFastFileNames(
            longZones,
            "mods/example",
            IsLocalized,
            longNames.data(),
            longNames.size()),
        "the long referenced-name value must fit its standalone buffer");
    Expect(
        std::strlen(longNames.data()) >= 2080,
        "the aggregate boundary must exercise a genuinely long names value");

    std::array<char, kSystemInfoCapacity> namesSuffix{};
    Expect(
        AppendPreformattedSuffix(
            namesSuffix.data(),
            namesSuffix.size(),
            "\\sv_referencedFFNames\\"),
        "the long names key must fit its standalone suffix buffer");
    Expect(
        AppendPreformattedSuffix(
            namesSuffix.data(),
            namesSuffix.size(),
            longNames.data()),
        "the long names value must fit its standalone suffix buffer");
    Expect(
        HasExactKey(namesSuffix.data(), "sv_referencedFFNames"),
        "the long names suffix must contain the intended exact key");

    const std::size_t suffixLength = std::strlen(namesSuffix.data());
    Expect(
        suffixLength < kSystemInfoCapacity,
        "the long names suffix must be representable by itself");
    if (suffixLength == 0 || suffixLength >= kSystemInfoCapacity)
        return;

    const std::size_t existingLength = kSystemInfoCapacity - suffixLength;
    constexpr char existingPairPrefix[] = "\\existing\\";
    Expect(
        existingLength >= sizeof(existingPairPrefix),
        "the aggregate fixture must leave room for an existing pair");
    if (existingLength < sizeof(existingPairPrefix))
        return;

    std::array<char, kSystemInfoCapacity> aggregate{};
    aggregate.fill('x');
    std::memcpy(
        aggregate.data(),
        existingPairPrefix,
        sizeof(existingPairPrefix) - 1);
    aggregate[existingLength] = '\0';
    Expect(
        HasExactKey(aggregate.data(), "existing"),
        "the aggregate fixture must begin with a formatted existing pair");
    const auto unchangedAggregate = aggregate;
    Expect(
        !AppendPreformattedSuffix(
            aggregate.data(), aggregate.size(), namesSuffix.data()),
        "an aggregate reaching exactly 8192 bytes must be rejected");
    Expect(
        aggregate == unchangedAggregate,
        "aggregate SYSTEMINFO overflow must leave existing pairs unchanged");
}
} // namespace

int main()
{
    TestZoneSlotConstants();
    TestSlotWalk();
    TestSignedDecimalFormatting();
    TestInfoStringPredicates();
    TestSignedDecimalTokenParsing();
    TestRejectedNameComponents();
    TestInfoStringAppend();
    TestExactKeyDetection();
    TestNameFormatting();
    TestExpandedSystemInfoCapacity();
    TestSystemInfoSuffixAssembly();

    if (g_failures != 0)
        return 1;

    std::puts("referenced fast-file tests passed");
    return 0;
}
