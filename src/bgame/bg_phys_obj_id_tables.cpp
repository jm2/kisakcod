// SPDX-License-Identifier: GPL-3.0
//
// phys_obj_id_tables.cpp - linker-visible storage for the priority-7 native64
// physics-body sidecars. The cpose_t, BreakablePiece, and DynEntityClient
// ownership families all park their native dxBody* pointers here instead of
// in the legacy 32-bit physObjId field, so the field can keep its frozen
// 32-bit width (preserving the DynEntityClient 12-byte save image, the
// BreakablePiece 0xC size, and the MP cpose_t 0x64/0x68 layout) while the
// runtime pointer never gets narrowed on any target.
//
// Each definition is guarded by a KISAK_PHYS_OBJ_ID_DEFINE_TABLES macro so
// the engine TU and the unit-test TU can both include this file without
// seeing duplicate-storage link errors. The engine defines the macro
// unconditionally; the unit-test defines it only when it wants its own
// copy (which is the default for tests, since they don't link the engine).

#include <universal/phys_obj_id.h>

#ifndef KISAK_PHYS_OBJ_ID_DEFINE_TABLES
#define KISAK_PHYS_OBJ_ID_DEFINE_TABLES 1
#endif

#if KISAK_PHYS_OBJ_ID_DEFINE_TABLES
phys_obj_id::BodySidecar<kCposeBodySidecarCapacity> g_cposeBodySidecar;
phys_obj_id::BodySidecar<kBreakablePieceBodySidecarCapacity> g_breakablePieceBodySidecar;
phys_obj_id::BodySidecar<kDynEntClientBodySidecarCapacity> g_dynEntClientBodySidecar;
#endif

