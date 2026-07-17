#include "bgame/bg_target_protocol.h"
#include "game/g_target_table.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <utility>

struct gentity_s
{
    int flags;
    bool inuse;
    int number;
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

template <std::size_t EntityCount>
bool TryStageOrdinaryEntity(
    gentity_s *const candidateEntity,
    gentity_s (&liveEntities)[EntityCount],
    const int liveEntityCount,
    const int worldEntityNumber,
    int *const stagedEntityNumber)
{
    if (!candidateEntity || !stagedEntityNumber)
        return false;

    const int candidate = candidateEntity->number;
    if (candidate < 0
        || candidate >= liveEntityCount
        || candidate >= worldEntityNumber
        || static_cast<std::size_t>(candidate) >= EntityCount)
    {
        return false;
    }

    gentity_s &liveEntity = liveEntities[candidate];
    if (&liveEntity != candidateEntity
        || !liveEntity.inuse
        || liveEntity.number != candidate)
    {
        return false;
    }

    *stagedEntityNumber = candidate;
    return true;
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

    target.ent = reinterpret_cast<gentity_s *>(std::uintptr_t{1});
    target.offset[0] = 9.0f;
    target.offset[1] = 8.0f;
    target.offset[2] = 7.0f;
    target.materialIndex = 3;
    target.offscreenMaterialIndex = 4;
    target.flags = 3;
    ClearTargetEntry(&target);
    Check(target.ent == nullptr
            && target.offset[0] == 0.0f
            && target.offset[1] == 0.0f
            && target.offset[2] == 0.0f
            && target.materialIndex == protocol::kNoMaterial
            && target.offscreenMaterialIndex == protocol::kNoMaterial
            && target.flags == 0,
        "storage initialization must clear a stale pointer without reading it");

    constexpr int targetFlag = 0x40000;
    entities[0] = {targetFlag | 1, true, 0};
    entities[1] = {targetFlag | 2, true, 1};
    target_t restored[2]{};
    restored[0].ent = reinterpret_cast<gentity_s *>(std::uintptr_t{1});
    restored[1].ent = reinterpret_cast<gentity_s *>(std::uintptr_t{3});

    // Model the production publication order: authoritative live flags are
    // reset without consulting old table pointers, then storage is cleared and
    // accepted entries are attached to the current entity generation.
    for (gentity_s &entity : entities)
    {
        if (entity.inuse)
            entity.flags &= ~targetFlag;
    }
    for (target_t &entry : restored)
        ClearTargetEntry(&entry);
    restored[1].ent = &entities[1];
    entities[1].flags |= targetFlag;

    Check(restored[0].ent == nullptr
            && restored[1].ent == &entities[1]
            && (entities[0].flags & targetFlag) == 0
            && (entities[1].flags & targetFlag) != 0,
        "staged publication must replace stale pointers and target flags");
}

void TestProducerEntityDomainModel()
{
    gentity_s entities[5]{};
    for (int entityNumber = 0; entityNumber < 5; ++entityNumber)
    {
        entities[entityNumber].inuse = true;
        entities[entityNumber].number = entityNumber;
    }

    int stagedEntityNumber = -1;
    Check(TryStageOrdinaryEntity(
              &entities[1], entities, 4, 3, &stagedEntityNumber)
            && stagedEntityNumber == 1,
        "the producer model must stage a live ordinary entity identity");

    for (const auto failure : {
             std::pair{&entities[3], "the producer must reject WORLD"},
             std::pair{&entities[4], "the producer must reject NONE"},
             std::pair{&entities[2], "the producer must reject the live bound"}})
    {
        stagedEntityNumber = -1;
        const int liveEntityCount = failure.first == &entities[2] ? 2 : 5;
        Check(!TryStageOrdinaryEntity(
                  failure.first,
                  entities,
                  liveEntityCount,
                  3,
                  &stagedEntityNumber)
                && stagedEntityNumber == -1,
            failure.second);
    }

    entities[0].inuse = false;
    Check(!TryStageOrdinaryEntity(
              &entities[0], entities, 4, 3, &stagedEntityNumber),
        "the producer must reject a non-inuse entity");
    entities[0].inuse = true;

    gentity_s staleCopy = entities[1];
    Check(!TryStageOrdinaryEntity(
              &staleCopy, entities, 4, 3, &stagedEntityNumber),
        "the producer must reject a stale pointer with a copied identity");

    entities[1].number = 2;
    Check(!TryStageOrdinaryEntity(
              &entities[1], entities, 4, 3, &stagedEntityNumber),
        "the producer must reject a pointer/index identity mismatch");
}

void TestCompleteAndDefaultConfigs()
{
    protocol::ParsedConfig parsed{};
    protocol::ConfigParseError error = protocol::ParseConfig(
        "\\ent\\2173\\offs\\1.25 -2 3e2\\mat\\127\\offmat\\-1"
        "\\flags\\3\\future\\value",
        2174,
        &parsed);
    Check(error == protocol::ConfigParseError::None,
        "a complete bounded target config must parse");
    Check(parsed.entityNumber == 2173,
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

    error = protocol::ParseConfig(
        "\\ent\\1\\offs\\-2147483648 0 2147483520",
        2174,
        &parsed);
    Check(error == protocol::ConfigParseError::None
            && parsed.offset[0] == -2147483648.0f
            && parsed.offset[2] == 2147483520.0f,
        "binary32 offsets at the safe signed-int endpoints must parse");

    parsed = {77, {4.0f, 5.0f, 6.0f}, 8, 9, 3};
    error = protocol::ParseConfig("\\ent\\0", 2174, &parsed);
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
        2174,
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

    ExpectFailure("\\ent\\2174", 2174,
        protocol::ConfigParseError::InvalidEntity,
        "the world slot must be excluded by the ordinary-entity limit");
    ExpectFailure("\\ent\\2175", 2174,
        protocol::ConfigParseError::InvalidEntity,
        "the none slot must be excluded by the ordinary-entity limit");
    ExpectFailure("\\ent\\10", 10,
        protocol::ConfigParseError::InvalidEntity,
        "an entity at the current level.num_entities bound must fail");
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
             "\\ent\\1\\offs\\2147483648 2 3",
             "\\ent\\1\\offs\\-2147483904 2 3",
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
             "\\ent\\1\\mat\\0",
             "\\ent\\1\\mat\\-2",
             "\\ent\\1\\mat\\128",
             "\\ent\\1\\mat\\7junk",
             "\\ent\\1\\mat\\999999999999999999999"})
    {
        ExpectFailure(config, 8,
            protocol::ConfigParseError::InvalidMaterial,
            "material indices other than -1 or 1..127 must fail closed");
    }

    for (const char *const config : {
             "\\ent\\1\\offmat\\0",
             "\\ent\\1\\offmat\\-2",
             "\\ent\\1\\offmat\\128",
             "\\ent\\1\\offmat\\7junk"})
    {
        ExpectFailure(config, 8,
            protocol::ConfigParseError::InvalidOffscreenMaterial,
            "offscreen material indices other than -1 or 1..127 must fail");
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

void ExpectDurationFailure(
    const double seconds,
    const char *const message)
{
    int milliseconds = 73;
    Check(!protocol::TryEncodeLockOnDuration(seconds, &milliseconds), message);
    Check(milliseconds == 73,
        "failed duration conversion must leave the destination unchanged");
}

void TestLockOnDurationEncoding()
{
    int milliseconds = -1;
    Check(protocol::TryEncodeLockOnDuration(0.0, &milliseconds)
            && milliseconds == 0,
        "zero seconds must encode exactly");
    Check(protocol::TryEncodeLockOnDuration(1.25, &milliseconds)
            && milliseconds == 1250,
        "valid seconds must retain the retail millisecond wire value");
    Check(protocol::TryEncodeLockOnDuration(1.2349, &milliseconds)
            && milliseconds == static_cast<int>(1.2349f * 1000.0f),
        "valid fractional seconds must retain retail float truncation");
    Check(protocol::TryEncodeLockOnDuration(2147483.5, &milliseconds)
            && milliseconds == 2147483520,
        "the largest practical binary32 duration must remain encodable");
    Check(!protocol::TryEncodeLockOnDuration(1.0, nullptr),
        "a null duration destination must fail explicitly");

    ExpectDurationFailure(-0.001,
        "negative lock-on seconds must be rejected");
    ExpectDurationFailure(
        (std::numeric_limits<double>::quiet_NaN)(),
        "NaN lock-on seconds must be rejected");
    ExpectDurationFailure(
        (std::numeric_limits<double>::infinity)(),
        "infinite lock-on seconds must be rejected");
    ExpectDurationFailure(
        static_cast<double>((std::numeric_limits<int>::max)()) / 1000.0,
        "a duration whose retail float product rounds past INT_MAX must fail");
    ExpectDurationFailure(
        static_cast<double>((std::numeric_limits<int>::max)()) / 1000.0
            + 1.0,
        "lock-on seconds above the signed millisecond range must fail");
}
} // namespace

int main()
{
    TestNativePointerLayout();
    TestProducerEntityDomainModel();
    TestCompleteAndDefaultConfigs();
    TestArgumentAndGrammarFailures();
    TestEntityFailures();
    TestOffsetFailures();
    TestScalarRangeFailures();
    TestLockOnDurationEncoding();
    return failures == 0 ? 0 : 1;
}
