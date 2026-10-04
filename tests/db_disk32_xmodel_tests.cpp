// db_disk32_xmodel_tests.cpp: the 64-bit XModel loader (NOW row 12) on
// hand-built disk32 zone images (disk32_fixture.hpp), with the production
// Load_ScriptStringCustom for bone names and Material's, TechniqueSet's and
// PhysPreset's real steps for the references. Beyond the fixture's seams, only
// the asset pools and the zone handle are replaced.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>
#include <database/db_load_legacy_bridge.h>

#include <array>
#include <cstring>

XAssetList *varXAssetList; // the envelope's native list (db_disk32_envelope.cpp)

namespace
{
using namespace disk32_test;

// Zone script-string index n holds interned id 100 + n, as the envelope leaves it.
std::array<const char *, 6> g_strings{};
XAssetList g_list{{6, g_strings.data()}, 0, nullptr};
XModel g_models[2];
Material g_materials[2];
MaterialTechniqueSet g_sets[2];
PhysPreset g_presets[2];
int g_modelCount = 0;
int g_materialCount = 0;
int g_setCount = 0;
int g_presetCount = 0;

// What a test changes in the one-surface, two-bone model Model() writes.
struct Options
{
    std::uint32_t names = kInline;
    std::uint32_t parents = kInline;
    std::uint32_t materials = kInline;
    std::uint32_t collSurfs = 0;
    std::uint32_t collCount = 0;
    std::uint32_t roots = 1;
    std::uint32_t lodBits = 0xC000'0000u; // bones 0 and 1
    float lodDist = 0.f;
    std::uint32_t preset = 0;
    std::uint32_t physGeoms = 0;
    std::uint32_t surfaces = 1;
    std::uint32_t boneInfo = kInline; // any non-null token means inline
};

// What a test changes in the one deformed surface Surface() writes.
struct SurfaceOptions
{
    std::uint32_t deformed = 1;
    std::uint32_t triCount = 2;
    std::uint32_t blendCount = 3;
    std::uint32_t vertList = 0;
    std::uint32_t vertCount = 3;
    std::uint32_t listCount = 1;
};

// What a test changes in the one rigid list and tree RigidData() writes.
struct Rigid
{
    std::uint32_t tree = kInline;
    std::uint32_t listVerts = 3;
    std::uint32_t boneOffset = 0;
    std::uint32_t nodeCount = 1;
    float scale = 1.f;
    std::uint32_t leaf = 0x8000; // triangles 0 and 1
    std::uint32_t children = 0x8001; // one leaf
};

// A rigid surface: no skinning, one rigid list.
SurfaceOptions RigidSurface(std::uint32_t list = kInline)
{
    SurfaceOptions surface;
    surface.deformed = 0;
    surface.blendCount = 0;
    surface.vertList = list;
    return surface;
}

struct File : FileBuilder<File>
{
    File &Byte(std::uint8_t value)
    {
        g_file.push_back(value);
        return *this;
    }
    // The 220-byte retail record (two bones, one a root, one surface, one LOD;
    // the unused LODs, bounds and flags hold distinct values), then its name.
    File &Model(const Options &o = {})
    {
        Word(kInline).Word(2 | o.roots << 8 | o.surfaces << 16).Word(o.names).Word(o.parents).Word(kInline).Word(kInline);
        Word(kInline).Word(kInline).Word(kInline).Word(o.materials);
        Float(o.lodDist).Word(o.surfaces).Word(o.lodBits).Word(0).Word(0).Word(0).Word(0x00CC0000u);
        for (std::uint32_t lod = 1; lod < 4; ++lod)
            Float(9).Word(0).Word(0).Word(0).Word(0).Word(0).Word(0x00CC0000u | lod);
        Word(o.collSurfs).Word(o.collCount).Word(0).Word(o.boneInfo).Float(5).Float(-1).Float(-2).Float(-3);
        Float(1).Float(2).Float(3).Word(1).Word(0xDEADBEEF).Word(0x1234).Word(0x0201).Word(o.preset).Word(o.physGeoms);
        return Text("mdl");
    }
    // Quaternions, translations, classifications and base matrices.
    File &Pose(float weight = 2.f)
    {
        Word(0x4000).Word(0).Float(1).Float(2).Float(3).Float(0).Byte(0).Byte(0);
        for (int bone = 0; bone < 2; ++bone)
            Float(0).Float(0).Float(0).Float(1).Float(0).Float(0).Float(static_cast<float>(bone)).Float(weight);
        return *this;
    }
    // Every bone array, inline: names, the parent list, then Pose().
    File &Arrays(std::uint8_t parent = 1, float weight = 2.f)
    {
        return Word(1 | 2u << 16).Byte(parent).Pose(weight);
    }
    // The 56-byte surface record, its first vertex and triangle at the bases.
    File &SurfaceRecord(const SurfaceOptions &o = {}, std::uint32_t baseVertex = 0, std::uint32_t baseTriangle = 0)
    {
        Word(o.deformed << 8 | o.vertCount << 16).Word(o.triCount | 0xEE0000u).Word(baseTriangle | baseVertex << 16);
        Word(kInline).Word(o.blendCount).Word(0).Word(o.blendCount ? kInline : 0).Word(kInline);
        return Word(o.vertList ? o.listCount : 0).Word(o.vertList).Word(0xC000'0000u).Word(0).Word(0).Word(0);
    }
    // A surface's blend records, vertices and indices.
    File &SurfaceData(const SurfaceOptions &o = {}, float sign = 1.f, std::uint32_t lastIndex = 2)
    {
        for (std::uint32_t record = 0; record < o.blendCount; ++record)
            Byte(record == 1 ? 64 : 0).Byte(0);
        return Vertices(o.vertCount, sign).Indices(lastIndex);
    }
    File &Vertices(std::uint32_t count, float sign = 1.f)
    {
        for (std::uint32_t vertex = 0; vertex < count; ++vertex)
            Float(static_cast<float>(vertex)).Float(1).Float(2).Float(sign).Word(0).Word(0).Word(0).Word(0);
        return *this;
    }
    File &Indices(std::uint32_t lastIndex = 2)
    {
        return Word(0 | 1u << 16).Word(2 | 1u << 16).Word(0 | lastIndex << 16);
    }
    // One rigid list, then its tree: one node holding one two-triangle leaf.
    File &RigidData(const Rigid &r = {})
    {
        Word(r.boneOffset | r.listVerts << 16).Word(0 | 2u << 16).Word(r.tree);
        return r.tree == kInline ? Tree(r) : *this;
    }
    // A tree: one node holding one leaf.
    File &Tree(const Rigid &r = {})
    {
        Float(0).Float(0).Float(0).Float(r.scale).Float(r.scale).Float(r.scale).Word(r.nodeCount).Word(kInline);
        Word(1).Word(kInline).Word(0).Word(0 | 1u << 16).Word(1 | 1u << 16).Word(0 | r.children << 16);
        return Byte(static_cast<std::uint8_t>(r.leaf)).Byte(static_cast<std::uint8_t>(r.leaf >> 8));
    }
    // A rigid surface's record, vertices, rigid data (none when its list is
    // named by offset) and indices.
    File &RigidSurface(const Rigid &r = {}, std::uint32_t list = kInline)
    {
        SurfaceRecord(::RigidSurface(list)).Vertices(3);
        return (list == kInline ? RigidData(r) : *this).Indices();
    }
    File &Surface(const SurfaceOptions &o = {}, float sign = 1.f, std::uint32_t lastIndex = 2)
    {
        return SurfaceRecord(o).SurfaceData(o, sign, lastIndex);
    }
    // The material handle, then a minimal material: no textures, constants or
    // state bits, and an empty technique set.
    File &MaterialTail(int count = 1)
    {
        for (int handle = 0; handle < count; ++handle)
            Word(kInline);
        for (int material = 0; material < count; ++material)
        {
            Word(kInline).Word(0x07).Word(0).Word(0).Word(0).Word(0);
            for (int i = 0; i < 34; ++i)
                Byte(0xFF);
            Word(0).Byte(0).Byte(0).Word(kInline).Word(0).Word(0).Word(0).Text("mat").Word(kInline).Word(0).Word(0);
            for (int i = 0; i < 34; ++i)
                Word(0);
            Text("ts");
        }
        return *this;
    }
    // Two bone infos: unit boxes about the origin.
    File &BoneInfo(float radiusSquared = 3.f)
    {
        for (int bone = 0; bone < 2; ++bone)
            Float(-1).Float(-1).Float(-1).Float(1).Float(1).Float(1).Float(0).Float(0).Float(0).Float(radiusSquared);
        return *this;
    }
    // A whole model: record, arrays, surface, material and bone info.
    File &Whole(const Options &o = {}, std::uint8_t parent = 1, float weight = 2.f)
    {
        return Model(o).Arrays(parent, weight).Surface().MaterialTail().BoneInfo();
    }
};

struct Zone : disk32_test::Zone<1024>
{
    explicit Zone(std::uint32_t tempBytes = 512) : disk32_test::Zone<1024>(tempBytes)
    {
        for (std::uintptr_t index = 1; index < 6; ++index)
            g_strings[index] = reinterpret_cast<const char *>(100 + index);
        varXAssetList = &g_list;
        g_modelCount = g_materialCount = g_setCount = g_presetCount = 0;
    }
    bool InVirt(const void *pointer) const
    {
        const auto *bytes = static_cast<const std::uint8_t *>(pointer);
        return bytes >= virt && bytes < virt + sizeof(virt);
    }
};

XModel *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadXModelPtrDisk32, slotValue);
}

void ExpectBlend(const Zone &zone, const XSurface &surface)
{
    Expect(zone.InVirt(surface.vertInfo.vertsBlend) && surface.vertInfo.vertsBlend[1] == 64 && !surface.vertList,
           "the blend records stay in block 4, and a deformed surface has no rigid lists");
}

void ExpectSurface(const Zone &zone, const XSurface &surface)
{
    Expect(surface.deformed && surface.vertCount == 3 && surface.triCount == 2 && surface.zoneHandle == 7
               && surface.vertInfo.vertCount[0] == 3 && surface.partBits[0] == static_cast<int>(0xC000'0000u),
           "the surface's scalars convert, its zone handle the current zone's");
    Expect(surface.verts0 == reinterpret_cast<const GfxPackedVertex *>(zone.vertex) && surface.verts0[2].xyz[0] == 2.f
               && surface.triIndices == reinterpret_cast<const std::uint16_t *>(zone.index)
               && surface.triIndices[4] == 0,
           "the vertices stream into block 7 and the indices into block 8");
    ExpectBlend(zone, surface);
}

void ExpectModel(const Zone &zone, const XModel &model);

void TestWholeModel()
{
    Zone zone;
    File().Whole();
    const XModel *const model = Load(kInline);
    Expect(model == &g_models[0] && g_materialCount == 1 && g_setCount == 1, "a model and its material publish");
    if (model == &g_models[0] && InArena(model->surfs) && InArena(model->materialHandles))
        ExpectModel(zone, *model);
    Expect(g_read == g_file.size(), "every disk byte is consumed");
}

// The bone arrays TestWholeModel builds.
void ExpectBones(const Zone &zone, const XModel &model)
{
    Expect(model.boneNames[0] == 101 && model.boneNames[1] == 102 && model.parentList[0] == 1
               && model.baseMat[1].trans[2] == 1.f && zone.InVirt(model.boneInfo)
               && model.boneInfo[1].radiusSquared == 3.f,
           "the bone arrays and bone info stay in block 4, names interned");
}

// The model TestWholeModel builds.
void ExpectModel(const Zone &zone, const XModel &m)
{
    const XModel *const model = &m;
    Expect(zone.Holds(model->name) && model->numBones == 2 && model->numsurfs == 1 && model->numLods == 1
               && model->radius == 5.f && model->maxs[2] == 3.f && model->memUsage == 0x1234 && model->bad,
           "the model's scalars convert from their retail offsets");
    ExpectBones(zone, m);
    ExpectSurface(zone, model->surfs[0]);
    Expect(model->materialHandles[0] == &g_materials[0] && !model->physPreset && !model->collSurfs,
           "the material loads through Material's step");
}

void TestTwoSurfaces()
{
    Zone zone;
    Options two;
    two.surfaces = 2;
    File().Model(two).Arrays().SurfaceRecord().SurfaceRecord({}, 3, 2).SurfaceData().SurfaceData();
    File().MaterialTail(2).BoneInfo();
    const XModel *const model = Load(kInline);
    Expect(model == &g_models[0] && g_materialCount == 2, "a two-surface model publishes with two materials");
    if (model != &g_models[0] || !InArena(model->surfs))
        return;
    const XSurface &second = model->surfs[1];
    Expect(second.baseVertIndex == 3 && second.baseTriIndex == 2 && second.vertCount == 3,
           "the second surface converts from its own retail element");
    Expect(second.verts0 == reinterpret_cast<const GfxPackedVertex *>(zone.vertex + 96)
               && second.triIndices == reinterpret_cast<const std::uint16_t *>(zone.index + 16)
               && model->materialHandles[1] == &g_materials[1] && g_read == g_file.size(),
           "its vertices and indices follow the first's, 16-aligned, and its material is its own");
}

void ExpectRigid(const XSurface &surface)
{
    const XRigidVertList *const list = surface.vertList;
    Expect(InArena(list) && list->vertCount == 3 && list->triCount == 2 && InArena(list->collisionTree),
           "a rigid list converts into native storage, its tree too");
    if (!InArena(list) || !InArena(list->collisionTree))
        return;
    const XSurfaceCollisionTree &tree = *list->collisionTree;
    Expect(tree.scale[2] == 1.f && tree.nodeCount == 1 && tree.nodes[0].childCount == 0x8001
               && tree.leafs[0].triangleBeginIndex == 0x8000,
           "the tree's nodes and leaf stay in block 4");
}

void TestRigidSurfaces()
{
    Zone zone;
    // Two rigid surfaces; the second names the first's rigid lists by offset.
    // Block 4: the bone arrays end at 104, the surface records (104..216),
    // then the first surface's list (216..228), tree (228..268), node
    // (272..288) and leaf (288..290).
    Options two;
    two.surfaces = 2;
    File().Model(two).Arrays().SurfaceRecord(RigidSurface()).SurfaceRecord(RigidSurface(VirtualOffset(216)), 3, 2);
    File().Vertices(3).RigidData().Indices().Vertices(3).Indices().MaterialTail(2).BoneInfo();
    const XModel *const model = Load(kInline);
    Expect(model == &g_models[0] && g_read == g_file.size(), "a model with rigid surfaces publishes");
    if (model != &g_models[0] || !InArena(model->surfs))
        return;
    ExpectRigid(model->surfs[0]);
    Expect(model->surfs[1].vertList == model->surfs[0].vertList && !model->surfs[1].deformed,
           "a rigid-list offset resolves to the earlier native lists");
}

void TestTwoRigidLists()
{
    Zone zone;
    // One surface split into two rigid lists: bone 0 owns vertices 0..1 and
    // triangle 0, bone 1 vertex 2 and triangle 1, each with its own tree.
    SurfaceOptions split = RigidSurface();
    split.listCount = 2;
    Rigid first;
    first.leaf = 0;
    Rigid second;
    second.leaf = 1;
    File().Model().Arrays().SurfaceRecord(split).Vertices(3);
    File().Word(0 | 2u << 16).Word(0 | 1u << 16).Word(kInline).Word(64 | 1u << 16).Word(1 | 1u << 16).Word(kInline);
    File().Tree(first).Tree(second).Indices().MaterialTail().BoneInfo();
    const XModel *const model = Load(kInline);
    Expect(model == &g_models[0] && g_read == g_file.size(), "a surface with two rigid lists publishes");
    if (model != &g_models[0] || !InArena(model->surfs) || !InArena(model->surfs[0].vertList))
        return;
    const XRigidVertList *const lists = model->surfs[0].vertList;
    Expect(lists[1].boneOffset == 64 && lists[1].vertCount == 1 && lists[1].triOffset == 1
               && lists[1].collisionTree != lists[0].collisionTree && lists[1].collisionTree->leafs[0].triangleBeginIndex == 1,
           "the second list converts from its own retail element, with its own tree");
}

void TestNamedArraysAndPreset()
{
    Zone zone;
    // Block 4: "mdl" (0..4), names (4..8), parent (8). The second model names
    // those by offset, so its names are not interned twice; its preset loads
    // through PhysPreset's step.
    File().Whole();
    Options named;
    named.names = VirtualOffset(4);
    named.parents = VirtualOffset(8);
    named.preset = kInline;
    named.boneInfo = VirtualOffset(4); // still inline, as on x86
    File().Model(named).Pose().Surface().MaterialTail().BoneInfo();
    File().Word(kInline).Word(0).Float(1).Float(0.5f).Float(0.5f).Float(1).Float(1).Word(0).Float(0).Float(0).Word(0);
    File().Text("pp");
    const XModel *const first = Load(kInline);
    const XModel *const second = Load(kInline);
    Expect(first == &g_models[0] && second == &g_models[1] && g_presetCount == 1, "both models publish");
    if (second != &g_models[1])
        return;
    Expect(second->boneNames == first->boneNames && second->parentList == first->parentList
               && second->boneNames[1] == 102,
           "named bone arrays resolve to the first model's, names interned once");
    Expect(second->physPreset == &g_presets[0] && second->boneInfo != first->boneInfo && g_read == g_file.size(),
           "the preset loads through PhysPreset's step, and any bone-info token streams inline");
}

struct Malformed
{
    const char *what;
    void (*build)();
    const char *error;
};

// A model up to its surface record.
File Head(const Options &o = {})
{
    return File().Model(o).Arrays();
}

const Malformed kMalformed[] = {
    {"truncated record", [] { File().Word(kInline).Word(3); }, "ended unexpectedly"},
    {"more roots than bones", [] { Options o; o.roots = 3; File().Model(o); }, "bone counts"},
    {"collision surfaces without a count", [] { Options o; o.collSurfs = kInline; File().Model(o); }, "collision"},
    {"a collision count without surfaces", [] { Options o; o.collCount = 2; File().Model(o); }, "collision"},
    {"bone name past the string list", [] { File().Model().Word(9); }, "script-string index"},
    {"unmapped parent offset", [] { Options o; o.parents = VirtualOffset(512); File().Model(o).Word(1); },
     "pointer offset"},
    {"deformed byte 2", [] { SurfaceOptions s; s.deformed = 2; Head().Surface(s); }, "surface pointer/count layout"},
    {"one triangle", [] { SurfaceOptions s; s.triCount = 1; Head().Surface(s); }, "layout"},
    {"skinning that misses a vertex", [] { SurfaceOptions s; s.blendCount = 2; Head().Surface(s); }, "layout"},
    {"a tree with no nodes", [] { Rigid r; r.nodeCount = 0; Head().RigidSurface(r); }, "collision-tree layout"},
    {"a tree of scale 0", [] { Rigid r; r.scale = 0.f; Head().RigidSurface(r); }, "collision transform"},
    {"a node without children", [] { Rigid r; r.children = 0x8000; Head().RigidSurface(r); }, "collision topology"},
    {"a leaf past its list", [] { Rigid r; r.leaf = 0x8002; Head().RigidSurface(r); }, "collision relationship"},
    {"a list missing a vertex", [] { Rigid r; r.listVerts = 2; Head().RigidSurface(r).MaterialTail().BoneInfo(); },
     "rigid surface partition"},
    {"a list bone offset of 1", [] { Rigid r; r.boneOffset = 1; Head().RigidSurface(r).MaterialTail().BoneInfo(); },
     "rigid surface partition"},
    {"an unmapped tree offset", [] { Rigid r; r.tree = VirtualOffset(512); Head().RigidSurface(r); }, "alias offset"},
    {"an unmapped list offset", [] { Head().RigidSurface({}, VirtualOffset(512)); }, "alias offset"},
    {"vertices past block 7", [] { SurfaceOptions s; s.vertCount = 9; s.blendCount = 9; Head().Surface(s); },
     "exceeds stream block"},
    {"binormal sign 0", [] { Head().Surface({}, 0.f); }, "surface geometry"},
    {"index past the vertices", [] { Head().Surface({}, 1.f, 3); }, "surface geometry"},
    {"no material handles", [] { Options o; o.materials = 0; Head(o).Surface().BoneInfo(); }, "model header"},
    {"collision surfaces", [] { Options o; o.collSurfs = kInline; o.collCount = 1; Head(o).Surface().MaterialTail(); },
     "collision surfaces are not"},
    {"physics geometry", [] { Options o; o.physGeoms = kInline; File().Whole(o); }, "physics geometry is not"},
    {"a non-unit base pose", [] { File().Whole({}, 1, 1.f); }, "array span"},
    {"a wrong bone radius", [] { Head().Surface().MaterialTail().BoneInfo(2.f); }, "array span"},
    {"a surface bone outside its LOD", [] { Options o; o.lodBits = 0x8000'0000u; File().Whole(o); }, "escape its LOD"},
    {"a negative LOD distance", [] { Options o; o.lodDist = -1.f; File().Whole(o); }, "model LOD"},
    {"a parent offset of 0", [] { File().Whole({}, 0); }, "parent"},
    {"unmapped model alias", [] {}, "alias offset"},
};

void TestMalformedFailsClosed()
{
    for (const Malformed &test : kMalformed)
    {
        Zone zone;
        test.build();
        const std::uintptr_t slot = g_file.empty() ? VirtualOffset(16) : kInline;
        ExpectDrop(test.what, test.error, [&] { Load(slot); });
    }
    Zone zone;
    ExpectDrop("slot wider than a token", "no disk32 token", [] { Load(std::uintptr_t{1} << 32); });
}
} // namespace

// db_stringtable_load.cpp's marking path, which no load reaches.
db::load_legacy_bridge::LegacyBridgeStatus db::load_legacy_bridge::DbLoadLegacyBridge::TryAddUser4(std::uint32_t) noexcept
{
    Expect(false, "loading marks no script string");
    return LegacyBridgeStatus::Success;
}

bool db::load_legacy_bridge::DbLoadLegacyBridge::InSession() noexcept
{
    return false;
}

void __cdecl Load_GetCurrentZoneHandle(uint8_t *handle)
{
    *handle = 7;
}

void __cdecl Load_XModelAsset(XAssetHeader *header)
{
    XModel &entry = g_models[g_modelCount++];
    ++g_published;
    entry = *header->model;
    header->model = &entry;
}

void __cdecl Load_MaterialAsset(XAssetHeader *header)
{
    g_materials[g_materialCount] = *header->material;
    header->material = &g_materials[g_materialCount++];
}

void __cdecl Load_MaterialTechniqueSetAsset(XAssetHeader *header)
{
    MaterialTechniqueSet &entry = g_sets[g_setCount++];
    entry = *header->techniqueSet;
    entry.remappedTechniqueSet = &entry; // as DB_MediaRemapTechniqueSet leaves an unremapped set
    header->techniqueSet = &entry;
}

void __cdecl Load_PhysPresetAsset(XAssetHeader *header)
{
    g_presets[g_presetCount] = *header->physPreset;
    header->physPreset = &g_presets[g_presetCount++];
}

void __cdecl Load_GfxImageAsset(XAssetHeader *)
{
    Expect(false, "a model's material here has no textures");
}

void __cdecl DB_LoadedExternalData(std::int32_t)
{
    Expect(false, "no image loads");
}

int main()
{
    return Run({TestWholeModel, TestTwoSurfaces, TestRigidSurfaces, TestTwoRigidLists, TestNamedArraysAndPreset, TestMalformedFailsClosed});
}
