// SPDX-License-Identifier: GPL-3.0
//
// cpose_phys_obj_id.h - inline helpers for the cpose_t::physObjId field on
// MP, where the field is a 32-bit generation-checked token backed by
// g_cposeBodySidecar instead of a raw pointer. SP keeps the field as
// uintptr_t and reads/writes it natively, so these helpers are only used
// on the MP build. The helpers preserve the legacy sentinel contract:
// 0x00000000 == no body, 0xFFFFFFFF == dead (creation failed).
#pragma once

// C++-only header: the helpers below use C++ casts, nullptr, and
// phys_obj_id:: sidecar types. Every includer is a C++ translation
// unit, so the __cplusplus guard keeps C-language tooling (C-mode
// compiler passes, CppCheck-based analyzers) from parsing C++
// constructs as C and reporting bogus syntax errors.
#ifdef __cplusplus

#include <cstdint>

#include <bgame/bg_local.h>
#include <universal/phys_obj_id.h>

#ifndef KISAK_SP
[[nodiscard]] inline dxBody *CG_CPosePhysObjId_GetBody(const centity_s *cent)
{
    if (cent == nullptr)
        return nullptr;
    return phys_obj_id::ReadResolve<dxBody>(
        g_cposeBodySidecar,
        cent->pose.physObjId);
}

[[nodiscard]] inline uint16_t CG_CPosePhysObjId_OwnerIndex(const centity_s *cent)
{
    // The cpose sidecar is indexed by the centity number, which always
    // fits in 16 bits for the in-game entity pool (ENTITYNUM_NONE = 1023).
    return static_cast<uint16_t>(cent->nextState.number);
}

[[nodiscard]] inline bool CG_CPosePhysObjId_Assign(
    centity_s *cent,
    dxBody *body)
{
    if (cent == nullptr || body == nullptr)
        return false;
    if (cent->pose.physObjId != phys_obj_id::INVALID_BODY_TOKEN)
        return false;
    const phys_obj_id::OwnerIndex owner = CG_CPosePhysObjId_OwnerIndex(cent);
    // The frozen MP field is int32_t; the sidecar token is the
    // corresponding unsigned type, so alias through it explicitly
    // (signed/unsigned pairs may alias).
    const phys_obj_id::TokenResult bind = phys_obj_id::WriteBind(
        g_cposeBodySidecar,
        reinterpret_cast<phys_obj_id::BodyToken *>(&cent->pose.physObjId),
        owner,
        body);
    return bind.status == phys_obj_id::Status::Success;
}

[[nodiscard]] inline dxBody *CG_CPosePhysObjId_TakeBody(centity_s *cent)
{
    if (cent == nullptr)
        return nullptr;
    dxBody *body = nullptr;
    const phys_obj_id::BodyResult r =
        g_cposeBodySidecar.Release(cent->pose.physObjId);
    if (r)
        body = static_cast<dxBody *>(r.body);
    cent->pose.physObjId = phys_obj_id::INVALID_BODY_TOKEN;
    return body;
}

[[nodiscard]] inline bool CG_CPosePhysObjId_IsDead(const centity_s *cent)
{
    return cent != nullptr && phys_obj_id::IsDead(cent->pose.physObjId);
}
#endif

#endif // __cplusplus
