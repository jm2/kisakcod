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
// Copies a compiler blob into *buffer (when buffer is not null) and releases
// the blob; a null blob is left alone.
std::int32_t TakeBlob(ID3DBlob *blob, ShaderBuffer **buffer)
{
    if (!blob)
        return kOk;
    if (!buffer)
    {
        blob->Release();
        return kOk;
    }
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
    *buffer = nullptr;
    if (size > kShaderBufferMaxBytes)
        return kOutOfMemory;

    // Both allocations are nothrow: the material loader passes sizes read from
    // the shader cache, and a failure must reach its E_OUTOFMEMORY path.
    std::unique_ptr<std::uint8_t[]> bytes(new (std::nothrow) std::uint8_t[size]());
    if (!bytes)
        return kOutOfMemory;
    *buffer = new (std::nothrow) ShaderBuffer(std::move(bytes), size);
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
    TakeBlob(errors, messages);
    const std::int32_t taken = TakeBlob(code, SUCCEEDED(result) ? program : nullptr);
    return SUCCEEDED(result) ? taken : static_cast<std::int32_t>(result);
#else
    (void)sourceLength;
    return kNotImplemented;
#endif
}
