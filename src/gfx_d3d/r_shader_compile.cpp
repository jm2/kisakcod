#include "r_shader_compile.h"

#include <algorithm>
#include <new>

#ifdef _WIN32
#include <d3dcompiler.h>
#endif

namespace
{
constexpr std::int32_t kOk = 0;
constexpr std::int32_t kNotImplemented = static_cast<std::int32_t>(0x80004001u);
constexpr std::int32_t kOutOfMemory = static_cast<std::int32_t>(0x8007000Eu);
constexpr std::int32_t kInvalidArgument = static_cast<std::int32_t>(0x80070057u);

#ifdef _WIN32
// Copies a compiler blob into a ShaderBuffer and releases the blob.
std::int32_t TakeBlob(ID3DBlob *blob, ShaderBuffer **buffer)
{
    const auto size = static_cast<std::uint32_t>(blob->GetBufferSize());
    const std::int32_t result = R_CreateShaderBuffer(size, buffer);
    if (result == kOk)
    {
        const auto *bytes = static_cast<const std::uint8_t *>(blob->GetBufferPointer());
        std::copy_n(bytes, size, static_cast<std::uint8_t *>((*buffer)->GetBufferPointer()));
    }
    blob->Release();
    return result;
}
#endif
} // namespace

std::int32_t R_CreateShaderBuffer(std::uint32_t size, ShaderBuffer **buffer)
{
    if (!buffer)
        return kInvalidArgument;
    *buffer = new (std::nothrow) ShaderBuffer(size);
    return *buffer ? kOk : kOutOfMemory;
}

std::int32_t R_CompileShader(
    const char *source,
    std::uint32_t sourceLength,
    const char *entryPoint,
    const char *target,
    ShaderBuffer **program,
    ShaderBuffer **messages)
{
    if (messages)
        *messages = nullptr;
    if (!program)
        return kInvalidArgument;
    *program = nullptr;
    if (!source || !entryPoint || !target)
        return kInvalidArgument;

#ifdef _WIN32
    ID3DBlob *code = nullptr;
    ID3DBlob *errors = nullptr;
    HRESULT result = D3DCompile(source, sourceLength, nullptr, nullptr, nullptr, entryPoint, target, 0, 0,
        &code, &errors);
    if (errors)
    {
        if (messages)
            TakeBlob(errors, messages);
        else
            errors->Release();
    }
    if (code)
    {
        if (SUCCEEDED(result))
            result = TakeBlob(code, program);
        else
            code->Release();
    }
    return static_cast<std::int32_t>(result);
#else
    (void)sourceLength;
    return kNotImplemented;
#endif
}
