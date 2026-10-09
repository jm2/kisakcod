#pragma once

#include <cstdint>

// Shader Model 2/3 bytecode reflection without D3DX (G5). D3DX9 ships no
// ARM64 library and none for the POSIX targets, so the material loader's
// D3DXGetShaderConstantTable is replaced by reading the 'CTAB' comment block
// the HLSL compiler embeds in the bytecode. Every offset in the block is
// bounds-checked here: shader programs come from fast files and the shader
// cache, so a malformed table must fail the load, not read past it.

// The CTAB payload: the bytes after the 'CTAB' FourCC, laid out as
// D3DXSHADER_CONSTANTTABLE and exactly what ID3DXConstantTable's
// GetBufferPointer() returns. It points into the program it was found in.
struct ShaderConstantTableView
{
    const std::uint8_t *data;
    std::uint32_t size;
    std::uint32_t constantCount;
};

// The fields of one D3DXSHADER_CONSTANTINFO and its D3DXSHADER_TYPEINFO that
// the material loader reads.
struct ShaderConstantDesc
{
    const char *name;
    std::uint16_t registerSet;   // D3DXREGISTER_SET
    std::uint16_t registerIndex;
    std::uint16_t registerCount;
    std::uint16_t typeClass;     // D3DXPARAMETER_CLASS
    std::uint16_t typeType;      // D3DXPARAMETER_TYPE
};

// Finds the first CTAB block in a well-formed vs/ps 1.x, 2.x or 3.x program of
// dwordCount tokens. True only when the stream ends in its end token and the
// whole table validates: the header size, the constant array, every type
// record and every NUL-terminated name lie inside the payload. 1.x
// instructions carry no length, so there only the comments right after the
// version token are searched, which is where the compiler puts the table.
bool R_ShaderFindConstantTable(
    const std::uint32_t *program,
    std::uint32_t dwordCount,
    ShaderConstantTableView *table);

// Reads constant `index` (< table.constantCount) of a table that
// R_ShaderFindConstantTable returned.
bool R_ShaderGetConstantDesc(
    const ShaderConstantTableView &table,
    std::uint32_t index,
    ShaderConstantDesc *desc);

// One D3DXSEMANTIC: a D3DDECLUSAGE and its usage index, in D3DXSEMANTIC's layout.
struct ShaderSemantic
{
    std::uint32_t usage;
    std::uint32_t usageIndex;
};

// The replacements for D3DXGetShaderInputSemantics and
// D3DXGetShaderOutputSemantics, matching native D3DX9 on vs/ps 1.x to 3.x.
// Declared semantics come from dcl instructions: vertex inputs, vs_3_0
// outputs and pixel inputs from ps_2_0 on (ps_2_x dcl carries no usage, so
// v# reads as COLOR and t# as TEXCOORD). The rest come from the registers the
// program names: ps_1_x inputs (t#, v#), and every other output (oT#, oD#,
// oC#, ps_1_x's r0, oPos/oFog/oPts, oDepth), listed as texture coordinates,
// then colors, then rasterizer outputs, then depth. False on a malformed
// program or more than `capacity` semantics; *count is then 0.
bool R_ShaderGetInputSemantics(
    const std::uint32_t *program,
    std::uint32_t dwordCount,
    ShaderSemantic *semantics,
    std::uint32_t capacity,
    std::uint32_t *count);
bool R_ShaderGetOutputSemantics(
    const std::uint32_t *program,
    std::uint32_t dwordCount,
    ShaderSemantic *semantics,
    std::uint32_t capacity,
    std::uint32_t *count);
