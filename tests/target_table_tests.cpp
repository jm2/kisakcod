#include "bgame/bg_target_protocol.h"
#include "game/g_target_table.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>

struct gentity_s
{
    int marker;
};

namespace protocol = bg::target_protocol;

namespace
{
int failures = 0;

void Check(const bool condition, const char *const message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

bool SameConfig(
    const protocol::ParsedConfig &lhs,
    const protocol::ParsedConfig &rhs)
{
    return lhs.entityNumber == rhs.entityNumber
        && lhs.offset[0] == rhs.offset[0]
        && lhs.offset[1] == rhs.offset[1]
        && lhs.offset[2] == rhs.offset[2]
        && lhs.materialIndex == rhs.materialIndex
        && lhs.offscreenMaterialIndex == rhs.offscreenMaterialIndex
        && lhs.flags == rhs.flags;
}

void ExpectFailure(
    const char *const info,
    const int maxEntities,
    const protocol::ConfigParseError expected,
    const char *const message)
{
    const protocol::ParsedConfig sentinel = {
        91, {1.0f, 2.0f, 3.0f}, 17, 19, 3};
    protocol::ParsedConfig parsed = sentinel;
    const protocol::ConfigParseError actual =
        protocol::ParseConfig(info, maxEntities, &parsed);
    Check(actual == expected, message);
    Check(SameConfig(parsed, sentinel),
        "parse failure must leave the destination unchanged");
}

void TestNativePointerLayout()
{
    gentity_s entities[2]{};
    target_t target{};
    target.ent = &entities[1];
    target.offset[0] = 7.0f;
    target.flags = protocol::kKnownFlags;

    Check(target.ent == &entities[1],
        "target entries must retain a native-width entity pointer");
    Check(target.offset[0] == 7.0f,
        "native pointer storage must not overlap the offset array");
    Check(target.flags == 3,
        "target flags must follow the native-layout scalar fields");
}

void TestCompleteAndDefaultConfigs()
{
    protocol::ParsedConfig parsed{};
    protocol::ConfigParseError error = protocol::ParseConfig(
        "\\ent\\2175\\offs\\1.25 -2 3e2\\mat\\127\\offmat\\-1"
        "\\flags\\3\\future\\value",
        2176,
        &parsed);
    Check(error == protocol::ConfigParseError::None,
        "a complete bounded target config must parse");
    Check(parsed.entityNumber == 2175,
        "the largest in-range entity number must be retained");
    Check(parsed.offset[0] == 1.25f
            && parsed.offset[1] == -2.0f
            && parsed.offset[2] == 300.0f,
        "three finite offset components must parse exactly");
    Check(parsed.materialIndex == 127
            && parsed.offscreenMaterialIndex == -1,
        "material indices must retain their bounded endpoint values");
    Check(parsed.flags == protocol::kKnownFlags,
        "both known target flags must round-trip");

    parsed = {77, {4.0f, 5.0f, 6.0f}, 8, 9, 3};
    error = protocol::ParseConfig("\\ent\\0", 2176, &parsed);
    Check(error == protocol::ConfigParseError::None,
        "historically optional target fields may be absent");
    Check(parsed.entityNumber == 0
            && parsed.offset[0] == 0.0f
            && parsed.offset[1] == 0.0f
            && parsed.offset[2] == 0.0f
            && parsed.materialIndex == -1
            && parsed.offscreenMaterialIndex == -1
            && parsed.flags == 0,
        "absent optional fields must use retail load defaults");

    error = protocol::ParseConfig(
        "\\ent\\1\\offs\\\\mat\\\\offmat\\\\flags\\",
        2176,
        &parsed);
    Check(error == protocol::ConfigParseError::None,
        "present-empty optional fields must use retail load defaults");
    Check(parsed.offset[0] == 0.0f
            && parsed.materialIndex == -1
            && parsed.offscreenMaterialIndex == -1
            && parsed.flags == 0,
        "empty optional values must reset every optional field");
}

void TestArgumentAndGrammarFailures()
{
    Check(
        protocol::ParseConfig("\\ent\\1", 8, nullptr)
            == protocol::ConfigParseError::InvalidArgument,
        "a null destination must be rejected explicitly");
    ExpectFailure(nullptr, 8,
        protocol::ConfigParseError::InvalidArgument,
        "a null config must be rejected");
    ExpectFailure("", 8,
        protocol::ConfigParseError::InvalidArgument,
        "an empty active config must be rejected");
    ExpectFailure("\\ent\\1", 0,
        protocol::ConfigParseError::InvalidArgument,
        "a nonpositive entity domain must be rejected");
    ExpectFailure("\\ent", 8,
        protocol::ConfigParseError::MalformedInfoString,
        "an orphan key must be rejected");
    ExpectFailure("\\ent\\1\\ent\\2", 8,
        protocol::ConfigParseError::MalformedInfoString,
        "duplicate entity keys must be rejected");
    ExpectFailure("\\ent\\1\\flags\\0\\flags\\1", 8,
        protocol::ConfigParseError::MalformedInfoString,
        "duplicate optional keys must be rejected");
    ExpectFailure("\\mat\\1", 8,
        protocol::ConfigParseError::MissingEntity,
        "an active config requires an entity key");
    ExpectFailure("\\ent\\", 8,
        protocol::ConfigParseError::MissingEntity,
        "an active config requires a nonempty entity value");
}

void TestEntityFailures()
{
    for (const char *const config : {
             "\\ent\\-1",
             "\\ent\\8",
             "\\ent\\1junk",
             "\\ent\\+1",
             "\\ent\\999999999999999999999"})
    {
        ExpectFailure(config, 8,
            protocol::ConfigParseError::InvalidEntity,
            "invalid entity tokens must fail closed");
    }
}

void TestOffsetFailures()
{
    for (const char *const config : {
             "\\ent\\1\\offs\\1 2",
             "\\ent\\1\\offs\\1 2 3 4",
             "\\ent\\1\\offs\\1,2,3",
             "\\ent\\1\\offs\\nan 2 3",
             "\\ent\\1\\offs\\inf 2 3",
             "\\ent\\1\\offs\\1e100 2 3",
             "\\ent\\1\\offs\\1\n2 3"})
    {
        ExpectFailure(config, 8,
            protocol::ConfigParseError::InvalidOffset,
            "non-finite, out-of-range, or non-triplet offsets must fail");
    }
}

void TestScalarRangeFailures()
{
    for (const char *const config : {
             "\\ent\\1\\mat\\-2",
             "\\ent\\1\\mat\\128",
             "\\ent\\1\\mat\\7junk",
             "\\ent\\1\\mat\\999999999999999999999"})
    {
        ExpectFailure(config, 8,
            protocol::ConfigParseError::InvalidMaterial,
            "material indices outside [-1,127] must fail closed");
    }

    for (const char *const config : {
             "\\ent\\1\\offmat\\-2",
             "\\ent\\1\\offmat\\128",
             "\\ent\\1\\offmat\\7junk"})
    {
        ExpectFailure(config, 8,
            protocol::ConfigParseError::InvalidOffscreenMaterial,
            "offscreen material indices outside [-1,127] must fail closed");
    }

    for (const char *const config : {
             "\\ent\\1\\flags\\-1",
             "\\ent\\1\\flags\\4",
             "\\ent\\1\\flags\\1junk",
             "\\ent\\1\\flags\\999999999999999999999"})
    {
        ExpectFailure(config, 8,
            protocol::ConfigParseError::InvalidFlags,
            "unknown or malformed target flag values must fail closed");
    }
}
} // namespace

int main()
{
    TestNativePointerLayout();
    TestCompleteAndDefaultConfigs();
    TestArgumentAndGrammarFailures();
    TestEntityFailures();
    TestOffsetFailures();
    TestScalarRangeFailures();
    return failures == 0 ? 0 : 1;
}
