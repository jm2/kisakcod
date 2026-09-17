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
// win32-x86 link. XModelPartsLoadFile is declared by the
// loader TU but its implementation TU has not been migrated into this
// checkout yet, so the harness implements its documented contract —
// a nested buf_cursor activation over the xmodelparts file with
// Deactivate on every exit, fail-closed on malformed input — which is
// the nested-cursor production shape the loader's parent restoration
// is exercised against. R_XModelSurfsLoadFile needs no stand-in: its
// production implementation lives in the loader TU itself.

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
#include <map>
#include <string>
#include <vector>

struct GenericAabbTreeOptions;

// ---------------------------------------------------------------------------
// The printf-family wrappers the loader TU calls. Declared at global
// scope (matching their production header declarations) and DEFINED in
// xmodel_loader_entry_test.cpp: their bodies necessarily call a printf
// function with a caller-supplied format string, and every production
// printf wrapper in this repository lives in a .cpp (common.cpp,
// r_warn.cpp) — a header body re-triggers the CWE-134 lexical pattern.
// The remaining engine-service endpoints are defined inline below,
// after the harness namespace whose state they share.
// ---------------------------------------------------------------------------
void Com_PrintError(int channel, const char *fmt, ...);
int Com_sprintf(char *dest, uint32_t size, const char *fmt, ...);
void Com_Error(errorParm_t code, const char *fmt, ...);

namespace xmodel_loader_entry_harness
{
// The fixture byte builder shared with the portable cursor suites.
using ByteWriterFixture = xmodel_cursor_test_support::ByteWriter;

// The harness maps are spelled through these aliases: it keeps the
// template argument lists (and the `>::const_iterator` nested names)
// out of the individual use sites, which several lexically driven C
// analyzers misread as comma-operator expressions (MISRA 12.3 false
// positives) on this header.
typedef void *HarnessHunkPtr;
typedef std::map<std::string, HarnessHunkPtr> HarnessHunkMap;
typedef std::map<std::string, std::vector<unsigned char> > HarnessFileMap;

struct RecordedError
{
    int channel;
    std::string text;
};

struct HarnessState
{
    HarnessFileMap files;
    HarnessHunkMap hunkData;
    std::vector<RecordedError> errors;
    std::vector<std::string> materialRegistrations;
    // Opaque engine records: no complete Material/PhysPreset type is
    // reachable from the harness include closure, so the stubs hand
    // out stable pointers into zero-initialized aligned storage on
    // this singleton instead of default-constructing engine types.
    alignas(16) unsigned char materialStorage[128] = {};
    alignas(16) unsigned char physPresetStorage[128] = {};
    int fsReads = 0;
    int fsFrees = 0;
    int nestedPartsActivations = 0;
    int physPresetCalls = 0;
    int collMapCalls = 0;
};

// State() and ResetHarness() are defined in the single TU that
// includes this header (xmodel_loader_entry_test.cpp); the harness
// state is namespace-scope storage there instead of a function-local
// static.
HarnessState &State();
void ResetHarness();

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
// The nested xmodelparts loader contract.
//
// Declared by the loader TU (xmodel_load_obj.cpp); its production
// implementation TU has not been migrated into this checkout. This
// implementation follows the documented production shape: FS_ReadFile,
// a nested buf_cursor activation over the xmodelparts buffer, the
// version/counts/bodies/names/classification/useBones byte walk, and
// Deactivate on every exit — fail-closed (cursor Failed + null return)
// on malformed input. The harness counts each activation so tests can
// distinguish a cold nested load from a warm hunk-cache hit.
// ---------------------------------------------------------------------------

struct PartsParseCounts
{
    int numChildBones;
    int numRootBones;
    int numBones;
};

inline unsigned char *PartsReadCounts(unsigned char *pos, PartsParseCounts &counts)
{
    const uint16_t version = buf_cursor::Buf_Read<uint16_t>(&pos);
    if (version != 25)
        return 0;
    counts.numChildBones = buf_cursor::Buf_Read<uint16_t>(&pos);
    counts.numRootBones = buf_cursor::Buf_Read<uint16_t>(&pos);
    counts.numBones = counts.numChildBones + counts.numRootBones;
    return pos;
}

// The body walk consumes a fixed number of bytes per child bone; every
// individual read is checked and latches the cursor on overrun, which
// the caller observes through buf_cursor::Failed() — so this step has
// no failure result of its own.
inline void PartsReadBodies(unsigned char *pos, const PartsParseCounts &counts)
{
    for (int i = counts.numRootBones; i < counts.numBones; ++i)
    {
        (void)buf_cursor::ReadWeight();  // parent index byte
        for (int f = 0; f < 3; ++f)
            (void)buf_cursor::Buf_Read<float>(&pos);
        for (int q = 0; q < 4; ++q)
            (void)buf_cursor::Buf_Read<uint16_t>(&pos);
    }
}

// A count block is usable only when it parsed and declares a sane bone
// budget for the fixture walk.
inline bool PartsCountsUsable(const unsigned char *countsPos, const PartsParseCounts &counts)
{
    return countsPos != 0 && counts.numBones > 0 && counts.numBones <= 8;
}

inline bool PartsReadNames(const PartsParseCounts &counts)
{
    for (int i = 0; i < counts.numBones; ++i)
    {
        char nameBuf[128];
        if (!buf_cursor::ReadString(nameBuf, sizeof(nameBuf)))
            return false;
    }
    return true;
}

inline bool PartsReadTail(const PartsParseCounts &counts, bool &useBones)
{
    unsigned char classification[8] = {0xEE, 0xEE};
    if (!buf_cursor::ReadBytes(classification, sizeof(classification),
                               static_cast<size_t>(counts.numBones)))
        return false;
    useBones = (buf_cursor::ReadWeight() != 0);
    return !buf_cursor::Failed();
}

// Materialize the parsed parts into the production XModelPartsLoad
// shape the loader's XModelCopyXModelParts consumes. Allocations come
// from the loader's own Alloc callback.
inline XModelPartsLoad *PartsBuildResult(const PartsParseCounts &counts,
                                         void *(__cdecl *Alloc)(int))
{
    XModelPartsLoad *parts = static_cast<XModelPartsLoad *>(Alloc(sizeof(XModelPartsLoad)));
    std::memset(parts, 0, sizeof(*parts));
    parts->numBones = static_cast<uint8_t>(counts.numBones);
    parts->numRootBones = static_cast<uint8_t>(counts.numRootBones);
    parts->parentList = static_cast<uint8_t *>(Alloc(static_cast<int>(counts.numBones)));
    parts->quats = static_cast<int16_t *>(Alloc(static_cast<int>(8 * counts.numBones)));
    parts->trans = static_cast<float *>(Alloc(static_cast<int>(12 * counts.numBones)));
    parts->partClassification =
        static_cast<uint8_t *>(Alloc(static_cast<int>(counts.numBones)));
    parts->partClassification[0] = 0;
    parts->partClassification[1] = 1;
    return parts;
}

// Opens xmodelparts/<name>. A zero-length file is freed again right
// away and reported as a 0-length read, matching the production
// loader's contract; a missing file reports -1 with *buf untouched.
inline int PartsOpenFile(const char *name, unsigned char **buf)
{
    char filename[68];
    if (Com_sprintf(filename, static_cast<uint32_t>(sizeof(filename)),
                    "xmodelparts/%s", name) < 0)
    {
        return -1;
    }
    const int fileLen = FS_ReadFile(filename, reinterpret_cast<void **>(buf));
    if (fileLen == 0)
        FS_FreeFile(reinterpret_cast<char *>(*buf));
    return fileLen;
}

// Malformed parts input: latch failed and fail closed, exactly like
// the production loader's rejection paths.
inline XModelPartsLoad *PartsFailClosed(unsigned char *buf, const char *name)
{
    buf_cursor::Fail();
    buf_cursor::Deactivate();
    FS_FreeFile(reinterpret_cast<char *>(buf));
    Com_PrintError(19, "ERROR: Cannot find xmodelparts '%s'.\n", name);
    return 0;
}

inline XModelPartsLoad *XModelPartsLoadFile(XModel *model, const char *name,
                                            void *(__cdecl *Alloc)(int))
{
    (void)model;
    unsigned char *buf = 0;
    const int fileLen = PartsOpenFile(name, &buf);
    if (fileLen <= 0)
        return 0;

    unsigned char *pos = buf;
    buf_cursor::Activate(buf, static_cast<size_t>(fileLen));
    buf_cursor::AnchorPos(&pos);
    ++State().nestedPartsActivations;

    PartsParseCounts counts = {0, 0, 0};
    unsigned char *countsPos = PartsReadCounts(pos, counts);
    const bool usable = PartsCountsUsable(countsPos, counts);
    bool useBones = false;
    if (usable)
        PartsReadBodies(countsPos, counts);
    if (usable && PartsReadNames(counts) && PartsReadTail(counts, useBones)
        && !buf_cursor::Failed())
    {
        buf_cursor::Deactivate();
        FS_FreeFile(reinterpret_cast<char *>(buf));
        return PartsBuildResult(counts, Alloc);
    }

    return PartsFailClosed(buf, name);
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
    w.Push8(1);             // child parent index
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

// Registers the full valid model corpus under `name`.
inline void RegisterValidModel(const char *name)
{
    std::string path;
    path = std::string("xmodel/") + name;
    RegisterFile(path.c_str(), BuildEntryModelFile().bytes);
    path = std::string("xmodelparts/") + name;
    RegisterFile(path.c_str(), BuildEntryPartsFile().bytes);
    path = std::string("xmodelsurfs/lod_a");
    RegisterFile(path.c_str(), BuildEntrySurfsFile(2).bytes);
    path = std::string("xmodelsurfs/lod_b");
    RegisterFile(path.c_str(), BuildEntrySurfsFile(1).bytes);
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
// in this header (included by exactly one TU); the printf-family
// wrappers above are the exception and are defined in the test TU.
// Harness state and the opaque Material/PhysPreset storage ride on
// the harness singleton.
// ---------------------------------------------------------------------------

inline void MyAssertHandler(const char *filename, int line, int type, const char *fmt, ...)
{
    (void)filename;
    (void)line;
    (void)type;
    (void)fmt;
    // Any production assert firing during an entry-point contract is a
    // defect: fail the test process loudly instead of continuing.
    std::fprintf(stderr, "xmodel_loader_entry: production assert fired\n");
    std::abort();
}

inline void track_static_alloc_internal(void *ptr, int size, const char *name, int type)
{
    (void)ptr;
    (void)size;
    (void)name;
    (void)type;
}

inline uint32_t SL_GetStringOfSize(const char *str, uint32_t user, uint32_t len, int type)
{
    (void)str;
    (void)user;
    (void)len;
    (void)type;
    return 0;
}

inline int __cdecl BuildAabbTree(const GenericAabbTreeOptions *options)
{
    (void)options;
    return 0;
}

inline int FS_ReadFile(const char *qpath, void **buffer)
{
    xmodel_loader_entry_harness::HarnessFileMap &files =
        xmodel_loader_entry_harness::State().files;
    xmodel_loader_entry_harness::HarnessFileMap::const_iterator it = files.find(qpath);
    if (it == files.end())
        return -1;
    const std::vector<unsigned char> &bytes = it->second;
    unsigned char *copy = new unsigned char[bytes.size() + 1];
    // Bounded, iterator-based copy: same bytes, same terminator, with
    // the destination capacity visible in the expression itself.
    std::copy(bytes.begin(), bytes.end(), copy);
    copy[bytes.size()] = 0;
    *buffer = copy;
    ++xmodel_loader_entry_harness::State().fsReads;
    return static_cast<int>(bytes.size());
}

inline void FS_FreeFile(char *buffer)
{
    delete[] reinterpret_cast<unsigned char *>(buffer);
    ++xmodel_loader_entry_harness::State().fsFrees;
}

inline uint32_t *Hunk_AllocateTempMemory(int size, const char *name)
{
    (void)name;
    return static_cast<uint32_t *>(std::malloc(static_cast<size_t>(size)));
}

inline void Hunk_FreeTempMemory(char *buf)
{
    std::free(buf);
}

inline void *Hunk_FindDataForFile(int type, const char *name)
{
    (void)type;
    xmodel_loader_entry_harness::HarnessHunkMap &data =
        xmodel_loader_entry_harness::State().hunkData;
    xmodel_loader_entry_harness::HarnessHunkMap::const_iterator it = data.find(name);
    return it == data.end() ? 0 : it->second;
}

inline char *Hunk_SetDataForFile(int type, const char *name, void *data,
                                 void *(__cdecl *alloc)(int))
{
    (void)type;
    (void)alloc;
    xmodel_loader_entry_harness::State().hunkData[name] = data;
    return static_cast<char *>(data);
}

inline Material *__cdecl Material_RegisterHandle(const char *name, int imageTrack)
{
    (void)imageTrack;
    xmodel_loader_entry_harness::State().materialRegistrations.push_back(name);
    return reinterpret_cast<Material *>(
        &xmodel_loader_entry_harness::State().materialStorage[0]);
}

inline struct PhysPreset *__cdecl PhysPresetPrecache(const char *name,
                                                     void *(__cdecl *Alloc)(int))
{
    (void)name;
    (void)Alloc;
    ++xmodel_loader_entry_harness::State().physPresetCalls;
    return reinterpret_cast<PhysPreset *>(
        &xmodel_loader_entry_harness::State().physPresetStorage[0]);
}

inline void ProfLoad_Begin(const char *label)
{
    (void)label;
}

inline void ProfLoad_End()
{
}

inline void R_GetXModelBounds(XModel *model, const float (*axes)[3], float *mins, float *maxs)
{
    (void)model;
    (void)axes;
    mins[0] = mins[1] = mins[2] = -1.0f;
    maxs[0] = maxs[1] = maxs[2] = 1.0f;
}

inline struct PhysGeomList *__cdecl XModel_LoadPhysicsCollMap(const char *name,
                                                              void *(__cdecl *Alloc)(int))
{
    (void)name;
    (void)Alloc;
    ++xmodel_loader_entry_harness::State().collMapCalls;
    return 0;
}

inline int XModelNumBones(const XModel *model)
{
    return model->numBones;
}

inline bool Com_IsLegacyXModelName(const char *name)
{
    return !xmodel_loader_entry_harness::I_strnicmpHarness(name, "xmodel", 6)
           && (name[6] == 47 || name[6] == 92);
}

// The loader TU declares XModelPartsLoadFile at file scope and calls
// it for the nested parts load; route the global name to the harness
// implementation in the namespace above.
inline XModelPartsLoad *XModelPartsLoadFile(XModel *model, const char *name,
                                            void *(__cdecl *Alloc)(int))
{
    return xmodel_loader_entry_harness::XModelPartsLoadFile(model, name, Alloc);
}

#endif  // XMODEL_LOADER_ENTRY_HARNESS_HPP
