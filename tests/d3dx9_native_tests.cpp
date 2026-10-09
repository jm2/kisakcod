// d3dx9_native_tests.cpp: the D3DX9 subset a dxvk-native client links in
// place of the D3DX9 SDK (_platform/posix/d3dx9_native.cpp), called through
// the declarations gfx_d3d/r_d3dx9.h gives the renderer. The renderer copies
// cached shader bytecode into D3DXCreateBuffer's buffer, so that one is real;
// compiling and reflecting must fail cleanly, with every output cleared.

#include <cstdio>
#include <cstring>

#include <_platform/posix/d3dx9_native.h>

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))
} // namespace

int main()
{
    // A buffer of the requested size, zeroed and writable, with COM lifetime.
    ID3DXBuffer *buffer = nullptr;
    CHECK(D3DXCreateBuffer(64, &buffer) == S_OK && buffer);
    if (buffer)
    {
        CHECK(buffer->GetBufferSize() == 64);
        auto *bytes = static_cast<unsigned char *>(buffer->GetBufferPointer());
        CHECK(bytes && bytes[0] == 0 && bytes[63] == 0);
        if (bytes)
            std::memset(bytes, 0xAB, 64);
        IUnknown *unknown = nullptr;
        CHECK(buffer->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void **>(&unknown)) == S_OK && unknown);
        CHECK(buffer->Release() == 1);
        CHECK(buffer->Release() == 0);
    }
    ID3DXBuffer *empty = nullptr;
    CHECK(D3DXCreateBuffer(0, &empty) == S_OK && empty && empty->GetBufferSize() == 0);
    if (empty)
        empty->Release();
    CHECK(D3DXCreateBuffer(4, nullptr) == D3DERR_INVALIDCALL);

    // No compiler: every output is cleared and the call fails.
    ID3DXBuffer *shader = buffer;
    ID3DXBuffer *errors = buffer;
    ID3DXConstantTable *table = reinterpret_cast<ID3DXConstantTable *>(buffer);
    CHECK(FAILED(D3DXCompileShader("float4 main() : COLOR { return 0; }", 34, nullptr, nullptr, "main", "ps_2_0",
        0, &shader, &errors, &table)));
    CHECK(!shader && !errors && !table);

    // No reflection either.
    const DWORD program[] = {0xFFFF0200u, 0x0000FFFFu};
    table = reinterpret_cast<ID3DXConstantTable *>(buffer);
    CHECK(FAILED(D3DXGetShaderConstantTable(program, &table)) && !table);
    D3DXSEMANTIC semantics[4] = {};
    UINT count = 7;
    CHECK(FAILED(D3DXGetShaderInputSemantics(program, semantics, &count)) && count == 0);
    count = 7;
    CHECK(FAILED(D3DXGetShaderOutputSemantics(program, semantics, &count)) && count == 0);

    if (g_failures == 0)
        std::printf("d3dx9 native subset: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
