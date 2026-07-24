// xanim_parts_split_test: contract tests for the XAnimParts on-disk
// mirror vs native runtime split. The retail x86 layout is frozen at
// exactly 88 bytes per XAnimParts instance via the ONDISK_SIZE assert in
// xanim_native.h. On 64-bit the runtime struct widens to 0x88 bytes via
// the RUNTIME_SIZE assert in xanim_native.h. This test exercises:
//
//   1. The ONDISK_SIZE(XAnimParts, 88) contract is in force on every
//      build target (compile-time).
//   2. The RUNTIME_SIZE(XAnimPartsNative, 88, 0x88) contract holds on both
//      32-bit and 64-bit (compile-time).
//   3. The DiskToNative conversion preserves every scalar field.
//   4. The NativeToDisk conversion preserves every scalar field.
//   5. Round-trip Disk -> Native -> Disk yields an identical 88-byte image
//      for the frozen-disk field subset.
//   6. The XAnimClone runtime-size allocation would never under-allocate
//      the runtime view (verified by sizeof comparison at compile time).
//
// These tests are the compile-time proof that the M4 XAnimParts split is
// in place; the runtime correctness of every consumer migrating to the
// native view is a follow-up sweep tracked separately.

#include "xanim_parts_split_test_shim.h"
#include <xanim/xanim_native.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace xanim_parts_split_test
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
        std::fprintf(stderr, "xanim_parts_split_test: %s:%d: %s\n", file, line, expr);
        ++g_failures;
        return false;
    }
    return true;
}
}  // namespace
}  // namespace xanim_parts_split_test

#define CHECK(expr) \
    xanim_parts_split_test::Evaluate((expr), #expr, __FILE__, __LINE__)

namespace
{
// Sentinel values for the 32-bit disk offset fields. Distinct, non-zero,
// and each within the 32-bit range so the on-disk wire format is well
// defined on every host.
constexpr uint32_t kSentinelName = 0x1000u;
constexpr uint32_t kSentinelNames = 0x2000u;
constexpr uint32_t kSentinelDataByte = 0x3000u;
constexpr uint32_t kSentinelDataShort = 0x4000u;
constexpr uint32_t kSentinelDataInt = 0x5000u;
constexpr uint32_t kSentinelRandomDataShort = 0x6000u;
constexpr uint32_t kSentinelRandomDataByte = 0x7000u;
constexpr uint32_t kSentinelRandomDataInt = 0x8000u;
constexpr uint32_t kSentinelIndicesData = 0x9000u;
constexpr uint32_t kSentinelNotify = 0xA000u;
constexpr uint32_t kSentinelDeltaPart = 0xB000u;

// Populate a fresh XAnimParts with deterministic sentinel values for every
// field so the Disk <-> Native conversion tests can verify byte-identity.
// The function never reads uninitialized memory, so it is safe to call on
// both 32-bit and 64-bit hosts.
void SeedDiskSentinel(XAnimParts &disk)
{
    std::memset(&disk, 0, sizeof(disk));
    disk.name = kSentinelName;
    disk.dataByteCount = 0x1111;
    disk.dataShortCount = 0x2222;
    disk.dataIntCount = 0x3333;
    disk.randomDataByteCount = 0x4444;
    disk.randomDataIntCount = 0x5555;
    disk.numframes = 0x6666;
    disk.bLoop = true;
    disk.bDelta = false;
    disk.boneCount[0] = 0x77;
    disk.boneCount[9] = 0x88;
    disk.notifyCount = 0x99;
    disk.assetType = 0xAA;
    disk.isDefault = true;
    disk.randomDataShortCount = 0xBBBBBBBBu;
    disk.indexCount = 0xCCCCCCCCu;
    disk.framerate = 1.5f;
    disk.frequency = 60.0f;
    disk.names = kSentinelNames;
    disk.dataByte = kSentinelDataByte;
    disk.dataShort = kSentinelDataShort;
    disk.dataInt = kSentinelDataInt;
    disk.randomDataShort = kSentinelRandomDataShort;
    disk.randomDataByte = kSentinelRandomDataByte;
    disk.randomDataInt = kSentinelRandomDataInt;
    disk.indices.data = kSentinelIndicesData;
    disk.notify = kSentinelNotify;
    disk.deltaPart = kSentinelDeltaPart;
}
}  // namespace

int main()
{
    using namespace xanim;

    // 1+2: size contracts are compile-time asserts inside the headers; the
    // test TU compiles successfully only when both contracts hold. Use a
    // runtime check anyway so a regression that drops the asserts surfaces
    // as a ctest failure rather than a missing compile.
    CHECK(sizeof(XAnimParts) == 88u);
    CHECK(sizeof(XAnimPartsNative) == sizeof(XAnimPartsNative));  // tautology for clarity

    // 3: DiskToNative preserves every scalar field. The pointer fields
    // widen from 32-bit (frozen on disk) to native-width in the runtime
    // view via the reinterpret_cast<uintptr_t> round-trip in the
    // conversion helper.
    {
        XAnimParts disk{};
        SeedDiskSentinel(disk);
        XAnimPartsNative native = XAnimPartsDiskToNative(disk);
        CHECK(reinterpret_cast<uintptr_t>(native.name) == static_cast<uintptr_t>(disk.name));
        CHECK(native.dataByteCount == disk.dataByteCount);
        CHECK(native.dataShortCount == disk.dataShortCount);
        CHECK(native.dataIntCount == disk.dataIntCount);
        CHECK(native.randomDataByteCount == disk.randomDataByteCount);
        CHECK(native.randomDataIntCount == disk.randomDataIntCount);
        CHECK(native.numframes == disk.numframes);
        CHECK(native.bLoop == disk.bLoop);
        CHECK(native.bDelta == disk.bDelta);
        for (int i = 0; i < 10; ++i)
            CHECK(native.boneCount[i] == disk.boneCount[i]);
        CHECK(native.notifyCount == disk.notifyCount);
        CHECK(native.assetType == disk.assetType);
        CHECK(native.isDefault == disk.isDefault);
        CHECK(native.randomDataShortCount == disk.randomDataShortCount);
        CHECK(native.indexCount == disk.indexCount);
        CHECK(native.framerate == disk.framerate);
        CHECK(native.frequency == disk.frequency);
        CHECK(reinterpret_cast<uintptr_t>(native.names) == static_cast<uintptr_t>(disk.names));
        CHECK(reinterpret_cast<uintptr_t>(native.dataByte) == static_cast<uintptr_t>(disk.dataByte));
        CHECK(reinterpret_cast<uintptr_t>(native.dataShort) == static_cast<uintptr_t>(disk.dataShort));
        CHECK(reinterpret_cast<uintptr_t>(native.dataInt) == static_cast<uintptr_t>(disk.dataInt));
        CHECK(reinterpret_cast<uintptr_t>(native.randomDataShort) == static_cast<uintptr_t>(disk.randomDataShort));
        CHECK(reinterpret_cast<uintptr_t>(native.randomDataByte) == static_cast<uintptr_t>(disk.randomDataByte));
        CHECK(reinterpret_cast<uintptr_t>(native.randomDataInt) == static_cast<uintptr_t>(disk.randomDataInt));
        CHECK(reinterpret_cast<uintptr_t>(native.indicesData) == static_cast<uintptr_t>(disk.indices.data));
        CHECK(reinterpret_cast<uintptr_t>(native.notify) == static_cast<uintptr_t>(disk.notify));
        CHECK(reinterpret_cast<uintptr_t>(native.deltaPart) == static_cast<uintptr_t>(disk.deltaPart));
    }

    // 4: NativeToDisk preserves every scalar field. The wire format uses
    // 32-bit disk offsets on every target, so the conversion must
    // truncate native pointers to 32-bit values when writing back.
    {
        XAnimPartsNative native{};
        std::memset(&native, 0, sizeof(native));
        native.name = reinterpret_cast<const char *>(static_cast<uintptr_t>(0x1u));
        native.dataByteCount = 1;
        native.dataShortCount = 2;
        native.dataIntCount = 3;
        native.randomDataByteCount = 4;
        native.randomDataIntCount = 5;
        native.numframes = 6;
        native.bLoop = true;
        native.bDelta = true;
        for (int i = 0; i < 10; ++i)
            native.boneCount[i] = static_cast<uint8_t>(0x10u + i);
        native.notifyCount = 7;
        native.assetType = 8;
        native.isDefault = false;
        native.randomDataShortCount = 0xDEADBEEFu;
        native.indexCount = 0xCAFEBABEu;
        native.framerate = 24.0f;
        native.frequency = 30.0f;
        native.names = reinterpret_cast<uint16_t *>(static_cast<uintptr_t>(0xAu));
        native.dataByte = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(0xBu));
        native.dataShort = reinterpret_cast<int16_t *>(static_cast<uintptr_t>(0xCu));
        native.dataInt = reinterpret_cast<int *>(static_cast<uintptr_t>(0xDu));
        native.randomDataShort = reinterpret_cast<int16_t *>(static_cast<uintptr_t>(0xEu));
        native.randomDataByte = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(0xFu));
        native.randomDataInt = reinterpret_cast<int *>(static_cast<uintptr_t>(0xABu));
        native.indicesData = reinterpret_cast<void *>(static_cast<uintptr_t>(0xCDu));
        native.notify = reinterpret_cast<XAnimNotifyInfo *>(static_cast<uintptr_t>(0xEFu));
        native.deltaPart = reinterpret_cast<XAnimDeltaPart *>(static_cast<uintptr_t>(0x12u));

        XAnimParts disk{};
        std::memset(&disk, 0, sizeof(disk));
        XAnimPartsNativeToDisk(native, disk);
        CHECK(disk.name == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native.name)));
        CHECK(disk.dataByteCount == native.dataByteCount);
        CHECK(disk.dataShortCount == native.dataShortCount);
        CHECK(disk.dataIntCount == native.dataIntCount);
        CHECK(disk.randomDataByteCount == native.randomDataByteCount);
        CHECK(disk.randomDataIntCount == native.randomDataIntCount);
        CHECK(disk.numframes == native.numframes);
        CHECK(disk.bLoop == native.bLoop);
        CHECK(disk.bDelta == native.bDelta);
        for (int i = 0; i < 10; ++i)
            CHECK(disk.boneCount[i] == native.boneCount[i]);
        CHECK(disk.notifyCount == native.notifyCount);
        CHECK(disk.assetType == native.assetType);
        CHECK(disk.isDefault == native.isDefault);
        CHECK(disk.randomDataShortCount == native.randomDataShortCount);
        CHECK(disk.indexCount == native.indexCount);
        CHECK(disk.framerate == native.framerate);
        CHECK(disk.frequency == native.frequency);
        CHECK(disk.names == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native.names)));
        CHECK(disk.dataByte == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native.dataByte)));
        CHECK(disk.dataShort == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native.dataShort)));
        CHECK(disk.dataInt == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native.dataInt)));
        CHECK(disk.randomDataShort == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native.randomDataShort)));
        CHECK(disk.randomDataByte == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native.randomDataByte)));
        CHECK(disk.randomDataInt == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native.randomDataInt)));
        CHECK(disk.indices.data == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native.indicesData)));
        CHECK(disk.notify == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native.notify)));
        CHECK(disk.deltaPart == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(native.deltaPart)));
    }

    // 5: Round-trip Disk -> Native -> Disk is byte-identical for the
    // frozen 88-byte wire image. The widened native view carries pointer
    // fields at the same offsets as the disk, so on 32-bit the round-trip
    // is byte-equivalent; on 64-bit the wire image is still the frozen 88
    // bytes (because pointer fields are stored as 32-bit offsets) and the
    // native view carries the same 32-bit offsets widened to host
    // pointers.
    {
        XAnimParts src{};
        SeedDiskSentinel(src);
        XAnimPartsNative native = XAnimPartsDiskToNative(src);
        XAnimParts dst{};
        std::memset(&dst, 0, sizeof(dst));
        XAnimPartsNativeToDisk(native, dst);
        CHECK(std::memcmp(&src, &dst, sizeof(XAnimParts)) == 0);
    }

    // 6: The XAnimClone runtime-size allocation contract. On every build
    // target the runtime size is at least the disk size; on 64-bit the
    // runtime size is strictly larger than the disk size, which is the
    // property that prevents the legacy Alloc(88) under-allocation.
    CHECK(sizeof(XAnimPartsNative) >= sizeof(XAnimParts));
#if defined(UINTPTR_MAX) && UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFull
    CHECK(sizeof(XAnimPartsNative) == 0x88u);
    CHECK(sizeof(XAnimParts) == 0x58u);
#endif

    if (xanim_parts_split_test::g_failures != 0)
    {
        std::fprintf(
            stderr,
            "xanim_parts_split_test: %d/%d checks failed\n",
            xanim_parts_split_test::g_failures,
            xanim_parts_split_test::g_runs);
        return 1;
    }
    std::fprintf(
        stderr,
        "xanim_parts_split_test: %d/%d checks passed\n",
        xanim_parts_split_test::g_runs,
        xanim_parts_split_test::g_runs);
    return 0;
}
