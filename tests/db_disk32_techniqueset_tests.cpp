// db_disk32_techniqueset_tests.cpp: the 64-bit TechniqueSet loader (NOW row 12)
// on hand-built disk32 zone images (disk32_fixture.hpp). Beyond the fixture's
// seams, only the asset pool (Load_MaterialTechniqueSetAsset) is replaced.
// Shader arguments are not converted yet, so every technique fails closed at its
// first pass's arguments, after its header, passes, declaration and shaders.

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

struct File : FileBuilder<File>
{
    // The 148-byte retail set: technique slot `at` holds `token`, every other
    // is null. The flag bytes and the remapped pointer hold junk.
    File &Set(std::uint32_t name, std::uint32_t format, std::uint32_t at = 34, std::uint32_t token = 0)
    {
        Word(name).Word(0x00CD0100u | format).Word(0xDEADBEEF);
        for (std::uint32_t slot = 0; slot < 34; ++slot)
            Word(slot == at ? token : 0);
        return *this;
    }
    File &Technique(std::uint32_t name, std::uint32_t flags, std::uint32_t passes)
    {
        return Word(name).Word(flags | passes << 16);
    }
    // A 20-byte pass with one stable argument.
    File &Pass(std::uint32_t decl, std::uint32_t shaders = kInline, std::uint32_t stable = 1, std::uint32_t args = kInline)
    {
        return Word(decl).Word(shaders).Word(shaders).Word(stable << 16 | 0x01000000u).Word(args);
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

using Zone = disk32_test::Zone<512>;

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
    Expect(!*std::max_element(std::begin(decl.routing.decl), std::end(decl.routing.decl)),
           "the declaration's runtime handles are null, never their disk bytes");
}

// A converted shader: its name, a null handle and a program in block 4.
template <typename Shader>
bool ShaderIs(const Zone &zone, const Shader *shader, const char *name, std::size_t program)
{
    return InArena(shader) && Is(zone, shader->name, name) && !shader->prog.loadDef.loadForRenderer
        && shader->prog.loadDef.program == zone.virt + program && shader->prog.loadDef.programSize == 2;
}

void TestPassUpToArguments()
{
    Zone zone;
    // Block 4: name (0..3), the technique (4..12) and its pass (12..32), the
    // declaration (32..132), the vertex shader (132..148), "vs" (148..151) and
    // its program (152..160), then the pixel shader (160..176), "ps" and its
    // program (180..188).
    File().Set(kInline, 2, 5, kInline).Text("ts").Technique(kInline, 0x8021, 1).Pass(kInline).Decl(3, 5);
    File().Shader(kInline, "vs", 0xFFFE0200).Shader(kInline, "ps", 0xFFFF0200);
    ExpectDrop("a pass with arguments", "arguments are not converted", [] { Load(kInline); });
    const auto *const technique = reinterpret_cast<const MaterialTechnique *>(g_arena);
    const MaterialPass &pass = technique->passArray[0];
    Expect(technique->flags == 0x21 && pass.stableArgCount == 1 && pass.customSamplerFlags == 1
               && InArena(pass.vertexDecl),
           "the technique and its pass convert, its flags losing bit 15");
    if (InArena(pass.vertexDecl))
        ExpectDecl(*pass.vertexDecl);
    Expect(ShaderIs(zone, pass.vertexShader, "vs", 152) && !pass.vertexShader->prog.vs,
           "the vertex shader converts, its program in block 4 and its handle null");
    Expect(ShaderIs(zone, pass.pixelShader, "ps", 180) && !pass.pixelShader->prog.ps,
           "the pixel shader converts, its program in block 4 and its handle null");
    Expect(g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 188,
           "every disk byte is consumed at its retail block-4 offset");
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

const Malformed kMalformed[] = {
    {"truncated set", [] { File().Word(kInline).Word(0); }, "ended unexpectedly"},
    {"null set name", [] { File().Set(0, 0); }, "technique set header"},
    {"vertex format 12", [] { File().Set(kInline, 12).Text("s"); }, "technique set header"},
    {"empty set name", [] { File().Set(kInline, 0).Text(""); }, "set has no name"},
    {"technique named by offset", [] { File().Set(kInline, 0, 3, VirtualOffset(8)).Text("s"); }, "by offset"},
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
    {"declaration named by offset", [] { Prefix().Pass(VirtualOffset(4)); }, "by offset"},
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
    {"pixel shader named by offset", [] { WithDecl(VirtualOffset(8)).Shader(kInline, "v", 0xFFFE0200); }, "by offset"},
    {"mixed renderer variants", [] { WithDecl().Shader(kInline, "v", 0xFFFE0200).Shader(kInline, "p", 0xFFFF0300); },
     "mixes renderer"},
    {"native storage exhausted", [] { WithDecl().Shader(kInline, "v", 0xFFFE0200); }, "exhausted",
     offsetof(MaterialTechnique, passArray) + sizeof(MaterialPass) + sizeof(MaterialVertexDeclaration)},
    {"unmapped set alias", [] {}, "alias offset"},
};

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

int main()
{
    return Run({TestEmptySets, TestPassUpToArguments, TestMalformedFailsClosed});
}
