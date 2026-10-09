// SM2/SM3 constant-table reflection (gfx_d3d/r_shader_reflect.cpp), the
// D3DXGetShaderConstantTable replacement for targets without D3DX (G5).
// Every program here is hand-built: a CTAB payload laid out as
// D3DXSHADER_CONSTANTTABLE / _CONSTANTINFO / _TYPEINFO, wrapped in a comment
// token inside a vs/ps token stream. No retail or compiler output is used.

#include <gfx_d3d/r_shader_reflect.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void Check(bool ok, const char *what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

struct Constant
{
    std::string name;
    std::uint16_t registerSet;
    std::uint16_t registerIndex;
    std::uint16_t registerCount;
    std::uint16_t typeClass;
    std::uint16_t typeType;
};

// Writes a fixture field; a field that would not fit fails the test instead.
template <typename T>
void Put(std::vector<std::uint8_t> &bytes, std::size_t at, T value)
{
    if (at > bytes.size() || sizeof(value) > bytes.size() - at)
    {
        Check(false, "fixture field inside its buffer");
        return;
    }
    const auto *source = reinterpret_cast<const std::uint8_t *>(&value);
    std::copy_n(source, sizeof(value), bytes.begin() + static_cast<std::ptrdiff_t>(at));
}

// Reads a field of a found table in place; one past its end fails the test.
template <typename T>
T Get(const ShaderConstantTableView &table, std::size_t at)
{
    T value{};
    if (at > table.size || sizeof(value) > table.size - at)
    {
        Check(false, "in-place read inside the payload");
        return value;
    }
    std::copy_n(table.data + at, sizeof(value), reinterpret_cast<std::uint8_t *>(&value));
    return value;
}

void Put32(std::vector<std::uint8_t> &bytes, std::size_t at, std::uint32_t value)
{
    Put(bytes, at, value);
}

void Put16(std::vector<std::uint8_t> &bytes, std::size_t at, std::uint16_t value)
{
    Put(bytes, at, value);
}

// header (28) | constant infos (20 each) | type infos (16 each) | names, padded to 4.
std::vector<std::uint8_t> BuildCtab(const std::vector<Constant> &constants)
{
    const std::size_t infoOffset = 28;
    const std::size_t typeOffset = infoOffset + 20 * constants.size();
    std::size_t nameOffset = typeOffset + 16 * constants.size();
    std::vector<std::uint8_t> bytes(nameOffset);
    Put32(bytes, 0, 28);                                     // Size
    Put32(bytes, 12, static_cast<std::uint32_t>(constants.size())); // Constants
    Put32(bytes, 16, static_cast<std::uint32_t>(infoOffset));       // ConstantInfo
    for (std::size_t i = 0; i < constants.size(); ++i)
    {
        const Constant &c = constants[i];
        const std::size_t info = infoOffset + 20 * i;
        const std::size_t type = typeOffset + 16 * i;
        Put32(bytes, info, static_cast<std::uint32_t>(nameOffset));
        Put16(bytes, info + 4, c.registerSet);
        Put16(bytes, info + 6, c.registerIndex);
        Put16(bytes, info + 8, c.registerCount);
        Put32(bytes, info + 12, static_cast<std::uint32_t>(type));
        Put16(bytes, type, c.typeClass);
        Put16(bytes, type + 2, c.typeType);
        bytes.insert(bytes.end(), c.name.begin(), c.name.end());
        bytes.push_back('\0');
        nameOffset = bytes.size();
    }
    bytes.resize((bytes.size() + 3) & ~std::size_t{ 3 });
    return bytes;
}

constexpr std::uint32_t kVs30 = 0xFFFE0300u;
constexpr std::uint32_t kPs20 = 0xFFFF0200u;
constexpr std::uint32_t kDcl = 0x0200001Fu; // dcl, two parameter tokens
constexpr std::uint32_t kMov = 0x02000001u; // mov, two parameter tokens

// version | [an instruction] | comment('CTAB' + payload) | mov | end
std::vector<std::uint32_t> BuildProgram(std::uint32_t version, const std::vector<std::uint8_t> &ctab,
    bool instructionFirst = false)
{
    std::vector<std::uint32_t> program{ version };
    if (instructionFirst)
        program.insert(program.end(), { kDcl, 0x80000000u, 0x900F0000u });
    // The payload is the CTAB bytes zero-padded up to whole dwords.
    const std::size_t ctabDwords = (ctab.size() + 3) / 4;
    const auto payloadDwords = static_cast<std::uint32_t>(1 + ctabDwords);
    program.push_back((payloadDwords << 16) | 0xFFFEu);
    program.push_back(0x42415443u);
    const std::size_t at = program.size();
    program.resize(at + ctabDwords, 0);
    std::copy_n(ctab.begin(), ctab.size(), reinterpret_cast<std::uint8_t *>(program.data() + at));
    program.insert(program.end(), { kMov, 0xC00F0000u, 0x90E40000u, 0x0000FFFFu });
    return program;
}

bool Find(const std::vector<std::uint32_t> &program, ShaderConstantTableView *table)
{
    return R_ShaderFindConstantTable(program.data(), static_cast<std::uint32_t>(program.size()), table);
}

const std::vector<Constant> kConstants{
    { "worldViewProjectionMatrix", 2, 0, 4, 3, 3 }, // float4x4, column-major (class 3: transposed)
    { "colorTint", 2, 4, 1, 1, 3 },                  // float4 vector
    { "colorMapSampler", 3, 0, 1, 4, 12 },           // sampler2D
    { "lightSpotCube", 3, 1, 1, 4, 14 },             // samplerCUBE
};

void TestParsesEveryConstant()
{
    const std::vector<std::uint8_t> ctab = BuildCtab(kConstants);
    const std::vector<std::uint32_t> program = BuildProgram(kVs30, ctab);
    ShaderConstantTableView table{};
    Check(Find(program, &table), "vs_3_0 table found");
    Check(table.data == reinterpret_cast<const std::uint8_t *>(&program[3]), "payload follows the FourCC");
    Check(table.size == ctab.size(), "payload size");
    Check(table.constantCount == kConstants.size(), "constant count");
    for (std::uint32_t i = 0; i < kConstants.size(); ++i)
    {
        ShaderConstantDesc desc{};
        Check(R_ShaderGetConstantDesc(table, i, &desc), "desc read");
        const Constant &c = kConstants[i];
        Check(desc.name && c.name == desc.name, "name");
        Check(desc.registerSet == c.registerSet && desc.registerIndex == c.registerIndex
                && desc.registerCount == c.registerCount, "registers");
        Check(desc.typeClass == c.typeClass && desc.typeType == c.typeType, "type class and type");

        // R_SetParameterDefArray reads the payload in place: ConstantInfo
        // entries 20 bytes apart, then the WORDs at the TypeInfo offset.
        const auto infoOffset = Get<std::uint32_t>(table, 16);
        const auto typeOffset = Get<std::uint32_t>(table, infoOffset + 20 * i + 12);
        Check(Get<std::uint16_t>(table, typeOffset) == desc.typeClass
                && Get<std::uint16_t>(table, typeOffset + 2) == desc.typeType, "in-place layout read");
    }
    ShaderConstantDesc desc{};
    Check(!R_ShaderGetConstantDesc(table, table.constantCount, &desc), "index past the end");
}

void TestFindsTableAfterAnInstruction()
{
    const std::vector<std::uint32_t> program = BuildProgram(kPs20, BuildCtab(kConstants), true);
    ShaderConstantTableView table{};
    Check(Find(program, &table) && table.constantCount == kConstants.size(), "ps_2_0 table after dcl");
}

void TestEmptyTable()
{
    ShaderConstantTableView table{};
    Check(Find(BuildProgram(kVs30, BuildCtab({})), &table) && table.constantCount == 0, "empty table");
}

// Shader model 1: instruction tokens carry no length, so only the comments
// right after the version token are searched, as the compiler emits them.
std::vector<std::uint32_t> BuildSm1Program(std::uint32_t version, const std::vector<std::uint8_t> &ctab,
    bool instructionFirst)
{
    const std::vector<std::uint32_t> withTable = BuildProgram(version, ctab);
    // BuildProgram's version, comment and table, minus its SM2 mov and end token.
    std::vector<std::uint32_t> program(withTable.begin(), withTable.end() - 4);
    const std::vector<std::uint32_t> mov{ 0x00000001u, 0x800F0000u, 0x90E40000u }; // mov r0, v0
    program.insert(instructionFirst ? program.begin() + 1 : program.end(), mov.begin(), mov.end());
    program.push_back(0x0000FFFFu);
    return program;
}

void TestShaderModel1()
{
    const std::vector<std::uint8_t> ctab = BuildCtab(kConstants);
    ShaderConstantTableView table{};
    for (const std::uint32_t version : { 0xFFFE0101u, 0xFFFF0101u, 0xFFFF0104u })
    {
        const std::vector<std::uint32_t> program = BuildSm1Program(version, ctab, false);
        Check(Find(program, &table) && table.constantCount == kConstants.size()
                && table.data == reinterpret_cast<const std::uint8_t *>(&program[3]), "1.x leading table");
        ShaderConstantDesc desc{};
        Check(R_ShaderGetConstantDesc(table, 2, &desc) && desc.registerSet == 3, "1.x constant");
    }
    Check(!Find(BuildSm1Program(0xFFFF0104u, ctab, true), &table), "1.x table after an instruction is not searched");
    std::vector<std::uint32_t> program = BuildSm1Program(0xFFFE0101u, ctab, false);
    program.back() = 0;
    Check(!Find(program, &table), "1.x without its end token");
    program = BuildSm1Program(0xFFFE0101u, ctab, false);
    program.erase(program.end() - 4, program.end() - 1);
    Check(!Find(program, &table), "1.x with no instruction");
}

void TestRejectsMalformed()
{
    ShaderConstantTableView table{};
    const std::vector<std::uint8_t> good = BuildCtab(kConstants);
    auto rejects = [&](const std::vector<std::uint32_t> &program, const char *what) {
        Check(!Find(program, &table), what);
    };
    auto withCtab = [&](auto edit) {
        std::vector<std::uint8_t> ctab = good;
        edit(ctab);
        return BuildProgram(kVs30, ctab);
    };

    rejects(withCtab([](auto &b) { Put32(b, 0, 32); }), "header Size other than 28");
    rejects(withCtab([](auto &b) { Put32(b, 12, 0x10000000u); }), "constant count overflows the payload");
    rejects(withCtab([&](auto &b) { Put32(b, 16, static_cast<std::uint32_t>(b.size()) - 8); }),
        "constant array past the payload");
    rejects(withCtab([&](auto &b) { Put32(b, 28, static_cast<std::uint32_t>(b.size())); }), "name past the payload");
    rejects(withCtab([&](auto &b) { Put32(b, 28 + 12, static_cast<std::uint32_t>(b.size()) - 8); }),
        "type info past the payload");
    // The last name's NUL and up to three padding bytes are the payload's tail.
    rejects(withCtab([](auto &b) {
        for (std::size_t at = b.size() - 5; at < b.size(); ++at)
            b[at] = 'x';
    }), "last name not NUL-terminated");

    std::vector<std::uint32_t> program = BuildProgram(kVs30, good);
    std::vector<std::uint32_t> noEnd(program.begin(), program.end() - 1);
    rejects(noEnd, "no end token");
    std::vector<std::uint32_t> trailing = program;
    trailing.push_back(0);
    rejects(trailing, "tokens after the end token");
    std::vector<std::uint32_t> overrun = program;
    overrun[1] = 0x7FFF0000u | 0xFFFEu;
    rejects(overrun, "comment longer than the program");
    std::vector<std::uint32_t> shortComment = { kVs30, (1u << 16) | 0xFFFEu, 0x42415443u, 0x0000FFFFu };
    rejects(shortComment, "CTAB comment shorter than its header");
    std::vector<std::uint32_t> noCtab = { kVs30, kMov, 0xC00F0000u, 0x90E40000u, 0x0000FFFFu };
    rejects(noCtab, "no CTAB comment");
    std::vector<std::uint32_t> badOpcode(program.begin(), program.end() - 1);
    badOpcode.push_back(0x0100FFFFu);
    badOpcode.push_back(program.back());
    rejects(badOpcode, "opcode 0xFFFF that is not the end token");
    rejects(BuildProgram(0x12340300u, good), "neither vertex nor pixel");
    Check(!R_ShaderFindConstantTable(nullptr, 4, &table), "null program");
    Check(!R_ShaderFindConstantTable(program.data(), static_cast<std::uint32_t>(program.size()), nullptr),
        "null out");
}
// Semantics. The expected lists are what native D3DX9_43 returns for
// compiler output of the same shape (checked once by hand; not in CI).
constexpr std::uint32_t kVs20 = 0xFFFE0200u;
constexpr std::uint32_t kPs30 = 0xFFFF0300u;
constexpr std::uint32_t kDef = 0x05000051u; // def, a register and four floats

// D3DSPR_* parameter tokens: the type's low bits at 28, its high bits at 11.
constexpr std::uint32_t Reg(std::uint32_t type, std::uint32_t number)
{
    return 0x800F0000u | ((type & 7u) << 28) | ((type & 0x18u) << 8) | number;
}
constexpr std::uint32_t Usage(std::uint32_t usage, std::uint32_t index)
{
    return 0x80000000u | (index << 16) | usage;
}
enum : std::uint32_t { kTemp = 0, kInput = 1, kConst = 2, kTexture = 3, kRastOut = 4, kAttrOut = 5, kOutput = 6,
    kColorOut = 8, kDepthOut = 9, kSampler = 10 };
enum : std::uint32_t { kPosition = 0, kNormal = 3, kPSize = 4, kTexcoord = 5, kColor = 10, kFog = 11, kDepth = 12 };

std::vector<std::uint32_t> Program(std::uint32_t version, std::vector<std::uint32_t> body)
{
    body.insert(body.begin(), version);
    body.push_back(0x0000FFFFu);
    return body;
}

std::string Semantics(const std::vector<std::uint32_t> &program, bool output, std::uint32_t capacity = 16)
{
    ShaderSemantic semantics[16]{};
    std::uint32_t count = 99;
    const auto dwords = static_cast<std::uint32_t>(program.size());
    const bool ok = output ? R_ShaderGetOutputSemantics(program.data(), dwords, semantics, capacity, &count)
                           : R_ShaderGetInputSemantics(program.data(), dwords, semantics, capacity, &count);
    if (!ok)
        return count == 0 ? "fail" : "fail with a count";
    std::string text;
    for (std::uint32_t i = 0; i < count; ++i)
        text += (i ? " " : "") + std::to_string(semantics[i].usage) + ":" + std::to_string(semantics[i].usageIndex);
    return text;
}

void CheckSemantics(const std::string &got, const char *expected, const char *what)
{
    if (got != expected)
        std::fprintf(stderr, "  %s: got '%s', expected '%s'\n", what, got.c_str(), expected);
    Check(got == expected, what);
}

void TestDeclaredSemantics()
{
    // vs_3_0 declares both sides; dcl usage tokens carry usage and index.
    const auto vs = Program(kVs30, {
        kDcl, Usage(kPosition, 0), Reg(kInput, 0),
        kDcl, Usage(kNormal, 0), Reg(kInput, 1),
        kDcl, Usage(kTexcoord, 1), Reg(kInput, 2),
        kDcl, Usage(kPosition, 0), Reg(kOutput, 0),
        kDcl, Usage(kColor, 1), Reg(kOutput, 1),
        kDcl, Usage(kTexcoord, 3), Reg(kOutput, 2),
        kMov, Reg(kOutput, 0), Reg(kInput, 0) | 0x00E40000u });
    CheckSemantics(Semantics(vs, false), "0:0 3:0 5:1", "vs_3_0 inputs from dcl");
    CheckSemantics(Semantics(vs, true), "0:0 10:1 5:3", "vs_3_0 outputs from dcl");

    // ps_3_0 inputs are declared with usages; samplers name no semantic.
    const auto ps = Program(kPs30, {
        kDcl, Usage(kColor, 1), Reg(kInput, 0),
        kDcl, Usage(kTexcoord, 5), Reg(kInput, 2),
        kDcl, 0x90000000u, Reg(kSampler, 0) });
    CheckSemantics(Semantics(ps, false), "10:1 5:5", "ps_3_0 inputs from dcl");
}

void TestPs20Inputs()
{
    // ps_2_0 dcl has no usage: v1 is COLOR with the dcl's zero index (native
    // D3DX reports COLOR0, not COLOR1), t2 is TEXCOORD2, s0 is skipped.
    const auto ps = Program(kPs20, {
        kDcl, 0x80000000u, Reg(kInput, 1),
        kDcl, 0x80000000u, Reg(kTexture, 2),
        kDcl, 0x90000000u, Reg(kSampler, 0) });
    CheckSemantics(Semantics(ps, false), "10:0 5:2", "ps_2_0 inputs from register type");
}

void TestUndeclaredOutputs()
{
    // vs_2_0 outputs come from the registers written, texcoords first, then
    // colors, then oPos/oFog/oPts. A def's floats are skipped even when one
    // (-2.0f, 0xC0000000) looks like an oPos parameter token.
    const auto vs = Program(kVs20, {
        kDcl, Usage(kPosition, 0), Reg(kInput, 0),
        kDef, Reg(kConst, 0), 0xC0000000u, 0, 0, 0,
        kMov, Reg(kRastOut, 1), Reg(kInput, 0),
        kMov, Reg(kOutput, 3), Reg(kInput, 0),
        kMov, Reg(kAttrOut, 1), Reg(kInput, 0),
        kMov, Reg(kRastOut, 2), Reg(kInput, 0),
        kMov, Reg(kOutput, 0), Reg(kInput, 0) });
    CheckSemantics(Semantics(vs, false), "0:0", "vs_2_0 inputs from dcl");
    CheckSemantics(Semantics(vs, true), "5:0 5:3 10:1 11:0 4:0", "vs_2_0 outputs from written registers");

    // Pixel outputs are the oC# and oDepth writes; reading v# is not an output.
    const auto ps = Program(kPs30, {
        kMov, Reg(kColorOut, 1), Reg(kInput, 0),
        kMov, Reg(kDepthOut, 0), Reg(kInput, 2),
        kMov, Reg(kColorOut, 0), Reg(kInput, 1) });
    CheckSemantics(Semantics(ps, true), "10:0 10:1 12:0", "ps outputs from oC# and oDepth");
}

void TestShaderModel1Semantics()
{
    // ps_1_x has no dcl: t# reads are texture coordinates and v# reads colors,
    // by register number, texture coordinates first; temporaries are not
    // inputs. Its output is r0 alone: writing r1 adds nothing.
    const auto ps = Program(0xFFFF0101u, {
        0x00000042u, Reg(kTexture, 3),                                    // tex t3
        0x00000042u, Reg(kTexture, 0),                                    // tex t0
        0x00000005u, Reg(kTemp, 1), Reg(kInput, 1), Reg(kTexture, 3),    // mul r1, v1, t3
        0x00000005u, Reg(kTemp, 0), Reg(kInput, 0), Reg(kTexture, 0),    // mul r0, v0, t0
        0x00000002u, Reg(kTemp, 0), Reg(kTemp, 0), Reg(kTemp, 1) });     // add r0, r0, r1
    CheckSemantics(Semantics(ps, false), "5:0 5:3 10:0 10:1", "ps_1_1 inputs from t# and v#");
    CheckSemantics(Semantics(ps, true), "10:0", "ps_1_1 output is r0");

    // ps_1_4: phase has no parameters; texcrd and texld name t# as sources.
    const auto ps14 = Program(0xFFFF0104u, {
        0x00000040u, Reg(kTemp, 1), Reg(kTexture, 2),                     // texcrd r1, t2
        0x00000042u, Reg(kTemp, 0), Reg(kTexture, 0),                     // texld r0, t0
        0x0000FFFDu,                                                      // phase
        0x00000005u, Reg(kTemp, 0), Reg(kTemp, 0), Reg(kInput, 1) });    // mul r0, r0, v1
    CheckSemantics(Semantics(ps14, false), "5:0 5:2 10:1", "ps_1_4 inputs across a phase");

    // vs_1_1: inputs are declared; outputs come from the registers written,
    // and the instruction lengths are read from the parameter tokens (a def's
    // -2.0f value looks like an oPos token but is skipped).
    const auto vs = Program(0xFFFE0101u, {
        kDcl & 0xFFFFu, Usage(kPosition, 0), Reg(kInput, 0),
        kDcl & 0xFFFFu, Usage(kTexcoord, 1), Reg(kInput, 2),
        kDef & 0xFFFFu, Reg(kConst, 4), 0x3F800000u, 0xC0000000u, 0, 0,
        0x00000001u, Reg(kRastOut, 0), Reg(kInput, 0),
        0x00000001u, Reg(kAttrOut, 1), Reg(kInput, 0),
        0x00000001u, Reg(kOutput, 2), Reg(kInput, 2) });
    CheckSemantics(Semantics(vs, false), "0:0 5:1", "vs_1_1 inputs from dcl");
    CheckSemantics(Semantics(vs, true), "5:2 10:1 0:0", "vs_1_1 outputs from written registers");

    // A parameter run past the end of the program is malformed.
    CheckSemantics(Semantics({ 0xFFFF0101u, 0x00000001u, Reg(kTemp, 0) }, true), "fail", "ps_1_1 without its end token");

    // Native D3DX reads lengths without checking operand counts: a mov with no
    // parameter tokens is accepted (structure is D3D9ShaderBytecodeValid's job),
    // and a ps_1_x program reports its r0 COLOR0 output whether or not it
    // names r0. A vs_1_1 program reports only what it writes.
    const std::vector<std::uint32_t> bareMov{ 0xFFFF0101u, 0x00000001u, 0x0000FFFFu };
    CheckSemantics(Semantics(bareMov, false), "", "ps_1_1 bare mov: no inputs");
    CheckSemantics(Semantics(bareMov, true), "10:0", "ps_1_1 output is COLOR0 without naming r0");
    const std::vector<std::uint32_t> bareVsMov{ 0xFFFE0101u, 0x00000001u, 0x0000FFFFu };
    CheckSemantics(Semantics(bareVsMov, true), "", "vs_1_1 bare mov: no outputs");
}

void TestSemanticsFailClosed()
{
    const auto vs = Program(kVs30, { kDcl, Usage(kPosition, 0), Reg(kInput, 0),
        kDcl, Usage(kNormal, 0), Reg(kInput, 1) });
    CheckSemantics(Semantics(vs, false, 1), "fail", "more semantics than capacity");
    CheckSemantics(Semantics(std::vector<std::uint32_t>(vs.begin(), vs.end() - 1), false), "fail",
        "no end token");
    CheckSemantics(Semantics(Program(0x12340300u, {}), true), "fail", "neither vertex nor pixel");
    std::uint32_t count = 7;
    Check(!R_ShaderGetInputSemantics(vs.data(), static_cast<std::uint32_t>(vs.size()), nullptr, 4, &count)
            && count == 0, "null semantics with a capacity");
}
} // namespace

int main()
{
    TestParsesEveryConstant();
    TestFindsTableAfterAnInstruction();
    TestEmptyTable();
    TestShaderModel1();
    TestRejectsMalformed();
    TestDeclaredSemantics();
    TestPs20Inputs();
    TestUndeclaredOutputs();
    TestShaderModel1Semantics();
    TestSemanticsFailClosed();
    if (failures)
        return 1;
    std::puts("shader reflection passed");
    return 0;
}
