#pragma once

#include <cstdint>
#include <vector>

// The D3DX shader buffer and HLSL compiler the material loader uses, without
// D3DX (G5): D3DX9 ships no ARM64 library and none for the POSIX targets.
// ShaderBuffer keeps ID3DXBuffer's three calls so the loader's buffer handling
// moves over unchanged; the compiler is d3dcompiler_47's D3DCompile, which is
// part of Windows and builds for x86, x64 and ARM64. Results are HRESULTs.

class ShaderBuffer final
{
public:
    void *GetBufferPointer() { return bytes.data(); }
    std::uint32_t GetBufferSize() const { return static_cast<std::uint32_t>(bytes.size()); }
    void Release() { delete this; }

private:
    explicit ShaderBuffer(std::uint32_t size) : bytes(size) {}
    ~ShaderBuffer() = default;
    friend std::int32_t R_CreateShaderBuffer(std::uint32_t size, ShaderBuffer **buffer);

    std::vector<std::uint8_t> bytes;
};

// D3DXCreateBuffer: a zero-filled buffer of `size` bytes, released with Release().
std::int32_t R_CreateShaderBuffer(std::uint32_t size, ShaderBuffer **buffer);

// D3DXCompileShader without defines, includes or flags: compiles `entryPoint`
// for `target` (vs_2_0 ... ps_3_0). *program receives the bytecode on success;
// *messages, when not null, receives the compiler's errors or warnings when it
// reported any. Off Windows there is no compiler: E_NOTIMPL.
std::int32_t R_CompileShader(
    const char *source,
    std::uint32_t sourceLength,
    const char *entryPoint,
    const char *target,
    ShaderBuffer **program,
    ShaderBuffer **messages);
