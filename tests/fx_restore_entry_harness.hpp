// fx_restore_entry_harness: engine-service harness for the production
// FX archive save / restore entry-point contracts (ki-458h / #125).
//
// fx_restore_entry_test.cpp drives the REAL production archive
// pipeline:
//
//   - src/EffectsCore/fx_archive.cpp: FX_Save / FX_Restore - the
//     production effect-table capture, Disk32 serialization,
//     candidate build, rollback snapshot and restore-control
//     publication pipeline.
//   - src/EffectsCore/fx_effect_table_save.cpp /
//     fx_effect_table_restore.cpp: the production effect-definition
//     table snapshot and lease machinery.
//   - src/EffectsCore/fx_archive_reader_disk32.cpp /
//     fx_archive_restore_candidate_disk32.cpp /
//     fx_archive_restore_control.cpp /
//     fx_archive_physics_batch_control.cpp: the production reader,
//     candidate builder, restore control and physics batch control.
//   - src/universal/memfile.cpp: the production MemoryFile archive
//     writer/reader behind FX_Save / FX_Restore.
//   - the stage-3 database registry tier (db_registry.cpp): FX asset
//     admission via DB_AddXAsset and the production fast-file table
//     capture / registration lookups (DB_EnumXAssets,
//     DB_FindXAssetHeader).
//
// The FxSystem / FxSystemBuffers engine instances are harness-owned
// singletons returned by the FX_GetSystem / FX_GetSystemBuffers
// service stubs with production-faithful pristine state (exclusive
// iterator, fully chained free pools, linked buffer views). The
// archive gate is a harness-owned ArchiveGateValue cell driven by the
// FX_BeginArchive / FX_EndArchive / FX_ValidateArchiveExclusiveState
// stubs with their production single-threaded semantics - the gate
// control callbacks themselves live in the unenrolled fx_system.cpp
// runtime, and the single-threaded acquisition outcome (Exclusive on
// success, previous state on release) is exactly what those stubs
// reproduce.
//
// Fail-closed protocol, identical to the database tier: Com_Error
// RECORDS the error and returns for ERR_DROP (exercising the
// guard-return cleanup paths), MyAssertHandler and Sys_Error
// terminate the process with _Exit(3), so a production assertion or
// fatal is always a hard test failure.

#ifndef FX_RESTORE_ENTRY_HARNESS_HPP
#define FX_RESTORE_ENTRY_HARNESS_HPP

#include <EffectsCore/fx_system.h>
#include <EffectsCore/fx_archive_gate_control.h>
#include <EffectsCore/fx_pool.h>
#include <EffectsCore/fx_physics_sidecar.h>
#include <universal/memfile.h>

#include <universal/q_shared.h>
#include <universal/assertive.h>

#include <algorithm>
#include <csetjmp>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdint.h>
#include <string>
#include <vector>

// The printf/error family wrappers the archive TUs call. Declared
// here, DEFINED in fx_restore_entry_test.cpp: their bodies call a
// printf function with a caller-supplied format string, and every
// production printf wrapper in this repository lives in a .cpp.
void Com_PrintError(int channel, const char *fmt, ...);
int Com_sprintf(char *dest, uint32_t size, const char *fmt, ...);
void Com_Error(errorParm_t code, const char *fmt, ...);
void Sys_Error(const char *fmt, ...);

namespace fx_restore_entry_harness
{
struct RecordedError
{
    std::string text;
};

using RecordedErrorList = std::vector<RecordedError>;

// Single-argument spellings of the production handle queries
// (fx_pool.h): binding the production template parameters once inside
// plain functions keeps every declaration initializer in this harness
// a simple call. The analyzer's comma-operator heuristic (MISRA 12.3)
// misreads a multi-argument template list inside a declaration
// initializer as a comma operator, so the constant tables below go
// through these helpers instead.
inline constexpr std::size_t FxEffectHandleAdmissionStride() noexcept
{
    return FxHandleStride<FxEffect, FX_EFFECT_LIMIT,
                          FxEffect::HANDLE_SCALE>();
}

inline std::uint16_t EncodePristineEffectHandle(FxSystem &system,
                                                std::size_t slot) noexcept
{
    return FxEncodeHandle<FxEffect, FX_EFFECT_LIMIT,
                          FxEffect::HANDLE_SCALE>(system.effects,
                                                  &system.effects[slot]);
}

inline std::uint16_t EncodeHarnessEffectHandle(
    const FxSystem &system, const FxEffect *effect) noexcept
{
    return FxEncodeHandle<FxEffect, FX_EFFECT_LIMIT,
                          FxEffect::HANDLE_SCALE>(system.effects, effect);
}

// Mirrors the file-scope FxPoolAllocationStates of fx_system.cpp
// (the per-slot pool allocation sidecar state); the mangled link
// names of the pool-state helpers depend on this exact global-scope
// type name and layout.
struct FxPoolAllocationStates
{
    FxPoolAllocationState<MAX_ELEMS> elems;
    FxPoolAllocationState<MAX_TRAILS> trails;
    FxPoolAllocationState<MAX_TRAIL_ELEMS> trailElems;
};

struct HarnessState
{
    RecordedErrorList errors;
    RecordedErrorList printErrors;
    // The harness-owned FX engine singletons behind the service
    // stubs. FxSystemBuffers is ~0x47480 bytes: static storage.
    FxSystem system;
    FxSystemBuffers buffers;
    // The archive gate cell behind the FX_GetArchiveGate stub.
    volatile std::int32_t archiveGate;
    std::uint32_t iteratorGeneration;
    // The pool-allocation sidecar state behind
    // FX_GetPoolAllocationStates (production keeps one per system
    // slot; this harness hosts exactly one slot).
    FxPoolAllocationStates poolStates;
    // The effect-kill gate cell behind FX_GetEffectKillGate.
    volatile std::int32_t effectKillGate;
    // Per-effect owner-admission words behind
    // FX_GetEffectOwnerAdmissionState (production layout: one word
    // per FxEffectHandleAdmissionStride() handles).
    static constexpr std::size_t kEffectAdmissionStride =
        FxEffectHandleAdmissionStride();
    static constexpr std::size_t kEffectAdmissionWordCount =
        FX_EFFECT_LIMIT / kEffectAdmissionStride;
    std::int32_t effectOwnerAdmissionBlocked[kEffectAdmissionWordCount];
    // The live physics body sidecar behind FX_GetPhysicsBodySidecar
    // (production: one per system slot).
    fx::physics::BodySidecar physicsSidecar;
    // Coordination / observability counters.
    std::uint32_t beginArchiveCalls;
    std::uint32_t beginArchiveSuccesses;
    std::uint32_t endArchiveCalls;
    std::uint32_t errorCleanupCalls;
    std::uint32_t forEachEffectDefCalls;
    std::uint32_t physicsSidecarCalls;
    // Fail-closed observability: a nonzero count means the archive
    // pipeline tried to drive a native physics body transaction or
    // destroy a physics body. The harness hosts no ODE world, so the
    // Phys_* stubs refuse every transaction and record it here.
    std::uint32_t physicsRejectionCalls;
    // Failure injection: the next FX_BeginArchive returns false
    // (exclusive-ownership refusal path).
    bool failNextBeginArchive;
};

inline HarnessState &State()
{
    static HarnessState state;
    return state;
}

// The production FxSystem singleton prerequisites for archive
// ownership: linked buffer views, exclusive iterator, zeroed live
// counts, fully chained free pools (the classic production free-list
// encoding: firstFree* = 0 head, per-slot nextFree = index + 1, tail
// = -1), and the camera marked invalid. The setup phases below are
// behavior-preserving extractions; InitPristineFxSystem runs them in
// the original order.

inline void LinkPristineSystemBuffers(FxSystem &system,
                                      FxSystemBuffers &buffers)
{
    system.elems = buffers.elems;
    system.effects = buffers.effects;
    system.trails = buffers.trails;
    system.trailElems = buffers.trailElems;
    system.visState = buffers.visState;
    system.deferredElems = buffers.deferredElems;
    system.visStateBufferRead = &buffers.visState[0];
    system.visStateBufferWrite = &buffers.visState[1];
}

inline void ChainPristineFreeLists(FxSystem &system,
                                   FxSystemBuffers &buffers)
{
    system.firstFreeElem = 0;
    for (std::size_t i = 0; i < MAX_ELEMS; ++i)
        buffers.elems[i].nextFree = static_cast<std::int32_t>(i) + 1;
    buffers.elems[MAX_ELEMS - 1].nextFree = -1;

    system.firstFreeTrail = 0;
    for (std::size_t i = 0; i < MAX_TRAILS; ++i)
        buffers.trails[i].nextFree = static_cast<std::int32_t>(i) + 1;
    buffers.trails[MAX_TRAILS - 1].nextFree = -1;

    system.firstFreeTrailElem = 0;
    for (std::size_t i = 0; i < MAX_TRAIL_ELEMS; ++i)
        buffers.trailElems[i].nextFree = static_cast<std::int32_t>(i) + 1;
    buffers.trailElems[MAX_TRAIL_ELEMS - 1].nextFree = -1;
}

inline void InitPristineSystemRuntimeState(FxSystem &system)
{
    system.iteratorCount = -1;
    system.isArchiving = 0;
    system.isInitialized = 1;
    system.localClientNum = 0;
    system.activeSpotLightBoltDobj = -1;
    // Production pre-first-view state: no frame has been drawn, so the
    // cameras stay canonical-invalid (all-zero) and msecDraw carries
    // the exact -1 sentinel the archive validators require
    // (FX_AreArchiveCamerasReady / ValidateArchiveSystemState).
    system.msecNow = 0;
    system.msecDraw = -1;
}

// Production initialization encodes the identity handle permutation
// (the free-slot inventory doubles as active-ring storage); the
// archive graph validator decodes every entry.
inline void InitPristineEffectHandles(FxSystem &system)
{
    for (std::size_t i = 0; i < FX_EFFECT_LIMIT; ++i)
    {
        system.allEffectHandles[i] =
            EncodePristineEffectHandle(system, i);
    }
}

inline void InitPristineArchiveGates(HarnessState &state)
{
    state.archiveGate = static_cast<std::int32_t>(
        fx::archive::ArchiveGateValue::Open);
    state.iteratorGeneration = 0;
    state.effectKillGate = 0;
    memset(state.effectOwnerAdmissionBlocked,
           0,
           sizeof(state.effectOwnerAdmissionBlocked));
    // The production runtime initializes each system slot's pool
    // allocation sidecar through FxPoolResetAllocationState
    // (initialized-and-empty); a plain memset would leave initialized
    // = false and the archive graph validator would reject the
    // pristine system outright.
    FxPoolResetAllocationState(&state.poolStates.elems);
    FxPoolResetAllocationState(&state.poolStates.trails);
    FxPoolResetAllocationState(&state.poolStates.trailElems);
}

inline void InitPristineSpotLightState(FxSystem &system)
{
    Sys_AtomicStore(&system.activeSpotLightEffectCount, 0);
    Sys_AtomicStore(&system.activeSpotLightElemCount, 0);
    system.activeSpotLightEffectHandle = FX_INVALID_HANDLE;
    system.activeSpotLightElemHandle = FX_INVALID_HANDLE;
    Sys_AtomicStore(&system.gfxCloudCount, 0);
    Sys_AtomicStore(&system.visState[0].blockerCount, 0);
    Sys_AtomicStore(&system.visState[1].blockerCount, 0);
}

// The production runtime initializes each system slot's physics
// sidecar as initialized-and-empty; ResetEmpty on a default
// (uninitialized) sidecar is exactly that state. Reconstruct the
// sidecar through its default constructor + production ResetEmpty
// rather than memset: BodySidecar is a real class.
inline void InitPristinePhysicsSidecar(HarnessState &state)
{
    state.physicsSidecar.~BodySidecar();
    new (&state.physicsSidecar) fx::physics::BodySidecar();
    if (fx::physics::ResetEmpty(&state.physicsSidecar)
        != fx::physics::SidecarStatus::Success)
    {
        fprintf(stderr, "harness: sidecar ResetEmpty failed\n");
        std::abort();
    }
    state.physicsRejectionCalls = 0;
}

inline void InitPristineFxSystem()
{
    HarnessState &state = State();
    memset(&state.system, 0, sizeof(state.system));
    memset(&state.buffers, 0, sizeof(state.buffers));

    LinkPristineSystemBuffers(state.system, state.buffers);
    ChainPristineFreeLists(state.system, state.buffers);
    InitPristineSystemRuntimeState(state.system);
    InitPristineEffectHandles(state.system);
    InitPristineArchiveGates(state);
    InitPristineSpotLightState(state.system);
    InitPristinePhysicsSidecar(state);
}

inline void ResetHarness()
{
    HarnessState &state = State();
    state.errors.clear();
    state.printErrors.clear();
    state.beginArchiveCalls = 0;
    state.beginArchiveSuccesses = 0;
    state.endArchiveCalls = 0;
    state.errorCleanupCalls = 0;
    state.forEachEffectDefCalls = 0;
    state.physicsSidecarCalls = 0;
    state.failNextBeginArchive = false;
    InitPristineFxSystem();
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

// A growable MemoryFile backing store with the production archive
// layout (segments disabled, no compression - the same mode the
// save-game path uses for FX archives).
struct ArchiveImage
{
    std::vector<unsigned char> bytes;
    MemoryFile memFile;

    void InitForSave()
    {
        bytes.resize(8 * 1024 * 1024);
        memset(bytes.data(), 0, bytes.size());
        MemFile_InitForWriting(&memFile,
                               static_cast<int>(bytes.size()),
                               bytes.data(),
                               false,
                               false);
    }

    void InitForRead(const unsigned char *imageBytes, int byteCount)
    {
        MemFile_InitForReading(&memFile,
                               byteCount,
                               const_cast<unsigned char *>(imageBytes),
                               false);
    }
};

// The production unwinding protocol (identical to the database
// tier): production Com_Error(ERR_DROP) longjmps; the default harness
// protocol is record-and-return for the guard-return sites, and a
// case that drives a production-unwound path arms the jump target.
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
} // namespace fx_restore_entry_harness

// Shared harness accessors defined at global scope in
// fx_restore_entry_stubs.cpp and consumed by
// fx_restore_entry_graph_stubs.cpp: mirrors of the production
// fx_system.cpp slot lookups, all reducing to the single harness
// slot.
FxPoolAllocationStates *FX_GetPoolAllocationStates(
    const FxSystem *const system) noexcept;
volatile std::int32_t *FX_GetArchiveGate(
    const FxSystem *const system) noexcept;
volatile std::int32_t *FX_GetEffectOwnerAdmissionState(
    const FxSystem *const system,
    const FxEffect *const effect) noexcept;

#endif // FX_RESTORE_ENTRY_HARNESS_HPP
