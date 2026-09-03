// script_value_split_test: contract tests for the script VM value-cell
// save-image mirror vs native runtime split (M4). The retail x86 value
// cell is frozen at exactly 4-byte payload / 8-byte cell via the
// ONDISK_SIZE asserts in scr_native.h. The native runtime views widen to
// 8-byte payload / 16-byte cell on 64-bit via RUNTIME_SIZE. This test
// exercises:
//
//   1. The ONDISK_SIZE contracts hold on every build target (compile-time,
//      re-checked at runtime so a regression that drops the asserts
//      surfaces as a ctest failure).
//   2. The RUNTIME_SIZE contracts hold on 32-bit and 64-bit.
//   3. The script::Vartype numeric values match the retail encoding.
//   4. Disk -> Native -> Disk round-trip is bit-identical for every
//      value-bearing vartype.
//   5. Runtime-reconstruction vartypes are reported as reconstruction-
//      required, with the native pointer member nulled -- never a
//      fabricated host pointer built from save bytes.
//   6. A live reconstruction-kind pointer refuses NativeToDisk instead of
//      silently truncating (the exact failure the save path avoids by
//      dereferencing contents).
//   7. The legacy engine cell sizes that the save image freezes
//      (VariableUnion == 4, VariableValue == 8) still hold in the engine
//      header on this target, so the mirror and the engine cannot drift
//      apart silently on 32-bit.

#include <script/scr_native.h>

// Engine header include is safe in this portable test TU: scr_variable.h
// only asserts sizes (it does not pull in link-time dependencies), and
// pinning it here is the guard that the disk mirror stays in lockstep with
// the engine cell on ILP32 targets. Guarded to 32-bit because the engine
// header's static_asserts are ILP32-frozen and would fail on 64-bit.
#if !KISAK_ARCH_64BIT
#include <script/scr_variable.h>
#endif

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace script_value_split_test
{
namespace
{
int g_failures = 0;
int g_runs = 0;

bool Evaluate(bool cond, const char *const expr, const char *const file, int line)
{
    ++g_runs;
    if (!cond)
    {
        std::fprintf(stderr, "script_value_split_test: %s:%d: %s\n", file, line, expr);
        ++g_failures;
        return false;
    }
    return true;
}
}  // namespace
}  // namespace script_value_split_test

#define CHECK(expr) \
    script_value_split_test::Evaluate((expr), #expr, __FILE__, __LINE__)

namespace
{
// Distinct sentinel payload dwords, each a legal value in every
// value-bearing encoding (ids stay inside the 16-bit/24-bit ranges the
// engine uses; the int/float payloads exercise full 32-bit patterns).
constexpr uint32_t kSentinelInt = 0xC3A5F00Du;
constexpr uint32_t kSentinelObjectId = 0x00012345u;
constexpr uint32_t kSentinelStringId = 0x0000ABCDu;
constexpr uint32_t kSentinelAnimIndex = 0x0000FEDCu;

script::VariableValueDisk MakeDiskCell(uint32_t type, uint32_t payload)
{
    script::VariableValueDisk cell{};
    cell.u.intValue = static_cast<int32_t>(payload);
    cell.type = type;
    return cell;
}

void SeedAllValueBearingRoundTrips()
{
    const uint32_t types[] = {
        script::VAR_UNDEFINED,
        script::VAR_POINTER,
        script::VAR_STRING,
        script::VAR_ISTRING,
        script::VAR_FLOAT,
        script::VAR_INTEGER,
        script::VAR_ANIMATION,
        script::VAR_THREAD,
        script::VAR_NOTIFY_THREAD,
        script::VAR_TIME_THREAD,
        script::VAR_CHILD_THREAD,
        script::VAR_OBJECT,
        script::VAR_DEAD_ENTITY,
        script::VAR_ENTITY,
        script::VAR_ARRAY,
        script::VAR_DEAD_THREAD,
        script::VAR_THREAD_LIST,
        script::VAR_ENDON_LIST,
    };
    const uint32_t payloads[] = {
        kSentinelInt,
        kSentinelObjectId,
        kSentinelStringId,
        kSentinelAnimIndex,
    };

    for (uint32_t type : types)
    {
        CHECK(script::IsValueBearingVartype(type));
        CHECK(!script::IsRuntimeReconstructionVartype(type));
        for (uint32_t payload : payloads)
        {
            script::VariableValueDisk disk = MakeDiskCell(type, payload);
            script::ScriptValueDiskToNativeResult toNative =
                script::ScriptValueDiskToNative(disk);
            CHECK(toNative.complete);
            CHECK(toNative.value.type == disk.type);
            CHECK(static_cast<uint32_t>(toNative.value.u.intValue) == payload);

            script::VariableValueDisk back{};
            CHECK(script::ScriptValueNativeToDisk(toNative.value, back));
            CHECK(back.type == disk.type);
            CHECK(std::memcmp(&back.u, &disk.u, sizeof(back.u)) == 0);
        }
    }
}

void CheckReconstructionKinds()
{
    const uint32_t types[] = {
        script::VAR_VECTOR,
        script::VAR_CODEPOS,
        script::VAR_PRECODEPOS,
        script::VAR_FUNCTION,
        script::VAR_STACK,
        script::VAR_DEVELOPER_CODEPOS,
        script::VAR_INCLUDE_CODEPOS,
    };

    for (uint32_t type : types)
    {
        CHECK(script::IsRuntimeReconstructionVartype(type));
        CHECK(!script::IsValueBearingVartype(type));

        // Disk -> Native reports reconstruction-required and nulls the
        // pointer member instead of fabricating a host pointer from the
        // save payload dword.
        script::VariableValueDisk disk = MakeDiskCell(type, kSentinelInt);
        script::ScriptValueDiskToNativeResult toNative =
            script::ScriptValueDiskToNative(disk);
        CHECK(!toNative.complete);
        CHECK(toNative.value.type == disk.type);
        CHECK(toNative.value.u.stackValue == nullptr);
        CHECK(toNative.value.u.vectorValue == nullptr);
        CHECK(toNative.value.u.codePosValue == nullptr);

        // A null reconstruction-kind cell round-trips as a defined zero
        // payload and still reports that the disk image is not complete.
        script::VariableValueDisk back{};
        CHECK(!script::ScriptValueNativeToDisk(toNative.value, back));
        CHECK(back.type == disk.type);
        CHECK(static_cast<uint32_t>(back.u.intValue) == 0u);

        // A LIVE reconstruction-kind pointer must refuse the payload-copy
        // conversion and leave the disk cell untouched -- serializing it
        // requires the content-walking save path.
        script::VariableValueNative live{};
        live.type = type;
        alignas(script::VariableStackBufferNative) unsigned char storage[16];
        live.u.stackValue =
            reinterpret_cast<script::VariableStackBufferNative *>(storage);
        script::VariableValueDisk untouched = MakeDiskCell(type, kSentinelInt);
        script::VariableValueDisk out = untouched;
        CHECK(!script::ScriptValueNativeToDisk(live, out));
        CHECK(std::memcmp(&out, &untouched, sizeof(out)) == 0);
    }
}

void CheckVartypeEncoding()
{
    // The numeric values are the retail save-path encoding; the dispatcher
    // and the save/load code must never drift apart.
    CHECK(script::VAR_UNDEFINED == 0x0u);
    CHECK(script::VAR_POINTER == 0x1u);
    CHECK(script::VAR_STRING == 0x2u);
    CHECK(script::VAR_ISTRING == 0x3u);
    CHECK(script::VAR_VECTOR == 0x4u);
    CHECK(script::VAR_FLOAT == 0x5u);
    CHECK(script::VAR_INTEGER == 0x6u);
    CHECK(script::VAR_CODEPOS == 0x7u);
    CHECK(script::VAR_PRECODEPOS == 0x8u);
    CHECK(script::VAR_FUNCTION == 0x9u);
    CHECK(script::VAR_STACK == 0xAu);
    CHECK(script::VAR_ANIMATION == 0xBu);
    CHECK(script::VAR_DEVELOPER_CODEPOS == 0xCu);
    CHECK(script::VAR_INCLUDE_CODEPOS == 0xDu);
    CHECK(script::VAR_THREAD == 0xEu);
    CHECK(script::VAR_NOTIFY_THREAD == 0xFu);
    CHECK(script::VAR_TIME_THREAD == 0x10u);
    CHECK(script::VAR_CHILD_THREAD == 0x11u);
    CHECK(script::VAR_OBJECT == 0x12u);
    CHECK(script::VAR_DEAD_ENTITY == 0x13u);
    CHECK(script::VAR_ENTITY == 0x14u);
    CHECK(script::VAR_ARRAY == 0x15u);
    CHECK(script::VAR_DEAD_THREAD == 0x16u);
    CHECK(script::VAR_COUNT == 0x17u);
    CHECK(script::VAR_THREAD_LIST == 0x18u);
    CHECK(script::VAR_ENDON_LIST == 0x19u);
    CHECK(script::kVarMask == 0x1Fu);
}

void CheckFloatPayloadPreservation()
{
    // The FLOAT path must move bits, not values: -0.0f, NaN payloads, and
    // denormals all survive as the same dword.
    const uint32_t bitPatterns[] = {
        0x00000000u, // +0.0f
        0x80000000u, // -0.0f
        0x7FC00000u, // quiet NaN
        0x00000001u, // denormal
        0x7F7FFFFFu, // FLT_MAX
        0xBF800000u, // -1.0f
    };
    for (uint32_t bits : bitPatterns)
    {
        script::VariableValueDisk disk = MakeDiskCell(script::VAR_FLOAT, bits);
        script::ScriptValueDiskToNativeResult toNative =
            script::ScriptValueDiskToNative(disk);
        CHECK(toNative.complete);
        uint32_t roundTripBits;
        std::memcpy(&roundTripBits, &toNative.value.u.floatValue, sizeof(roundTripBits));
        CHECK(roundTripBits == bits);
    }
}
}  // namespace

int main()
{
    // 1+2: size contracts are compile-time asserts inside scr_native.h; the
    // test TU compiles only when they hold. Runtime re-checks surface a
    // regression that drops the asserts as a ctest failure instead.
    CHECK(sizeof(script::VariableUnionDisk) == 4u);
    CHECK(sizeof(script::VariableValueDisk) == 8u);
    CHECK(sizeof(script::VariableUnionNative) >= sizeof(script::VariableUnionDisk));
    CHECK(sizeof(script::VariableValueNative) >= sizeof(script::VariableValueDisk));
#if KISAK_ARCH_64BIT
    CHECK(sizeof(script::VariableUnionNative) == 8u);
    CHECK(sizeof(script::VariableValueNative) == 16u);
    CHECK(sizeof(script::VariableStackBufferNative) == 0x10u);
#else
    CHECK(sizeof(script::VariableUnionNative) == 4u);
    CHECK(sizeof(script::VariableValueNative) == 8u);
    CHECK(sizeof(script::VariableStackBufferNative) == 0xCu);
#endif

    // 3: retail vartype encoding.
    CheckVartypeEncoding();

    // 4: value-bearing kinds round-trip bit-identically.
    SeedAllValueBearingRoundTrips();

    // 5+6: reconstruction kinds never fabricate or truncate pointers.
    CheckReconstructionKinds();

    // 7: float payloads move as bits.
    CheckFloatPayloadPreservation();

    // 8: engine-cell / disk-mirror lockstep on ILP32 targets (compile-time
    // via the engine header's own asserts in the include above; runtime
    // re-check here).
#if !KISAK_ARCH_64BIT
    CHECK(sizeof(VariableUnion) == sizeof(script::VariableUnionDisk));
    CHECK(sizeof(VariableValue) == sizeof(script::VariableValueDisk));
#endif

    if (script_value_split_test::g_failures != 0)
    {
        std::fprintf(stderr, "script_value_split_test: %d/%d checks failed\n",
                     script_value_split_test::g_failures, script_value_split_test::g_runs);
        return 1;
    }
    std::printf("script_value_split_test: %d checks passed\n", script_value_split_test::g_runs);
    return 0;
}
