// xmodel_loader_entry_harness: engine-service harness for the
// production-loader entry-point contracts (ki-okmr / #124).
//
// xmodel_loader_entry_test.cpp drives the REAL XModelLoadFile from
// src/xanim/xmodel_load_obj.cpp — the production parse, its cursor
// enrollment, its checked material second pass and every early-exit
// cleanup path — over controlled xmodel / xmodelparts / xmodelsurfs
// fixtures. The loader TU compiles only on the win32-x86 platform
// (DirectX / Miles / ODE header web; MSVC decompiled dialect), so this
// target is built exclusively by the Windows x86 CI leg; the portable
// 64-bit legs never configure it.
//
// The harness supplies exactly the services the loader TU references
// but a unit test cannot host: an in-memory file system behind
// FS_ReadFile / FS_FreeFile, an in-memory hunk data cache behind
// Hunk_FindDataForFile / Hunk_SetDataForFile (the cold/warm precache
// seam), and no-op or capture stubs for the renderer/physics
// endpoints. Every engine-service endpoint is defined at GLOBAL scope
// with its production signature: the loader TU's unqualified
// references resolve against the production header declarations, so a
// namespaced definition would mangle differently and strand the
// win32-x86 link. XModelPartsLoadFile and R_XModelSurfsLoadFile need
// no stand-in: their production implementations live in the loader TU
// itself, which drives the REAL nested parts parse. Since ki-458h
// stage 2 the tests also drive the real XAnim entry points from the
// same production TU — XAnimLoadFile, XModelPiecesLoadFile and
// XModelPiecesPrecache — over controlled xanim / xmodelpieces
// fixtures (BuildEntryAnim* / BuildEntryPieces* below).

#ifndef XMODEL_LOADER_ENTRY_HARNESS_HPP
#define XMODEL_LOADER_ENTRY_HARNESS_HPP

#include <xanim/xmodel.h>
#include <xanim/buf_cursor.hpp>

#include <qcommon/com_error.h>
#include <universal/com_files.h>
#include <universal/com_memory.h>
#include <universal/assertive.h>
#include <universal/q_shared.h>
#include <script/scr_stringlist.h>

#include "xmodel_cursor_test_support.hpp"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdint.h>
#include <map>
#include <string>
#include <vector>

struct GenericAabbTreeOptions;

// ---------------------------------------------------------------------------
// The printf-family wrappers the loader TU calls — Com_PrintError,
// Com_sprintf, Com_Error, and the production assert handler
// MyAssertHandler. Declared at global scope (matching their production
// header declarations) and DEFINED in xmodel_loader_entry_test.cpp:
// their bodies necessarily call a printf function with a
// caller-supplied format string, and every production printf wrapper
// in this repository lives in a .cpp (common.cpp, r_warn.cpp) — a
// header body re-triggers the CWE-134 lexical pattern. The remaining
// engine-service endpoints are defined inline below, after the
// harness namespace whose state they share.
// ---------------------------------------------------------------------------
void Com_PrintError(int channel, const char *fmt, ...);
int Com_sprintf(char *dest, uint32_t size, const char *fmt, ...);
void Com_Error(errorParm_t code, const char *fmt, ...);

namespace xmodel_loader_entry_harness
{
// The fixture byte builder shared with the portable cursor suites.
using ByteWriterFixture = xmodel_cursor_test_support::ByteWriter;

// The harness containers are spelled through these aliases: it keeps
// the template argument lists (and the `>::const_iterator` nested
// names) out of the individual use sites, which several lexically
// driven C analyzers misread as comma-operator expressions (MISRA
// 12.3 false positives) on this header. The hunk cache additionally
// stores its opaque void* records as uintptr_t (an exact pointer-width
// integer on every supported platform); reinterpret_cast round-trips
// the record addresses exactly.
//
// The hunk cache is a flat vector of records rather than a
// two-argument map: Codacy's MISRA 12.3 pass re-spells a member's
// type at its declaration site (three consecutive runs flagged the
// hunkData member through two different value-type spellings), so
// the member must expand to a comma-free spelling. A single-argument
// container over a key/pointer record keeps the exact set/get/clear
// surface the loader endpoints need (unique "<type>:<name>" keys,
// operator[]-style overwrite on re-set, exact-match lookup).
struct HarnessHunkRecord
{
    std::string key;
    uintptr_t data;
};

using HarnessHunkMap = std::vector<HarnessHunkRecord>;
using HarnessFileMap = std::map<std::string, std::vector<unsigned char> >;

// The hunk-cache surface the production loader endpoints use. Set
// overwrites an existing record's data in place (std::map operator[]
// semantics); Get is an exact-match lookup returning 0 when the key
// is absent (std::map find + end-check semantics).
inline void HunkCacheSet(HarnessHunkMap &cache, const std::string &key,
                         uintptr_t data)
{
    for (HarnessHunkRecord &record : cache)
    {
        if (record.key == key)
        {
            record.data = data;
            return;
        }
    }
    HarnessHunkRecord record;
    record.key = key;
    record.data = data;
    cache.push_back(record);
}

inline uintptr_t HunkCacheGet(const HarnessHunkMap &cache, const std::string &key)
{
    for (const HarnessHunkRecord &record : cache)
    {
        if (record.key == key)
            return record.data;
    }
    return 0;
}

// Declared BEFORE the RecordedErrorList alias that embeds it: MSVC
// rejects an undeclared template argument at the alias point, and
// this header is parsed only by the win32-x86 leg (MSVC), so the
// portable suite cannot catch an ordering slip here.
struct RecordedError
{
    int channel;
    std::string text;
};

using RecordedErrorList = std::vector<RecordedError>;

struct HarnessState
{
    HarnessFileMap files;
    HarnessHunkMap hunkData;
    RecordedErrorList errors;
    std::vector<std::string> materialRegistrations;
    // Opaque engine records: no complete Material/PhysPreset/XModel
    // type is reachable from the harness include closure, so the stubs
    // hand out stable pointers into zero-initialized aligned storage
    // on this singleton instead of default-constructing engine types.
    alignas(16) unsigned char materialStorage[128] = {};
    alignas(16) unsigned char physPresetStorage[128] = {};
    alignas(16) unsigned char modelStorage[128] = {};
    int fsReads = 0;
    int fsFrees = 0;
    int partsFileReads = 0;
    int physPresetCalls = 0;
    int collMapCalls = 0;
    // Transient va() buffers, fixture-owned: the production va()
    // (common.cpp) rotates eight 1024-byte static buffers; the harness
    // reproduces the same rotation, truncation and termination from
    // this per-test storage instead of function-local statics (see
    // the va() definition below). ResetHarness() restarts the
    // rotation phase so every test starts like a fresh process.
    char vaBuffers[8][1024] = {};
    int vaIndex = 0;
};

// State() and ResetHarness() are defined in the single TU that
// includes this header (xmodel_loader_entry_test.cpp); the harness
// state is namespace-scope storage there instead of a function-local
// static.
HarnessState &State();
void ResetHarness();

// In-header readers over the recorded errors: the tests assert through
// these, so the RecordedError fields and the errors list are exercised
// by the harness itself rather than only by the including test TU.
inline int ErrorCount()
{
    return static_cast<int>(State().errors.size());
}

// True when some recorded error was printed on `channel` and carries
// `substring` in its formatted text.
inline bool ErrorsContain(int channel, const char *substring)
{
    const RecordedErrorList &errors = State().errors;
    for (size_t i = 0; i < errors.size(); ++i)
    {
        if (errors[i].channel == channel
            && errors[i].text.find(substring) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

inline int I_strnicmpHarness(const char *s0, const char *s1, int n)
{
    for (int i = 0; i < n; ++i)
    {
        const unsigned char a = static_cast<unsigned char>(s0[i]);
        const unsigned char b = static_cast<unsigned char>(s1[i]);
        if (std::tolower(a) != std::tolower(b))
            return a < b ? -1 : 1;
        if (!a)
            return 0;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// File system and hunk-cache services (the cold/warm precache seam).
// RegisterFile and the Alloc callbacks are harness-internal; the FS_*
// and Hunk_* endpoints the loader TU calls are defined at global scope
// below.
// ---------------------------------------------------------------------------

inline void RegisterFile(const char *qpath, const std::vector<unsigned char> &bytes)
{
    State().files[qpath] = bytes;
}

// The loader's Alloc/AllocColl callbacks mirror hunk allocation
// semantics: the engine hands out zeroed memory, and several loader
// paths (e.g. XModelSurfs partBits) OR into freshly allocated storage
// without initializing it first.
inline void *__cdecl HarnessAlloc(int size)
{
    return std::calloc(1, static_cast<size_t>(size));
}

inline void *__cdecl HarnessAllocColl(int size)
{
    return std::calloc(1, static_cast<size_t>(size));
}

// ---------------------------------------------------------------------------
// Controlled fixtures built to the production file layouts.
//
// One valid model: two populated LODs ("lod_a" with two surfaces,
// "lod_b" with one), two bones, one rigid surface shape per xmodelsurfs
// entry, no collision data, physics preset "phys/x". The surface body
// is the minimal rigid shape XModelReadSurface accepts: a single rigid
// vert list on bone 0, three vertices on an orthonormal basis (the
// normal/tangent/binormal orthogonality assert is live in Debug), one
// triangle.
// ---------------------------------------------------------------------------

inline void PushRigidSurfaceBody(ByteWriterFixture &w)
{
    w.Push8(0);      // tileMode
    w.Push16(0);     // reserved
    w.Push16(3);     // vertCount
    w.Push16(1);     // triCount
    w.Push16(3);     // vertList[0].vertCount
    w.Push16(0);     // vertList[0].bone (ReadBone: u16)
    w.Push16(0);     // vert list terminator
    for (int v = 0; v < 3; ++v)
    {
        w.PushFloat(0.0f); w.PushFloat(0.0f); w.PushFloat(1.0f);  // normal
        w.Push8(0); w.Push8(0); w.Push8(0); w.Push8(0);           // bgra color
        w.PushFloat(0.0f); w.PushFloat(0.0f);                     // texCoord
        w.PushFloat(0.0f); w.PushFloat(1.0f); w.PushFloat(0.0f);  // binormal
        w.PushFloat(1.0f); w.PushFloat(0.0f); w.PushFloat(0.0f);  // tangent
        w.PushFloat(0.0f); w.PushFloat(0.0f); w.PushFloat(0.0f);  // offset
    }
    w.Push16(0); w.Push16(1); w.Push16(2);  // one triangle
}

inline void PushModelConfigAndCollision(ByteWriterFixture &w)
{
    w.Push16(25);           // config version
    w.Push8(0);             // flags
    w.PushFloat(-1.0f); w.PushFloat(-1.0f); w.PushFloat(-1.0f);
    w.PushFloat(1.0f); w.PushFloat(1.0f); w.PushFloat(1.0f);
    w.PushString("phys/x");
    w.PushFloat(0.0f); w.PushString("lod_a");
    w.PushFloat(150.0f); w.PushString("lod_b");
    w.PushFloat(300.0f); w.PushString("");
    w.PushFloat(600.0f); w.PushString("");
    w.Push32(0);            // collLod
    w.Push32(0);            // numCollSurfs
}

inline void PushModelLodTable(ByteWriterFixture &w)
{
    w.Push16(2);
    w.PushString("mat_first_a");
    w.PushString("mat_first_b");
    w.Push16(1);
    w.PushString("mat_first_c");
}

inline void PushModelBoneInfos(ByteWriterFixture &w, int numBones)
{
    for (int i = 0; i < numBones; ++i)
    {
        w.PushFloat(0.25f); w.PushFloat(0.5f); w.PushFloat(0.75f);
        w.PushFloat(1.25f); w.PushFloat(1.5f); w.PushFloat(1.75f);
    }
}

inline ByteWriterFixture BuildEntrySurfsFile(int numsurfs)
{
    ByteWriterFixture w;
    w.Push16(25);
    w.Push16(static_cast<uint16_t>(numsurfs));
    for (int s = 0; s < numsurfs; ++s)
        PushRigidSurfaceBody(w);
    return w;
}

inline ByteWriterFixture BuildEntryPartsFile()
{
    ByteWriterFixture w;
    w.Push16(25);
    w.Push16(1);            // numChildBones
    w.Push16(1);            // numRootBones
    // Child parent index. The production XModelPartsLoadFile asserts
    // index < i per child bone (i starts at numRootBones), so the only
    // valid parent for the single child is root bone 0.
    w.Push8(0);
    w.PushFloat(0.0f); w.PushFloat(0.0f); w.PushFloat(0.0f);
    w.Push16(0); w.Push16(0); w.Push16(0); w.Push16(0x7FFF);
    w.PushString("tag_root");
    w.PushString("tag_child");
    w.Push8(0); w.Push8(1); // partClassification
    w.Push8(1);             // useBones
    return w;
}

inline ByteWriterFixture BuildEntryModelFile()
{
    ByteWriterFixture w;
    PushModelConfigAndCollision(w);
    PushModelLodTable(w);
    PushModelBoneInfos(w, 2);
    return w;
}

// Registers the full valid model corpus under `name`. The parts file
// is served under the name the production loader resolves:
// XModelLoadFile passes (const char *)&config — config.entries[0]
// .filename, "lod_a" for this fixture — to XModelPartsPrecache, which
// keys the hunk cache with it and reads "xmodelparts/lod_a" from the
// file system. The surfs files follow the same entries[i].filename
// convention they always did.
inline void RegisterValidModel(const char *name)
{
    std::string path;
    path = std::string("xmodel/") + name;
    RegisterFile(path.c_str(), BuildEntryModelFile().bytes);
    RegisterFile("xmodelparts/lod_a", BuildEntryPartsFile().bytes);
    RegisterFile("xmodelsurfs/lod_a", BuildEntrySurfsFile(2).bytes);
    RegisterFile("xmodelsurfs/lod_b", BuildEntrySurfsFile(1).bytes);
}

// --- XAnim entry fixtures (ki-458h stage 2) ------------------------------
//
// XAnimLoadFile (version 17) disk layout, byte-exact because the
// entry's closing assert (buf + fileSize == pos) is live in Debug
// builds: i16 version, u16 numframes, i16 numBones, u8 partFlags
// (bit0 bLoop, bit1 bDelta), u8 assetType, i16 framerate; when bDelta
// a delta-part body (u16 quat-index count + payload, u16 trans-index
// count + mins); then ceil(numBones/8) bytes of flip bits, the same
// count of simple-quat bits, numBones NUL-terminated part names, per
// bone a u16 quat-index count + payload and a u16 trans-index count +
// payload (smallTrans byte + 6 floats + data bytes), then the
// note-track block (u8 track count; per track a NUL-terminated name
// and a u16 frame). The production part-type enum (PART_TYPE_NO_QUAT
// .. PART_TYPE_ALL, values 0..9 in xanim_load_obj.cpp) indexes
// XAnimParts::boneCount; it is file-private, so tests use local
// constants documented against the production source.
inline ByteWriterFixture BuildEntryAnimHeader(uint16_t numframes, int16_t numBones,
                                              uint8_t partFlags, uint8_t assetType,
                                              int16_t framerate)
{
    ByteWriterFixture w;
    w.Push16(17);                                    // version 17
    w.Push16(numframes);                             // numframes (loop adds 1)
    w.Push16(static_cast<uint16_t>(numBones));
    w.Push8(partFlags);                              // bit0 bLoop, bit1 bDelta
    w.Push8(assetType);
    w.Push16(static_cast<uint16_t>(framerate));
    return w;
}

// Zero-bone single-frame anim: the smallest fixture that still reaches
// the note-track reader (one synthesized "end" entry) and the full
// hunk-user teardown.
inline ByteWriterFixture BuildEntryAnimMinimalFile()
{
    ByteWriterFixture w = BuildEntryAnimHeader(1, 0, 0, 0, 30);
    w.Push8(0);  // zero note tracks
    return w;
}

// One bone with no quat data and no translation data, plus one note
// track. The simple-quat bitmap bit MUST be set for bone 0: a zero
// quat-index count with a cleared bitmap bit trips the live
// MyAssertHandler in XAnimGetPartQuatType.
inline ByteWriterFixture BuildEntryAnimSingleBoneFile()
{
    ByteWriterFixture w = BuildEntryAnimHeader(1, 1, 0, 0, 30);
    w.Push8(0x00);   // flip bitmap: no flips
    w.Push8(0x01);   // simple bitmap: bone 0 simple (required for null quat)
    w.PushString("tag_root");
    w.Push16(0);     // bone 0: no quat indices
    w.Push16(0);     // bone 0: no trans indices
    w.Push8(1);      // one note track
    w.PushString("fire");
    w.Push16(0);     // at frame 0 (parts->numframes is 0, so time 0.0)
    return w;
}

// One bone with a two-index simple (half) quaternion part and a
// two-index small translation part. numframes 2 makes the loop-frame
// count 2, so both index counts self-generate identity tables (no
// file bytes) and the payloads are consumed directly: two ConsumeQuat2
// pairs, then the LoadTrans small-trans body (6 floats + 3 bytes per
// frame through the REAL Vec3Scale).
inline ByteWriterFixture BuildEntryAnimQuatTransFile()
{
    ByteWriterFixture w = BuildEntryAnimHeader(2, 1, 0, 0, 30);
    w.Push8(0x00);   // flip bitmap: no flips
    w.Push8(0x01);   // simple bitmap: bone 0 simple -> half quat
    w.PushString("tag_root");
    w.Push16(2);     // quat indices == loop frames: identity self-generate
    w.Push16(16384);
    w.Push16(8192);  // two ConsumeQuat2 x components (w components derived)
    w.Push16(2);     // trans indices == loop frames: identity self-generate
    w.Push8(1);      // smallTrans
    w.PushFloat(1.0f); w.PushFloat(2.0f); w.PushFloat(3.0f);       // mins
    w.PushFloat(255.0f); w.PushFloat(255.0f); w.PushFloat(255.0f); // size (Vec3Scale by 1/255)
    w.Push8(0xAA); w.Push8(0xBB); w.Push8(0xCC);   // frame 0 bytes
    w.Push8(0x11); w.Push8(0x22); w.Push8(0x33);   // frame 1 bytes
    w.Push8(0);      // zero note tracks
    return w;
}

// Three bones covering four part-type buckets at once: bone 0 no
// quat + no trans (NO_QUAT / NO_TRANS), bone 1 simple two-index quat
// + small trans (HALF_QUAT / SMALL_TRANS), bone 2 full two-index quat
// (simple bit clear: ConsumeQuat with a derived fourth component)
// + single-index trans (TRANS_NO_SIZE, mins only). Exercises the
// production quat/trans sort and the per-type data-emission loops.
inline ByteWriterFixture BuildEntryAnimMixedTypesFile()
{
    ByteWriterFixture w = BuildEntryAnimHeader(2, 3, 0, 0, 30);
    w.Push8(0x00);   // flip bitmap: no flips
    w.Push8(0x03);   // simple bitmap: bones 0-1 simple, bone 2 full quat
    w.PushString("b0");
    w.PushString("b1");
    w.PushString("b2");
    // bone 0: null quat, null trans
    w.Push16(0);
    w.Push16(0);
    // bone 1: half quat (2 frames) + small trans (2 frames)
    w.Push16(2);
    w.Push16(16384);
    w.Push16(8192);
    w.Push16(2);
    w.Push8(1);      // smallTrans
    w.PushFloat(1.0f); w.PushFloat(2.0f); w.PushFloat(3.0f);
    w.PushFloat(255.0f); w.PushFloat(255.0f); w.PushFloat(255.0f);
    w.Push8(0x0A); w.Push8(0x0B); w.Push8(0x0C);
    w.Push8(0x1A); w.Push8(0x1B); w.Push8(0x1C);
    // bone 2: full quat (2 frames, 3 components each) + trans-no-size
    w.Push16(2);
    w.Push16(16384); w.Push16(0); w.Push16(0);
    w.Push16(8192);  w.Push16(0); w.Push16(0);
    w.Push16(1);     // single trans index: mins only, no frames
    w.PushFloat(4.0f); w.PushFloat(5.0f); w.PushFloat(6.0f);
    w.Push8(0);      // zero note tracks
    return w;
}

// Delta anim (partFlags bit1): single-loop-frame delta quat (ConsumeQuat2)
// and delta trans (mins only), no bones.
inline ByteWriterFixture BuildEntryAnimDeltaFile()
{
    ByteWriterFixture w = BuildEntryAnimHeader(1, 0, 2, 0, 30);
    w.Push16(1);     // delta quat: single index -> size 0
    w.Push16(16384); // ConsumeQuat2 x component
    w.Push16(1);     // delta trans: single index -> mins only
    w.PushFloat(7.0f); w.PushFloat(8.0f); w.PushFloat(9.0f);
    w.Push8(0);      // zero note tracks
    return w;
}

// Looping anim (partFlags bit0): numframes 2 plus the synthesized loop
// frame makes 3 total frames; parts->numframes stays 2 and the
// frequency becomes framerate / numframes.
inline ByteWriterFixture BuildEntryAnimLoopFile()
{
    ByteWriterFixture w = BuildEntryAnimHeader(2, 0, 1, 0, 30);
    w.Push8(0);      // zero note tracks
    return w;
}

// XModelPieces version-1 file: u16 version, u16 numpieces, then per
// piece a NUL-terminated name and 3 offset floats. Piece models
// resolve through the stubbed R_RegisterModel, so no xmodel file is
// needed behind the piece name.
inline ByteWriterFixture BuildEntryPiecesFile()
{
    ByteWriterFixture w;
    w.Push16(1);     // version
    w.Push16(1);     // numpieces
    w.PushString("pc_body");
    w.PushFloat(1.5f); w.PushFloat(2.5f); w.PushFloat(3.5f);
    return w;
}

}  // namespace xmodel_loader_entry_harness

// ---------------------------------------------------------------------------
// Global-scope engine-service definitions.
//
// The loader TU (xmodel_load_obj.cpp) references every function below
// unqualified through the production headers; its references resolve
// to GLOBAL symbols, so these definitions must live at global scope
// with the production signatures — a namespaced definition would
// mangle differently and strand the win32-x86 link. The bodies live
// in this header (included by exactly one TU) as STRONG (non-inline)
// definitions: the production OBJs carry plain external references,
// and an inline function the including TU never calls is never
// emitted, which strands the link (LNK2019) exactly like a missing
// definition. The printf-family wrappers and the production assert
// handler (MyAssertHandler) are the exception: their bodies format
// caller-supplied format strings, so they are defined in the test TU
// like the production printf wrappers they mirror. Harness state and
// the opaque Material/PhysPreset storage ride on the harness
// singleton.
// ---------------------------------------------------------------------------

void track_static_alloc_internal(void *ptr, int size, const char *name, int type)
{
    (void)ptr;
    (void)size;
    (void)name;
    (void)type;
}

uint32_t SL_GetStringOfSize(const char *str, uint32_t user, uint32_t len, int type)
{
    (void)str;
    (void)user;
    (void)len;
    (void)type;
    return 0;
}

int __cdecl BuildAabbTree(const GenericAabbTreeOptions *options)
{
    (void)options;
    return 0;
}

int FS_ReadFile(const char *qpath, void **buffer)
{
    auto &state = xmodel_loader_entry_harness::State();
    const auto fileIt = state.files.find(qpath);
    if (fileIt == state.files.end())
        return -1;
    const std::vector<unsigned char> &bytes = fileIt->second;
    unsigned char *copy = new unsigned char[bytes.size() + 1];
    // Bounded, iterator-based copy: same bytes, same terminator, with
    // the destination capacity visible in the expression itself.
    std::copy(bytes.begin(), bytes.end(), copy);
    copy[bytes.size()] = 0;
    *buffer = copy;
    ++state.fsReads;
    // Real service-boundary instrumentation: the tests distinguish a
    // cold nested parts load from a warm hunk-cache hit by counting
    // actual xmodelparts file reads here, instead of observing loader
    // internals.
    if (std::strncmp(qpath, "xmodelparts/", 12) == 0)
        ++state.partsFileReads;
    return static_cast<int>(bytes.size());
}

void FS_FreeFile(char *buffer)
{
    delete[] reinterpret_cast<unsigned char *>(buffer);
    ++xmodel_loader_entry_harness::State().fsFrees;
}

uint32_t *Hunk_AllocateTempMemory(int size, const char *name)
{
    (void)name;
    return static_cast<uint32_t *>(std::malloc(static_cast<size_t>(size)));
}

void Hunk_FreeTempMemory(char *buf)
{
    std::free(buf);
}

void *Hunk_FindDataForFile(int type, const char *name)
{
    // Retail hunk-data cache keys on the file type AND the name: the
    // xmodelparts and xmodelsurfs records of one LOD share a bare name
    // ("lod_a"), so a name-only key collides the two records and hands
    // the surfs lookup a parts header — observed as a staging
    // XModelSurfs whose surfs pointer was the parts numBones/
    // numRootBones word (0x102), faulting on the first surface read.
    // Compose the type into the key to keep the namespaces apart.
    const uintptr_t record = xmodel_loader_entry_harness::HunkCacheGet(
        xmodel_loader_entry_harness::State().hunkData,
        std::to_string(type) + ":" + name);
    return reinterpret_cast<void *>(record);
}

char *Hunk_SetDataForFile(int type, const char *name, void *data,
                                 void *(__cdecl *alloc)(int))
{
    (void)alloc;
    xmodel_loader_entry_harness::HunkCacheSet(
        xmodel_loader_entry_harness::State().hunkData,
        std::to_string(type) + ":" + name, reinterpret_cast<uintptr_t>(data));
    return static_cast<char *>(data);
}

Material *__cdecl Material_RegisterHandle(const char *name, int imageTrack)
{
    (void)imageTrack;
    xmodel_loader_entry_harness::State().materialRegistrations.push_back(name);
    return reinterpret_cast<Material *>(
        &xmodel_loader_entry_harness::State().materialStorage[0]);
}

struct PhysPreset *__cdecl PhysPresetPrecache(const char *name,
                                                     void *(__cdecl *Alloc)(int))
{
    (void)name;
    (void)Alloc;
    ++xmodel_loader_entry_harness::State().physPresetCalls;
    return reinterpret_cast<PhysPreset *>(
        &xmodel_loader_entry_harness::State().physPresetStorage[0]);
}

void ProfLoad_Begin(const char *label)
{
    (void)label;
}

void ProfLoad_End()
{
}

void R_GetXModelBounds(XModel *model, const float (*axes)[3], float *mins, float *maxs)
{
    (void)model;
    (void)axes;
    mins[0] = mins[1] = mins[2] = -1.0f;
    maxs[0] = maxs[1] = maxs[2] = 1.0f;
}

struct PhysGeomList *__cdecl XModel_LoadPhysicsCollMap(const char *name,
                                                              void *(__cdecl *Alloc)(int))
{
    (void)name;
    (void)Alloc;
    ++xmodel_loader_entry_harness::State().collMapCalls;
    return 0;
}

int XModelNumBones(const XModel *model)
{
    return model->numBones;
}

bool Com_IsLegacyXModelName(const char *name)
{
    return !xmodel_loader_entry_harness::I_strnicmpHarness(name, "xmodel", 6)
           && (name[6] == 47 || name[6] == 92);
}

// ---------------------------------------------------------------------------
// Missing production externals the enrolled TUs reference. Cycle-3
// link errors listed these against xmodel_load_obj.obj /
// com_math.obj; each stub below mirrors the production declaration
// exactly (symbol spelling) and either reproduces the retail contract
// or is provably never driven by the entry-point contracts.
// ---------------------------------------------------------------------------

// Transient string formatting. Production va() (common.cpp) hands out
// rotating static buffers formatted with the truncating CRT primitive;
// the loader builds transient filenames/strings through it, so the
// contract matters. The harness reproduces the same eight-buffer
// rotation, truncation and termination from fixture-owned storage
// (HarnessState::vaBuffers, rotation phase reset by ResetHarness())
// instead of function-local statics. Same spelling, same truncation
// behavior (_vsnprintf, covered by _CRT_SECURE_NO_WARNINGS on the
// test TU).
char *va(const char *format, ...)
{
    auto &state = xmodel_loader_entry_harness::State();
    char *buffer = state.vaBuffers[state.vaIndex];
    state.vaIndex = (state.vaIndex + 1) & 7;
    va_list args;
    va_start(args, format);
    _vsnprintf(buffer, sizeof(state.vaBuffers[0]), format, args);
    va_end(args);
    buffer[sizeof(state.vaBuffers[0]) - 1] = '\0';
    return buffer;
}

// Angle helpers reached only through com_math paths the entry-point
// contracts never drive (surface/bone records come from file bytes);
// deterministic neutral values keep the stubs honest.
void __cdecl AngleVectors(const float *angles, float *forward, float *right, float *up)
{
    (void)angles;
    if (forward)
    {
        forward[0] = 0.0f;
        forward[1] = 0.0f;
        forward[2] = 0.0f;
    }
    if (right)
    {
        right[0] = 0.0f;
        right[1] = 0.0f;
        right[2] = 0.0f;
    }
    if (up)
    {
        up[0] = 0.0f;
        up[1] = 0.0f;
        up[2] = 0.0f;
    }
}

float __cdecl AngleDelta(float a1, float a2)
{
    (void)a1;
    (void)a2;
    return 0.0f;
}

namespace xmodel_loader_entry_harness
{
// Retail storage for the r_modelVertColor dvar, which
// XModel_UseModelVertColor reads (xmodel_load_obj.cpp:23). Retail
// registers it as a bool defaulting to 1 (r_dvars.cpp:1097 — "Set to
// 0 to replace all model vertex colors with white when loaded"), so
// the tests exercise the vertex-color path commercial builds take.
// The record is BUILT BY VALUE into namespace-scope storage in this
// single-TU header (the same pattern as the harness state itself):
// no function-local static, and the global pointer below binds to the
// record's address, so the loader reads a fully initialized record on
// every path with no cross-object initialization-order dependency.
dvar_t MakeRModelVertColorRecord()
{
    dvar_t record = {};
    record.name = "r_modelVertColor";
    record.current.enabled = true;
    return record;
}

dvar_t RModelVertColorRecord = MakeRModelVertColorRecord();
}  // namespace xmodel_loader_entry_harness

// Definition mirrors r_dvars.h:247 / r_dvars.cpp:243 exactly so the
// symbol matches the production reference.
const dvar_t *r_modelVertColor = &xmodel_loader_entry_harness::RModelVertColorRecord;

// ---------------------------------------------------------------------------
// Stubs for xanim_load_obj.cpp.
//
// xanim_load_obj.cpp is enrolled in the test target so the real
// XModelPartsLoadFile parse runs (its ConsumeQuatNoSwap decodes the
// child-bone quaternions), and since ki-458h stage 2 the entry-point
// tests also drive the TU's own XAnimLoadFile, XModelPiecesLoadFile
// and XModelPiecesPrecache over the BuildEntryAnim* / BuildEntryPieces*
// fixtures. The Hunk_User* endpoints below therefore serve real
// animation-data allocations; the string/model registries keep handing
// out stable opaque records and I_strnicmp reuses the harness
// comparator.
// ---------------------------------------------------------------------------

HunkUser *Hunk_UserCreate(int maxSize, const char *name, bool fixed,
                                 bool tempMem, int type)
{
    (void)maxSize;
    (void)name;
    (void)fixed;
    (void)tempMem;
    (void)type;
    return static_cast<HunkUser *>(std::calloc(1, sizeof(HunkUser)));
}

void *Hunk_UserAlloc(HunkUser *user, uint32_t size, int alignment)
{
    (void)user;
    (void)alignment;
    return std::calloc(1, static_cast<size_t>(size));
}

void Hunk_UserDestroy(HunkUser *user)
{
    std::free(user);
}

int I_strnicmp(const char *s0, const char *s1, int n)
{
    return xmodel_loader_entry_harness::I_strnicmpHarness(s0, s1, n);
}

XModel *__cdecl R_RegisterModel(const char *name)
{
    (void)name;
    return reinterpret_cast<XModel *>(
        &xmodel_loader_entry_harness::State().modelStorage[0]);
}

uint32_t SL_GetString_(const char *str, uint32_t user, int type)
{
    (void)str;
    (void)user;
    (void)type;
    return 0;
}

XModel *__cdecl XModelPrecache(char *name, void *(__cdecl *Alloc)(int),
                                      void *(__cdecl *AllocColl)(int))
{
    (void)name;
    (void)Alloc;
    (void)AllocColl;
    // Only the XAnim-side loader paths call this; the entry-point
    // tests never reach it.
    return 0;
}

#endif  // XMODEL_LOADER_ENTRY_HARNESS_HPP
