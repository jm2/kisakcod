#pragma once

// Everything in this header is C++ (namespace, references). Guarding the body
// keeps C tooling — including static analysis that parses .h as C — from
// choking on the syntax while leaving the C++ view unchanged.
#ifdef __cplusplus

#ifndef KISAK_SP
#error This file is for SinglePlayer only
#endif

#include <cstddef>
#include <cstdint>

#include <game/taginfo_disk32.h>
#include <universal/memfile.h>

// Production save-record I/O for the pointer-bearing tagInfo_s record. This
// is the module g_save.cpp's WriteField2 / ReadField SF_TYPE_TAG_INFO
// branches delegate to; it owns every tagInfo-specific save/load step:
//
//   1. Entity indices are derived from the FULL native entity pointers with
//      stride-boundary and range validation. The legacy WriteField1
//      SF_ENTITY walker cannot be used here: it classifies and subtracts
//      through the low 32 bits of the pointer-sized field and writes the
//      index back over only the low 4 bytes of a 64-bit pointer, so a
//      full-pointer read observes a garbled value on 64-bit hosts.
//   2. The wire image is the fixed 0x70-byte Disk32 record emitted by
//      taginfo_save::TagInfoToDisk32, with the retail 0/1 name mark at
//      offset 0x08 and the name text as a trailing CString.
//   3. Load validates the 1-based entity indices and rebuilds full native
//      host pointers before the record is released back to the game.
//
// The module is deliberately free of game-side types (no tagInfo_s, no
// gentity_s, no g_save.h dependency tree) so the production translation unit
// compiles and runs inside the test tree against the real universal/memfile
// stream primitives.
namespace taginfo_save
{
// How the host addresses entities for save-stream indices. Production wires
// this to g_entities / sizeof(gentity_s) / MAX_GENTITIES.
struct TagInfoEntityMap
{
    const void *base;    // entity array base
    std::size_t stride;  // entity array element stride in bytes
    std::uint32_t count; // number of entity slots (index bound)
};

// Reports an unrecoverable record defect. Must not return: production routes
// to Com_Error(ERR_DROP, ...); the test harness unwinds through longjmp.
using TagInfoFailFn = void (*)(const char *message);

// Live scr string table shims (production: SL_ConvertToString / SL_GetString).
using TagInfoStringFromHandleFn = const char *(*)(std::uint16_t handle);
using TagInfoHandleFromStringFn = std::uint16_t (*)(const char *text);

// The live host-side record as the save path observes it, with full native
// entity pointers and the live 16-bit name handle (0 = none).
struct TagInfoLiveRecord
{
    const void *parent;
    const void *next;
    std::uint16_t name;
    std::int32_t index;
    float axis[4][3];
    float parentInvAxis[4][3];
};

// The loaded record with entity indices already resolved into full native
// host pointers and the name handle restored from the string table.
struct TagInfoRestoredRecord
{
    const void *parent;
    const void *next;
    std::uint16_t name;
    std::int32_t index;
    float axis[4][3];
    float parentInvAxis[4][3];
};

// Serialize `live` into `file` as the fixed 0x70-byte Disk32 record followed
// by the name CString when the live handle is nonzero. The entity pointers
// are converted to validated 1-based indices through `entities`; any
// malformed pointer is reported through `fail`.
void WriteHostRecord(
    MemoryFile *file,
    const TagInfoEntityMap &entities,
    TagInfoFailFn fail,
    TagInfoStringFromHandleFn stringFromHandle,
    const TagInfoLiveRecord &live);

// Consume one record written by WriteHostRecord from `file`, validating the
// entity indices against `entities` and rebuilding full native pointers.
// The trailing name CString is restored through `handleFromString` (a zero
// mark consumes nothing).
void ReadHostRecord(
    MemoryFile *file,
    const TagInfoEntityMap &entities,
    TagInfoFailFn fail,
    TagInfoHandleFromStringFn handleFromString,
    TagInfoRestoredRecord &out);
} // namespace taginfo_save

#endif // __cplusplus
