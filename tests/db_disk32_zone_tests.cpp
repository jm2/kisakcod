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
#include <ui/ui_shared.h>

#include <cstring>
#include <iterator>

namespace
{
using namespace zone_test;

// Block-4 offsets the writers record for later tokens.
struct Offsets
{
    std::uint32_t rawFile, physPreset, curve, loaded, sound, image, techniqueSet, material, fx, menu;
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
    {ASSET_TYPE_LIGHT_DEF, kInline, "e2e/lightdef", WriteLightDef},
    {ASSET_TYPE_TECHNIQUE_SET, kShared, "e2e/techset", WriteTechniqueSet},
    {ASSET_TYPE_MATERIAL, kShared, "e2e/material", WriteMaterial},
    {ASSET_TYPE_FONT, kInline, "e2e/font", WriteFont},
    {ASSET_TYPE_FX, kShared, "e2e/fx", WriteFx},
    {ASSET_TYPE_IMPACT_FX, kInline, "e2e/impacts", WriteImpactFx},
    {ASSET_TYPE_MENU, kShared, "e2e/menu", WriteMenu},
    {ASSET_TYPE_MENULIST, kInline, "e2e/menus", WriteMenuList},
};
const char *const kScriptStrings[] = {"e2e_tag"};
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
}
