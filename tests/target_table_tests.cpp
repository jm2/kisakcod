#include "bgame/bg_target_protocol.h"
#include "game/g_target_table.h"

#include <array>
#include <cerrno>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <cstring>
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

    for (const auto &failure : {
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

    error = protocol::ParseConfig(
        "\\ent\\1\\offs\\.5 1. -2E+2", 2174, &parsed);
    Check(error == protocol::ConfigParseError::None
            && parsed.offset[0] == 0.5f
            && parsed.offset[1] == 1.0f
            && parsed.offset[2] == -200.0f,
        "strict decimal offsets must retain supported decimal spellings");

    error = protocol::ParseConfig(
        "\\ent\\1\\offs\\1e-40 0 0", 2174, &parsed);
    Check(error == protocol::ConfigParseError::None
            && parsed.offset[0] != 0.0f
            && std::fpclassify(parsed.offset[0]) == FP_SUBNORMAL,
        "representable finite subnormal offsets must remain valid");

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
             "\\ent\\1\\offs\\1e-50 2 3",
             "\\ent\\1\\offs\\+1 2 3",
             "\\ent\\1\\offs\\. 2 3",
             "\\ent\\1\\offs\\1e 2 3",
             "\\ent\\1\\offs\\1e+ 2 3",
             "\\ent\\1\\offs\\0x1p0 2 3",
             "\\ent\\1\\offs\\2147483648 2 3",
             "\\ent\\1\\offs\\-2147483904 2 3",
             "\\ent\\1\\offs\\1\n2 3"})
    {
        ExpectFailure(config, 8,
            protocol::ConfigParseError::InvalidOffset,
            "non-finite, out-of-range, or non-triplet offsets must fail");
    }
}

void TestPortableFloatFallback()
{
#if defined(__APPLE__) || defined(__linux__) || defined(__unix__)
    const std::array<char, 3> unterminated = {'1', '.', '5'};
    float parsed = 0.0f;
    Check(protocol::detail::TryParseFloatTokenFallback(
              unterminated.data(),
              unterminated.data() + unterminated.size(),
              &parsed)
            && parsed == 1.5f,
        "the libc fallback must parse a bounded non-NUL-terminated range");

    std::array<char, protocol::kMaxNumericTokenLength + 1> overlong{};
    overlong.fill('0');
    parsed = 17.0f;
    Check(!protocol::detail::TryParseFloatTokenFallback(
              overlong.data(),
              overlong.data() + overlong.size(),
              &parsed)
            && parsed == 17.0f,
        "the libc fallback must reject an overlong token atomically");

    constexpr char subnormal[] = "1e-40";
    parsed = 0.0f;
    Check(protocol::detail::TryParseFloatTokenFallback(
              subnormal,
              subnormal + sizeof(subnormal) - 1,
              &parsed)
            && parsed != 0.0f
            && std::fpclassify(parsed) == FP_SUBNORMAL,
        "the libc fallback must retain representable subnormal offsets");

    constexpr char underflow[] = "1e-50";
    parsed = 17.0f;
    Check(!protocol::detail::TryParseFloatTokenFallback(
              underflow,
              underflow + sizeof(underflow) - 1,
              &parsed)
            && parsed == 17.0f,
        "the libc fallback must reject underflow-to-zero atomically");

    constexpr char exactZero[] = "0e-999";
    parsed = 17.0f;
    Check(protocol::detail::TryParseFloatTokenFallback(
              exactZero,
              exactZero + sizeof(exactZero) - 1,
              &parsed)
            && parsed == 0.0f,
        "the libc fallback must accept an exact zero with a large exponent");

    constexpr char midpoint[] = "1.000000059604644775390625";
    const int originalRoundingMode = std::fegetround();
    Check(originalRoundingMode != -1,
        "the float fallback test requires a readable rounding mode");
    if (originalRoundingMode != -1
        && std::fesetround(FE_UPWARD) == 0)
    {
        errno = EDOM;
        parsed = 0.0f;
        Check(protocol::detail::TryParseFloatTokenFallback(
                  midpoint,
                  midpoint + sizeof(midpoint) - 1,
                  &parsed)
                && parsed == 1.0f,
            "the libc fallback must always use ties-to-even rounding");
        Check(std::fegetround() == FE_UPWARD,
            "the libc fallback must restore the caller's rounding mode");
        Check(errno == EDOM,
            "the libc fallback must restore the caller's errno");
        Check(std::fesetround(originalRoundingMode) == 0,
            "the float fallback test must restore its rounding mode");
    }
#endif
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

template <std::size_t StorageSize>
char *BuildPaddedTargetConfig(
    std::array<char, StorageSize> *const storage,
    const std::size_t configLength)
{
    constexpr char prefix[] = "\\ent\\1\\future\\";
    static_assert(StorageSize >= 1026);

    storage->fill(static_cast<char>(0x5a));
    char *const config = storage->data() + 1;
    const std::size_t prefixLength = sizeof(prefix) - 1;
    Check(configLength >= prefixLength && configLength < 1024,
        "padded target fixture length must fit the wire buffer");
    std::memcpy(config, prefix, prefixLength);
    std::memset(
        config + prefixLength,
        'x',
        configLength - prefixLength);
    config[configLength] = '\0';
    return config;
}

void TestCheckedInfoValueReplacement()
{
    constexpr std::size_t capacity = 1024;
    std::array<char, capacity + 2> guarded{};
    std::array<char, capacity> scratch{};

    char *const exact = BuildPaddedTargetConfig(&guarded, 1017);
    Check(info_string::TrySetValueForKey(
              exact,
              capacity,
              scratch.data(),
              scratch.size(),
              "mat",
              "1"),
        "the checked setter must allow exactly 1023 content bytes");
    Check(std::strlen(exact) == 1023
            && exact[1023] == '\0'
            && guarded.front() == static_cast<char>(0x5a)
            && guarded.back() == static_cast<char>(0x5a),
        "the checked setter must terminate in-bounds and preserve canaries");

    char *const full = BuildPaddedTargetConfig(&guarded, 1018);
    const std::array<char, capacity + 2> fullBefore = guarded;
    Check(!info_string::TrySetValueForKey(
              full,
              capacity,
              scratch.data(),
              scratch.size(),
              "mat",
              "1")
            && guarded == fullBefore,
        "a 1024-byte result must fail without changing its source");

    const auto Load = [&guarded](const char *const value) {
        guarded.fill(static_cast<char>(0x5a));
        char *const destination = guarded.data() + 1;
        std::memcpy(destination, value, std::strlen(value) + 1);
        return destination;
    };

    char *current = Load("\\ent\\1\\mat\\123456789\\future\\x");
    Check(info_string::TrySetValueForKey(
              current,
              capacity,
              scratch.data(),
              scratch.size(),
              "mat",
              "2")
            && std::strcmp(
                   current, "\\ent\\1\\future\\x\\mat\\2") == 0,
        "replacement must remove the first old value and shrink to fit");

    Check(info_string::TrySetValueForKey(
              current,
              capacity,
              scratch.data(),
              scratch.size(),
              "mat",
              "")
            && std::strcmp(current, "\\ent\\1\\future\\x") == 0,
        "an empty clean value must remove the existing key");

    current = Load("\\ent\\1");
    Check(info_string::TrySetValueForKey(
              current,
              capacity,
              scratch.data(),
              scratch.size(),
              "future",
              "a\\b;c\"d")
            && std::strcmp(current, "\\ent\\1\\future\\abcd") == 0,
        "checked replacement must retain legacy delimiter cleaning");

    current = Load("ent\\1\\future\\x");
    Check(info_string::TrySetValueForKey(
              current,
              capacity,
              scratch.data(),
              scratch.size(),
              "mat",
              "1")
            && std::strcmp(
                   current, "ent\\1\\future\\x\\mat\\1") == 0,
        "replacement must preserve an optional missing leading delimiter");

    current = Load("mat\\2\\ent\\1");
    Check(info_string::TrySetValueForKey(
              current,
              capacity,
              scratch.data(),
              scratch.size(),
              "mat",
              "3")
            && std::strcmp(current, "\\ent\\1\\mat\\3") == 0,
        "removing a leading first pair must retain legacy delimiter placement");

    for (const char *const invalidKey : {
             "", "bad\\key", "bad;key", "bad\"key"})
    {
        current = Load("\\ent\\1\\mat\\2");
        const std::array<char, capacity + 2> before = guarded;
        Check(!info_string::TrySetValueForKey(
                  current,
                  capacity,
                  scratch.data(),
                  scratch.size(),
                  invalidKey,
                  "1")
                && guarded == before,
            "invalid keys must leave the published info string unchanged");
    }

    std::array<char, capacity + 1> overlongValue{};
    overlongValue.fill('x');
    overlongValue.back() = '\0';
    current = Load("\\ent\\1\\mat\\2");
    const std::array<char, capacity + 2> before = guarded;
    Check(!info_string::TrySetValueForKey(
              current,
              capacity,
              scratch.data(),
              scratch.size(),
              "mat",
              overlongValue.data())
            && guarded == before,
        "an overlong value must preserve an existing value atomically");
}

bool TryPublishMissingMaterialModel(
    char *const publishedConfig,
    const std::size_t capacity,
    int *const liveMaterial)
{
    if (!publishedConfig || capacity != 1024 || !liveMaterial)
        return false;

    std::array<char, 1024> staged{};
    const std::size_t currentLength = std::strlen(publishedConfig);
    if (currentLength >= staged.size())
        return false;
    std::memcpy(staged.data(), publishedConfig, currentLength + 1);
    std::array<char, 1024> scratch{};
    if (!info_string::TrySetValueForKey(
            staged.data(),
            staged.size(),
            scratch.data(),
            scratch.size(),
            "mat",
            "1"))
    {
        return false;
    }

    protocol::ParsedConfig parsed{};
    if (protocol::ParseConfig(staged.data(), 8, &parsed)
            != protocol::ConfigParseError::None
        || parsed.entityNumber != 1
        || parsed.materialIndex != 1)
    {
        return false;
    }

    std::memcpy(publishedConfig, staged.data(), std::strlen(staged.data()) + 1);
    *liveMaterial = parsed.materialIndex;
    return true;
}

void TestFailureAtomicTargetConfigStaging()
{
    constexpr std::size_t capacity = 1024;

    std::array<char, capacity + 2> exactStorage{};
    char *const exact = BuildPaddedTargetConfig(&exactStorage, 1017);
    int liveMaterial = 17;
    Check(TryPublishMissingMaterialModel(
              exact, capacity, &liveMaterial),
        "an absent material key must fit when content ends at byte 1023");
    Check(std::strlen(exact) == 1023
            && exact[1023] == '\0'
            && liveMaterial == 1,
        "an exact bounded update must publish wire and native state together");
    Check(exactStorage.front() == static_cast<char>(0x5a)
            && exactStorage.back() == static_cast<char>(0x5a),
        "an exact bounded update must preserve both wire-buffer canaries");

    for (const std::size_t currentLength : {1018u, 1019u})
    {
        std::array<char, capacity + 2> rejectedStorage{};
        char *const rejected =
            BuildPaddedTargetConfig(&rejectedStorage, currentLength);
        const std::array<char, capacity + 2> before = rejectedStorage;
        liveMaterial = 17;
        Check(!TryPublishMissingMaterialModel(
                  rejected, capacity, &liveMaterial),
            "a target update at or above full content capacity must fail");
        Check(rejectedStorage == before && liveMaterial == 17,
            "a failed capacity update must leave wire/native state unchanged");
    }

    for (const char *const invalid : {
             "\\ent",
             "\\ent\\1\\mat\\2\\mat\\3"})
    {
        std::array<char, capacity + 2> invalidStorage{};
        invalidStorage.fill(static_cast<char>(0x5a));
        char *const published = invalidStorage.data() + 1;
        std::memcpy(published, invalid, std::strlen(invalid) + 1);
        const std::array<char, capacity + 2> before = invalidStorage;
        liveMaterial = 17;
        Check(!TryPublishMissingMaterialModel(
                  published, capacity, &liveMaterial),
            "malformed or duplicate target configs must not publish");
        Check(invalidStorage == before && liveMaterial == 17,
            "parse rejection must preserve published and native sentinels");
    }
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
    TestPortableFloatFallback();
    TestScalarRangeFailures();
    TestLockOnDurationEncoding();
    TestCheckedInfoValueReplacement();
    TestFailureAtomicTargetConfigStaging();
    return failures == 0 ? 0 : 1;
}
