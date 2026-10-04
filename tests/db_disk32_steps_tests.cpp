// db_disk32_steps_tests.cpp: the generated pointer steps (gen_disk32.py) on
// test-only families (disk32_generator_steps.schema), with the production
// stream code. Each body fails without raising; the step must still drop,
// leave the slot unset and publish nothing.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_loaders.h> // generated from disk32_generator_steps.schema

namespace
{
using namespace disk32_test;

int g_bodies = 0; // how many bodies ran

void TestInsertedStepDrops()
{
    Zone<64> zone;
    g_bodies = 0;
    RawFile *slot = nullptr;
    ExpectDrop("an inserted body that fails silently", "Failed to load fast-file step raw file",
               [&] { db::disk32_load::LoadStepRawFilePtr(disk32::PointerToken{kInline}, &slot); });
    Expect(g_bodies == 1 && !slot, "the body ran, and the slot stays unset");
}

void TestCompletedStepDrops()
{
    Zone<64> zone;
    g_bodies = 0;
    StringTable *slot = nullptr;
    ExpectDrop("a completed body that fails silently", "Failed to load fast-file step table",
               [&] { db::disk32_load::LoadStepTablePtr(disk32::PointerToken{kInline}, &slot); });
    Expect(g_bodies == 1 && !slot, "the body ran, and the slot stays unset");
}
} // namespace

// The bodies fail without raising, as a validator that returns false would.
bool db::disk32_load::LoadStepRawFile(RawFile *)
{
    ++g_bodies;
    return false;
}

bool db::disk32_load::LoadStepTable(std::uint8_t *, StringTable **)
{
    ++g_bodies;
    return false;
}

void __cdecl Load_RawFileAsset(XAssetHeader *)
{
    Expect(false, "a failed body publishes nothing");
}

void __cdecl Load_StringTableAsset(XAssetHeader *)
{
    Expect(false, "a failed body publishes nothing");
}

int main()
{
    return Run({TestInsertedStepDrops, TestCompletedStepDrops});
}
