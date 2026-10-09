// d3dx9_native.cpp: the D3DX9 subset of d3dx9_native.h for a dxvk-native
// client. D3DXCreateBuffer is real -- the renderer copies cached shader
// bytecode into the buffer it returns. There is no D3DX9 shader compiler off
// Windows, so compiling and reflecting report failure with their outputs
// cleared: a dxvk-native client runs fast files' precompiled shaders, and only
// the loose-shader path (r_material_load_obj.cpp) calls these.

#include "d3dx9_native.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <new>

namespace
{
class Buffer final : public ID3DXBuffer
{
public:
    explicit Buffer(DWORD size) : m_size(size), m_data(size ? std::calloc(size, 1) : nullptr) {}
    ~Buffer() { std::free(m_data); }

    bool Valid() const { return m_size == 0 || m_data != nullptr; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override
    {
        if (!object)
            return E_POINTER;
        if (riid == __uuidof(IUnknown))
        {
            AddRef();
            *object = static_cast<IUnknown *>(this);
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG refs = --m_refs;
        if (refs == 0)
            delete this;
        return refs;
    }
    void *STDMETHODCALLTYPE GetBufferPointer() override { return m_data; }
    DWORD STDMETHODCALLTYPE GetBufferSize() override { return m_size; }

private:
    std::atomic<ULONG> m_refs{1};
    DWORD m_size;
    void *m_data;
};

template <typename T>
void Clear(T **out)
{
    if (out)
        *out = nullptr;
}
} // namespace

HRESULT WINAPI D3DXCreateBuffer(DWORD size, ID3DXBuffer **buffer)
{
    if (!buffer)
        return D3DERR_INVALIDCALL;
    *buffer = nullptr;
    Buffer *created = new (std::nothrow) Buffer(size);
    if (!created || !created->Valid())
    {
        delete created;
        return E_OUTOFMEMORY;
    }
    *buffer = created;
    return S_OK;
}

HRESULT WINAPI D3DXCompileShader(const char *, UINT, const D3DXMACRO *, ID3DXInclude *, const char *, const char *,
    DWORD, ID3DXBuffer **shader, ID3DXBuffer **error_messages, ID3DXConstantTable **constant_table)
{
    Clear(shader);
    Clear(error_messages);
    Clear(constant_table);
    return E_NOTIMPL;
}

HRESULT WINAPI D3DXGetShaderConstantTable(const DWORD *, ID3DXConstantTable **constant_table)
{
    Clear(constant_table);
    return E_NOTIMPL;
}

HRESULT WINAPI D3DXGetShaderInputSemantics(const DWORD *, D3DXSEMANTIC *, UINT *count)
{
    if (count)
        *count = 0;
    return E_NOTIMPL;
}

HRESULT WINAPI D3DXGetShaderOutputSemantics(const DWORD *, D3DXSEMANTIC *, UINT *count)
{
    if (count)
        *count = 0;
    return E_NOTIMPL;
}
