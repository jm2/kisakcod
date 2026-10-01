// db_disk32_generator_tests.cpp: what gen_disk32.py emits for the kinds the
// families in flight need (NOW row 12), on the test-only disk32_generator.schema.
// The generated ONDISK_*/RUNTIME_* asserts check the struct and dotted layouts
// at compile time; this test runs the copy=scalars helper.

#include "disk32_fixture.hpp"

#include <database/db_disk32_loaders.h> // generated from disk32_generator.schema

#include <cstring>
#include <type_traits>

namespace
{
using namespace disk32_test;

// A struct field's mirror member is the nested record's mirror.
static_assert(std::is_same_v<decltype(disk32::GenHeadDisk32::u), disk32::GenFramesDisk32>);

void TestCopyScalars()
{
    disk32::GenRecordDisk32 disk{};
    disk.rate = -7;
    disk.data = 0xDEADBEEFu; // retail pointer bytes, no token
    disk.values[0] = 1;
    disk.values[1] = -2;
    disk.values[2] = 3;
    disk.loop = 0x02;
    disk.scale = 2.5f;
    GenRecord native;
    std::memset(&native, 0xCD, sizeof(native)); // nothing the copy skips is zeroed for it
    db::disk32_load::CopyGenRecordScalars(disk, &native);
    // A converted bool is exactly 0 or 1; read its byte, not the bool.
    const unsigned char loopByte = *reinterpret_cast<const unsigned char *>(&native.loop);
    Expect(native.info.rate == -7 && native.scale == 2.5f, "dotted and plain scalars copy from their mirror members");
    Expect(native.values[0] == 1 && native.values[1] == -2 && native.values[2] == 3, "a fixed array copies whole");
    Expect(loopByte == 1, "a bool byte 0x02 copies as exactly true");
    Expect(!native.info.data, "a rawptr is null, never its disk bytes");
    Expect(native.pad == 0xCD, "padding the schema does not describe stays untouched");
}
} // namespace

int main()
{
    return Run({TestCopyScalars});
}
