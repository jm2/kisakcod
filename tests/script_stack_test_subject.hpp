#pragma once
// Private single-TU fixture: engine service doubles and actual production
// reader/writer slices. Behavioral cases live in script_readstack_nested_test.cpp.

struct ReadstackComError {};
int g_readstackUnexpectedReports = 0;

char g_readstackDiagnostic[] = "script readstack test diagnostic";

char *QDECL va(const char *format, ...)
{
    (void)format;
    return g_readstackDiagnostic;
}

void QDECL Com_Error(const errorParm_t code, const char *format, ...)
{
    (void)code;
    (void)format;
    throw ReadstackComError{};
}

void QDECL Com_Printf(const int channel, const char *format, ...)
{
    (void)channel;
    (void)format;
    ++g_readstackUnexpectedReports;
}

void MyAssertHandler(
    const char *filename,
    int line,
    int type,
    const char *format,
    ...)
{
    (void)type;
    (void)format;
    std::fprintf(
        stderr,
        "script_readstack_nested_test: production assert fired at %s:%d\n",
        filename,
        line);
    std::abort();
}

bool __cdecl Sys_IsMainThread()
{
    return true;
}

bool __cdecl Sys_IsRenderThread()
{
    return false;
}

bool __cdecl Sys_IsDatabaseThread()
{
    return false;
}

// Reader decoder doubles: deterministic, one byte per id/codepos token.
// The test images below encode every id/codepos as exactly one byte.

unsigned int __cdecl Scr_ReadId(MemoryFile *memFile, unsigned int tag)
{
    (void)tag;
    uint8_t byte = 0;
    MemFile_ReadData(memFile, 1, &byte);
    return byte;
}

const char *__cdecl Scr_ReadCodepos(MemoryFile *memFile)
{
    uint8_t byte = 0;
    MemFile_ReadData(memFile, 1, &byte);
    return reinterpret_cast<const char *>(static_cast<uintptr_t>(0x4000u + byte));
}

uint16_t __cdecl Scr_ReadString(MemoryFile *memFile)
{
    (void)memFile;
    // Not exercised by any test image: strings decode through the real
    // production path, which this TU does not link.
    std::fputs("script_readstack_nested_test: Scr_ReadString double entered\n", stderr);
    std::abort();
}

const float *__cdecl Scr_ReadVec3(MemoryFile *memFile)
{
    (void)memFile;
    std::fputs("script_readstack_nested_test: Scr_ReadVec3 double entered\n", stderr);
    std::abort();
}

// Allocation double: records every block so a nested record's stored
// pointer can be asserted against the allocation it must identify.
std::vector<VariableStackBuffer *> g_allocations;

void *__cdecl MT_Alloc(int numBytes, int type)
{
    (void)type;
    void *block = std::malloc(static_cast<size_t>(numBytes));
    g_allocations.push_back(static_cast<VariableStackBuffer *>(block));
    return block;
}

// The slice only touches scrVarPub.numScriptThreads; a minimal definition
// satisfies it without dragging in the full scr_main.h surface.
struct scrVarPub_t
{
    uint32_t numScriptThreads;
};

scrVarPub_t scrVarPub{};

// ---------------------------------------------------------------------------
// Verbatim production slice (extracted at configure time).
// ---------------------------------------------------------------------------

#include <script_readstack_slice.inc>

// Writer leaf services use the fixture's deliberately small token encoding.
// The stack traversal, headers, and full-width cell reads are production code.
void WriteCodepos(const char *pos, MemoryFile *memFile)
{
    const uint8_t token = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(pos) - 0x4000u);
    MemFile_WriteData(memFile, 1, &token);
}
void WriteId(unsigned int id, unsigned int tag, MemoryFile *memFile)
{
    const uint8_t token[2] = {static_cast<uint8_t>(tag), static_cast<uint8_t>(id)};
    MemFile_WriteData(memFile, 2, token);
}
namespace
{
void DoSaveEntryWithoutStack(unsigned int type, VariableUnion value, MemoryFile *memFile)
{
    const uint8_t encodedType = static_cast<uint8_t>(type << 3);
    MemFile_WriteData(memFile, 1, &encodedType);
    if (type == VAR_CODEPOS)
        WriteCodepos(value.codePosValue, memFile);
    else if (type == VAR_INTEGER || type == VAR_FLOAT)
        MemFile_WriteData(memFile, 4, &value.intValue);
    else
        std::abort();
}
}
#include <script_writestack_slice.inc>

