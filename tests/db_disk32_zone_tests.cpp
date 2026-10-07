// db_disk32_zone_tests.cpp: one synthetic zone holding the server-closure
// families the 64-bit loader converts (NOW row 12, gate G2), loaded through the
// real path by disk32_zone_harness.cpp. Each writer streams one asset as its
// family's loader reads it. An inserted family's record streams into the temp
// block and its strings into block 4; a -2 header first takes a 4-byte alias
// slot in block 4, which later offset tokens name. A completed family's record
// streams into block 4 itself.

#include "disk32_zone_harness.hpp"

#include <EffectsCore/fx_runtime.h>
#include <gfx_d3d/r_font.h>
#include <gfx_d3d/r_gfx.h>
#include <gfx_d3d/r_material.h>
#include <script/scr_stringlist.h>
#include <ui/ui_shared.h>
#include <database/db_disk32_mirrors.h>

#include <bit>
#include <cfloat>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <string>
#include <tuple>
#include <vector>

namespace
{
using namespace zone_test;

// Block-4 offsets the writers record for later tokens.
struct Offsets
{
    std::uint32_t rawFile, physPreset, curve, loaded, sound, image, lightDef, techniqueSet, material, fx, menu, model;
} g_at{};

void WriteRawFile(Image &z)
{
    z.Word(kInline).Word(2).Word(kInline).V("e2e/a.gsc").V("hi");
}
void WriteSharedRawFile(Image &z)
{
    g_at.rawFile = z.VAlloc(4);
    z.Word(kInline).Word(0).Word(0).V("e2e/b.cfg");
}
void WriteNothing(Image &) {} // an offset header token streams nothing

void WriteStringTable(Image &z) // 2 x 1: the record and value tokens in block 4
{
    z.VAlloc(16);
    z.Word(kInline).Word(2).Word(1).Word(kInline).V("e2e/table");
    z.VAlloc(8);
    z.Word(kInline).Word(kInline).V("a").V("b");
}
void WritePhysPreset(Image &z)
{
    g_at.physPreset = z.VAlloc(4);
    z.Word(kInline).Word(3).Float(2.5f).Float(0.5f).Float(0.25f).Float(1).Float(1).Word(kInline);
    z.Float(0.125f).Float(4).Word(1).V("e2e/phys").V("wood");
}
void WriteLocalize(Image &z)
{
    z.Word(kInline).Word(kInline).V("Hello").V("E2E_HELLO");
}
void WriteMapEnts(Image &z)
{
    z.Word(kInline).Word(kInline).Word(3).V("e2e/ents").V("{}");
}
void WriteGameWorldMp(Image &z)
{
    z.Word(kInline).V("e2e/gameworld");
}
void WriteComWorld(Image &z) // one primary light, its record in block 4
{
    z.Word(kInline).Word(1).Word(1).Word(kInline).V("e2e/comworld");
    z.VAlloc(0x44);
    z.Fill(0x40).Word(kInline).V("e2e/light0");
}

void WriteSoundCurve(Image &z)
{
    g_at.curve = z.VAlloc(4);
    z.Word(kInline).Word(2).Float(0).Float(1).Float(1).Float(0).Fill(48);
    z.V("e2e/curve");
}
void WriteLoadedSound(Image &z)
{
    g_at.loaded = z.VAlloc(4);
    z.Word(kInline).Word(1).Word(0xDEADBEEF).Word(0).Word(22050).Word(16).Word(1).Word(0).Word(2).Word(0).Word(0);
    z.V("e2e/loaded");
}
// A speaker map's 400 channel bytes: per source, 2 then 6 speakers.
void Channels(Image &z)
{
    for (std::uint32_t source = 0; source < 2; ++source)
    {
        for (std::uint32_t speakers : {2u, 6u})
        {
            z.Word(speakers);
            for (std::uint32_t speaker = 0; speaker < 6; ++speaker)
                z.Word(speaker < speakers ? speaker : 0).Word(speaker < speakers ? source + 1 : 0).Float(0.5f).Float(0.5f);
        }
    }
}
// One alias whose loaded sound and falloff curve are the earlier shared
// assets, by offset; its sound file and speaker map are completed in block 4.
void WriteSound(Image &z)
{
    g_at.sound = z.VAlloc(4);
    z.Word(kInline).Word(kInline).Word(1).V("e2e/snd");
    z.VAlloc(0x5C);
    z.Word(kInline).Word(0).Word(0).Word(0).Word(kInline).Word(0).Fill(24).Word(1u << 6).Fill(20);
    z.Word(Virt(g_at.curve)).Fill(12).Word(kInline);
    z.V("e2e/alias");
    z.VAlloc(12);
    z.Word(0x101).Word(Virt(g_at.loaded)).Word(0);
    z.VAlloc(0x198);
    z.Word(0).Word(kInline);
    Channels(z);
    z.V("e2e/map");
}
void WriteImageShared(Image &z) // no pixels: the texture token is null
{
    g_at.image = z.VAlloc(4);
    z.Word(2).Word(0).Fill(24).Word(kInline);
    z.V("e2e/img");
}
void WriteLightDef(Image &z) // the attenuation image is the shared image, by offset
{
    g_at.lightDef = z.VAlloc(4);
    z.Word(kInline).Word(Virt(g_at.image)).Word(1).Word(7);
    z.V("e2e/lightdef");
}

void WriteTechniqueSet(Image &z) // no techniques; the pool call sets its remap
{
    g_at.techniqueSet = z.VAlloc(4);
    z.Word(kInline).Word(0).Word(0xDEADBEEF).Fill(34 * 4).V("e2e/techset");
}
// Two textures, ordered by name hash: the shared image by offset, then a
// second image inline, which publishes as the material loads.
void WriteMaterial(Image &z)
{
    g_at.material = z.VAlloc(4);
    z.Word(kInline).Word(1u << 8).Fill(16).Fill(34, 0xFF).Fill(1, 2).Fill(5); // no technique: state 255
    z.Word(Virt(g_at.techniqueSet)).Word(kInline).Word(0).Word(0).V("e2e/material");
    z.VAlloc(24);
    z.Word(1).Word(0x02010201).Word(Virt(g_at.image)).Word(2).Word(0x02010201).Word(kInline);
    z.Word(2).Word(0).Fill(24).Word(kInline).V("e2e/image2");
}
void WriteFont(Image &z) // both materials name the shared one; 96 glyphs in block 4
{
    z.Word(kInline).Word(16).Word(96).Word(Virt(g_at.material)).Word(Virt(g_at.material)).Word(kInline);
    z.V("e2e/font");
    z.VAlloc(96 * 24);
    z.Fill(96 * 24);
}

// One one-shot sprite element drawing the shared material, with two velocity
// and two visual-state samples in block 4. totalSize is the record, the
// element, the name and the samples.
void WriteFx(Image &z)
{
    g_at.fx = z.VAlloc(4);
    z.Word(kInline).Word(0).Word(32 + 252 + 7 + 2 * 96 + 2 * 48).Word(0).Word(0).Word(1).Word(0).Word(kInline);
    z.V("e2e/fx");
    z.VAlloc(252);
    z.Record(252, {{0x08, 1}, {0x30, 100}, {0xAC, 1u << 16}, {0xB0, 0x01010100}, {0xB4, kInline}, {0xB8, kInline},
                   {0xBC, Virt(g_at.material)}});
    z.VAlloc(2 * 96);
    z.Fill(2 * 96);
    z.VAlloc(2 * 48);
    z.Fill(2 * 48);
}
void WriteImpactFx(Image &z) // 12 surfaces; the first names the shared effect
{
    z.Word(kInline).Word(kInline).V("e2e/impacts");
    z.VAlloc(12 * 0x84);
    z.Word(Virt(g_at.fx)).Fill(12 * 0x84 - 4);
}
// The window draws the shared material; one item (type 1, no type data)
// focuses with the shared sound.
void WriteMenu(Image &z)
{
    g_at.menu = z.VAlloc(4);
    z.Record(0x11C, {{0x00, kInline}, {0x98, Virt(g_at.material)}, {0xA4, 1}, {0x118, kInline}}).V("e2e/menu");
    z.VAlloc(4);
    z.Word(kInline);
    z.VAlloc(0x174);
    z.Record(0x174, {{0xB4, 1}, {0x120, Virt(g_at.sound)}});
}
void WriteMenuList(Image &z) // names the shared menu by offset
{
    z.Word(kInline).Word(1).Word(kInline).V("e2e/menus");
    z.VAlloc(4);
    z.Word(Virt(g_at.menu));
}

// Two bones (one root, named by zone script strings 1 and 2), one deformed
// surface (vertices in block 7, indices in block 8) drawing the shared
// material, and one LOD.
void WriteXModel(Image &z)
{
    g_at.model = z.VAlloc(4);
    z.Word(kInline).Word(2 | 1u << 8 | 1u << 16);
    for (int pointer = 0; pointer < 8; ++pointer)
        z.Word(kInline); // bone names, parents, quats, trans, classes, base matrices, surfaces, materials
    z.Float(0).Word(1).Word(0xC000'0000u).Fill(12).Word(0x00CC0000u);
    for (std::uint32_t lod = 1; lod < 4; ++lod)
        z.Float(9).Fill(20).Word(0x00CC0000u | lod);
    z.Word(0).Word(0).Word(0).Word(kInline).Float(5).Float(-1).Float(-2).Float(-3).Float(1).Float(2).Float(3);
    z.Word(1).Word(0xDEADBEEF).Word(0x1234).Word(0x0201).Word(0).Word(0).V("e2e/model");
    z.VAlloc(4, 2); z.VAlloc(1, 1); z.VAlloc(8, 2); z.VAlloc(16); z.VAlloc(2, 1); z.VAlloc(64);
    z.Word(1 | 2u << 16).Fill(1, 1).Word(0x4000).Word(0).Float(1).Float(2).Float(3).Float(0).Fill(2);
    for (int bone = 0; bone < 2; ++bone)
        z.Float(0).Float(0).Float(0).Float(1).Float(0).Float(0).Float(static_cast<float>(bone)).Float(2);
    z.VAlloc(56); z.VAlloc(6, 2); z.VAlloc(4); z.VAlloc(80); // surface, blend, material, bone info
    z.Word(1u << 8 | 3u << 16).Word(2 | 0xEE0000u).Word(0).Word(kInline).Word(3).Word(0).Word(kInline);
    z.Word(kInline).Word(0).Word(0).Word(0xC000'0000u).Fill(12);
    z.Fill(1).Fill(1).Fill(1, 64).Fill(3); // blend records
    for (int vertex = 0; vertex < 3; ++vertex)
        z.Float(static_cast<float>(vertex)).Float(1).Float(2).Float(1).Fill(16);
    z.Word(1u << 16).Word(2 | 1u << 16).Word(2u << 16).Word(Virt(g_at.material));
    for (int bone = 0; bone < 2; ++bone)
        z.Float(-1).Float(-1).Float(-1).Float(1).Float(1).Float(1).Float(0).Float(0).Float(0).Float(3);
}
// One one-shot model element showing the shared model.
void WriteFxModel(Image &z)
{
    z.Word(kInline).Word(0).Word(32 + 252 + 12 + 2 * 96 + 2 * 48).Word(0).Word(0).Word(1).Word(0).Word(kInline);
    z.V("e2e/fxmodel").VAlloc(252); z.VAlloc(2 * 96); z.VAlloc(2 * 48);
    z.Record(252, {{0x08, 1}, {0x30, 100}, {0xB0, 0x01010105}, {0xB4, kInline}, {0xB8, kInline},
                   {0xBC, Virt(g_at.model)}});
    z.Fill(2 * 96 + 2 * 48);
}
// One bone, named by zone script string 1; no frames data.
void WriteXAnimParts(Image &z)
{
    z.Word(kInline).Fill(10).Word(255 | 2u << 16).Fill(9).Fill(1, 1).Word(1u << 8);
    z.Word(0).Word(0).Float(30).Float(1.5f).Word(kInline).Fill(9 * 4).V("e2e/anim").Fill(1, 1).Fill(1);
    z.VAlloc(2, 2);
}
// A gun model, flash effect and reticle by offset, and a pickup sound by
// name through its string holder.
void WriteWeapon(Image &z)
{
    using W = disk32::WeaponDefDisk32;
    z.Record(sizeof(W), {{offsetof(W, szInternalName), kInline}, {offsetof(W, gunXModel), Virt(g_at.model)},
                         {offsetof(W, viewFlashEffect), Virt(g_at.fx)}, {offsetof(W, pickupSound), kInline},
                         {offsetof(W, reticleCenter), Virt(g_at.material)}});
    z.V("e2e/weapon").VAlloc(4);
    z.Word(kInline).V("e2e/snd");
}

// A brush: unit bounds (or a box brush's sentinels), its sides and edges.
void Brush(Image &z, bool isBox, std::uint32_t sides, std::uint32_t edges, std::uint32_t sideCount)
{
    const std::uint32_t low = std::bit_cast<std::uint32_t>(isBox ? FLT_MAX : 0.f);
    const std::uint32_t high = std::bit_cast<std::uint32_t>(isBox ? -FLT_MAX : 1.f);
    const std::uint32_t axial = isBox ? 0xFFFFFFFFu : 0;
    z.Record(80, {{0, low}, {4, low}, {8, low}, {12, isBox ? 0xFFFFFFFFu : 1}, {16, high}, {20, high}, {24, high},
                  {28, sideCount}, {32, sides}, {36, axial}, {40, axial}, {44, axial}, {48, edges}});
}
// A dynamic entity: its model, effect and physics preset are the shared
// ones; pieces inline, by offset, or none.
void Def(Image &z, std::uint32_t pieces)
{
    z.Word(1).Fill(28).Word(Virt(g_at.model)).Word(2 | 3u << 16).Word(Virt(g_at.fx)).Word(pieces);
    z.Word(Virt(g_at.physPreset)).Word(100).Fill(36).Word(5);
}
// ClipMapPvs, as the clip-map family test's whole map lays it out: planes, a
// static model, materials, sides, edges, nodes, a leaf, leaf-brush nodes and
// brushes, surfaces, vertices, triangles, borders, partitions, an AABB tree,
// a submodel, two brushes, visibility, map entities, the box brush, and
// three dynamic entities, two sharing their model pieces.
void WriteClipMap(Image &z)
{
    std::vector<std::pair<std::uint32_t, std::uint32_t>> words = {{0x000, kInline}, {0x004, 1}, {0x118, 0xC5C5C5C5}};
    for (const auto &[at, count] : std::initializer_list<std::pair<std::uint32_t, std::uint32_t>>{
             {0x008, 2}, {0x010, 1}, {0x018, 1}, {0x020, 2}, {0x028, 6}, {0x030, 2}, {0x038, 1}, {0x040, 3},
             {0x048, 2}, {0x050, 1}, {0x058, 1}, {0x060, 11}, {0x06C, 1}, {0x074, 2}, {0x07C, 1}, {0x084, 1},
             {0x08C, 2}})
        words.insert(words.end(), {{at, count}, {at + 4, kInline}});
    for (std::uint32_t at : {0x068, 0x09C, 0x0A4, 0x0A8, 0x0F8, 0x0FC, 0x100, 0x10C, 0x110})
        words.push_back({at, kInline});
    words.insert(words.end(), {{0x094, 2}, {0x098, 1}, {0x0F4, 2 | 1u << 16}});
    z.Record(0x11C, words);
    z.V("e2e/clipmap");
    const std::uint32_t planes = z.VAlloc(40);
    z.Float(1).Float(0).Float(0).Float(1).Word(0).Float(0).Float(-1).Float(0).Float(1).Word(3 | 2u << 8);
    z.VAlloc(80);
    z.Word(5).Word(Virt(g_at.model));
    for (int value = 0; value < 18; ++value)
        z.Float(static_cast<float>(value) + 0.5f);
    z.VAlloc(72);
    z.Fill(72, 0x22);
    const std::uint32_t side = z.VAlloc(24);
    z.Word(Virt(planes + 20)).Word(0).Word(3u << 16).Word(Virt(planes)).Word(0).Word(3u << 16);
    const std::uint32_t edges = z.VAlloc(6, 1);
    z.Fill(1, 1).Fill(1, 2).Fill(1, 0).Fill(1, 0).Fill(1, 1).Fill(1, 2);
    z.VAlloc(16);
    z.Word(Virt(planes)).Word(1 | 2u << 16).Word(kInline).Word(3 | 4u << 16);
    z.VAlloc(20);
    z.Float(9).Float(8).Float(7).Float(6).Float(5);
    z.VAlloc(44);
    z.Fill(44, 0x44);
    z.VAlloc(4, 2);
    z.Fill(4, 0x55);
    z.VAlloc(60);
    z.Word(1 | 1u << 16).Word(7).Word(kInline).Word(0).Word(0).Word(2 | 1u << 16).Word(9).Word(kInline).Word(0);
    z.Word(0).Word(0).Word(8).Float(1.5f).Float(2.5f).Word(3 | 4u << 16);
    z.VAlloc(2, 2);
    z.Fill(1, 1).Fill(1);
    z.VAlloc(2, 2);
    z.Fill(1, 6).Fill(1);
    for (const auto &[align, count, value] : {std::tuple{4u, 4u, 0x66}, {4u, 12u, 0x77}, {2u, 66u, 0x88},
                                              {1u, 8u, 0x99}, {4u, 28u, 0xAA}})
    {
        z.VAlloc(count, align);
        z.Fill(count, static_cast<std::uint8_t>(value));
    }
    z.VAlloc(24);
    const std::uint32_t border = z.VAlloc(28);
    z.Word(3 | 1u << 8).Word(5).Word(kInline).Word(4 | 1u << 8).Word(6).Word(Virt(border));
    for (int value = 0; value < 7; ++value)
        z.Float(static_cast<float>(value));
    z.VAlloc(32);
    z.Fill(32, 0xBB);
    z.VAlloc(72);
    z.Fill(72, 0xCC);
    z.VAlloc(160, 16);
    Brush(z, false, Virt(side), Virt(edges), 1);
    Brush(z, false, Virt(side + 12), Virt(edges + 3), 1);
    z.VAlloc(2, 1);
    z.Fill(2, 0xDD);
    z.Word(kInline).Word(kInline).Word(2).V("me").V("e");
    z.VAlloc(80, 16);
    Brush(z, true, 0, 0, 0);
    const std::uint32_t defs = z.VAlloc(192);
    Def(z, kInline);
    Def(z, Virt(defs + 192));
    z.VAlloc(12);
    z.Word(kInline).Word(1).Word(kInline).V("pc");
    z.VAlloc(16);
    z.Word(Virt(g_at.model)).Float(1).Float(2).Float(3);
    z.VAlloc(96);
    Def(z, 0);
}

// A raw file whose bytes end block 4 at 12 mod 128, so the world after it
// streams at the alignment the world family test lays it out for.
void WritePadding(Image &z)
{
    std::uint32_t bytes = (12u - (z.virt + 8)) % 128u;
    bytes = bytes ? bytes : 128;
    z.Word(kInline).Word(bytes - 1).Word(kInline).V("e2e/pad").V(std::string(bytes - 1, 'p'));
}

// GfxWorld, as the world family test lays it out from block-4 offset 12
// (`at` maps its offsets here): two portaled cells, a probe, a brush model,
// two static surfaces, a static model, two primary lights with shadow
// geometry and regions, a light grid, and both DPVS halves. Its image,
// light def and material are the shared ones.
void WriteGfxWorld(Image &z)
{
    const std::uint32_t base = z.virt - 12;
    const auto at = [base](std::uint32_t offset) {
        return offset == 0 ? Virt(g_at.image) : offset == 4 ? Virt(g_at.lightDef)
             : offset == 8 ? Virt(g_at.material) : Virt(base + offset);
    };
    std::vector<std::pair<std::uint32_t, std::uint32_t>> words = {
        {0x010, 3}, {0x018, 2}, {0x020, 2}, {0x028, at(0)}, {0x0DC, 2}, {0x0E4, 1}, {0x0F0, 2}, {0x100, 16},
        {0x150, 1}, {0x244, 1}, {0x248, 2}, {0x008, 2}, {0x00C, 2}, {0x108, 1}, {0x118, 2}, {0x11C, 3u << 16}, {0x128, 1},
        {0x130, 3}, {0x138, 1}, {0x140, 1}, {0x174, 1}, {0x180, at(8)}, {0x030, 1}, {0x03C, 3}, {0x21C, at(0)},
        {0x2B4, 1}, {0x2B8, 1}, {0x0E0, 1}, {0x24C, 1}, {0x268, 1}, {0x26C, 1}, {0x2AC, 1}, {0x2B0, 1}};
    for (std::uint32_t offset : {0x000, 0x004, 0x014, 0x024, 0x0E8, 0x0EC, 0x104, 0x154, 0x220, 0x28C, 0x0C8,
                                 0x0F4, 0x0F8, 0x0FC, 0x10C, 0x12C, 0x134, 0x13C, 0x144, 0x148, 0x14C, 0x178,
                                 0x034, 0x040, 0x224, 0x228, 0x230, 0x238, 0x234, 0x23C, 0x240})
        words.push_back({offset, kInline});
    for (std::uint32_t offset = 0x270; offset <= 0x2A4; offset += 4)
        words.push_back({offset, kInline});
    for (std::uint32_t offset = 0x2BC; offset <= 0x2D8; offset += 4)
        words.push_back({offset, kInline});
    z.Record(732, words);
    const auto half = [&z](std::uint16_t a, std::uint16_t b) { z.Word(a | static_cast<std::uint32_t>(b) << 16); };
    const auto floats = [&z](int count, float start = 0) {
        for (int value = 0; value < count; ++value)
            z.Float(start + static_cast<float>(value));
    };
    z.V("w").V("ba");
    z.bytes.insert(z.bytes.end(), {1, 0, 2, 0, 3, 0});
    z.Word(7).Word(8).Word(1 | 1u << 8).Float(0.5f);
    floats(11, 1);
    z.Word(3).Word(4).Word(at(4)).Float(1).Float(2).Float(3).Word(at(0));
    floats(10);
    half(5, 6);
    // Two cells, their trees, portals and indices.
    for (const auto &[trees, groups] : {std::pair{3u, 1u}, {1u, 0u}})
    {
        z.Float(0).Float(0).Float(0).Float(1).Float(1).Float(1).Word(trees).Word(kInline).Word(1).Word(kInline);
        z.Word(groups).Word(groups ? kInline : 0).Word(1).Word(kInline);
    }
    const auto tree = [&](std::uint16_t children, std::uint16_t surfaces, std::uint16_t first, std::uint16_t smodels,
                          std::uint32_t childrenOffset, std::uint16_t noDecal) {
        z.Float(0).Float(0).Float(0).Float(1).Float(1).Float(1);
        half(children, surfaces), half(first, noDecal), half(noDecal ? 2 : 0, smodels);
        z.Word(smodels ? kInline : 0).Word(childrenOffset);
    };
    const auto portal = [&](std::uint32_t cell) {
        z.Word(0).Word(0).Word(0).Float(1).Float(0).Float(0).Float(-1).Word(12 | 4u << 8 | 8u << 16);
        z.Word(cell).Word(kInline).Word(3);
        floats(6), floats(9);
    };
    tree(2, 2, 0, 1, 44, 1), tree(0, 1, 0, 0, 0, 1), tree(0, 1, 1, 0, 0, 0);
    z.bytes.insert(z.bytes.end(), {0, 0});
    portal(at(156 + 56));
    z.Word(0).Fill(1);
    tree(0, 0, 0, 0, 0, 0);
    portal(at(156));
    z.Fill(1);
    // The lightmap, light grid, brush model, material memory, vertex and layer bytes.
    z.Word(at(0)).Word(0);
    half(7, 8);
    z.Fill(1, 'a').Fill(1, 'b').Fill(1).Word(9);
    for (std::uint32_t value = 0; value < 42; ++value)
        z.Word(value);
    floats(12);
    half(2, 0), half(1, 0);
    z.Word(at(8)).Word(77);
    floats(11);
    z.Fill(1, 'x').Fill(1, 'y').Fill(1);
    // The primary lights' shadow geometry and regions.
    z.Word(1).Word(kInline).Word(0).Word(2 | 1u << 16).Word(kInline).Word(kInline);
    half(1, 0), half(1, 0);
    z.Word(1).Word(kInline).Word(0).Word(0);
    floats(18);
    z.Word(1).Word(kInline).Float(0).Float(0).Float(1).Float(2).Float(3);
    // The static DPVS.
    half(0, 1);
    z.bytes.insert(z.bytes.end(), {1, 0});
    for (std::uint32_t value = 20; value < 27; ++value)
        z.Word(value);
    for (std::uint32_t surface = 0; surface < 2; ++surface)
    {
        z.Word(surface).Word(0).Word(3).Word(0).Word(surface ? 0 : at(8)).Word(surface);
        floats(6);
    }
    floats(6);
    z.Word(1).Word(0).Float(500);
    for (int value = 0; value < 13; ++value)
        z.Float(1);
    z.Word(0);
    half(4, 5), half(6, 7);
    z.Word(0x01020304).Word(1);
}

const Asset kZone[] = {
    {ASSET_TYPE_RAWFILE, kInline, "e2e/a.gsc", WriteRawFile},
    {ASSET_TYPE_RAWFILE, kShared, "e2e/b.cfg", WriteSharedRawFile},
    {ASSET_TYPE_RAWFILE, 0, "e2e/b.cfg", WriteNothing, &g_at.rawFile},
    {ASSET_TYPE_STRINGTABLE, kInline, "e2e/table", WriteStringTable},
    {ASSET_TYPE_PHYSPRESET, kShared, "e2e/phys", WritePhysPreset},
    {ASSET_TYPE_LOCALIZE_ENTRY, kInline, "E2E_HELLO", WriteLocalize},
    {ASSET_TYPE_MAP_ENTS, kInline, "e2e/ents", WriteMapEnts},
    {ASSET_TYPE_GAMEWORLD_MP, kInline, "e2e/gameworld", WriteGameWorldMp},
    {ASSET_TYPE_COMWORLD, kInline, "e2e/comworld", WriteComWorld},
    {ASSET_TYPE_SOUND_CURVE, kShared, "e2e/curve", WriteSoundCurve},
    {ASSET_TYPE_LOADED_SOUND, kShared, "e2e/loaded", WriteLoadedSound},
    {ASSET_TYPE_SOUND, kShared, "e2e/snd", WriteSound},
    {ASSET_TYPE_IMAGE, kShared, "e2e/img", WriteImageShared},
    {ASSET_TYPE_LIGHT_DEF, kShared, "e2e/lightdef", WriteLightDef},
    {ASSET_TYPE_TECHNIQUE_SET, kShared, "e2e/techset", WriteTechniqueSet},
    {ASSET_TYPE_MATERIAL, kShared, "e2e/material", WriteMaterial},
    {ASSET_TYPE_FONT, kInline, "e2e/font", WriteFont},
    {ASSET_TYPE_FX, kShared, "e2e/fx", WriteFx},
    {ASSET_TYPE_IMPACT_FX, kInline, "e2e/impacts", WriteImpactFx},
    {ASSET_TYPE_MENU, kShared, "e2e/menu", WriteMenu},
    {ASSET_TYPE_MENULIST, kInline, "e2e/menus", WriteMenuList},
    {ASSET_TYPE_XMODEL, kShared, "e2e/model", WriteXModel},
    {ASSET_TYPE_FX, kInline, "e2e/fxmodel", WriteFxModel},
    {ASSET_TYPE_XANIMPARTS, kInline, "e2e/anim", WriteXAnimParts},
    {ASSET_TYPE_WEAPON, kInline, "e2e/weapon", WriteWeapon},
    {ASSET_TYPE_CLIPMAP_PVS, kInline, "e2e/clipmap", WriteClipMap},
    {ASSET_TYPE_RAWFILE, kInline, "e2e/pad", WritePadding},
    {ASSET_TYPE_GFXWORLD, kInline, "w", WriteGfxWorld},
};
const char *const kScriptStrings[] = {"e2e_tag", "bone_root", "bone_child"};
} // namespace

std::span<const Asset> zone_test::ZoneAssets()
{
    return kZone;
}

std::span<const char *const> zone_test::ZoneScriptStrings()
{
    return kScriptStrings;
}

// The fields each family converted, and every reference: each names the
// published asset the pool lookup finds.
void zone_test::CheckZone()
{
    const RawFile *raw = Find(ASSET_TYPE_RAWFILE, "e2e/a.gsc").rawfile;
    Expect(raw && raw->len == 2 && !std::strcmp(raw->buffer, "hi"), "the raw file's bytes load");
    const StringTable *table = Find(ASSET_TYPE_STRINGTABLE, "e2e/table").stringTable;
    Expect(table && table->columnCount == 2 && !std::strcmp(table->values[1], "b"), "the table's values load");
    const PhysPreset *phys = Find(ASSET_TYPE_PHYSPRESET, "e2e/phys").physPreset;
    Expect(phys && phys->mass == 2.5f && phys->tempDefaultToCylinder && !std::strcmp(phys->sndAliasPrefix, "wood"),
           "the preset's fields load");
    const LocalizeEntry *text = Find(ASSET_TYPE_LOCALIZE_ENTRY, "E2E_HELLO").localize;
    Expect(text && !std::strcmp(text->value, "Hello"), "the localized string loads");
    const MapEnts *ents = Find(ASSET_TYPE_MAP_ENTS, "e2e/ents").mapEnts;
    Expect(ents && ents->numEntityChars == 3 && !std::strcmp(ents->entityString, "{}"), "the map entities load");
    const ComWorld *world = Find(ASSET_TYPE_COMWORLD, "e2e/comworld").comWorld;
    Expect(world && world->primaryLightCount == 1 && !std::strcmp(world->primaryLights[0].defName, "e2e/light0"),
           "the com world's light loads");
    const snd_alias_list_t *sound = Find(ASSET_TYPE_SOUND, "e2e/snd").sound;
    const snd_alias_t *alias = sound && sound->count == 1 ? sound->head : nullptr;
    Expect(alias && Is(alias->soundFile->u.loadSnd, ASSET_TYPE_LOADED_SOUND, "e2e/loaded")
               && Is(alias->volumeFalloffCurve, ASSET_TYPE_SOUND_CURVE, "e2e/curve"),
           "a sound reaches its loaded sound and curve");
    const GfxLightDef *light = Find(ASSET_TYPE_LIGHT_DEF, "e2e/lightdef").lightDef;
    Expect(light && Is(light->attenuation.image, ASSET_TYPE_IMAGE, "e2e/img") && light->lmapLookupStart == 7,
           "a light def's attenuation is the shared image");
    const Material *material = Find(ASSET_TYPE_MATERIAL, "e2e/material").material;
    Expect(material && Is(material->techniqueSet, ASSET_TYPE_TECHNIQUE_SET, "e2e/techset")
               && material->techniqueSet->remappedTechniqueSet == material->techniqueSet && material->textureCount == 2
               && Is(material->textureTable[0].u.image, ASSET_TYPE_IMAGE, "e2e/img")
               && Is(material->textureTable[1].u.image, ASSET_TYPE_IMAGE, "e2e/image2"),
           "a material reaches its technique set and both images");
    const Font_s *font = Find(ASSET_TYPE_FONT, "e2e/font").font;
    Expect(font && font->glyphCount == 96 && Is(font->material, ASSET_TYPE_MATERIAL, "e2e/material")
               && font->glowMaterial == font->material,
           "a font's materials are the shared material");
    const FxEffectDef *fx = Find(ASSET_TYPE_FX, "e2e/fx").fx;
    Expect(fx && fx->elemDefs && Is(fx->elemDefs[0].visuals.instance.material, ASSET_TYPE_MATERIAL, "e2e/material"),
           "an effect's sprite draws the shared material");
    const FxImpactTable *impacts = Find(ASSET_TYPE_IMPACT_FX, "e2e/impacts").impactFx;
    Expect(impacts && Is(impacts->table[0].nonflesh[0], ASSET_TYPE_FX, "e2e/fx") && !impacts->table[11].flesh[3],
           "an impact table names the shared effect");
    const menuDef_t *menu = Find(ASSET_TYPE_MENU, "e2e/menu").menu;
    Expect(menu && menu->itemCount == 1 && Is(menu->window.background, ASSET_TYPE_MATERIAL, "e2e/material")
               && menu->items[0]->parent == menu && Is(menu->items[0]->focusSound, ASSET_TYPE_SOUND, "e2e/snd"),
           "a menu reaches its material and sound, and its item its pooled menu");
    const MenuList *menus = Find(ASSET_TYPE_MENULIST, "e2e/menus").menuList;
    Expect(menus && menus->menuCount == 1 && Is(menus->menus[0], ASSET_TYPE_MENU, "e2e/menu"),
           "a menu list names the shared menu");
    const XModel *model = Find(ASSET_TYPE_XMODEL, "e2e/model").model;
    Expect(model && model->numBones == 2 && model->boneNames[1] == SL_FindString("bone_child")
               && Is(model->materialHandles[0], ASSET_TYPE_MATERIAL, "e2e/material"),
           "a model's bones name zone script strings, and its surface draws the shared material");
    const FxEffectDef *fxModel = Find(ASSET_TYPE_FX, "e2e/fxmodel").fx;
    Expect(fxModel && Is(fxModel->elemDefs[0].visuals.instance.model, ASSET_TYPE_XMODEL, "e2e/model"),
           "an effect's model element shows the shared model");
    const XAnimParts *anim = Find(ASSET_TYPE_XANIMPARTS, "e2e/anim").parts;
    Expect(anim && anim->boneCount[9] == 1 && anim->names[0] == SL_FindString("bone_root"),
           "an animation's bone names a zone script string");
    const WeaponDef *weapon = Find(ASSET_TYPE_WEAPON, "e2e/weapon").weapon;
    Expect(weapon && Is(weapon->gunXModel[0], ASSET_TYPE_XMODEL, "e2e/model")
               && Is(weapon->viewFlashEffect, ASSET_TYPE_FX, "e2e/fx")
               && Is(weapon->reticleCenter, ASSET_TYPE_MATERIAL, "e2e/material")
               && Is(weapon->pickupSound, ASSET_TYPE_SOUND, "e2e/snd"),
           "a weapon reaches its model, effect, material and sound");
    const clipMap_t *map = Find(ASSET_TYPE_CLIPMAP_PVS, "e2e/clipmap").clipMap;
    const DynEntityDef *defs = map && map->dynEntCount[0] == 2 ? map->dynEntDefList[0] : nullptr;
    Expect(defs && Is(map->staticModelList[0].xmodel, ASSET_TYPE_XMODEL, "e2e/model")
               && Is(defs[0].xModel, ASSET_TYPE_XMODEL, "e2e/model") && Is(defs[0].destroyFx, ASSET_TYPE_FX, "e2e/fx")
               && Is(defs[0].physPreset, ASSET_TYPE_PHYSPRESET, "e2e/phys") && defs[1].destroyPieces == defs[0].destroyPieces
               && Is(defs[0].destroyPieces->pieces[0].model, ASSET_TYPE_XMODEL, "e2e/model")
               && Is(map->mapEnts, ASSET_TYPE_MAP_ENTS, "me"),
           "a clip map's static model, dynamic entities and their shared pieces reach the shared assets");
    const GfxWorld *gfx = Find(ASSET_TYPE_GFXWORLD, "w").gfxWorld;
    Expect(gfx && Is(gfx->skyImage, ASSET_TYPE_IMAGE, "e2e/img")
               && Is(gfx->sunLight->def, ASSET_TYPE_LIGHT_DEF, "e2e/lightdef")
               && Is(gfx->materialMemory[0].material, ASSET_TYPE_MATERIAL, "e2e/material")
               && Is(gfx->dpvs.surfaces[0].material, ASSET_TYPE_MATERIAL, "e2e/material"),
           "a world reaches its sky image, sun light def and materials");
}
