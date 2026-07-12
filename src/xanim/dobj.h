#pragma once
#include <ode/ode.h>
#include <qcommon/bitarray.h>
#include <cstdint>
#include <type_traits>

#include <universal/kisak_abi.h>

struct PhysPreset;

struct XAnimTree_s;
struct XModel;

#define DOBJ_HANDLE_NONE -1

#define DOBJ_MAX_PARTS 0x80
#define DOBJ_MAX_SUBMODELS 32

#define HIGH_BIT 0x80000000

#define NO_BONEINDEX 255

#define CLIENT_DOBJ_HANDLE_MAX (MAX_GENTITIES + 128)
#define SERVER_DOBJ_HANDLE_MAX (MAX_GENTITIES)

struct DObjTrace_s // sizeof=0x1C
{                                       // ...
    float fraction;                     // ...
    int surfaceflags;                   // ...
    float normal[3];                    // ...
    uint16_t modelIndex;        // ...
    uint16_t partName;          // ...
    uint16_t partGroup;         // ...
    // padding byte
    // padding byte
};

struct DObjAnimMat // sizeof=0x20
{                                       // ...
    float quat[4];                      // ...
    float trans[3];                     // ...
    float transWeight;                  // ...
};
static_assert(sizeof(DObjAnimMat) == 32);

struct DSkelPartBits // sizeof=0x30
{                                       // ...
    //int anim[4];                        // ...
    //int control[4];                     // ...
    //int skel[4];                        // ...
    bitarray<128> anim;
    bitarray<128> control;
    bitarray<128> skel;
};

struct DSkel // sizeof=0x38
{                                       // ...
    DSkelPartBits partBits;             // ...
    int timeStamp;                      // ...
    DObjAnimMat* mat;                   // ...
};
RUNTIME_SIZE(DSkel, 0x38, 0x40);
RUNTIME_OFFSET(DSkel, mat, 0x34, 0x38);

struct DObjSkelMat // sizeof=0x40
{                                       // ...
    float axis[3][4];
    float origin[4];
};

struct DObj_s // sizeof=0x64
{
    XAnimTree_s* tree;
    uint16_t duplicateParts;
    uint16_t entnum;
    unsigned __int8 duplicatePartsSize;
    unsigned __int8 numModels;
    unsigned __int8 numBones;
    // padding byte
    uint32_t ignoreCollision;
    volatile uint32_t locked;
    DSkel skel;
    float radius;
    uint32_t hidePartBits[4];
    XModel** models;
};
RUNTIME_SIZE(DObj_s, 0x64, 0x78);
RUNTIME_OFFSET(DObj_s, locked, 0x10, 0x14);
RUNTIME_OFFSET(DObj_s, skel, 0x14, 0x18);
RUNTIME_OFFSET(DObj_s, models, 0x60, 0x70);
static_assert(std::is_same_v<decltype(DObj_s::locked), volatile uint32_t>);
static_assert(std::is_standard_layout_v<DObj_s>);

struct DObjModel_s // sizeof=0x8
{                                       // ...
    XModel* model;                      // ...
    uint16_t boneName;          // ...
    bool ignoreCollision;               // ...
    // padding byte
};
RUNTIME_SIZE(DObjModel_s, 0x8, 0x10);
RUNTIME_OFFSET(DObjModel_s, boneName, 0x4, 0x8);
RUNTIME_OFFSET(DObjModel_s, ignoreCollision, 0x6, 0xA);

// Owns every fallible resource needed to publish a DObj. Callers must
// value-initialize the plan, then either commit or discard it exactly once.
// Keeping this separate preserves the engine-facing DObj_s ABI.
struct DObjCreatePlan
{
    XAnimTree_s *tree;
    XModel **models;
    uint32_t duplicateParts;
    uint32_t ignoreCollision;
    uint16_t entnum;
    uint8_t duplicatePartsSize;
    uint8_t numModels;
    uint8_t numBones;
    uint8_t reserved[3];
    float radius;
    uint32_t hidePartBits[4];
};

void __cdecl DObjInit();
void __cdecl DObjShutdown();
void __cdecl DObjDumpInfo(const DObj_s *obj);
bool __cdecl DObjIgnoreCollision(const DObj_s *obj, char modelIndex);
void __cdecl DObjGetHierarchyBits(const DObj_s *obj, int boneIndex, int *partBits);
bool __cdecl DObjSkelIsBoneUpToDate(DObj_s *obj, int boneIndex);
void __cdecl DObjSetTree(DObj_s *obj, XAnimTree_s *tree);
void DObjPrepareCreate(
    DObjModel_s *dobjModels,
    uint32_t numModels,
    XAnimTree_s *tree,
    uint16_t entnum,
    DObjCreatePlan *plan);
void DObjPrepareClone(const DObj_s *from, DObjCreatePlan *plan);
bool DObjTryCommitCreatePlan(DObjCreatePlan *plan, DObj_s *obj);
void DObjDiscardCreatePlan(DObjCreatePlan *plan);
void __cdecl DObjCreate(DObjModel_s *dobjModels, uint32_t numModels, XAnimTree_s *tree, DObj_s *buf, __int16 entnum);
void __cdecl DObjDumpCreationInfo(DObjModel_s *dobjModels, uint32_t numModels);
void __cdecl DObjComputeBounds(DObj_s *obj);
void __cdecl DObjFree(DObj_s *obj);
void __cdecl DObjGetCreateParms(
    const DObj_s *obj,
    DObjModel_s *dobjModels,
    uint16_t *numModels,
    XAnimTree_s **tree,
    uint16_t *entnum);
void __cdecl DObjArchive(DObj_s *obj);
void __cdecl DObjUnarchive(DObj_s *obj);
void __cdecl DObjSkelClear(const DObj_s *obj);
void __cdecl DObjGetBounds(const DObj_s *obj, float *mins, float *maxs);
void __cdecl DObjPhysicsGetBounds(const DObj_s *obj, float *mins, float *maxs);
void __cdecl DObjPhysicsSetCollisionFromXModel(const DObj_s *obj, PhysWorld worldIndex, dxBody *physId);
double __cdecl DObjGetRadius(const DObj_s *obj);
PhysPreset *__cdecl DObjGetPhysPreset(const DObj_s *obj);
const char *__cdecl DObjGetName(const DObj_s *obj);
const char *__cdecl DObjGetBoneName(const DObj_s *obj, int boneIndex);
char *__cdecl DObjGetModelParentBoneName(const DObj_s *obj, int modelIndex);
XAnimTree_s *__cdecl DObjGetTree(const DObj_s *obj);
void __cdecl DObjTraceline(DObj_s *obj, float *start, float *end, unsigned __int8 *priorityMap, DObjTrace_s *trace);
void __cdecl DObjTracelinePartBits(DObj_s *obj, int *partBits);
void __cdecl DObjGeomTraceline(
    DObj_s *obj,
    float *localStart,
    float *const localEnd,
    int contentmask,
    DObjTrace_s *results);
void __cdecl DObjGeomTracelinePartBits(DObj_s *obj, int contentmask, int *partBits);
int __cdecl DObjHasContents(DObj_s *obj, int contentmask);
int __cdecl DObjGetContents(const DObj_s *obj);
int __cdecl DObjSetLocalBoneIndex(DObj_s *obj, int *partBits, int boneIndex, const float *trans, const float *angles);
int __cdecl DObjGetBoneIndex(const DObj_s *obj, uint32_t name, unsigned __int8 *index);
int __cdecl DObjGetModelBoneIndex(const DObj_s *obj, const char *modelName, uint32_t name, unsigned __int8 *index);
void __cdecl DObjGetBasePoseMatrix(const DObj_s *obj, unsigned __int8 boneIndex, DObjAnimMat *outMat);
void __cdecl DObjSetHidePartBits(DObj_s *obj, const uint32_t *partBits);
int DObjGetNumSurfaces(const DObj_s *obj, const char *lods);
void DObjClone(const DObj_s *from, DObj_s *obj);


// dobj_skel
void __cdecl DObjCalcSkel(const DObj_s *obj, int *partBits);
void __cdecl GetControlAndDuplicatePartBits(
    const DObj_s *obj,
    const int *partBits,
    const int *ignorePartBits,
    const int *savedDuplicatePartBits,
    int *calcPartBits,
    int *controlPartBits);
const unsigned __int8 *__cdecl CalcSkelDuplicateBones(
    const XModel *model,
    DSkel *skel,
    int minBoneIndex,
    const unsigned __int8 *pos);
void __cdecl CalcSkelRootBonesNoParentOrDuplicate(
    const XModel *model,
    DSkel *skel,
    int minBoneIndex,
    int *calcPartBits);
void __cdecl CalcSkelRootBonesWithParent(
    const XModel *model,
    DSkel *skel,
    uint32_t minBoneIndex,
    uint32_t modelParent,
    int *calcPartBits,
    const int *controlPartBits);
void __cdecl CalcSkelNonRootBones(
    const XModel *model,
    DSkel *skel,
    int minBoneIndex,
    int *calcPartBits,
    const int *controlPartBits);
void __cdecl DObjCalcBaseSkel(const DObj_s *obj, DObjAnimMat *mat, int *partBits);
void __cdecl DObjCalcBaseAnim(const DObj_s *obj, DObjAnimMat *mat, int *partBits);
void __cdecl DObjGetBaseControlAndDuplicatePartBits(
    const DObj_s *obj,
    const int *partBits,
    const int *ignorePartBits,
    const int *savedDuplicatePartBits,
    int *calcPartBits,
    int *controlPartBits);
