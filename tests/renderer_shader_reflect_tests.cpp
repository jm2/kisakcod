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
        Put32(bytes, info + 0, static_cast<std::uint32_t>(nameOffset));
        Put16(bytes, info + 4, c.registerSet);
        Put16(bytes, info + 6, c.registerIndex);
        Put16(bytes, info + 8, c.registerCount);
        Put32(bytes, info + 12, static_cast<std::uint32_t>(type));
        Put16(bytes, type + 0, c.typeClass);
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

void TestRejectsMalformed()
{
    ShaderConstantTableView table{};
    const std::vector<std::uint8_t> good = BuildCtab(kConstants);
    auto rejects = [&](std::vector<std::uint32_t> program, const char *what) {
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
    std::vector<std::uint32_t> badOpcode = program;
    badOpcode.insert(badOpcode.end() - 1, 0x0100FFFFu);
    rejects(badOpcode, "opcode 0xFFFF that is not the end token");
    rejects(BuildProgram(0xFFFF0101u, good), "ps_1_1");
    rejects(BuildProgram(0x12340300u, good), "neither vertex nor pixel");
    Check(!R_ShaderFindConstantTable(nullptr, 4, &table), "null program");
    Check(!R_ShaderFindConstantTable(program.data(), static_cast<std::uint32_t>(program.size()), nullptr),
        "null out");
}
} // namespace

int main()
{
    TestParsesEveryConstant();
    TestFindsTableAfterAnInstruction();
    TestEmptyTable();
    TestRejectsMalformed();
    if (failures)
        return 1;
    std::puts("shader constant table reflection passed");
    return 0;
}
