// xanim_dobj_tests.cpp: the runtime DObj and XModel code (xanim/dobj.cpp,
// dobj_utils.cpp, xmodel.cpp) at native width, over the production script
// string list and memory tree. A body model and a weapon model hung off the
// body's head bone become one DObj: its bone indices, its models, its
// hide-part bits and its free must follow the 64-bit DObj_s/XModel layouts.

#include <xanim/dobj.h>
#include <xanim/dobj_utils.h>
#include <xanim/xanim.h>
#include <xanim/xmodel.h>
#include <script/scr_memorytree.h>
#include <script/scr_stringlist.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <qcommon/qcommon.h>
#include <universal/q_shared.h>

namespace
{
int g_failures = 0;

void Check(const bool ok, const char *const what)
{
    if (!ok)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

// A model with numBones named bones; bone i's parent offset is parent[i].
struct SyntheticModel
{
    XModel model{};
    uint16_t boneNames[8]{};
    unsigned __int8 parentList[8]{};

    SyntheticModel(const char *const name, const char *const *const bones, const int numBones,
                   const int numRootBones)
    {
        model.name = name;
        model.numBones = static_cast<unsigned __int8>(numBones);
        model.numRootBones = static_cast<unsigned __int8>(numRootBones);
        for (int i = 0; i < numBones; ++i)
            boneNames[i] = static_cast<uint16_t>(SL_GetString_(bones[i], 0, 4));
        for (int i = numRootBones; i < numBones; ++i)
            parentList[i - numRootBones] = 1; // each bone hangs off the one before
        model.boneNames = boneNames;
        model.parentList = parentList;
    }
};

uint32_t Name(const char *const bone)
{
    return SL_FindString(bone);
}

void TestBodyAndWeapon()
{
    const char *const bodyBones[] = {"tag_origin", "j_spine", "j_head"};
    SL_GetString_("tag_missing", 0, 4);
    const char *const gunBones[] = {"tag_weapon", "tag_flash"};
    SyntheticModel body("body_test", bodyBones, 3, 1);
    SyntheticModel gun("weapon_test", gunBones, 2, 1);

    Check(body.model.numBones == 3 && gun.model.numBones == 2, "models built");

    unsigned __int8 index = 0xFF;
    Check(XModelGetBoneIndex(&body.model, Name("j_head"), 0, &index) && index == 2, "XModel bone index");
    Check(!XModelGetBoneIndex(&body.model, Name("tag_flash"), 0, &index), "XModel lacks the other model's bone");

    DObjModel_s models[2]{};
    models[0].model = &body.model;
    models[1].model = &gun.model;
    models[1].boneName = static_cast<uint16_t>(Name("j_head"));

    DObj_s obj{};
    DObjCreate(models, 2, nullptr, &obj, 7);
    Check(DObjNumBones(&obj) == 5, "DObj bone count is the sum");
    Check(DObjGetNumModels(&obj) == 2, "DObj model count");
    Check(DObjGetModel(&obj, 0) == &body.model && DObjGetModel(&obj, 1) == &gun.model, "DObj models in order");

    const struct
    {
        const char *bone;
        unsigned __int8 expected;
    } bones[] = {{"tag_origin", 0}, {"j_spine", 1}, {"j_head", 2}, {"tag_weapon", 3}, {"tag_flash", 4}};
    // The index is a cache: callers start it at 254 (unknown); 255 caches a miss.
    for (const auto &bone : bones)
    {
        index = 254;
        Check(DObjGetBoneIndex(&obj, Name(bone.bone), &index) && index == bone.expected, bone.bone);
        Check(DObjGetBoneIndex(&obj, Name(bone.bone), &index) && index == bone.expected, "cached index hit");
    }
    index = 254;
    Check(!DObjGetBoneIndex(&obj, Name("tag_missing"), &index) && index == 255, "unknown bone caches a miss");
    Check(!DObjGetBoneIndex(&obj, Name("j_head"), &index), "a cached miss stays a miss");

    uint32_t hide[4] = {0x40000000u, 0u, 0u, 0u}; // hide j_spine
    DObjSetHidePartBits(&obj, hide);
    uint32_t readBack[4] = {};
    DObjGetHidePartBits(&obj, readBack);
    Check(std::memcmp(hide, readBack, sizeof(hide)) == 0, "hide-part bits round-trip");

    DObjFree(&obj);
    Check(obj.models == nullptr && obj.numModels == 0, "DObjFree releases the models");
}
} // namespace

// The engine boundary the DObj code reaches. Paths the checks never take
// abort, so reaching one fails the test.
void MyAssertHandler(const char *file, int line, int, const char *fmt, ...)
{
    std::fprintf(stderr, "engine assert %s:%d %s\n", file, line, fmt);
    std::abort();
}

void Com_Error(errorParm_t, const char *fmt, ...)
{
    std::fprintf(stderr, "Com_Error %s\n", fmt);
    std::abort();
}

void Com_Printf(int, const char *, ...)
{
}

void Com_PrintWarning(int, const char *, ...)
{
}

void Com_Memset(void *dest, const int val, const size_t count)
{
    std::memset(dest, val, count);
}

void __cdecl Sys_Sleep(uint32_t)
{
}

// Reached only for a DObj with an anim tree; these have none.
void __cdecl XAnimResetAnimMap(const DObj_s *, uint32_t)
{
    std::abort();
}

int main()
{
    SL_Init();
    DObjInit(); // the duplicate-bone string table
    TestBodyAndWeapon();
    DObjShutdown();
    if (g_failures == 0)
        std::puts("dobj and xmodel contracts passed");
    return g_failures == 0 ? 0 : 1;
}
