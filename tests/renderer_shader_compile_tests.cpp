// The D3DX buffer and compiler replacement (gfx_d3d/r_shader_compile.cpp, G5).
// The buffer behaves as D3DXCreateBuffer's does everywhere. On Windows the
// shaders below compile through D3DCompile into SM2/SM3 bytecode that carries
// its constant table; elsewhere the compiler reports E_NOTIMPL.

#include <gfx_d3d/r_shader_compile.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
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

constexpr std::int32_t kInvalidArgument = static_cast<std::int32_t>(0x80070057u);

void TestBuffers()
{
    ShaderBuffer *buffer = nullptr;
    Check(R_CreateShaderBuffer(16, &buffer) == 0 && buffer, "16-byte buffer");
    if (buffer)
    {
        Check(buffer->GetBufferSize() == 16, "buffer size");
        auto *bytes = static_cast<std::uint8_t *>(buffer->GetBufferPointer());
        Check(std::all_of(bytes, bytes + 16, [](std::uint8_t b) { return b == 0; }), "buffer zero-filled");
        std::fill_n(bytes, 16, std::uint8_t{ 0xA5 });
        Check(bytes[15] == 0xA5, "buffer writable");
        buffer->Release();
    }
    // D3DXCreateBuffer(0) succeeds with an empty buffer; so does this.
    buffer = nullptr;
    Check(R_CreateShaderBuffer(0, &buffer) == 0 && buffer && buffer->GetBufferSize() == 0, "empty buffer");
    if (buffer)
        buffer->Release();
    Check(R_CreateShaderBuffer(4, nullptr) == kInvalidArgument, "null out pointer");
}

void TestOversizedBuffers()
{
    ShaderBuffer *buffer = nullptr;
    // A corrupt cached length must come back as E_OUTOFMEMORY, not an
    // exception: the loader's cache path prints that error and moves on.
    constexpr std::int32_t kOutOfMemory = static_cast<std::int32_t>(0x8007000Eu);
    ShaderBuffer *stale = nullptr;
    Check(R_CreateShaderBuffer(1, &stale) == 0 && stale, "sentinel buffer");
    for (const std::uint32_t size : { kShaderBufferMaxBytes + 1u, 0xFFFFFFFFu })
    {
        buffer = stale; // a failed call must not leave an old pointer behind
        Check(R_CreateShaderBuffer(size, &buffer) == kOutOfMemory && !buffer, "oversized buffer fails cleanly");
    }
    if (stale)
        stale->Release();
    Check(R_CreateShaderBuffer(kShaderBufferMaxBytes, &buffer) == 0 && buffer
            && buffer->GetBufferSize() == kShaderBufferMaxBytes, "largest buffer");
    if (buffer)
        buffer->Release();
}

#ifdef _WIN32
const char kVertexSource[] =
    "float4x4 worldViewProjectionMatrix; float4 colorTint;\n"
    "struct VSOut { float4 pos : POSITION; float4 color : COLOR0; float2 uv : TEXCOORD0; };\n"
    "VSOut vs_main(float4 pos : POSITION, float4 color : COLOR0, float2 uv : TEXCOORD0)\n"
    "{ VSOut o; o.pos = mul(pos, worldViewProjectionMatrix); o.color = color * colorTint; o.uv = uv; return o; }\n";
const char kPixelSource[] =
    "sampler2D colorMapSampler;\n"
    "float4 ps_main(float4 color : COLOR0, float2 uv : TEXCOORD0) : COLOR { return tex2D(colorMapSampler, uv) * color; }\n";

// A compiled program: the expected version token, a 'CTAB' comment, and an
// end token as its last dword.
bool ProgramLooksCompiled(ShaderBuffer *program, std::uint32_t version)
{
    const std::uint32_t size = program->GetBufferSize();
    if (size < 12 || size % 4)
        return false;
    std::vector<std::uint32_t> tokens(size / 4);
    std::copy_n(static_cast<const std::uint8_t *>(program->GetBufferPointer()), size,
        reinterpret_cast<std::uint8_t *>(tokens.data()));
    const bool hasTable = tokens[1] >> 16 >= 1 && (tokens[1] & 0xFFFFu) == 0xFFFEu && tokens[2] == 0x42415443u;
    return tokens.front() == version && hasTable && tokens.back() == 0x0000FFFFu;
}

std::int32_t Compile(const char *source, const char *entry, const char *target, ShaderBuffer **program,
    ShaderBuffer **messages)
{
    return R_CompileShader(source, static_cast<std::uint32_t>(std::string_view(source).size()), entry, target,
        program, messages);
}

void TestCompiles()
{
    const struct
    {
        const char *source;
        const char *entry;
        const char *target;
        std::uint32_t version;
    } cases[] = {
        { kVertexSource, "vs_main", "vs_2_0", 0xFFFE0200u },
        { kVertexSource, "vs_main", "vs_3_0", 0xFFFE0300u },
        { kPixelSource, "ps_main", "ps_2_0", 0xFFFF0200u },
        { kPixelSource, "ps_main", "ps_3_0", 0xFFFF0300u },
    };
    for (const auto &c : cases)
    {
        ShaderBuffer *program = nullptr;
        ShaderBuffer *messages = nullptr;
        const std::int32_t result = Compile(c.source, c.entry, c.target, &program, &messages);
        Check(result >= 0 && program, c.target);
        if (program)
        {
            Check(ProgramLooksCompiled(program, c.version), "version, constant table and end token");
            program->Release();
        }
        if (messages)
            messages->Release();
    }
}

void TestCompileFailures()
{
    ShaderBuffer *program = nullptr;
    ShaderBuffer *messages = nullptr;
    Check(Compile("float4 vs_main(:POSITION { }", "vs_main", "vs_3_0", &program, &messages) < 0, "syntax error fails");
    Check(!program, "no program on failure");
    Check(messages != nullptr, "errors reported");
    if (messages)
    {
        const auto *text = static_cast<const char *>(messages->GetBufferPointer());
        Check(std::string(text, messages->GetBufferSize()).find("error") != std::string::npos, "error text");
        messages->Release();
    }
    Check(Compile(kVertexSource, "missing_main", "vs_3_0", &program, nullptr) < 0 && !program, "missing entry point");
    Check(Compile(kVertexSource, "vs_main", "vs_9_9", &program, nullptr) < 0 && !program, "unknown target");
    Check(Compile(kVertexSource, "vs_main", "vs_3_0", nullptr, nullptr) == kInvalidArgument, "null program pointer");
}
#else
void TestNoCompiler()
{
    ShaderBuffer *program = nullptr;
    Check(R_CompileShader("x", 1, "main", "vs_3_0", &program, nullptr) == static_cast<std::int32_t>(0x80004001u)
            && !program, "E_NOTIMPL off Windows");
}
#endif
} // namespace

int main()
{
    TestBuffers();
    TestOversizedBuffers();
#ifdef _WIN32
    TestCompiles();
    TestCompileFailures();
#else
    TestNoCompiler();
#endif
    if (failures)
        return 1;
    std::puts("shader compile replacement passed");
    return 0;
}
