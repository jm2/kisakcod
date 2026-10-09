// db_disk32_techniqueset_tests.cpp: the 64-bit TechniqueSet loader (NOW row 12)
// on hand-built disk32 zone images (disk32_fixture.hpp). Beyond the fixture's
// seams, only the asset pool (Load_MaterialTechniqueSetAsset) and the
// renderer's creation hooks (r_material.cpp) are replaced. It builds twice: as
// a client, which creates each declaration and shader once, and headless
// (KISAK_DEDI_HEADLESS), which never does.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>

#include <algorithm>
#include <cstring>
#include <iterator>

namespace
{
using namespace disk32_test;

MaterialTechniqueSet g_pool[4]; // what Load_MaterialTechniqueSetAsset published

#ifdef KISAK_DEDI_HEADLESS
constexpr bool kClient = false;
#else
constexpr bool kClient = true;
#endif

// The handles the creation hooks below hand out, and how often each ran.
const auto kDeclHandle = reinterpret_cast<IDirect3DVertexDeclaration9 *>(std::uintptr_t{0xDEC1000});
const auto kVertexHandle = reinterpret_cast<IDirect3DVertexShader9 *>(std::uintptr_t{0x5E7000});
const auto kPixelHandle = reinterpret_cast<IDirect3DPixelShader9 *>(std::uintptr_t{0x5E8000});
int g_declBuilds = 0;
int g_vertexCreates = 0;
int g_pixelCreates = 0;

bool Created(int decls, int vertex, int pixel)
{
    return kClient ? g_declBuilds == decls && g_vertexCreates == vertex && g_pixelCreates == pixel
                   : !g_declBuilds && !g_vertexCreates && !g_pixelCreates;
}

struct File : FileBuilder<File>
{
    // The 148-byte retail set: technique slot `at` holds `token`, every other
    // is null. The flag bytes and the remapped pointer hold junk.
    File &Set(std::uint32_t name, std::uint32_t format, std::uint32_t at = 34, std::uint32_t token = 0,
              std::uint32_t at2 = 34, std::uint32_t at3 = 34, std::uint32_t token3 = 0)
    {
        Word(name).Word(0x00CD0100u | format).Word(0xDEADBEEF);
        for (std::uint32_t slot = 0; slot < 34; ++slot)
            Word(slot == at || slot == at2 ? token : slot == at3 ? token3 : 0);
        return *this;
    }
    File &Technique(std::uint32_t name, std::uint32_t flags, std::uint32_t passes)
    {
        return Word(name).Word(flags | passes << 16);
    }
    // A 20-byte pass with `stable` stable arguments after `prim` per-primitive
    // ones; custom sampler 1 unless `prim`.
    File &Pass(std::uint32_t decl, std::uint32_t shaders = kInline, std::uint32_t stable = 1, std::uint32_t args = kInline,
               std::uint32_t prim = 0)
    {
        return Word(decl).Word(shaders).Word(shaders).Word(prim | stable << 16 | (prim ? 0 : 0x01000000u)).Word(args);
    }
    File &Argument(std::uint32_t type, std::uint32_t dest, std::uint32_t value)
    {
        return Word(type | dest << 16).Word(value);
    }
    // A pass's objects: a one-stream declaration, both shaders on `renderer`
    // and one code pixel constant.
    File &Objects(std::uint32_t renderer = 0)
    {
        Decl(1).Shader(kInline, "v", 0xFFFE0200 + (renderer << 8)).Shader(kInline, "p", 0xFFFF0200 + (renderer << 8));
        return Argument(5, 0, 1u << 24);
    }
    // A 100-byte declaration routing `count` streams (source n to dest n + 1),
    // its flags and handles junk.
    File &Decl(std::uint32_t count, std::uint32_t firstSource = 0)
    {
        Word(0x00FFFF00u | count);
        for (std::uint32_t stream = 0; stream < 16; stream += 2)
        {
            const auto pair = [&](std::uint32_t n) { return n < count ? (firstSource + n) | (n + 1) << 8 : 0u; };
            Word(pair(stream) | pair(stream + 1) << 16);
        }
        for (int handle = 0; handle < 16; ++handle)
            Word(0xBAD00000u + static_cast<std::uint32_t>(handle));
        return *this;
    }
    // A 16-byte shader record, its name, then a two-DWORD program whose
    // version token is `version` (vs_2_0 0xFFFE0200, ps_2_0 0xFFFF0200).
    File &Shader(std::uint32_t name, const char *text, std::uint32_t version, std::uint32_t size = 2,
                 std::uint32_t program = kInline)
    {
        Word(name).Word(0xBADC0DE5).Word(program).Word(size | (((version >> 8 & 0xFF) - 2) << 16));
        if (name == kInline)
            Text(text);
        Word(version);
        for (std::uint32_t dword = 1; dword < size; ++dword)
            Word(0x0000FFFF);
        return *this;
    }
};

using Zone = disk32_test::Zone<1024>;

MaterialTechniqueSet *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadMaterialTechniqueSetPtrDisk32, slotValue);
}

bool Is(const Zone &zone, const char *text, const char *expected)
{
    return zone.Holds(text) && !std::strcmp(text, expected);
}

void TestEmptySets()
{
    Zone zone;
    File().Set(kInline, 7).Text("ts_empty");     // after the alias slot: 4..13
    File().Set(VirtualOffset(4), 11);
    const MaterialTechniqueSet *const shared = Load(disk32::kSharedInline);
    const MaterialTechniqueSet *const named = Load(kInline);
    Expect(shared == &g_pool[0] && named == &g_pool[1], "sets without techniques publish");
    Expect(Is(zone, shared->name, "ts_empty") && shared->worldVertFormat == 7 && named->worldVertFormat == 11
               && !shared->hasBeenUploaded && !shared->remappedTechniqueSet && shared->unused[0] == 0xCD,
           "the scalars convert; the upload flag and the remapped set start clear");
    Expect(!*std::max_element(std::begin(shared->techniques), std::end(shared->techniques))
               && named->name == shared->name,
           "null technique tokens stay null, and a name offset resolves");
    Expect(reinterpret_cast<std::uintptr_t>(shared) > UINT32_MAX && Load(VirtualOffset(0)) == shared,
           "a set alias token resolves to the full native pointer");
    Expect(g_read == g_file.size() && !g_arenaUsed, "every disk byte is consumed and no native storage is used");
}

// The declaration TestPassUpToArguments builds: three routed streams.
void ExpectDecl(const MaterialVertexDeclaration &decl)
{
    Expect(decl.streamCount == 3 && decl.routing.data[2].source == 7 && decl.routing.data[2].dest == 3
               && decl.hasOptionalSource && decl.isLoaded,
           "the declaration's routing converts, and a source of 5 or more is optional");
    Expect(decl.routing.decl[0] == (kClient ? kDeclHandle : nullptr)
               && !*std::max_element(std::begin(decl.routing.decl) + 1, std::end(decl.routing.decl)),
           "a client builds the declaration's runtime handles, a server leaves them null; never their disk bytes");
}

// A converted shader: its name, a null handle and a program in block 4.
template <typename Shader>
bool ShaderIs(const Zone &zone, const Shader *shader, const char *name, std::size_t program)
{
    return InArena(shader) && Is(zone, shader->name, name) && !shader->prog.loadDef.loadForRenderer
        && shader->prog.loadDef.program == zone.virt + program && shader->prog.loadDef.programSize == 2;
}

void ExpectArguments(const Zone &zone, const MaterialShaderArgument *args);

// The pass TestFullTechnique builds: its declaration, shaders and arguments.
void ExpectPass(const Zone &zone, const MaterialPass &pass)
{
    Expect(pass.perPrimArgCount == 1 && pass.stableArgCount == 3 && InArena(pass.vertexDecl), "the pass converts");
    if (InArena(pass.vertexDecl))
        ExpectDecl(*pass.vertexDecl);
    Expect(ShaderIs(zone, pass.vertexShader, "vs", 152) && pass.vertexShader->prog.vs == (kClient ? kVertexHandle : nullptr),
           "the vertex shader converts, its program in block 4, and only a client creates its handle");
    Expect(ShaderIs(zone, pass.pixelShader, "ps", 180) && pass.pixelShader->prog.ps == (kClient ? kPixelHandle : nullptr),
           "the pixel shader converts, its program in block 4, and only a client creates its handle");
    if (InArena(pass.args))
        ExpectArguments(zone, pass.args);
}

// The four arguments TestFullTechnique builds.
void ExpectArguments(const Zone &zone, const MaterialShaderArgument *args)
{
    Expect(args[0].type == 3 && args[0].u.codeConst.index == 57 && args[0].u.codeConst.rowCount == 1
               && args[2].type == 5 && args[2].u.codeConst.rowCount == 1 && args[3].dest == 1,
           "code constants convert into the union's first bytes");
    Expect(args[1].u.literalConst == reinterpret_cast<const float *>(zone.virt + 220) && args[1].u.literalConst[2] == 3.f
               && args[3].u.literalConst == args[1].u.literalConst,
           "an inline literal points at its floats in block 4, and an offset literal at the same floats");
}

void TestFullTechnique()
{
    Zone zone;
    g_declBuilds = g_vertexCreates = g_pixelCreates = 0;
    // Block 4: name (0..3), the technique (4..12) and its pass (12..32), the
    // declaration (32..132), the vertex shader (132..148), "vs" and its program
    // (152..160), the pixel shader (160..176), "ps" and its program (180..188),
    // the four arguments (188..220), the literal floats (220..236), then the
    // technique's name (236..241).
    File().Set(kInline, 2, 5, kInline).Text("ts").Technique(kInline, 0x8021, 1).Pass(kInline, kInline, 3, kInline, 1);
    File().Decl(3, 5).Shader(kInline, "vs", 0xFFFE0200).Shader(kInline, "ps", 0xFFFF0200);
    File().Argument(3, 0, 57 | 1u << 24).Argument(1, 4, kInline).Argument(5, 0, 1u << 24).Argument(7, 1, VirtualOffset(220));
    File().Float(1.f).Float(2.f).Float(3.f).Float(4.f).Text("tech");
    const MaterialTechniqueSet *const set = Load(kInline);
    Expect(set == &g_pool[0] && InArena(set->techniques[5]), "a set with a technique publishes");
    if (set != &g_pool[0] || !InArena(set->techniques[5]))
        return;
    const MaterialTechnique &technique = *set->techniques[5];
    Expect(Is(zone, technique.name, "tech") && technique.flags == 0x21 && technique.passCount == 1,
           "the technique converts, its name after its passes and its flags losing bit 15");
    ExpectPass(zone, technique.passArray[0]);
    Expect(Created(1, 1, 1), "a client builds the declaration and creates both shaders once; a server none");
    Expect(g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 241,
           "every disk byte is consumed at its retail block-4 offset");
}

void TestTwoTechniques()
{
    Zone zone;
    File().Set(kInline, 0, 3, kInline, 30).Text("s").Technique(kInline, 0, 2).Pass(kInline).Pass(kInline);
    File().Objects(1).Objects(1).Text("a").Technique(kInline, 0, 1).Pass(kInline).Objects(1).Text("b");
    const MaterialTechniqueSet *const set = Load(kInline);
    Expect(set == &g_pool[0] && InArena(set->techniques[3]) && InArena(set->techniques[30]),
           "two techniques on one renderer variant publish");
    if (set == &g_pool[0] && InArena(set->techniques[3]))
        Expect(set->techniques[3]->passArray[1].pixelShader->prog.loadDef.loadForRenderer == 1
                   && set->techniques[3]->passArray[0].args != set->techniques[3]->passArray[1].args,
               "each pass of a technique converts its own objects");
}

struct Malformed
{
    const char *what;
    void (*build)();
    const char *error;
    std::size_t arena = kArenaBytes;
};

// A set whose technique 0 is inline: its header, then one pass.
File Prefix(std::uint32_t flags = 0, std::uint32_t passes = 1, std::uint32_t name = kInline)
{
    File().Set(kInline, 0, 0, kInline).Text("s").Technique(name, flags, passes);
    return File();
}

// Prefix, then one pass and its declaration: everything up to the shaders.
File WithDecl(std::uint32_t pixel = kInline)
{
    return Prefix().Word(kInline).Word(kInline).Word(pixel).Word(0x01010000u).Word(kInline).Decl(1);
}

// Prefix, then one pass with `prim` and `stable` arguments, its declaration
// and shaders: everything up to the arguments.
File WithShaders(std::uint32_t prim = 0, std::uint32_t stable = 1)
{
    return Prefix().Pass(kInline, kInline, stable, kInline, prim).Decl(1).Shader(kInline, "v", 0xFFFE0200)
        .Shader(kInline, "p", 0xFFFF0200);
}

const Malformed kMalformed[] = {
    {"truncated set", [] { File().Word(kInline).Word(0); }, "ended unexpectedly"},
    {"null set name", [] { File().Set(0, 0); }, "technique set header"},
    {"vertex format 12", [] { File().Set(kInline, 12).Text("s"); }, "technique set header"},
    {"empty set name", [] { File().Set(kInline, 0).Text(""); }, "set has no name"},
    {"unmapped technique offset", [] { File().Set(kInline, 0, 3, VirtualOffset(8)).Text("s"); }, "alias offset"},
    {"technique without a name", [] { Prefix(0, 1, 0).Pass(kInline); }, "technique header"},
    {"technique with no passes", [] { Prefix(0, 0); }, "technique header"},
    {"technique with five passes", [] { Prefix(0, 5); }, "technique header"},
    {"technique flag bit 6", [] { Prefix(0x40, 1).Pass(kInline); }, "technique header"},
    {"truncated passes", [] { Prefix(0, 2).Pass(kInline); }, "ended unexpectedly"},
    {"pass without a declaration", [] { Prefix().Pass(0); }, "pass header"},
    {"pass without shaders", [] { Prefix().Pass(kInline, 0); }, "pass header"},
    {"pass without arguments", [] { Prefix().Pass(kInline, kInline, 0, 0); }, "pass header"},
    {"pass with a count but no arguments", [] { Prefix().Pass(kInline, kInline, 1, 0); }, "pass header"},
    {"pass with 65 arguments", [] { Prefix().Pass(kInline, kInline, 65); }, "pass header"},
    {"declaration naming its pending technique", [] { Prefix().Pass(VirtualOffset(4)); }, "alias offset"},
    {"declaration with no streams", [] { Prefix().Pass(kInline).Decl(0); }, "declaration count"},
    {"declaration with 13 streams", [] { Prefix().Pass(kInline).Decl(13); }, "declaration count"},
    {"declaration source 9", [] { Prefix().Pass(kInline).Decl(1, 9); }, "declaration routing"},
    {"truncated declaration", [] { Prefix().Pass(kInline).Word(1); }, "ended unexpectedly"},
    {"vertex shader without a name", [] { WithDecl().Shader(0, "", 0xFFFE0200); }, "vertex shader has no name"},
    {"shader with an empty name", [] { WithDecl().Shader(kInline, "", 0xFFFE0200); }, "no completed name"},
    {"one-DWORD program", [] { WithDecl().Shader(kInline, "v", 0xFFFE0200, 1); }, "load definition"},
    {"renderer 2", [] { WithDecl().Shader(kInline, "v", 0xFFFE0400); }, "load definition"},
    {"shader without a program", [] { WithDecl().Shader(kInline, "v", 0xFFFE0200, 2, 0); }, "load definition"},
    {"pixel bytecode in a vertex shader", [] { WithDecl().Shader(kInline, "v", 0xFFFF0200); }, "bytecode"},
    {"truncated program", [] { WithDecl().Shader(kInline, "v", 0xFFFE0200, 3); g_file.resize(g_file.size() - 4); },
     "ended unexpectedly"},
    {"pixel shader naming a declaration", [] { WithDecl(VirtualOffset(32)).Shader(kInline, "v", 0xFFFE0200); },
     "alias offset"},
    {"vertex shader naming a pixel shader", [] { File().Set(kInline, 0, 3, kInline, 10).Text("s").Technique(kInline, 0, 1)
                                                     .Pass(kInline).Objects().Text("a").Technique(kInline, 0, 1)
                                                     .Pass(VirtualOffset(32), VirtualOffset(160)); }, "alias offset"},
    {"technique offset naming a declaration", [] { File().Set(kInline, 0, 3, kInline, 34, 30, VirtualOffset(32))
                                                       .Text("s").Technique(kInline, 0, 1).Pass(kInline).Objects()
                                                       .Text("a"); }, "alias offset"},
    {"mixed renderer variants", [] { WithDecl().Shader(kInline, "v", 0xFFFE0200).Shader(kInline, "p", 0xFFFF0300); },
     "mixes renderer"},
    {"literal without a value", [] { WithShaders().Argument(1, 0, 0); }, "has no value"},
    {"unmapped literal offset", [] { WithShaders().Argument(1, 0, VirtualOffset(900)); }, "pointer offset"},
    {"literal offset short of four floats", [] { WithShaders().Argument(1, 0, VirtualOffset(184)); }, "pointer offset"},
    {"literal past the stream", [] { WithShaders().Argument(1, 0, kInline).Float(1); }, "ended unexpectedly"},
    {"non-finite literal", [] { WithShaders().Argument(7, 0, kInline).Float(1).Float(1).Float(1).Word(0x7F800000); },
     "shader argument"},
    {"argument in the wrong segment", [] { WithShaders(1, 0).Argument(1, 0, kInline).Float(0).Float(0).Float(0).Float(0); },
     "shader argument"},
    {"unordered arguments", [] { WithShaders(0, 2).Argument(7, 1, kInline).Float(0).Float(0).Float(0).Float(0)
                                     .Argument(5, 0, 1u << 24); }, "Unordered"},
    {"overlapping pixel constants", [] { WithShaders(0, 2).Argument(5, 3, 1u << 24).Argument(5, 3, 1u << 24); },
     "Overlapping"},
    {"truncated arguments", [] { WithShaders(0, 2).Word(5); }, "ended unexpectedly"},
    {"technique with an empty name", [] { Prefix().Pass(kInline).Objects().Text(""); }, "technique has no name"},
    {"technique mixing renderers", [] { Prefix(0, 2).Pass(kInline).Pass(kInline).Objects(0).Objects(1).Text("t"); },
     "technique mixes renderer"},
    {"set mixing renderers", [] { File().Set(kInline, 0, 0, kInline, 1).Text("s").Technique(kInline, 0, 1)
                                      .Pass(kInline).Objects(0).Text("a").Technique(kInline, 0, 1).Pass(kInline)
                                      .Objects(1).Text("b"); }, "set mixes renderer"},
    {"native storage exhausted", [] { WithDecl().Shader(kInline, "v", 0xFFFE0200); }, "exhausted",
     offsetof(MaterialTechnique, passArray) + sizeof(MaterialPass) + sizeof(MaterialVertexDeclaration)},
    {"unmapped set alias", [] {}, "alias offset"},
};

void TestSharedObjects()
{
    Zone zone;
    g_declBuilds = g_vertexCreates = g_pixelCreates = 0;
    // Block 4: "s" (0..2), technique A (4..12), its pass (12..32), declaration
    // (32..132), vertex shader (132..148), pixel shader (160..176), argument
    // (188..196) and "a"; then technique B (200..208), whose pass names A's
    // declaration and shaders by offset, its argument and "b". Slot 30 names A.
    File().Set(kInline, 0, 3, kInline, 10, 30, VirtualOffset(4)).Text("s").Technique(kInline, 0, 1).Pass(kInline);
    File().Objects().Text("a").Technique(kInline, 0, 1).Word(VirtualOffset(32)).Word(VirtualOffset(132));
    File().Word(VirtualOffset(160)).Word(0x01010000u).Word(kInline).Argument(5, 0, 1u << 24).Text("b");
    const MaterialTechniqueSet *const set = Load(kInline);
    Expect(set == &g_pool[0] && g_read == g_file.size(), "a set sharing objects by offset publishes");
    if (set != &g_pool[0] || !InArena(set->techniques[3]) || !InArena(set->techniques[10]))
        return;
    const MaterialPass &a = set->techniques[3]->passArray[0];
    const MaterialPass &b = set->techniques[10]->passArray[0];
    Expect(set->techniques[30] == set->techniques[3], "a technique offset resolves to the native technique");
    Expect(b.vertexDecl == a.vertexDecl && b.vertexShader == a.vertexShader && b.pixelShader == a.pixelShader
               && InArena(b.vertexDecl) && b.args != a.args,
           "declaration and shader offsets resolve to the earlier native objects");
    Expect(Created(1, 1, 1), "objects shared by offset are created once");
}

void TestMalformedFailsClosed()
{
    for (const Malformed &test : kMalformed)
    {
        Zone zone;
        g_arenaCapacity = test.arena;
        test.build();
        const std::uintptr_t slot = g_file.empty() ? VirtualOffset(16) : kInline;
        ExpectDrop(test.what, test.error, [&] { Load(slot); });
    }
    Zone zone;
    ExpectDrop("slot wider than a token", "no disk32 token", [] { Load(std::uintptr_t{1} << 32); });
}
} // namespace

void __cdecl Load_MaterialTechniqueSetAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    MaterialTechniqueSet &entry = g_pool[g_published++];
    entry = *header->techniqueSet;
    Expect(entry.name && entry.name[0] != '\0', "a published set has a name");
    header->techniqueSet = &entry;
}

// The renderer's creation hooks (r_material.cpp): each records the call and
// hands out a marker handle.
void __cdecl Load_BuildVertexDecl(MaterialVertexDeclaration **mtlVertDecl)
{
    ++g_declBuilds;
    (*mtlVertDecl)->routing.decl[0] = kDeclHandle;
}

bool __cdecl Load_CreateMaterialVertexShader(GfxVertexShaderLoadDef *loadDef, MaterialVertexShader *mtlShader)
{
    ++g_vertexCreates;
    Expect(loadDef == &mtlShader->prog.loadDef && !mtlShader->prog.vs, "a vertex shader is created from its own program once");
    mtlShader->prog.vs = kVertexHandle;
    return true;
}

bool __cdecl Load_CreateMaterialPixelShader(GfxPixelShaderLoadDef *loadDef, MaterialPixelShader *mtlShader)
{
    ++g_pixelCreates;
    Expect(loadDef == &mtlShader->prog.loadDef && !mtlShader->prog.ps, "a pixel shader is created from its own program once");
    mtlShader->prog.ps = kPixelHandle;
    return true;
}

int main()
{
    return Run({TestEmptySets, TestFullTechnique, TestTwoTechniques, TestSharedObjects, TestMalformedFailsClosed});
}
