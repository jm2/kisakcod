#pragma once

// ============================================================================
//  scr_native.hpp -- native-width script VM value-cell runtime view + frozen
//  save-image mirror split (M4).
//
//  The script VM is the largest remaining 32-bit-layout-bound runtime family
//  (docs/task.md M4 evidence row). Its value representation is the problem:
//  scr_variable.h's VariableUnion is a 4-byte union that stores LIVE HOST
//  POINTERS (vectorValue, codePosValue, stackValue) next to scalar ids. On
//  32-bit that is lossless; on 64-bit every pointer store through those
//  members truncates, so the script VM translation units cannot be built
//  native64 today. The save/load boundary (scr_readwrite.cpp
//  DoSaveEntryInternal / Scr_DoLoadEntryInternal) is already pointer-free:
//  it writes only type bytes, scalar payload dwords, and 16-bit local ids,
//  and rebuilds host pointers at load through Scr_ReadVec3 /
//  Scr_ReadCodepos / Scr_ReadStack.
//
//  This header provides portable-test-only split infrastructure following
//  the xanim_native.h precedent (ki-0aq):
//
//    - script::VariableUnionDisk / script::VariableValueDisk : the frozen
//      save-image value cell. Every field stays 32-bit so the mirror is
//      bit-identical to the legacy VariableUnion/VariableValue image on
//      every target (ONDISK_SIZE pins 4/8 bytes, matching the retail
//      static_asserts in scr_variable.h).
//
//    - script::VariableUnionNative / script::VariableValueNative /
//      script::VariableStackBufferNative : the widened runtime views. On
//      32-bit hosts they are bit-identical to the engine structs; on 64-bit
//      the pointer members grow to native width (RUNTIME_SIZE 4->8, 8->16,
//      0xC->0x10) while the save-image mirror stays frozen.
//
//    - ScriptValueDiskToNative / ScriptValueNativeToDisk : vartype-dispatched
//      conversions that never dereference anything. Value-bearing kinds
//      (UNDEFINED, POINTER, STRING, ISTRING, FLOAT, INTEGER, ANIMATION)
//      transfer exactly and round-trip bit-identically. Runtime-reconstruction
//      kinds (VECTOR, the codepos family, STACK) carry dereferenced CONTENTS
//      in the save image, not a payload token, so the conversion reports them
//      as reconstruction-required and leaves the native pointer member null
//      instead of fabricating a host pointer from save bytes.
//
//  The engine script VM translation units use the width-aware engine
//  VariableUnion (RUNTIME_SIZE 4 -> 8) and have been migrated to store live
//  host pointers through the pointer-width union members (M4 ki-n1et); the
//  save/load boundary moves only type bytes and scalar payloads, so the
//  serialized formats do not move with the widening. This header remains the
//  portable-test-only mirror that pins the frozen save-image shape against
//  the widened runtime views.
//
//  See docs/task.md M4 and src/xanim/xanim_native.h for the precedent.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include <universal/kisak_abi.h>

namespace script
{

// ----------------------------------------------------------------------------
//  Vartype -- numeric mirror of Vartype_t (src/script/scr_variable.h). The
//  values are the retail encoding written by the save path; they must not
//  drift. Declared independently so portable test TUs do not need the engine
//  header (scr_variable.h pulls in q_shared.h and the engine string list).
// ----------------------------------------------------------------------------
enum Vartype : uint32_t
{
    VAR_UNDEFINED = 0x0,
    VAR_POINTER = 0x1, // also VAR_BEGIN_REF; script object local id
    VAR_STRING = 0x2,
    VAR_ISTRING = 0x3,
    VAR_VECTOR = 0x4,
    VAR_FLOAT = 0x5, // also VAR_END_REF
    VAR_INTEGER = 0x6,
    VAR_CODEPOS = 0x7,
    VAR_PRECODEPOS = 0x8,
    VAR_FUNCTION = 0x9,
    VAR_STACK = 0xA,
    VAR_ANIMATION = 0xB,
    VAR_DEVELOPER_CODEPOS = 0xC,
    VAR_INCLUDE_CODEPOS = 0xD,
    VAR_THREAD = 0xE,
    VAR_NOTIFY_THREAD = 0xF,
    VAR_TIME_THREAD = 0x10,
    VAR_CHILD_THREAD = 0x11,
    VAR_OBJECT = 0x12,
    VAR_DEAD_ENTITY = 0x13,
    VAR_ENTITY = 0x14,
    VAR_ARRAY = 0x15,
    VAR_DEAD_THREAD = 0x16,
    VAR_COUNT = 0x17,
    VAR_THREAD_LIST = 0x18,
    VAR_ENDON_LIST = 0x19,
};

// VAR_MASK from scr_variable.h: the vartype bits inside the packed table's
// w.type word. The save path masks every type through it.
constexpr uint32_t kVarMask = 0x1F;

// ----------------------------------------------------------------------------
//  Vartype classification -- the save/load dispatch in scr_readwrite.cpp
//  splits the vartypes into exactly two families:
//
//    Value-bearing: the cell's native value IS the payload dword (a scalar,
//    float, script-object local id, string-list id, or anim index). The save
//    path writes it (or its derived text) and the load path stores the
//    rebuilt dword straight into the cell. Disk -> Native -> Disk is
//    bit-identical.
//
//    Runtime-reconstruction: the cell stores a LIVE HOST POINTER into
//    engine-owned storage (VariableStackBuffer, the vector pool, loaded
//    script code). The save path DEREFERENCES it and writes the pointed-to
//    contents (WriteVector / WriteCodepos / WriteStack); the load path
//    allocates fresh storage and returns a new pointer (Scr_ReadVec3 /
//    Scr_ReadCodepos / Scr_ReadStack). No dword in the save image identifies
//    the pointer, so a conversion must not pretend otherwise.
// ----------------------------------------------------------------------------
// The value-bearing kinds, as a table. Object-ish kinds (threads, entities,
// arrays, lists) are saved through the VAR_POINTER id path: their cell
// payload is a script object local id, so they are value-bearing too.
inline constexpr uint32_t kValueBearingVartypes[] = {
    VAR_UNDEFINED, VAR_POINTER,  VAR_STRING,     VAR_ISTRING,
    VAR_FLOAT,     VAR_INTEGER,  VAR_ANIMATION,  VAR_THREAD,
    VAR_NOTIFY_THREAD, VAR_TIME_THREAD, VAR_CHILD_THREAD, VAR_OBJECT,
    VAR_DEAD_ENTITY, VAR_ENTITY, VAR_ARRAY,      VAR_DEAD_THREAD,
    VAR_THREAD_LIST, VAR_ENDON_LIST,
};

// Table scan instead of a switch so the classification stays a flat,
// complexity-bounded predicate (the 18-case switch blew past the analyzer's
// cyclomatic limit of 10 with identical semantics).
constexpr bool IsValueBearingVartype(uint32_t type)
{
    const uint32_t masked = type & kVarMask;
    for (const uint32_t candidate : kValueBearingVartypes)
    {
        if (candidate == masked)
            return true;
    }
    return false;
}

constexpr bool IsRuntimeReconstructionVartype(uint32_t type)
{
    switch (type & kVarMask)
    {
    case VAR_VECTOR:
    case VAR_CODEPOS:
    case VAR_PRECODEPOS:
    case VAR_FUNCTION:
    case VAR_STACK:
        // Developer-only codepos kinds never reach the retail save path
        // (DoSaveEntryInternal asserts on them) but their cells hold host
        // pointers into script code, so they classify identically.
    case VAR_DEVELOPER_CODEPOS:
    case VAR_INCLUDE_CODEPOS:
        return true;
    default:
        return false;
    }
}

// ----------------------------------------------------------------------------
//  Save-image mirror -- the frozen 32-bit value cell. Bit-identical to the
//  legacy engine VariableUnion / VariableValue image (retail static_asserts
//  pin sizeof(VariableUnion) == 4 and sizeof(VariableValue) == 8 in
//  scr_variable.h); these mirrors pin the same numbers on EVERY target so
//  the save-image shape cannot drift when the runtime side widens.
// ----------------------------------------------------------------------------
union VariableUnionDisk
{
    int32_t intValue;
    float floatValue;
    uint32_t stringValue;   // string-list id (SL_)
    uint32_t pointerValue;  // script object local id / anim index
    uint32_t entityOffset;
};
ONDISK_SIZE(VariableUnionDisk, 4);
ONDISK_OFFSET(VariableUnionDisk, intValue, 0);

struct VariableValueDisk
{
    VariableUnionDisk u;
    uint32_t type; // script::Vartype
};
ONDISK_SIZE(VariableValueDisk, 8);
ONDISK_OFFSET(VariableValueDisk, u, 0);
ONDISK_OFFSET(VariableValueDisk, type, 4);

// ----------------------------------------------------------------------------
//  Native runtime views -- the widened forms the script VM migration sweep
//  adopts. On 32-bit hosts every pointer member is 4 bytes so the views are
//  bit-identical to the engine structs; on 64-bit the pointer members grow
//  to native width while the save-image mirror above stays frozen.
// ----------------------------------------------------------------------------

// VariableStackBufferNative mirrors scr_variable.h's VariableStackBuffer
// (0xC bytes on 32-bit). The runtime struct holds a live `pos` pointer; the
// save path writes only the buffer CONTENTS (WriteStack), never the struct
// image, so there is no on-disk contract for this type -- only the runtime
// widening contract.
struct VariableStackBufferNative
{
    const char *pos;
    uint16_t size;
    uint16_t bufLen;
    uint16_t localId;
    uint8_t time;
    char buf[1];
};
RUNTIME_SIZE(VariableStackBufferNative, 0xC, 0x10);
RUNTIME_OFFSET(VariableStackBufferNative, size, 0x4, 0x8);
RUNTIME_OFFSET(VariableStackBufferNative, buf, 0xB, 0xF);

union VariableUnionNative
{
    int32_t intValue;
    float floatValue;
    uint32_t stringValue;
    const float *vectorValue;               // VAR_VECTOR: live vector-pool pointer
    const char *codePosValue;               // codepos family: live script-code pointer
    uint32_t pointerValue;                  // script object local id / anim index
    VariableStackBufferNative *stackValue;  // VAR_STACK: live stack-buffer pointer
    uint32_t entityOffset;
};
RUNTIME_SIZE(VariableUnionNative, 4, 8);

struct VariableValueNative
{
    VariableUnionNative u;
    uint32_t type; // script::Vartype
};
RUNTIME_SIZE(VariableValueNative, 8, 16);
RUNTIME_OFFSET(VariableValueNative, type, 4, 8);

// ----------------------------------------------------------------------------
//  Conversions -- vartype-dispatched, never dereference any storage, and
//  never fabricate a host pointer from save bytes.
//
//  ScriptValueDiskToNative:
//    - value-bearing kinds: the payload dword transfers exactly; returns
//      true (the native cell is fully determined by the disk cell).
//    - runtime-reconstruction kinds: returns false and nulls the native
//      pointer member; the real pointer is rebuilt only by the scr_readwrite
//      load path after fresh storage is allocated.
//
//  ScriptValueNativeToDisk:
//    - value-bearing kinds: the payload dword transfers exactly; returns
//      true.
//    - runtime-reconstruction kinds with a NULL pointer member (a cleared
//      cell): zeroes the payload and returns false.
//    - runtime-reconstruction kinds with a LIVE pointer member: returns
//      false and leaves the disk cell untouched -- serializing such a cell
//      requires the content-walking save path (WriteVector/WriteCodepos/
//      WriteStack), not a payload copy.
// ----------------------------------------------------------------------------

struct ScriptValueDiskToNativeResult
{
    VariableValueNative value;
    bool complete; // false = runtime reconstruction required (see above)
};

inline ScriptValueDiskToNativeResult ScriptValueDiskToNative(const VariableValueDisk &disk)
{
    // Value-initialize so every byte of the (wider) native cell is defined
    // before the payload transfer; a union's non-active bytes must never be
    // indeterminate in a conversion helper.
    ScriptValueDiskToNativeResult result = {};
    result.value.type = disk.type;
    result.complete = IsValueBearingVartype(disk.type);
    if (result.complete)
    {
        result.value.u.intValue = disk.u.intValue;
    }
    else
    {
        // Null the pointer member explicitly: on 64-bit the union is wider
        // than the disk payload and must never inherit stale high bits.
        result.value.u.stackValue = nullptr;
    }
    return result;
}

inline bool ScriptValueNativeToDisk(const VariableValueNative &native, VariableValueDisk &disk)
{
    // Contract: a value-bearing cell converts completely (returns true); a
    // LIVE reconstruction-kind pointer refuses WITHOUT touching the disk
    // cell; everything else stores a defined zero payload. The type word is
    // only written on paths that fully define the destination, so a caller
    // reusing a populated destination never observes a half-updated cell.
    if (IsValueBearingVartype(native.type))
    {
        disk.type = native.type;
        disk.u.intValue = native.u.intValue;
        return true;
    }
    if (IsRuntimeReconstructionVartype(native.type) && native.u.stackValue != nullptr)
    {
        // Live pointer: content-walking save path required; do not fabricate
        // and do not modify the destination.
        return false;
    }
    // Cleared reconstruction-kind cell (or a pointer-free runtime kind): a
    // defined zero payload.
    disk.type = native.type;
    disk.u.intValue = 0;
    return false;
}

} // namespace script
