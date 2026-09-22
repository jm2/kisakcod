// db_load_entry_harness: engine-service harness for the production
// database admission / stream-relocation entry-point contracts
// (ki-458h / #125).
//
// db_load_entry_test.cpp drives the REAL DB registry and stream-load
// production units:
//
//   - src/database/db_registry.cpp: DB_Init, DB_AddXAsset,
//     DB_FindXAssetHeader, DB_FindXAssetEntry, DB_EnumXAssets,
//     DB_RemoveXAsset - the production hash-table admission and
//     lookup chain over the production per-type asset pools and the
//     production zone-runtime table substrate.
//   - src/database/db_stream_load.cpp: Load_Stream, Load_StreamArray,
//     Load_DelayStream, DB_ConvertOffsetToPointer / ToAlias /
//     ToCString / ToTempString - the production fast-file stream
//     admission and relocation writers over the production relocation
//     resolver (db_relocation.cpp) and stream block machinery
//     (db_stream.cpp).
//   - src/database/db_load_legacy_bridge.cpp: the production
//     temp-string intern path driven by DB_ConvertOffsetToTempString.
//
// These units are enrolled exactly as in the stage-2 loader tier: the
// production sources compile only against the win32-x86 dialect
// (DirectX / MSVC decompiled header web), so the committed CMake
// target is gated to the Windows x86 CI leg. The engine services the
// registry and stream TUs reference but a unit test cannot host are
// supplied in db_load_entry_stubs.cpp with their PRODUCTION
// signatures at GLOBAL scope (single-threaded coordination, zone
// lifecycle, render fan-out, harness singletons) together with the
// controlled fast-file provider and the printf/assert family in
// db_load_entry_test.cpp. This header holds only the shared harness
// state and fixture builders - the global-scope service DEFINITIONS
// live in the .cpp TUs, because the production units must see real
// strong definitions to link.
//
// Fail-closed protocol, identical to the stage-2 tier: Com_Error
// RECORDS the error and RETURNS, which exercises the production
// guard-return cleanup paths after an ERR_DROP (production longjmps,
// but every recorded call site returns null / leaves its output field
// untouched); MyAssertHandler and fatal paths terminate the process
// with _Exit(3), so a production assertion or fatal is always a hard
// test failure.

#ifndef DB_LOAD_ENTRY_HARNESS_HPP
#define DB_LOAD_ENTRY_HARNESS_HPP

#include <database/database.h>
#include <database/db_stream.h>
#include <database/db_relocation.h>
#include <database/db_zone_memory.h>

#include <universal/q_shared.h>
#include <universal/assertive.h>

#include <algorithm>
#include <csetjmp>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdint.h>
#include <string>
#include <vector>

// The printf-family wrappers the registry and stream TUs call.
// Declared here, DEFINED in db_load_entry_test.cpp: their bodies
// necessarily call a printf function with a caller-supplied format
// string, and every production printf wrapper in this repository
// lives in a .cpp (common.cpp, r_warn.cpp) - a header body
// re-triggers the CWE-134 lexical pattern.
void Com_PrintError(int channel, const char *fmt, ...);
int Com_sprintf(char *dest, uint32_t size, const char *fmt, ...);
void Com_Error(errorParm_t code, const char *fmt, ...);

namespace db_load_entry_harness
{
// The fixture byte builder shared with the portable cursor suites and
// the stage-2 entry harness. The push methods avoid the terse U8/U32
// spellings: preprocessing this TU's include web can mint those
// tokens from unrelated definition sites, and the long names cannot
// collide.
struct ByteWriter
{
    std::vector<unsigned char> bytes;

    void PushU8(unsigned char value) { bytes.push_back(value); }

    void PushU32(uint32_t value)
    {
        bytes.push_back(static_cast<unsigned char>(value & 0xFFu));
        bytes.push_back(static_cast<unsigned char>((value >> 8) & 0xFFu));
        bytes.push_back(static_cast<unsigned char>((value >> 16) & 0xFFu));
        bytes.push_back(static_cast<unsigned char>((value >> 24) & 0xFFu));
    }

    void Zeros(uint32_t count)
    {
        for (uint32_t i = 0; i < count; ++i)
            bytes.push_back(0);
    }
};

struct RecordedError
{
    int channel;
    std::string text;
};

using RecordedErrorList = std::vector<RecordedError>;

struct HarnessState
{
    RecordedErrorList errors;
    RecordedErrorList printErrors;
    // The controlled fast-file byte source behind DB_LoadXFileData.
    std::vector<unsigned char> fileBytes;
    uint32_t fileCursor;
    // Total provider bytes served across every read: cases use the
    // cursor to place provider content at the exact offset the next
    // read will consume.
    uint32_t servedBytes;
    // Coordination records for the database-thread no-ops.
    uint32_t wakeCount;
    uint32_t wake2Count;
    uint32_t notifyCount;
    uint32_t syncCount;
    uint32_t loadXFileDataCalls;
    uint32_t milliseconds;
};

inline HarnessState &State()
{
    static HarnessState state;
    return state;
}

inline void ResetHarness()
{
    HarnessState &state = State();
    state.errors.clear();
    state.printErrors.clear();
    state.fileBytes.clear();
    state.fileCursor = 0;
    state.servedBytes = 0;
    state.wakeCount = 0;
    state.wake2Count = 0;
    state.notifyCount = 0;
    state.syncCount = 0;
    state.loadXFileDataCalls = 0;
    state.milliseconds = 0;
}

inline const RecordedErrorList &Errors() { return State().errors; }

inline const RecordedErrorList &PrintErrors()
{
    return State().printErrors;
}

inline bool HasErrorContaining(const char *needle,
                               const RecordedErrorList &errors =
                                   State().errors)
{
    for (const RecordedError &error : errors)
    {
        if (error.text.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

// The controlled fast-file byte source: the test installs the bytes
// of one fast-file image and DB_LoadXFileData consumes them
// sequentially, exactly like the production FS read behind the
// database loader.
inline void SetFastFileBytes(const std::vector<unsigned char> &bytes)
{
    State().fileBytes = bytes;
    State().fileCursor = 0;
}

// The provider cursor: the offset within the CURRENT image the next
// DB_LoadXFileData read consumes.
inline uint32_t ServedBytes() { return State().servedBytes; }

// The production stream blocks: the harness owns one backing buffer
// per block so the relocation resolver sees real addresses.
struct StreamBlockFixture
{
    unsigned char data[9][256];
    XZoneMemory zoneMemory;

    StreamBlockFixture()
    {
        memset(data, 0, sizeof(data));
        memset(&zoneMemory, 0, sizeof(zoneMemory));
    }

    void Configure()
    {
        memset(data, 0, sizeof(data));
        memset(&zoneMemory, 0, sizeof(zoneMemory));
        // Block 0: the primary load block. Block 1: the zero-fill /
        // materialization block. Blocks 2 / 3: the delayed-load
        // blocks. Block 4: the alias slot block. Block 5: a provider
        // block (DB_LoadXFileData).
        zoneMemory.blocks[0].data = data[0];
        zoneMemory.blocks[0].size = 256;
        zoneMemory.blocks[1].data = data[1];
        zoneMemory.blocks[1].size = 256;
        zoneMemory.blocks[2].data = data[2];
        zoneMemory.blocks[2].size = 256;
        zoneMemory.blocks[3].data = data[3];
        zoneMemory.blocks[3].size = 256;
        zoneMemory.blocks[4].data = data[4];
        zoneMemory.blocks[4].size = 256;
        zoneMemory.blocks[5].data = data[5];
        zoneMemory.blocks[5].size = 256;
    }
};

// Serializes a block-4 alias slot token: the resolver decodes
// (token - 1) as block:offset with the alias block fixed at 4
// (db_relocation.cpp AliasRegistry::Resolve).
inline uint32_t AliasToken(uint32_t slotOffset)
{
    return (4u << 28) | (slotOffset + 1u);
}

// Serializes a block-N data pointer token (db_disk32.h DecodeOffset).
inline uint32_t OffsetToken(uint32_t block, uint32_t offset)
{
    return (block << 28) | (offset + 1u);
}

// RawFile payload the admission tests register. The production
// DB_CloneXAssetInternal copies DB_GetXAssetTypeSize(type) bytes out
// of the caller's header into the pool slot, so the payload must
// outlive every admission case; the tests keep these as statics.
struct RawFilePayload
{
    const char *name;
    int len;
    const char *buffer;
};

// The useFastFile dvar instance the harness configuration points at:
// fast-file mode enabled, the only mode the enrolled entry points
// support. db_load_entry_test.cpp binds the production extern to it.
inline dvar_t &UseFastFileDvar()
{
    static dvar_t dvar;
    dvar.name = "useFastFile";
    dvar.current.enabled = true;
    return dvar;
}

// The production unwinding protocol. Production Com_Error(ERR_DROP)
// longjmps to the zone-load frame; every call site either has a
// guard-return cleanup after the error (the relocation writers leave
// their output fields untouched) or RELIES on the unwinding
// (DB_CreateDefaultEntry falls straight into memcpy after its
// "could not load default asset" drop). The default harness protocol
// is record-and-return, which faithfully exercises the guard-return
// sites; a case that drives a production-unwound path arms the jump
// target first, exactly like the production setjmp around the zone
// load. Com_Error longjmps through the armed target after recording.
struct ErrDropUnwind
{
    jmp_buf target;
    bool armed;
};

inline ErrDropUnwind &ErrDrop()
{
    static ErrDropUnwind unwind;
    return unwind;
}

inline jmp_buf &ArmErrDrop()
{
    ErrDropUnwind &unwind = ErrDrop();
    unwind.armed = true;
    return unwind.target;
}

inline void DisarmErrDrop()
{
    ErrDrop().armed = false;
}
} // namespace db_load_entry_harness

// IsFastFileLoad comes from q_shared.h (reads the useFastFile dvar);
// the test TU binds the extern to the harness dvar record above.

#endif // DB_LOAD_ENTRY_HARNESS_HPP
