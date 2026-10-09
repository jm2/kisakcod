#pragma once

// The D3DX9 subset a dxvk-native client compiles against (KISAK_DXVK_NATIVE),
// in place of the full D3DX9 SDK headers, which need GDI, COM streams and the
// .x file API that dxvk-native's Windows shim does not provide. Only what the
// engine uses is declared: ID3DXBuffer, the shader compiler and its reflection
// entry points. Signatures are D3DX9's (as in dxvk-native's d3dx9shader.h).

#include <d3d9.h>

struct D3DXMACRO
{
    const char *Name;
    const char *Definition;
};

typedef struct _D3DXSEMANTIC
{
    UINT Usage;
    UINT UsageIndex;
} D3DXSEMANTIC;

// The constant table D3DX9 shaders carry in their CTAB comment block.
typedef struct _D3DXSHADER_CONSTANTTABLE
{
    DWORD Size;
    DWORD Creator;
    DWORD Version;
    DWORD Constants;
    DWORD ConstantInfo;
    DWORD Flags;
    DWORD Target;
} D3DXSHADER_CONSTANTTABLE, *LPD3DXSHADER_CONSTANTTABLE;

typedef struct _D3DXSHADER_CONSTANTINFO
{
    DWORD Name;
    WORD  RegisterSet;
    WORD  RegisterIndex;
    WORD  RegisterCount;
    WORD  Reserved;
    DWORD TypeInfo;
    DWORD DefaultValue;
} D3DXSHADER_CONSTANTINFO, *LPD3DXSHADER_CONSTANTINFO;


struct ID3DXInclude;

struct ID3DXBuffer : public IUnknown
{
    virtual void *STDMETHODCALLTYPE GetBufferPointer() = 0;
    virtual DWORD STDMETHODCALLTYPE GetBufferSize() = 0;
};
typedef ID3DXBuffer *LPD3DXBUFFER;

// The engine only releases constant tables; ID3DXConstantTable's own methods
// begin with ID3DXBuffer's.
struct ID3DXConstantTable : public ID3DXBuffer
{
};

HRESULT WINAPI D3DXCreateBuffer(DWORD size, ID3DXBuffer **buffer);
HRESULT WINAPI D3DXCompileShader(const char *src_data, UINT data_len, const D3DXMACRO *defines,
    ID3DXInclude *include, const char *function_name, const char *profile, DWORD flags,
    ID3DXBuffer **shader, ID3DXBuffer **error_messages, ID3DXConstantTable **constant_table);
HRESULT WINAPI D3DXGetShaderConstantTable(const DWORD *byte_code, ID3DXConstantTable **constant_table);
HRESULT WINAPI D3DXGetShaderInputSemantics(const DWORD *function, D3DXSEMANTIC *semantics, UINT *count);
HRESULT WINAPI D3DXGetShaderOutputSemantics(const DWORD *function, D3DXSEMANTIC *semantics, UINT *count);
