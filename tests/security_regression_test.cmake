cmake_minimum_required(VERSION 3.16)

function(require_source_contains RELATIVE_PATH NEEDLE DESCRIPTION)
    file(READ "${SOURCE_ROOT}/src/${RELATIVE_PATH}" _source)
    string(FIND "${_source}" "${NEEDLE}" _position)
    if (_position EQUAL -1)
        message(FATAL_ERROR "Missing security invariant (${DESCRIPTION}) in src/${RELATIVE_PATH}")
    endif()
endfunction()

function(require_source_not_contains RELATIVE_PATH NEEDLE DESCRIPTION)
    file(READ "${SOURCE_ROOT}/src/${RELATIVE_PATH}" _source)
    string(FIND "${_source}" "${NEEDLE}" _position)
    if (NOT _position EQUAL -1)
        message(FATAL_ERROR "Forbidden security regression (${DESCRIPTION}) in src/${RELATIVE_PATH}")
    endif()
endfunction()

function(require_source_ordered RELATIVE_PATH FIRST SECOND DESCRIPTION)
    file(READ "${SOURCE_ROOT}/src/${RELATIVE_PATH}" _source)
    string(FIND "${_source}" "${FIRST}" _first_position)
    string(FIND "${_source}" "${SECOND}" _second_position)
    if (_first_position EQUAL -1
        OR _second_position EQUAL -1
        OR _first_position GREATER _second_position)
        message(FATAL_ERROR
            "Missing or unordered security invariant (${DESCRIPTION}) in src/${RELATIVE_PATH}")
    endif()
endfunction()

file(GLOB_RECURSE _callback_sources "${SOURCE_ROOT}/src/*.cpp")
foreach(_callback_source IN LISTS _callback_sources)
    file(READ "${_callback_source}" _callback_text)
    foreach(_forbidden_cast
        "(void(__cdecl *)(XAssetHeader, void *))"
        "(void(__cdecl*)(XAssetHeader, void*))"
        "(void(*)(XAssetHeader, void*))")
        string(FIND "${_callback_text}" "${_forbidden_cast}" _callback_cast)
        if (NOT _callback_cast EQUAL -1)
            message(FATAL_ERROR
                "XAsset enumeration callback cast remains in ${_callback_source}")
        endif()
    endforeach()
endforeach()

require_source_contains(
    "bgame/bg_local.h"
    "constexpr int32_t MAX_WEAPONS = 128"
    "weapon-table width must be owned by shared game code")
require_source_contains(
    "bgame/bg_local.h"
    "return itemIndex / MAX_WEAPONS;"
    "weapon item indices must decode their model without applying the table stride")
require_source_contains(
    "bgame/bg_misc.cpp"
    "BG_GetItemWeaponModel(ent->index.item) * MAX_WEAPONS + weapIdx"
    "weapon item lookup must apply the model stride exactly once")
require_source_not_contains(
    "bgame/bg_misc.cpp"
    "ITEM_WEAPMODEL"
    "legacy weapon-model macro could multiply the item-table stride twice")
require_source_contains(
    "win32/win_syscon.cpp"
    "const size_t inputCapacity = capacity - currentLength - 1;"
    "console input must reserve space for its command separator and terminator")
require_source_not_contains(
    "win32/win_syscon.cpp"
    "strncat("
    "console input must not use underflow-prone remaining-size concatenation")
require_source_contains(
    "win32/win_syscon.cpp"
    "static char s_headlessConsoleHistory[0x4000];"
    "headless startup diagnostics must remain available when the console is created late")
require_source_contains(
    "win32/win_syscon.cpp"
    "Conbuf_AppendHeadlessHistory(msg);"
    "headless diagnostics must enter bounded history before console-window creation")
require_source_contains(
    "win32/win_main.cpp"
    "I_strncpyz(b, s, static_cast<int32_t>(v2 + 1));"
    "console events must preserve their final character and trailing terminator")
require_source_contains(
    "cgame_mp/dedicated_cgame.cpp"
    "{ \"code_post_gfx_mp\", 2, 0 }"
    "headless startup must load authoritative MP code assets")
require_source_contains(
    "cgame_mp/dedicated_cgame.cpp"
    "{ \"localized_code_post_gfx_mp\", 0, 0 }"
    "headless startup must load localized MP code assets")
require_source_contains(
    "cgame_mp/dedicated_cgame.cpp"
    "{ \"common_mp\", 4, 0 }"
    "headless startup must load authoritative MP common assets")
require_source_contains(
    "cgame_mp/dedicated_cgame.cpp"
    "{ \"localized_common_mp\", 1, 0 }"
    "headless startup must load localized MP common assets")
require_source_not_contains(
    "cgame_mp/dedicated_cgame.cpp"
    "ui_mp"
    "headless startup must not load client UI assets")
require_source_contains(
    "universal/q_shared.h"
    "#ifdef KISAK_DEDI_HEADLESS
\treturn true;"
    "headless builds must be compile-time fast-file-only")
require_source_contains(
    "qcommon/common.cpp"
    "#ifdef KISAK_DEDI_HEADLESS
        DVAR_ROM,
#else
        DVAR_INIT,"
    "headless useFastFile must not be disabled from the command line")
require_source_contains(
    "bgame/bg_public.h"
    "static const pmoveHandler_t pmoveHandlers[2] = { { G_TraceCapsule, NULL}, {G_TraceCapsule, G_PlayerEvent} };"
    "headless movement tables must not retain a client collision callback")
require_source_contains(
    "universal/com_files.cpp"
    "#ifndef KISAK_DEDI_HEADLESS
    SND_StopSounds(SND_STOP_STREAMED);
#endif"
    "headless filesystem shutdown must not link the sound backend")
require_source_contains(
    "qcommon/cmd.cpp"
    "dumpraw is unavailable because headless builds do not realize media resources"
    "headless media extraction must not dereference canonical null audio resources")
require_source_contains(
    "qcommon/cmd.cpp"
    "#ifndef KISAK_DEDI_HEADLESS
    Cmd_AddCommandInternal(\"dumpraw\", Cmd_Dumpraw_f, &Cmd_Dumpraw_f_VAR);
#endif"
    "headless builds must not advertise the unsupported media extraction command")
require_source_contains(
    "qcommon/cm_load_obj.cpp"
    "#if defined(KISAK_MP) && !defined(KISAK_DEDI_HEADLESS)"
    "headless load-object collision code must not link client dynamic entities")
require_source_contains(
    "universal/com_sndalias.cpp"
    "Load-object sound aliases are unavailable in a headless fast-file build"
    "headless builds must explicitly reject the load-object sound path")
require_source_contains(
    "universal/com_sndalias_load_obj.cpp"
    "void __cdecl Com_AddLoadedSoundFile(SoundFile *soundFile, char *fileName)
{
#ifdef KISAK_DEDI_HEADLESS"
    "headless load-object sound construction must compile out SND_LoadSoundFile")
require_source_contains(
    "universal/com_sndalias_load_obj.cpp"
    "int __cdecl Com_LoadSoundAliasSounds(SoundFileInfo *soundFileInfo)
{
#ifdef KISAK_DEDI_HEADLESS"
    "headless load-object sound verification must compile out sound dvars")
require_source_contains(
    "xanim/xmodel_load_obj.cpp"
    "XModel *__cdecl XModelLoadFile(char *name, void *(__cdecl *Alloc)(int), void *(__cdecl *AllocColl)(int))
{
#ifdef KISAK_DEDI_HEADLESS"
    "headless load-object model construction must compile out renderer dependencies")
require_source_contains(
    "qcommon/sv_msg_write_mp.cpp"
    "checkValue = ~static_cast<uint32_t>(value);"
    "signed bit-width checks must handle INT_MIN without negation overflow")
require_source_not_contains(
    "qcommon/sv_msg_write_mp.cpp"
    "_BitScanReverse"
    "shared message serialization must not depend on a Win32 compiler intrinsic")
require_source_contains(
    "qcommon/files.cpp"
    "ARRAY_COUNT(fs_serverReferencedFFCheckSums)"
    "fast-file reference capacity must be derived from the destination array")
require_source_contains(
    "qcommon/files.cpp"
    "strlen(pak) >= sizeof(szFile)"
    "server pak names must be bounded before copying")
require_source_contains(
    "qcommon/files.cpp"
    "strlen(iwd) >= sizeof(szFile)"
    "IWD names must be bounded before copying")
require_source_contains(
    "database/db_stream_load.cpp"
    "CheckedArrayBytes(count, stride, &byteCount)"
    "generated fast-file arrays must use checked count multiplication")
require_source_contains(
    "database/db_stream_load.cpp"
    "DB_ResolveInsertedPointer"
    "fast-file aliases must resolve through registered provenance")
require_source_contains(
    "database/db_stream_load.cpp"
    "DB_ResolveOffsetBytes"
    "migrated direct offsets must resolve through materialized-range provenance")
require_source_contains(
    "database/db_stream_load.cpp"
    "requiredBytes ? requiredBytes : 1"
    "non-null zero-count direct offsets must still reference materialized storage")
require_source_contains(
    "database/db_stream_load.cpp"
    "DB_ResolveOffsetCString"
    "direct fast-file strings must scan only bounded materialized storage")
require_source_contains(
    "database/db_stream_load.cpp"
    "DB_RegisterStreamCString"
    "inline fast-file strings must register exact start and extent provenance")
require_source_contains(
    "database/db_stream_load.cpp"
    "SL_GetStringOfSize"
    "direct temporary strings must be interned with their validated extent")
require_source_contains(
    "database/db_validation.h"
    "kMaxInternedStringBytes = 65531"
    "temporary strings must remain below the script-memory allocation ceiling")
require_source_contains(
    "database/db_stream_load.cpp"
    "db::validation::CanInternString(byteCount)"
    "inline and direct temporary strings must enforce the allocation ceiling")
require_source_contains(
    "database/db_load.cpp"
    "varMaterial->textureCount,
            \"material textures\")"
    "material textures must reject missing nonempty spans")
require_source_contains(
    "database/db_load.cpp"
    "varMaterial->constantCount,
            \"material constants\")"
    "material constants must reject missing nonempty spans")
require_source_contains(
    "database/db_load.cpp"
    "varMaterial->stateBitsCount,
            \"material state bits\")"
    "material state bits must reject missing nonempty spans")
require_source_contains(
    "database/db_load.cpp"
    "varGfxAabbTree->smodelIndexCount,
            \"world AABB static-model indices\")"
    "world AABB indices must reject missing nonempty spans")
require_source_contains(
    "database/db_load.cpp"
    "varGfxWorld->planeCount,
            \"world planes\")"
    "world planes must reject missing nonempty spans")
require_source_contains(
    "database/db_load.cpp"
    "db::validation::AllU16Below"
    "world AABB static-model indices must be bounded before runtime use")
require_source_contains(
    "database/db_validation.h"
    "ValidateWorldAabbTopology("
    "world AABB topology must use the portable linear-time validator")
require_source_contains(
    "database/db_load.cpp"
    "GfxCellLayoutValid(*varGfxCell, &extents)"
    "world cell child arrays must validate checked extents before loading")
require_source_contains(
    "database/db_load.cpp"
    "!DB_ValidateWorldAabbTrees(varGfxWorld)"
    "completed fast-file worlds must validate every owning AABB tree")
require_source_contains(
    "database/db_load.cpp"
    "if (!DB_ValidateWorldAabbCell(varGfxWorld, varGfxCell))"
    "every owning fast-file cell must validate its AABB topology")
require_source_contains(
    "database/db_validation.h"
    "kMaxGfxWorldCells = 1024"
    "world cells must fit fixed renderer bitsets and traversal lists")
require_source_contains(
    "database/db_validation.h"
    "kMaxGfxPortalVertices = 64"
    "portal geometry must fit fixed renderer hull workspaces")
require_source_contains(
    "database/db_validation.h"
    "kMaxGfxReflectionProbes = 254"
    "world reflection-probe iteration must not wrap its uint8 index")
require_source_contains(
    "database/db_validation.h"
    "aabbTreeCount < 1"
    "every world cell must retain the AABB root dereferenced by runtime queries")
require_source_contains(
    "database/db_validation.h"
    "GfxReflectionProbeRuntimeValid("
    "world reflection probes must validate finite origins and required images")
require_source_contains(
    "database/db_validation.h"
    "GfxCullGroupRuntimeValid("
    "world cull groups must validate bounds and sorted-surface spans")
require_source_contains(
    "database/db_load.cpp"
    "GfxWorldCellBitsValid(
            cellCount,
            varGfxWorld->cellBitsCount)"
    "world cell bit buffers must match the renderer's fixed chunk layout")
require_source_contains(
    "database/db_validation.h"
    "SerializedArrayElementIndex("
    "serialized member references must use explicit disk strides")
require_source_ordered(
    "database/db_load.cpp"
    "Load_StreamArray(
        atStreamStart,
        (uint8_t *)varGfxCell,
        count,
        disk32::kGfxCellBytes)"
    "DB_ResolveDirectPointer(
            &varGfxPortal->cell"
    "the complete cell header array must materialize before portal targets resolve")
require_source_ordered(
    "database/db_load.cpp"
    "varGfxPortal->writable = {};"
    "DB_ResolveDirectPointer(
            &varGfxPortal->cell"
    "serialized portal runtime pointers must be scrubbed before target fixups")
require_source_ordered(
    "database/db_load.cpp"
    "DB_ResolveDirectPointer(
            &varGfxPortal->cell"
    "SerializedArrayElementIndex(
            varGfxWorld->cells"
    "portal targets must resolve a bounded span before exact cell membership")
require_source_ordered(
    "database/db_load.cpp"
    "GfxWorldCellGraphValid(
            *varGfxWorld"
    "if (!Load_GfxWorld(1))"
    "completed world cell graphs must validate before returning to asset publication")
require_source_ordered(
    "database/db_load.cpp"
    "DB_ValidateMaterializedSpan(
            varGfxWorld->reflectionProbeTextures"
    "GfxWorldCellGraphValid(
            *varGfxWorld"
    "reflection texture scratch storage must materialize before world publication")
require_source_ordered(
    "database/db_load.cpp"
    "DB_ValidateMaterializedSpan(
            varGfxWorld->cellCasterBits"
    "GfxWorldCellGraphValid(
            *varGfxWorld"
    "cell-caster storage must materialize before world publication")
require_source_ordered(
    "database/db_load.cpp"
    "DB_ValidateMaterializedBlock4Span(
            varGfxWorld->dpvs.sortedSurfIndex"
    "GfxWorldCellGraphValid(
            *varGfxWorld"
    "sorted surfaces must materialize before graph validation dereferences them")
require_source_contains(
    "database/db_load.cpp"
    "db::validation::CheckedCountSum(
            varGfxWorld->dpvs.staticSurfaceCountNoDecal,
            varGfxWorld->dpvs.staticSurfaceCount,
            &sortedSurfaceCount)"
    "sorted-surface overflow must fail instead of collapsing to a zero-byte span")
require_source_ordered(
    "database/db_load.cpp"
    "DB_ValidateMaterializedBlock4Span(
            varGfxWorld->models"
    "!DB_ValidateWorldAabbTrees(varGfxWorld)"
    "world brush models must materialize before AABB validation dereferences model zero")
require_source_contains(
    "database/db_load.cpp"
    "!DB_IsStreamRangeValid(
                    *varGfxWorldPtr,
                    disk32::kGfxWorldBytes)"
    "world loading must preflight its complete top-level header allocation")
require_source_ordered(
    "database/db_load.cpp"
    "inserted = DB_InsertPointer(DBAliasKind::GfxWorld);
                if (!inserted)"
    "if (!Load_GfxWorld(1))"
    "shared world alias-slot failure must stop loading before asset publication")
require_source_ordered(
    "database/db_load.cpp"
    "!DB_IsStreamRangeValid(
                    varGfxWorld->sunLight,
                    disk32::kGfxLightBytes)"
    "DB_ValidateSunLight(varGfxWorld->sunLight)"
    "inline world sunlight must preflight its full schema before validation")
require_source_ordered(
    "database/db_load.cpp"
    "if (!Load_GfxWorld(1))"
    "Load_GfxWorldAsset((XAssetHeader *)varGfxWorldPtr)"
    "invalid portal graphs must stop world asset publication")
require_source_not_contains(
    "database/db_load.cpp"
    "varGfxPortal->cell = (GfxCell *)AllocLoad_FxElemVisStateSample()"
    "portal targets must not allocate cells outside the owned world array")
require_source_contains(
    "database/db_validation.h"
    "kMaxPathTreeDepth = 64"
    "path-tree traversal must reject recursion-depth denial of service")
require_source_contains(
    "database/db_validation.h"
    "kMaxPathChainDepth = 256"
    "path-chain parents must bound recursive gameplay traversal depth")
require_source_contains(
    "database/db_validation.h"
    "PathNodeTypeValid("
    "path-node types must be bounded before runtime table lookup and bit shifts")
require_source_ordered(
    "database/db_load.cpp"
    "&varpathnode_t->dynamic.pOwner"
    "&varpathnode_t->transient"
    "PathNodeTypeValid("
    "runtime-only path-node state must be scrubbed before type validation")
require_source_contains(
    "database/db_validation.h"
    "PathDataLayoutValid("
    "path-data child arrays must derive checked disk32 extents")
require_source_contains(
    "database/db_validation.h"
    "PathVisibilityBytes("
    "path visibility must cover every runtime-indexed directed node pair")
require_source_contains(
    "database/db_validation.h"
    "PathLinksRuntimeValid("
    "path links must reject out-of-range runtime node indices")
require_source_ordered(
    "database/db_load.cpp"
    "PathNodesRuntimeValid(
            varPathData->nodes"
    "PathChainMapsRuntimeValid(
            varPathData->chainNodeForNode"
    "PathTreeGraphValid(
            varPathData->nodeTree"
    "path runtime indices and parent topology must validate before tree publication")
require_source_ordered(
    "database/db_load.cpp"
    "Load_StreamArray(
        atStreamStart,
        (uint8_t *)varpathnode_tree_t,
        count,
        disk32::kPathTreeBytes)"
    "if (!Load_pathnode_tree_t(0))"
    "the complete flat path-tree header array must materialize before child fixups")
require_source_ordered(
    "database/db_load.cpp"
    "DB_ResolveDirectPointer(
            varpathnode_tree_ptr"
    "SerializedArrayElementIndex(
            varPathData->nodeTree"
    "path-tree children must resolve a full header before exact owner membership")
require_source_ordered(
    "database/db_load.cpp"
    "PathTreeGraphValid(
            varPathData->nodeTree"
    "if (!Load_GameWorldSp(1))"
    "completed path trees must validate before returning toward SP-world publication")
require_source_ordered(
    "database/db_load.cpp"
    "if (!Load_GameWorldSp(1))"
    "Load_GameWorldSpAsset((XAssetHeader *)varGameWorldSpPtr)"
    "invalid path graphs must stop SP-world asset publication")
require_source_not_contains(
    "database/db_load.cpp"
    "*varpathnode_tree_ptr = (pathnode_tree_t *)AllocLoad_FxElemVisStateSample()"
    "path-tree children must not allocate nodes outside the owned flat array")
require_source_not_contains(
    "database/db_stream_load.cpp"
    "DB_ConvertOffsetToPointerLegacy"
    "all direct fast-file offsets must use bounded typed resolution")
require_source_contains(
    "database/db_load.cpp"
    "void __cdecl Load_XAssetHeader(bool atStreamStart)
{
    if (varXAsset->type < 0 || varXAsset->type >= ASSET_TYPE_COUNT)"
    "top-level asset loading must validate the serialized type before dispatch")
require_source_contains(
    "database/db_load.cpp"
    "if (!DB_IsXAssetTypeSupportedForBuild(varXAsset->type))
    {
        Com_Error(
            ERR_DROP,
            \"Fast-file asset type '%s' is not supported by this build\""
    "top-level asset loading must reject disallowed build-mode and unavailable types before dispatch")
require_source_contains(
    "database/db_load.cpp"
    "void __cdecl Mark_XAssetHeader()
{
    if (varXAsset->type < 0 || varXAsset->type >= ASSET_TYPE_COUNT)"
    "asset marking must validate the type before dispatch")
require_source_contains(
    "database/db_load.cpp"
    "\"Cannot mark asset type '%s' in this build\""
    "asset marking must reject disallowed build-mode and unavailable types before dispatch")
require_source_contains(
    "database/db_registry.cpp"
    "if (!DB_IsXAssetTypeSupportedForBuild(type))
    {
        Sys_UnlockWrite(&db_hashCritSect);
        Com_Error(
            ERR_DROP,
            \"Cannot allocate asset type %d in this build\""
    "asset allocation must reject invalid, unavailable, and wrong-mode types before pool indexing")
require_source_contains(
    "database/db_registry.cpp"
    "\"Cannot publish asset type %d in this build\""
    "asset publication must reject invalid, unavailable, and wrong-mode types before insertion")
require_source_contains(
    "database/db_registry.cpp"
    "\"Cannot mark asset type %d in this build\""
    "registry marking must reject invalid, unavailable, and wrong-mode types before name lookup")
require_source_contains(
    "database/db_load.cpp"
    "world->models[0].surfaceCount
            != world->dpvs.staticSurfaceCount"
    "world AABB metadata must match the world brush-model surface partition")
require_source_contains(
    "database/db_load.cpp"
    "db::validation::CoverageComplete("
    "world AABB cell roots must cover each sorted-surface partition exactly once")
require_source_contains(
    "database/db_load.cpp"
    "varGfxWorldDpvsStatic->smodelCount
        > db::validation::kMaxWorldAabbStaticModels"
    "fast-file static-model counts must fit uint16 AABB indices before payload loading")
require_source_contains(
    "database/db_load.cpp"
    "Fast-file world has an invalid sorted surface index"
    "world sorted-surface values must be bounded before renderer traversal")
require_source_contains(
    "gfx_d3d/r_gfx.h"
    "GfxAabbTree_GetChildren("
    "world AABB child offsets must have one byte-displacement accessor")
require_source_contains(
    "gfx_d3d/r_gfx.h"
    "GfxAabbTree_SetChildren("
    "world AABB producers must range-check native byte displacements")
require_source_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "ValidateImplicitWorldAabbForest("
    "raw BSP world AABB forests must validate before recursive reconstruction")
require_source_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "const std::uint32_t staticSurfaceCount = s_world.models[0].surfaceCount"
    "raw BSP AABB ranges must use the static sorted-surface partition")
require_source_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "R_ValidateBrushModelSurfaceRanges(&s_world)"
    "raw BSP brush-model surface ranges must validate before AABB consumption")
require_source_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "if (tree->smodelIndexCount == UINT16_MAX)"
    "load-object AABB static-model lists must reject count wraparound")
require_source_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "if (smodelIndex < 0 || smodelIndex > UINT16_MAX)"
    "load-object AABB static-model indices must validate before uint16 storage")
require_source_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "if (tree->childCount == UINT16_MAX)"
    "load-object AABB child appends must reject count wraparound")
require_source_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "if (smodelCount > UINT16_MAX)"
    "raw BSP entity parsing must reject unrepresentable static-model indices before allocation")
require_source_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "static_cast<std::uint32_t>(sizeof(GfxStaticModelCombinedInst))"
    "load-object static-model sorting must allocate the native combined stride")
require_source_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "&& depth < db::validation::kMaxWorldAabbDepth"
    "load-object AABB sorting must stop subdividing at the runtime recursion budget")
require_source_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "R_PreflightNoDecalAabbTree("
    "raw BSP AABB no-decal output must preflight capacity and field widths")
require_source_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "R_ValidateLoadedWorldAabbTrees("
    "flattened load-object world AABB trees must validate before publication")
require_source_not_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "tree + tree->childrenOffset"
    "world AABB byte offsets must not be scaled as element offsets")
require_source_not_contains(
    "gfx_d3d/r_marks.cpp"
    "(char *)tree + tree->childrenOffset"
    "renderer mark traversal must use the canonical AABB child accessor")
require_source_not_contains(
    "gfx_d3d/r_dpvs_static.cpp"
    "(char *)tree + tree->childrenOffset"
    "renderer visibility traversal must use the canonical AABB child accessor")

file(READ
    "${SOURCE_ROOT}/src/gfx_d3d/r_bsp_load_obj.cpp"
    _world_aabb_load_obj_source)
string(FIND
    "${_world_aabb_load_obj_source}"
    "if (!R_PreflightNoDecalAabbTree("
    _world_aabb_preflight_call)
string(FIND
    "${_world_aabb_load_obj_source}"
    "startSurfIndex = R_BuildNoDecalAabbTree_r("
    _world_aabb_no_decal_build_call)
if (_world_aabb_preflight_call EQUAL -1
    OR _world_aabb_no_decal_build_call EQUAL -1
    OR _world_aabb_preflight_call GREATER _world_aabb_no_decal_build_call)
    message(FATAL_ERROR
        "Raw BSP AABB no-decal output must preflight before writing")
endif()
require_source_contains(
    "database/db_load.cpp"
    "db::validation::CountInRange(varFont->glyphCount, 96, 65536)"
    "font glyph tables must cover direct ASCII indexing without oversized counts")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varMaterialArgumentDef->literalConst,
                16,
                4,
                kDirectBlock4"
    "literal material constants must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varMaterial->constantTable,
                constantByteCount,
                16,
                kDirectBlock4"
    "material constant tables must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varMaterial->stateBitsTable,
                stateBitsByteCount,
                4,
                kDirectBlock4"
    "material state-bit tables must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "DB_ResolveDirectPointer(
                &varGfxAabbTree->smodelIndexes,
                smodelIndexByteCount,
                2,
                kDirectBlock4"
    "world AABB indices must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varGfxWorldDpvsPlanes->planes,
                planeByteCount,
                4,
                kDirectBlock4"
    "world planes must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varXSurfaceVertexInfo->vertsBlend,
                blendByteCount,
                2,
                kDirectBlock4"
    "surface blend weights must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varXModel->boneNames,
                boneNameByteCount,
                2,
                kDirectBlock4"
    "model bone names must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varcNode_t->plane,
                20,
                4,
                kDirectBlock4"
    "collision-node planes must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varcLeafBrushNodeLeaf_t->brushes,
                brushIndexByteCount,
                2,
                kDirectBlock4"
    "leaf-brush indices must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "Load_CollisionBorderArray(1, varCollisionPartition->borderCount)"
    "inline collision partitions must load their complete border array")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varCollisionPartition->borders,
                borderByteCount,
                4,
                kDirectBlock4"
    "collision partition borders must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varFont->glyphs,
                glyphByteCount,
                4,
                kDirectBlock4"
    "font glyphs must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "OptionalMirroredCountInRange(count, originalCount, 2, 16)"
    "weapon accuracy graphs must fit fixed runtime buffers and use matching counts")
require_source_contains(
    "database/db_load.cpp"
    "if (!DB_ValidateWeaponAccuracyGraph(
                varWeaponDef,"
    "weapon definitions must run accuracy-graph validation before loading payloads")
require_source_contains(
    "database/db_load.cpp"
    "db::validation::NormalizedGraphKnots("
    "weapon accuracy graphs must validate normalized interpolation inputs")
require_source_contains(
    "database/db_load.cpp"
    "if (!DB_ValidateWeaponAccuracyGraphKnots(varWeaponDef, 0))"
    "first weapon accuracy graph must be validated after materialization")
require_source_contains(
    "database/db_load.cpp"
    "if (!DB_ValidateWeaponAccuracyGraphKnots(varWeaponDef, 1))"
    "second weapon accuracy graph must be validated after materialization")
require_source_contains(
    "database/db_load.cpp"
    "Load_vec2_tArray(1, varWeaponDef->originalAccuracyGraphKnotCount[0])"
    "first original accuracy graph must load with the count used by its runtime consumer")
require_source_contains(
    "database/db_load.cpp"
    "Load_vec2_tArray(1, varWeaponDef->originalAccuracyGraphKnotCount[1])"
    "second original accuracy graph must load with the count used by its runtime consumer")
require_source_contains(
    "bgame/bg_weapons_load_obj.cpp"
    "CountInRange(declaredKnotCount, 2, 16)"
    "load-object weapon graphs must reject counts outside their stack-buffer capacity")
require_source_contains(
    "bgame/bg_weapons_load_obj.cpp"
    "if (knotCountIndex >= 16)"
    "load-object weapon graphs must check capacity before storing a knot")
require_source_contains(
    "bgame/bg_weapons_load_obj.cpp"
    "db::validation::NormalizedGraphKnots("
    "load-object weapon graphs must enforce runtime interpolation invariants")
require_source_contains(
    "database/db_load.cpp"
    "if (!Load_MaterialVertexDeclaration(1))
                return false;"
    "material vertex declarations must validate before runtime realization")
require_source_contains(
    "gfx_d3d/r_material.cpp"
    "db::validation::MaterialVertexRoutingValid"
    "all material vertex-declaration construction paths must validate routing indices")
require_source_contains(
    "gfx_d3d/r_material.cpp"
    "memset((*mtlVertDecl)->routing.decl, 0"
    "material vertex declarations must discard serialized runtime handles")
require_source_contains(
    "database/db_load.cpp"
    "uint16_t destinationMask = 0"
    "material vertex declarations must reject duplicate destination semantics")
require_source_contains(
    "database/db_load.cpp"
    "db::validation::MaterialPassLayoutValid("
    "material passes must bound serialized argument partitions and custom flags")
require_source_contains(
    "database/db_load.cpp"
    "db::validation::MaterialArgumentShapeValid("
    "material shader arguments must bound runtime register and source indices")
require_source_contains(
    "database/db_load.cpp"
    "MaterialCodeConstantAllowedInSegment("
    "material code constants must match their serialized update-frequency partition")
require_source_contains(
    "database/db_load.cpp"
    "MaterialCodeSamplerAllowedInSegment("
    "material code samplers must match their serialized update-frequency partition")
require_source_contains(
    "database/db_load.cpp"
    "if (!DB_ValidateMaterialPassArguments(varMaterialPass, argumentCount))"
    "material arguments must be validated after their literal fixups")
require_source_contains(
    "database/db_load.cpp"
    "uint32_t vertexRegisterMask = 0"
    "material arguments must reject overlapping vertex constant registers")
require_source_contains(
    "database/db_load.cpp"
    "uint16_t pixelSamplerMask = 0"
    "material arguments must reject stored/custom pixel sampler collisions")
require_source_contains(
    "database/db_load.cpp"
    "uint32_t pixelConstantMask[8] = {}"
    "material arguments must reject overlapping pixel constant registers")
require_source_contains(
    "database/db_load.cpp"
    "CountInRange(varMaterialTechnique->passCount, 1, 4)"
    "material techniques must fit the four-pass authored/runtime bound")
require_source_contains(
    "database/db_load.cpp"
    "varMaterialTechnique->flags &= UINT16_C(0x3F)"
    "material techniques must clear the serialized runtime upload bit")
require_source_contains(
    "database/db_load.cpp"
    "varMaterialTechniqueSet->worldVertFormat,
            0,
            11"
    "material world vertex formats must not index past declaration variants")
require_source_contains(
    "database/db_load.cpp"
    "varMaterialTechniqueSet->hasBeenUploaded = false"
    "material technique sets must not trust serialized upload state")
require_source_contains(
    "database/db_load.cpp"
    "varMaterialTechniqueSet->remappedTechniqueSet = nullptr"
    "material technique sets must not trust serialized remap pointers")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "consts->count >= ARRAY_COUNT(consts->dest)"
    "pixel literal collection must enforce its runtime capacity")
require_source_contains(
    "gfx_d3d/rb_shade.cpp"
    "static_cast<uint32_t>(primState->vertDeclType) >= VERTDECL_COUNT"
    "material binding must bound the runtime vertex declaration type")
require_source_contains(
    "gfx_d3d/r_shade.cpp"
    "static_cast<uint32_t>(state->prim.vertDeclType) >= VERTDECL_COUNT"
    "material updates must bound the runtime vertex declaration type")
require_source_contains(
    "gfx_d3d/rb_uploadshaders.cpp"
    "static_cast<uint32_t>(vertDeclType) >= VERTDECL_COUNT"
    "material uploads must bound the runtime vertex declaration type")

file(READ
    "${SOURCE_ROOT}/src/bgame/bg_weapons_load_obj.cpp"
    _weapon_graph_load_obj_source)
string(FIND
    "${_weapon_graph_load_obj_source}"
    "if (knotCountIndex >= 16)"
    _weapon_graph_capacity_check)
string(FIND
    "${_weapon_graph_load_obj_source}"
    "knots[knotCountIndex][0] = x"
    _weapon_graph_first_store)
if (_weapon_graph_capacity_check EQUAL -1
    OR _weapon_graph_first_store EQUAL -1
    OR _weapon_graph_capacity_check GREATER _weapon_graph_first_store)
    message(FATAL_ERROR
        "Load-object weapon graph capacity must be checked before the first knot store")
endif()
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varWeaponDef->accuracyGraphKnots[0],
                accuracyGraphByteCount[0],
                4,
                kDirectBlock4"
    "first working accuracy graph must use its full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varWeaponDef->originalAccuracyGraphKnots[0],
                accuracyGraphByteCount[0],
                4,
                kDirectBlock4"
    "first original accuracy graph must use its full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varWeaponDef->accuracyGraphKnots[1],
                accuracyGraphByteCount[1],
                4,
                kDirectBlock4"
    "second working accuracy graph must use its full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varWeaponDef->originalAccuracyGraphKnots[1],
                accuracyGraphByteCount[1],
                4,
                kDirectBlock4"
    "second original accuracy graph must use its full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "DBAliasKind::XStringPointerSlot"
    "direct string-holder references must use completed-object provenance")
require_source_contains(
    "database/db_relocation.h"
    "CompletedSharedObjectSchemaValid("
    "completed shared objects must use a portable tested disk32 schema")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varsnd_alias_t->soundFile,
                DBAliasKind::SoundFile,
                disk32::kSoundFileBytes"
    "shared sound files must resolve through exact typed provenance")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varsnd_alias_t->speakerMap,
                DBAliasKind::SpeakerMap,
                disk32::kSpeakerMapBytes"
    "shared speaker maps must resolve through exact typed provenance")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varsnd_alias_list_t->head,
                DBAliasKind::SndAliasArray,
                aliasByteCount"
    "shared sound-alias arrays must resolve through exact typed provenance")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varWeaponDef->bounceSound,
                DBAliasKind::WeaponBounceSoundTable,
                disk32::kWeaponBounceSoundTableBytes"
    "weapon bounce-sound tables must resolve through exact typed provenance")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varGfxWorld->sunLight,
                DBAliasKind::GfxLight,
                disk32::kGfxLightBytes"
    "shared world sun lights must resolve through exact typed provenance")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)varStringTablePtr,
                DBAliasKind::StringTable,
                disk32::kStringTableBytes"
    "shared string tables must resolve through exact typed provenance")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)varXModelPiecesPtr,
                DBAliasKind::XModelPieces,
                disk32::kXModelPiecesBytes"
    "shared model-pieces headers must resolve through exact typed provenance")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varXRigidVertList->collisionTree,
                DBAliasKind::XSurfaceCollisionTree,
                disk32::kXSurfaceCollisionTreeBytes"
    "shared surface collision trees must resolve through exact typed provenance")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varXSurface->vertList,
                DBAliasKind::XRigidVertListArray,
                rigidListBytes"
    "shared rigid-vertex lists must resolve through exact typed provenance")
require_source_contains(
    "database/db_load.cpp"
    "db::validation::SoundFileHeaderValid("
    "sound-file union tags and existence flags must validate before dispatch")
require_source_contains(
    "database/db_load.cpp"
    "DB_ValidateSpeakerMap(varSpeakerMap)"
    "speaker-map fixed arrays and levels must validate before completion")
require_source_contains(
    "database/db_load.cpp"
    "CountInRange(varSndCurve->knotCount, 2, 8)"
    "sound falloff curves must fit their fixed knot array")
require_source_contains(
    "database/db_load.cpp"
    "NormalizedGraphKnots(
            varSndCurve->knots"
    "sound falloff curves must be finite, normalized, and strictly ordered")
require_source_contains(
    "database/db_load.cpp"
    "if (!DB_ValidateSoundAlias(varsnd_alias_t))"
    "sound aliases must validate required completed children before array publication")
require_source_contains(
    "database/db_load.cpp"
    "if (!varsnd_alias_list_t->aliasName || !*varsnd_alias_list_t->aliasName)"
    "sound-alias lists must have a completed name before asset publication")
require_source_contains(
    "database/db_load.cpp"
    "if (!varStringTable->name || !*varStringTable->name)"
    "string tables must have a completed name before asset publication")
require_source_contains(
    "database/db_load.cpp"
    "varStringTable->values,
            valueCount,
            \"string-table values\""
    "string-table values must match their validated dimensions")
require_source_contains(
    "database/db_load.cpp"
    "if (!DB_ValidateSunLight(varGfxWorld->sunLight))"
    "world sun-light values must validate before exact completion")
require_source_contains(
    "database/db_load.cpp"
    "XModelPiecesLayoutValid(
            pieces->name != nullptr,
            pieces->pieces != nullptr,
            pieces->numpieces"
    "model-pieces arrays must preserve pointer/count and fit the uint16 source-format extent")
require_source_contains(
    "database/db_load.cpp"
    "XModelPieceRuntimeValid("
    "model pieces must have finite offsets and completed model pointers")
require_source_contains(
    "database/db_load.cpp"
    "DB_IsStreamRangeValid(
            varXModelPiece,
            static_cast<uint32_t>(pieceBytes))"
    "model-pieces child spans must fit before element fixups begin")
require_source_contains(
    "database/db_load.cpp"
    "ValidateXSurfaceCollisionTopology("
    "surface collision nodes, leaves, and triangle spans must validate before publication")
require_source_contains(
    "database/db_load.cpp"
    "XSurfaceRigidPartitionValid("
    "rigid surface partitions must validate against completed geometry")
require_source_contains(
    "database/db_load.cpp"
    "XSurfaceTriangleIndicesValid("
    "surface triangle indices must remain inside the completed vertex span")
require_source_contains(
    "database/db_validation.h"
    "kMaxBrushNonaxialSides = 26"
    "model physics brushes must retain the source builder side budget")
require_source_contains(
    "database/db_validation.h"
    "kMaxBrushAdjacencyEntries = 32 * 12"
    "model physics brush adjacency must retain the source builder edge budget")
require_source_contains(
    "database/db_validation.h"
    "side.plane != &brush.planes[sideIndex]"
    "completed physics brush sides must reference their indexed owned planes")
require_source_contains(
    "database/db_validation.h"
    "brush.axialMaterialNum[direction][axis] != 0"
    "model physics brushes must retain their source-generated zero axial materials")
require_source_contains(
    "database/db_validation.h"
    "side.materialNum != 0"
    "model physics brush sides must retain their source-generated zero materials")
require_source_contains(
    "database/db_validation.h"
    "BrushMaterialIndex("
    "runtime brush materials must use a portable bounds-checked selector")
require_source_contains(
    "physics/phys_world_collision.cpp"
    "if (!cm.materials
        || !db::validation::BrushMaterialIndex("
    "physics collision must validate brush and material indices before table access")
require_source_contains(
    "database/db_validation.h"
    "PhysGeomInfoRuntimeValid("
    "physics geometry tags, primitive dimensions, and nested brushes must validate")
require_source_contains(
    "database/db_validation.h"
    "PhysMassFinite(list.mass)"
    "physics mass properties must be finite before publication")
require_source_contains(
    "database/db_load.cpp"
    "disk32::PointerToken sidePlaneTokens["
    "physics brush-side forward tokens must be preserved outside native pointer fields")
require_source_ordered(
    "database/db_load.cpp"
    "Load_cplane_tArray(
                1,
                static_cast<int32_t>(varBrushWrapper->numsides));"
    "const db::relocation::Status status = DB_ResolveOffsetBytes(
            token,"
    "physics brush-side forward references must wait for the wrapper plane array")
require_source_ordered(
    "database/db_load.cpp"
    "DB_RegisterPointerSlot(
                varPhysGeomInfo->brush,
                DBAliasKind::BrushWrapper)"
    "if (!Load_BrushWrapper(1))"
    "physics brush provenance must register before child fixups")
require_source_ordered(
    "database/db_load.cpp"
    "if (!Load_BrushWrapper(1))"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::BrushWrapper"
    "physics brush provenance must publish after graph validation")
require_source_contains(
    "database/db_load.cpp"
    "DB_ResolveCompletedPointer(
                &varPhysGeomInfo->brush,
                DBAliasKind::BrushWrapper,
                disk32::kBrushWrapperBytes"
    "shared physics brushes must resolve through exact typed provenance")
require_source_ordered(
    "database/db_load.cpp"
    "DB_RegisterPointerSlot(
                varXModel->physGeoms,
                DBAliasKind::PhysGeomList)"
    "if (!Load_PhysGeomList(1)"
    "physics geometry-list provenance must register before nested brush loading")
require_source_ordered(
    "database/db_load.cpp"
    "if (!Load_PhysGeomList(1)"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::PhysGeomList"
    "physics geometry lists must publish only after nested validation")
require_source_contains(
    "database/db_load.cpp"
    "DB_ResolveCompletedPointer(
                &varXModel->physGeoms,
                DBAliasKind::PhysGeomList,
                disk32::kPhysGeomListBytes"
    "shared physics geometry lists must resolve through exact typed provenance")
require_source_contains(
    "database/db_validation.h"
    "kMaxClipMapBrushNonaxialSides = 250"
    "clipmap brushes must fit fixed 256-plane collision workspaces")
require_source_contains(
    "database/db_validation.h"
    "ClipMapPlaneValid("
    "clipmap plane metadata and finite values must validate before use")
require_source_contains(
    "database/db_validation.h"
    "ClipMapBrushGraphValid("
    "clipmap brush sides and adjacency must form an exact global partition")
require_source_ordered(
    "database/db_load.cpp"
    "ClipMapBrushLayoutValid(
            *varclipMap_t,
            &brushExtents)"
    "varXString = &varclipMap_t->name;"
    "clipmap array extents must validate before child materialization")
require_source_contains(
    "database/db_load.cpp"
    "Invalid inline fast-file clipmap brush sides"
    "clipmap brush sides must use bounded global direct references")
require_source_ordered(
    "database/db_load.cpp"
    "ClipMapBrushAdjacencyPrefixExtent(
            *varcbrush_t,
            &adjacencyBytes)"
    "DB_ResolveDirectPointer(
                &varcbrush_t->baseAdjacentSide"
    "clipmap adjacency extents must be derived before resolving their token")
require_source_ordered(
    "database/db_load.cpp"
    "DB_ResolveDirectPointer(
                &varcbrush_t->baseAdjacentSide"
    "if (!DB_GetClipBrushAdjacencyBytes(
            varcbrush_t,
            &validatedAdjacencyBytes)"
    "clipmap adjacency contents must not be read before bounded resolution")
require_source_ordered(
    "database/db_load.cpp"
    "DB_RegisterPointerSlot(
                varclipMap_t->box_brush,
                DBAliasKind::ClipMapBoxBrush)"
    "if (!Load_cbrush_t(1))"
    "clipmap box-brush provenance must register before child fixups")
require_source_contains(
    "database/db_load.cpp"
    "DB_ResolveCompletedPointer(
                &varclipMap_t->box_brush,
                DBAliasKind::ClipMapBoxBrush,
                disk32::kCBrushBytes"
    "shared clipmap box brushes must resolve through exact typed provenance")
require_source_ordered(
    "database/db_load.cpp"
    "ClipMapBrushGraphValid(*varclipMap_t)"
    "DB_CompleteObject(
            completedBoxBrush,
            DBAliasKind::ClipMapBoxBrush"
    "clipmap box brushes must publish only after whole-graph validation")
require_source_ordered(
    "database/db_load.cpp"
    "if (!Load_clipMap_t(1))"
    "Load_ClipMapAsset((XAssetHeader *)varclipMap_ptr)"
    "invalid clipmap graphs must stop asset publication")
foreach(_clipmap_collision_source
    "physics/phys_coll_boxbrush.cpp"
    "physics/phys_coll_capsulebrush.cpp"
    "physics/phys_coll_cylinderbrush.cpp")
    require_source_contains(
        "${_clipmap_collision_source}"
        "db::validation::kMaxClipMapBrushNonaxialSides"
        "clipmap collision workspaces must reject oversized brush graphs")
endforeach()
require_source_ordered(
    "xanim/xmodel.cpp"
    "if (nextNodeQueueEnd == locals->nodeQueueBegin)"
    "locals->nodeQueue[locals->nodeQueueEnd].beginIndex = childBeginIndex"
    "surface collision traversal must reject a full node queue before writing")
require_source_not_contains(
    "xanim/xmodel.cpp"
    "0xFFFFFF80"
    "surface prefetch stubs must not truncate native-width addresses")
require_source_ordered(
    "DynEntity/DynEntity_pieces.cpp"
    "if (!model->physPreset)"
    "model->physPreset->piecesUpwardVelocity"
    "piece spawning must reject missing physics presets before dereference")
require_source_ordered(
    "database/db_load.cpp"
    "DB_RegisterPointerSlot(
                varsnd_alias_t->soundFile,
                DBAliasKind::SoundFile)"
    "if (!Load_SoundFile(1))"
    "sound-file provenance must register before child fixups")
require_source_ordered(
    "database/db_load.cpp"
    "if (!Load_SoundFile(1))"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::SoundFile"
    "sound-file provenance must publish after child fixups")
require_source_ordered(
    "database/db_load.cpp"
    "DB_RegisterPointerSlot(
                varsnd_alias_t->speakerMap,
                DBAliasKind::SpeakerMap)"
    "if (!Load_SpeakerMap(1))"
    "speaker-map provenance must register before child fixups")
require_source_ordered(
    "database/db_load.cpp"
    "if (!Load_SpeakerMap(1))"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::SpeakerMap"
    "speaker-map provenance must publish after validation")
require_source_ordered(
    "database/db_load.cpp"
    "DB_RegisterPointerSlot(
                varsnd_alias_list_t->head,
                DBAliasKind::SndAliasArray)"
    "if (!Load_snd_alias_tArray(1, varsnd_alias_list_t->count))"
    "sound-alias array provenance must register before element fixups")
require_source_ordered(
    "database/db_load.cpp"
    "if (!Load_snd_alias_tArray(1, varsnd_alias_list_t->count))"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::SndAliasArray"
    "sound-alias array provenance must publish after element fixups")
require_source_ordered(
    "database/db_load.cpp"
    "DB_RegisterPointerSlot(
                varWeaponDef->bounceSound,
                DBAliasKind::WeaponBounceSoundTable)"
    "Load_snd_alias_list_nameArray(
                1,
                disk32::kWeaponBounceSoundCount)"
    "bounce-sound provenance must register before holder fixups")
require_source_ordered(
    "database/db_load.cpp"
    "Load_snd_alias_list_nameArray(
                1,
                disk32::kWeaponBounceSoundCount)"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::WeaponBounceSoundTable"
    "bounce-sound provenance must publish after holder fixups")
require_source_ordered(
    "database/db_load.cpp"
    "DB_RegisterPointerSlot(
                serializedStringTable,
                DBAliasKind::StringTable)"
    "Load_StringTable(1)"
    "string-table provenance must register before child fixups")
require_source_ordered(
    "database/db_load.cpp"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::StringTable"
    "Load_StringTableAsset((XAssetHeader *)varStringTablePtr)"
    "string tables must publish their serialized identity before asset canonicalization")
require_source_ordered(
    "database/db_load.cpp"
    "DB_RegisterPointerSlot(
                varGfxWorld->sunLight,
                DBAliasKind::GfxLight)"
    "Load_GfxLight(1)"
    "sun-light provenance must register before child fixups")
require_source_ordered(
    "database/db_load.cpp"
    "if (!DB_ValidateSunLight(varGfxWorld->sunLight))"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::GfxLight"
    "sun-light provenance must publish after semantic validation")
require_source_ordered(
    "database/db_load.cpp"
    "DB_RegisterPointerSlot(
                *varXModelPiecesPtr,
                DBAliasKind::XModelPieces)"
    "if (!Load_XModelPieces(1))"
    "model-pieces provenance must register before child fixups")
require_source_ordered(
    "database/db_load.cpp"
    "if (!Load_XModelPieces(1))"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::XModelPieces"
    "model-pieces provenance must publish after child validation")
require_source_ordered(
    "database/db_load.cpp"
    "return DB_ValidateXModelPieces("
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::XModelPieces"
    "model-pieces semantic validation must precede publication")
require_source_contains(
    "database/db_load.cpp"
    "if (!Load_XModelPiecesPtr(0))
        return;"
    "dynamic-entity loading must stop after model-pieces rejection")
require_source_ordered(
    "database/db_load.cpp"
    "DB_RegisterPointerSlot(
                varXRigidVertList->collisionTree,
                DBAliasKind::XSurfaceCollisionTree)"
    "if (!Load_XSurfaceCollisionTree(1))"
    "surface collision-tree provenance must register before child loading")
require_source_ordered(
    "database/db_load.cpp"
    "if (!Load_XSurfaceCollisionTree(1))"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::XSurfaceCollisionTree"
    "surface collision trees must publish after topology validation")
require_source_ordered(
    "database/db_load.cpp"
    "completedRigidLists = DB_RegisterPointerSlot(
                varXSurface->vertList,
                DBAliasKind::XRigidVertListArray)"
    "if (!Load_XRigidVertListArray("
    "rigid-list provenance must register before nested tree loading")
require_source_ordered(
    "database/db_load.cpp"
    "Load_r_index16_tArray(1, indexCount)"
    "if (!DB_ValidateLoadedXSurface(
            varXSurface,
            deformed,
            varXModel ? varXModel->numBones : 0))"
    "rigid-list validation must wait for surface triangle loading")
require_source_ordered(
    "database/db_load.cpp"
    "if (!DB_ValidateLoadedXSurface(
            varXSurface,
            deformed,
            varXModel ? varXModel->numBones : 0))"
    "DB_CompleteObject(
            completedRigidLists,
            DBAliasKind::XRigidVertListArray"
    "rigid lists must publish only after consumer-specific validation")
require_source_ordered(
    "database/db_load.cpp"
    "if (!Load_XModel(1))"
    "Load_XModelAsset((XAssetHeader *)varXModelPtr)"
    "invalid surface graphs must stop model asset publication")
require_source_contains(
    "database/db_stream.cpp"
    "DB_ValidateStreamCString"
    "completed string holders must validate registered pointee provenance")
require_source_contains(
    "database/db_relocation.cpp"
    "resolvedAddress != aliasBlock.base + record.offset"
    "completed string holders must publish their exact registered slot")
require_source_contains(
    "database/db_relocation.cpp"
    "RequiresExactStartPublication(expectedKind)"
    "completed material objects must publish their exact registered start")
require_source_contains(
    "database/db_stream.cpp"
    "g_directResolver.ValidateAddress("
    "completed material objects must cover a fully materialized stream span")
require_source_contains(
    "database/db_load.cpp"
    "DBAliasKind::MaterialVertexDeclaration"
    "material vertex declaration references must use completed-object provenance")
require_source_contains(
    "database/db_load.cpp"
    "DB_ConvertOffsetToAlias(
                (uint32_t*)&varMaterialPass->vertexDecl,
                DBAliasKind::MaterialVertexDeclaration,
                disk32::kMaterialVertexDeclarationBytes)"
    "shared material vertex declarations must resolve through exact typed provenance")
require_source_contains(
    "database/db_load.cpp"
    "disk32::kMaterialVertexDeclarationBytes"
    "material vertex declaration provenance must use its fixed disk32 extent")
require_source_contains(
    "database/db_load.cpp"
    "DB_ConvertOffsetToAlias(
                (uint32_t*)varMaterialTechniquePtr,
                DBAliasKind::MaterialTechnique,
                disk32::kMaterialTechniqueSchema)"
    "shared material techniques must resolve through exact typed provenance")
require_source_contains(
    "database/db_validation.h"
    "MaterialTechniqueDiskBytes("
    "material technique extents must be derived from their bounded pass count")
require_source_contains(
    "database/db_stream.cpp"
    "expectedKind == DBAliasKind::MaterialTechnique"
    "completed material techniques must recheck their serialized pass count")
require_source_contains(
    "database/db_stream.cpp"
    "Completed fast-file objects require DB_CompleteObject validation"
    "generic alias publication must reject completed material object kinds")
require_source_contains(
    "database/db_load.cpp"
    "D3D9ShaderBytecodeValid("
    "material shader bytecode must be structurally bounded before D3D creation")
require_source_contains(
    "database/db_load.cpp"
    "DB_ValidateStreamAddress("
    "material shader bytecode must cover a fully materialized stream span")
require_source_contains(
    "database/db_load.cpp"
    "DB_ConvertOffsetToAlias(
                (uint32_t*)varMaterialVertexShaderPtr,
                DBAliasKind::MaterialVertexShader,
                disk32::kMaterialVertexShaderBytes)"
    "shared material vertex shaders must resolve through stage-specific provenance")
require_source_contains(
    "database/db_load.cpp"
    "DB_ConvertOffsetToAlias(
                (uint32_t*)varMaterialPixelShaderPtr,
                DBAliasKind::MaterialPixelShader,
                disk32::kMaterialPixelShaderBytes)"
    "shared material pixel shaders must resolve through stage-specific provenance")
require_source_contains(
    "database/db_load.cpp"
    "Fast-file material pass mixes renderer shader variants"
    "material passes must pair vertex and pixel shaders for the same renderer")
require_source_contains(
    "database/db_load.cpp"
    "Fast-file material technique mixes renderer shader variants"
    "all material technique passes must target the same renderer")
require_source_contains(
    "database/db_load.cpp"
    "Fast-file material technique set mixes renderer variants"
    "all techniques in a set must target the same renderer")
require_source_contains(
    "database/db_load.cpp"
    "DB_ConvertOffsetToAlias(
                (uint32_t*)varMaterialTextureDefInfo,
                DBAliasKind::MaterialWater,
                disk32::kMaterialWaterBytes)"
    "shared material water must resolve through exact typed provenance")
require_source_contains(
    "database/db_load.cpp"
    "FiniteComplexArray(
            varwater_t->H0,"
    "material water amplitudes must be finite")
require_source_contains(
    "database/db_load.cpp"
    "FiniteNonnegativeFloatArray(
            varwater_t->wTerm,"
    "material water frequencies must be finite and nonnegative")
require_source_contains(
    "database/db_load.cpp"
    "FiniteFloatArray(water->codeConstant, 4)"
    "material water code constants must be finite")
require_source_contains(
    "gfx_d3d/r_water.cpp"
    "water->image->width != expectedImageWidth
        || water->image->height != expectedImageHeight"
    "loaded material water must target its mode-specific water image dimensions")
require_source_contains(
    "gfx_d3d/r_water.cpp"
    "Com_Error(ERR_DROP, \"Invalid material water image contract\")"
    "loaded material water image contract failures must reject publication")
require_source_contains(
    "database/db_stream.cpp"
    "case DBAliasKind::MaterialWater:
        if (metadata != disk32::kMaterialWaterBytes
            || materializedBytes != disk32::kMaterialWaterBytes)"
    "completed material water must use its fixed disk32 schema")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "water_t setup = {}"
    "raw material water must initialize serialized and runtime state")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "(const char*)material + texdef->u.waterDefOffset"
    "raw material water offsets must use byte arithmetic")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "(const char*)mtlRaw
                            + textureTableRaw[texIndex].u.waterDefOffset"
    "raw material construction must use byte offsets for water definitions")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "setup.writable.floatTime = kWaterInitialTime"
    "raw material water input must initialize runtime time state")
require_source_contains(
    "gfx_d3d/r_water_load_obj.cpp"
    "destination->writable.floatTime = kWaterInitialTime"
    "cached raw material water must scrub runtime time state")
require_source_contains(
    "database/db_load.cpp"
    "return Load_MaterialTextureDefInfo(0)"
    "material texture definitions must propagate nested payload failure")
require_source_contains(
    "database/db_load.cpp"
    "if (!Load_MaterialTextureDef(0))"
    "material texture arrays must propagate element failure")
require_source_contains(
    "database/db_load.cpp"
    "if (!Load_MaterialTextureDefArray(1, varMaterial->textureCount))"
    "materials must propagate texture-table failure")
require_source_contains(
    "database/db_load.cpp"
    "if (!Load_Material(1))"
    "material handles must reject failed child graphs before publication")
require_source_contains(
    "database/db_load.cpp"
    "if (varMaterialTextureDefInfo && *varMaterialTextureDefInfo)"
    "material water marking must test the payload rather than its holder")
require_source_contains(
    "database/db_load.cpp"
    "StrictlyIncreasingNameHashes(
            material->constantTable"
    "material constant tables must be strictly hash ordered")
require_source_contains(
    "database/db_load.cpp"
    "FiniteFloatArray(
                material->constantTable[constantIndex].literal"
    "material constants must contain only finite values")
require_source_contains(
    "database/db_load.cpp"
    "if (argument.type == 2)
                {
                    if (!db::validation::SortedNameHashContains(
                            material->textureTable,
                            material->textureCount,
                            argument.u.nameHash))"
    "named material sampler arguments must resolve within the texture table")
require_source_contains(
    "database/db_load.cpp"
    "else if (argument.type == 0 || argument.type == 6)
                {
                    if (!db::validation::SortedNameHashContains(
                            material->constantTable,
                            material->constantCount,
                            argument.u.nameHash))"
    "named material constant arguments must resolve within the constant table")
require_source_contains(
    "database/db_load.cpp"
    "MaterialTechniqueStateSpanValid("
    "material technique pass states must fit the state table")
require_source_contains(
    "database/db_load.cpp"
    "MaterialRemapSlotValid("
    "material technique remaps must preserve occupied pass spans")
require_source_contains(
    "database/db_load.cpp"
    "MaterialStateBitsDecodeSafe("
    "material render states must not index beyond D3D decode tables")
require_source_contains(
    "database/db_load.cpp"
    "!db::validation::FiniteFloatArray(
                        argument.u.literalConst,
                        4)"
    "inline material shader literals must be finite")
require_source_contains(
    "database/db_load.cpp"
    "varMaterial->stateBitsTable = nullptr"
    "present-empty material state tables must be canonicalized")
require_source_contains(
    "database/db_load.cpp"
    "varMaterial->constantTable = nullptr"
    "present-empty material constant tables must be canonicalized")
require_source_contains(
    "database/db_load.cpp"
    "material->info.sortKey >= 64"
    "material sort keys must fit the renderer's fixed tables")
require_source_contains(
    "database/db_load.cpp"
    "candidate->worldVertFormat != original->worldVertFormat"
    "loaded material remaps must preserve the world vertex format")
require_source_contains(
    "database/db_load.cpp"
    "if (!DB_ValidateMaterialNamedInputs(material, original)
        || (candidate != original
            && !DB_ValidateMaterialNamedInputs(material, candidate)))"
    "both original and initially remapped techniques must resolve named inputs")
require_source_contains(
    "gfx_d3d/r_material_override.cpp"
    "bool __cdecl Material_ValidateRemappedTechniqueSet"
    "runtime material technique remaps must use release-build validation")
require_source_contains(
    "gfx_d3d/r_material_override.cpp"
    "else if (!Material_ValidateRemappedTechniqueSet(techSet))
    {
        techSet->remappedTechniqueSet = techSet;"
    "invalid dynamic material remaps must fall back to the original set")
require_source_contains(
    "gfx_d3d/r_material_override.cpp"
    "(void)Material_ValidateRemappedTechniqueSet(techSet);"
    "initial renderer remaps must remain visible to material graph validation")
require_source_contains(
    "gfx_d3d/r_shade.cpp"
    "texDef = db::validation::FindSortedNameHash(
        material->textureTable,
        material->textureCount,
        arg->u.nameHash);
    if (!texDef)"
    "runtime named texture lookup must fail within its material table")
require_source_contains(
    "gfx_d3d/r_shade.cpp"
    "constDef = db::validation::FindSortedNameHash(
            material->constantTable,
            material->constantCount,
            arg->u.nameHash);
        if (!constDef)"
    "runtime named shader constants must fail within their material table")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "constDef = db::validation::FindSortedNameHash(
                mtl->constantTable,
                mtl->constantCount,
                arg->u.nameHash);
            if (!constDef)"
    "material sorting must bound named pixel-constant lookup")
require_source_not_contains(
    "gfx_d3d/r_shade.cpp"
    "while (texDef->nameHash !="
    "runtime texture lookup must not walk beyond the material table")
require_source_not_contains(
    "gfx_d3d/r_shade.cpp"
    "while (constDef->nameHash !="
    "runtime constant lookup must not walk beyond the material table")
require_source_not_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "while (constDef->nameHash !="
    "material sorting must not walk beyond the constant table")
require_source_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "if (texdef->semantic == TS_WATER_MAP)
            {
                Com_Error("
    "sky water rejection must occur before interpreting the texture union as an image")
require_source_not_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "texdef->u.image->name"
    "sky errors must not dereference a water payload as an image")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "mtl->textureTable[texIndex].semantic != TS_NORMAL_MAP
        || !mtl->textureTable[texIndex].u.image
        || !mtl->textureTable[texIndex].u.image->name"
    "normal-map inspection must validate the image union member before use")
require_source_contains(
    "gfx_d3d/r_shade.cpp"
    "R_UploadWaterTexture(texDef->u.water, floatTime);
        image = texDef->u.water->image;"
    "runtime water validation must precede image dereference")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "material->textureTable[texIndex].u.water =
                    Material_RegisterWaterImage("
    "raw water construction must use the active water union member")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "newTexEntry->u = v5->u;"
    "layered material texture payloads must copy the complete union")
require_source_contains(
    "gfx_d3d/r_material.cpp"
    "const Material *material = header.material;"
    "material picmip enumeration must use the typed asset-header member")
require_source_contains(
    "gfx_d3d/r_material.cpp"
    "material->textureCount && !material->textureTable"
    "material picmip enumeration must validate pointer/count consistency")
require_source_contains(
    "gfx_d3d/r_material.cpp"
    "DB_EnumXAssets(ASSET_TYPE_MATERIAL, Material_UpdatePicmipSingle, 0, 1);"
    "material picmip enumeration must use a native-width callback")
require_source_not_contains(
    "gfx_d3d/r_material.cpp"
    "header.xmodelPieces["
    "material picmip enumeration must not depend on 32-bit header layout tricks")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "worldVertFormat = s_worldVertFormatForLayerCount[layerCount - 1];"
    "layered world vertex formats must use their dedicated bounded table")
require_source_not_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "s_stateMapDstStencilBitGroup[9].stateBitsMask[layerCount + 1]"
    "layered world vertex formats must not read beyond an unrelated stencil table")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "worldVertFormatIndex >= MTL_WORLDVERT_COUNT"
    "layered material world vertex formats must fit the renderer table")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "normalMapCount > 3"
    "layered material normal maps must fit the world vertex declarations")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "CheckedMaterialTableCountSum("
    "layered texture and constant counts must reject uint8 overflow")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "layerMtl[layerIndex]->textureCount
                && !layerMtl[layerIndex]->textureTable"
    "layered materials must validate source texture pointer/count consistency")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "layerMtl[layerIndex]->constantCount
                && !layerMtl[layerIndex]->constantTable"
    "layered materials must validate source constant pointer/count consistency")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "return (hash0 > hash1) - (hash0 < hash1);"
    "material hash comparators must report equality")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "if (mtl0 == mtl1)
        return 0;"
    "material qsort comparison must report equality for identical entries")
require_source_not_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "return comparison < 0;"
    "material qsort comparison must return a three-way result")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "StrictlyIncreasingNameHashes(
            newMtl->textureTable"
    "layered material tables must reject ambiguous duplicate hashes")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "memset(material, 0, sizeof(*material));"
    "raw runtime materials must initialize absent tables to null")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "StrictlyIncreasingNameHashes(
            material->textureTable"
    "raw material tables must reject ambiguous duplicate hashes")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "static_cast<uint64_t>(sizeof(Material))"
    "layered allocation layout must use native object size with checked arithmetic")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "stateBitsTable + *stateBitsCount,
        stateBitsForPass + partialMatchCount,
        sizeof(*stateBitsTable)"
    "layered render-state merging must use row-pointer arithmetic")
require_source_not_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "&(*stateBitsTable)[2 *"
    "layered render-state merging must not index beyond an inner row")
require_source_contains(
    "database/database.h"
    "using DBEnumXAssetCallback = void(__cdecl *)(XAssetHeader, void *);"
    "database enumeration must use one native-width callback type")
require_source_contains(
    "database/db_registry.cpp"
    "DB_EnumXAssets_LoadObj(type, func, inData);"
    "load-object enumeration must preserve the canonical callback signature")
require_source_contains(
    "database/db_registry.cpp"
    "asset.material = header;"
    "load-object material enumeration must activate the typed header member")
require_source_contains(
    "database/db_registry.cpp"
    "asset.techniqueSet = header;"
    "load-object technique enumeration must activate the typed header member")
require_source_contains(
    "database/db_registry.cpp"
    "asset.image = header;"
    "load-object image enumeration must activate the typed header member")
require_source_contains(
    "database/db_registry.cpp"
    "asset.model = static_cast<XModel *>(fileData->data);"
    "load-object model enumeration must activate the typed header member")
require_source_contains(
    "database/db_registry.cpp"
    "asset.parts = static_cast<XAnimParts *>(fileData->data);"
    "load-object animation enumeration must activate the typed header member")
require_source_not_contains(
    "database/db_registry.cpp"
    "(void(*)(void *, void *))func"
    "load-object enumeration must not cast canonical callbacks")
require_source_contains(
    "universal/com_memory.cpp"
    "AssetList *assetList = static_cast<AssetList *>(data);"
    "Hunk asset enumeration must use the typed native-width context")
require_source_contains(
    "universal/com_memory.cpp"
    "assetList->assets[assetList->assetCount] = header;"
    "Hunk asset enumeration must use native array indexing")
require_source_contains(
    "universal/com_memory.cpp"
    "++assetList->assetCount;"
    "count-only Hunk asset enumeration must retain the required count")
require_source_not_contains(
    "universal/com_memory.cpp"
    "_DWORD *data"
    "Hunk asset enumeration must not reinterpret its context as 32-bit words")
require_source_not_contains(
    "universal/com_memory.cpp"
    "data[2]"
    "Hunk asset enumeration must not truncate the output pointer")
require_source_contains(
    "database/db_registry.cpp"
    "AssetOutputWriteAllowed(
                        assets != nullptr,
                        assetCount,
                        maxCount)"
    "fast-file asset enumeration must bound every output write")
require_source_contains(
    "database/db_registry.cpp"
    "InterlockedDecrement(&db_hashCritSect.readCount);
    if (assets && assetCount > maxCount)"
    "asset capacity failure must occur after releasing the database read lock")
require_source_contains(
    "database/db_registry.cpp"
    "DB_RemoveTechniqueSetAsset"
    "technique-set removal must use an exact one-argument adapter")
require_source_contains(
    "database/db_registry.cpp"
    "DB_RemoveImageAsset"
    "image removal must use an exact one-argument adapter")
require_source_contains(
    "universal/com_sndalias.cpp"
    "SndCurve *curve = header.sndCurve;"
    "sound-curve enumeration must use its typed asset member")
require_source_contains(
    "gfx_d3d/r_image.cpp"
    "GfxImage *image = header.image;"
    "delayed image enumeration must use its typed asset member")
require_source_not_contains(
    "gfx_d3d/r_image.cpp"
    "header.xmodelPieces["
    "image enumeration must not depend on x86 header layout")
require_source_contains(
    "cgame/cg_visionsets.cpp"
    "header.rawfile->name"
    "vision-set enumeration must use the raw-file asset member")
require_source_contains(
    "cgame/cg_modelpreviewer.cpp"
    "context->type == ASSET_TYPE_XMODEL && header.model"
    "model-preview enumeration must discriminate model headers")
require_source_contains(
    "cgame/cg_modelpreviewer.cpp"
    "context->type == ASSET_TYPE_XANIMPARTS && header.parts"
    "model-preview enumeration must discriminate animation headers")
require_source_contains(
    "cgame/cg_modelpreviewer.cpp"
    "alignof(const char *)"
    "model-preview name tables must use native pointer alignment")
require_source_contains(
    "gfx_d3d/r_model.cpp"
    "context->entries[context->count++] = header.model;"
    "model enumeration must use an explicit bounded context")
require_source_contains(
    "gfx_d3d/r_material.cpp"
    "entry.material = header.material;"
    "material enumeration must use an explicit native-width context")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "XAssetHeader materialHeaders[ARRAY_COUNT(rgp.sortedMaterials)];"
    "material sorting must not alias a Material pointer array as asset headers")
require_source_contains(
    "gfx_d3d/r_material.cpp"
    "Material_ForEachTechniqueSet(Material_ReloadTechniqueSetResources, true);"
    "technique reloads must occur after database enumeration releases its read count")
require_source_contains(
    "gfx_d3d/r_material.cpp"
    "Material_ForEachTechniqueSet(Material_ReleaseTechniqueSetResources, true);"
    "technique releases must occur after database enumeration releases its read count")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "Material_ForEachTechniqueSet(Material_ReleaseTechniqueSetResources, true);"
    "load-object technique cleanup must occur after database enumeration")
require_source_contains(
    "EffectsCore/fx_archive.cpp"
    "memFile->errorOnOverflow = false;
    DB_EnumXAssets("
    "FX archive overflow must be deferred during database enumeration")
require_source_contains(
    "EffectsCore/fx_archive.cpp"
    "memFile->errorOnOverflow = errorOnOverflow;
    if (errorOnOverflow && memFile->memoryOverflow)
        Com_Error("
    "FX archive overflow must drop only after database enumeration")
require_source_contains(
    "database/db_load.cpp"
    "DB_ConvertOffsetToAlias(
                (uint32_t*)&varMaterial->textureTable,
                DBAliasKind::MaterialTextureTable,
                textureByteCount)"
    "shared material texture tables must resolve through exact typed provenance")
require_source_contains(
    "database/db_load.cpp"
    "db::validation::MaterialTextureHeaderValid("
    "material texture headers must be bounded before payload fixup")
require_source_contains(
    "database/db_load.cpp"
    "var[-1].nameHash >= var->nameHash"
    "material texture tables must be strictly hash ordered")
require_source_contains(
    "database/db_load.cpp"
    "Present-empty spans are legal in disk32"
    "empty material texture tables must be explicitly canonicalized")
require_source_contains(
    "database/db_load.cpp"
    "if (textureToken == disk32::kInline)"
    "empty inline material texture tables must preserve stream alignment")
require_source_contains(
    "database/db_load.cpp"
    "DB_ConvertOffsetToPointer(
                    (uint32_t*)&varMaterial->textureTable,
                    0,
                    4,
                    kDirectBlock4)"
    "empty direct material texture tables must still validate their token")
require_source_contains(
    "database/db_stream.cpp"
    "case DBAliasKind::MaterialTextureTable:
        if (metadata != materializedBytes
            || metadata < disk32::kMaterialTextureDefBytes"
    "completed material texture tables must validate their variable disk32 extent")
require_source_contains(
    "database/db_validation.h"
    "major != loadForRenderer + 2"
    "material shader bytecode models must match their renderer variant")
require_source_contains(
    "gfx_d3d/r_material.cpp"
    "const HRESULT result = dx.device->CreateVertexShader("
    "material vertex shader creation must inspect the D3D result")
require_source_contains(
    "gfx_d3d/r_material.cpp"
    "const HRESULT result = dx.device->CreatePixelShader("
    "material pixel shader creation must inspect the D3D result")
require_source_contains(
    "gfx_d3d/r_material.cpp"
    "partialShader->Release()"
    "failed material shader creation must release a partial COM output")
require_source_contains(
    "database/db_load.cpp"
    "disk32::kMaterialPassBytes"
    "material pass stream reads must use their fixed disk32 layout")
require_source_contains(
    "database/db_file_load.cpp"
    "DB_MarkStreamRangeMaterialized(pos, size)"
    "successful fast-file output must be recorded as materialized")
require_source_contains(
    "database/db_file_load.cpp"
    "InterlockedExchange(&g_fileReadBytes, static_cast<LONG>(dwNumberOfBytesTransfered))"
    "asynchronous fast-file reads must preserve the actual completion byte count")
require_source_contains(
    "database/db_file_load.cpp"
    "initialReadSize < 12"
    "fast-file magic and version reads must reject truncated headers")
require_source_contains(
    "database/db_file_load.cpp"
    "db::validation::CanAppendBytes"
    "fast-file ring-buffer accounting must reject invalid accumulated input")
require_source_contains(
    "database/db_stringtable_load.cpp"
    "*var >= static_cast<uint32_t>(varXAssetList->stringList.count)"
    "script-string tokens must be range-checked before indexing")
require_source_contains(
    "qcommon/com_bsp_load_obj.cpp"
    "comBspGlob.fileSize > INT32_MAX"
    "BSP allocation sizes must fit the signed zone allocator")
require_source_contains(
    "universal/physicalmemory.cpp"
    "PMem_TryAlloc"
    "zone allocations must report checked failure before generic fatal PMem OOM handling")
require_source_contains(
    "database/db_load.cpp"
    "#ifndef KISAK_DEDI_HEADLESS
    SND_SetData(mssSound, *data);
#else
    (void)data;"
    "headless loaded sounds must not link the playback data realizer")
require_source_contains(
    "database/db_load.cpp"
    "DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::SoundData,
                    sharedData,
                    sharedDataSize);"
    "headless loaded sounds must retain raw sound alias provenance")
require_source_contains(
    "database/db_load.cpp"
    "DB_ClearHeadlessSoundRuntimeData(varMssSound);"
    "headless loaded sounds must canonicalize playback-owned pointers before publication")
require_source_contains(
    "database/db_load.cpp"
    "#ifndef KISAK_DEDI_HEADLESS
            Load_Texture(varGfxTextureLoad, varGfxImage);
#else"
    "headless image loading must not link the D3D texture realizer")
require_source_contains(
    "database/db_load.cpp"
    "varGfxTextureLoad->basemap = nullptr;"
    "headless images must publish a canonical absent runtime texture")
require_source_contains(
    "database/db_load.cpp"
    "else if (!image->delayLoadPixels)"
    "headless non-delayed external images must be finalized immediately")
require_source_contains(
    "database/db_load.cpp"
    "DB_LoadedExternalData(externalDataSize);"
    "headless immediate external image loads must advance progress accounting")
require_source_contains(
    "database/db_file_load.cpp"
    "if (externalDataSize < 0)
    {
        if (imageLoadFailed)
            *imageLoadFailed = true;"
    "headless delayed external images must reject negative progress sizes")
require_source_contains(
    "database/db_file_load.cpp"
    "if (imageLoadFailed)
        Com_Error(ERR_DROP, \"Invalid headless delayed image size\");"
    "headless delayed image failures must be raised after database enumeration")
require_source_contains(
    "database/db_load.cpp"
    "if (!DB_FinalizeHeadlessTextureLoad(varGfxImageLoadDef, varGfxImage))"
    "headless texture loading must select embedded, delayed, or immediate data-only realization")
require_source_contains(
    "database/db_registry.cpp"
    "techniqueSet->remappedTechniqueSet = techniqueSet;"
    "headless technique sets must retain a canonical self-remap for material validation")
require_source_contains(
    "database/db_load.cpp"
    "#ifndef KISAK_DEDI_HEADLESS
    return Load_CreateMaterialVertexShader("
    "headless material loading must not link vertex-shader creation")
require_source_contains(
    "database/db_load.cpp"
    "#ifndef KISAK_DEDI_HEADLESS
    return Load_CreateMaterialPixelShader("
    "headless material loading must not link pixel-shader creation")
require_source_contains(
    "database/db_load.cpp"
    "varMaterialVertexShaderProgram->vs = nullptr;"
    "headless vertex shaders must retain bytecode with an absent runtime handle")
require_source_contains(
    "database/db_load.cpp"
    "varMaterialPixelShaderProgram->ps = nullptr;"
    "headless pixel shaders must retain bytecode with an absent runtime handle")
require_source_contains(
    "database/db_load.cpp"
    "#ifndef KISAK_DEDI_HEADLESS
            Load_BuildVertexDecl(&varMaterialPass->vertexDecl);
#endif"
    "headless material loading must not link vertex-declaration construction")
require_source_contains(
    "database/db_load.cpp"
    "sizeof(varMaterialVertexDeclaration->routing.decl));"
    "headless vertex declarations must clear serialized COM handles")
require_source_contains(
    "database/db_load.cpp"
    "varMaterialVertexDeclaration->isLoaded = true;"
    "headless vertex declarations must record completed data-only realization")
require_source_contains(
    "database/db_load.cpp"
    "#ifndef KISAK_DEDI_HEADLESS
            if (!Load_PicmipWater(varMaterialTextureDefInfo))
                return false;
#else
            // Dedicated simulation keeps the validated source-resolution CPU
            // grids; renderer picmip would destructively compact both arrays.
            if (!DB_ValidateHeadlessWaterContract(*varMaterialTextureDefInfo))"
    "headless water must validate its CPU/image contract without renderer picmip")
require_source_contains(
    "database/db_load.cpp"
    "water->image->width != water->M
        || water->image->height != water->N"
    "headless water must preserve source-resolution image dimensions")
require_source_contains(
    "database/db_load.cpp"
    "#ifndef KISAK_DEDI_HEADLESS
    Load_VertexBuffer("
    "headless world loading must not link vertex-buffer realization")
require_source_contains(
    "database/db_load.cpp"
    "varGfxWorldVertexData->worldVb = nullptr;"
    "headless world vertices must retain CPU data with an absent runtime buffer")
require_source_contains(
    "database/db_load.cpp"
    "#ifndef KISAK_DEDI_HEADLESS
    Load_VertexBuffer(&varGfxWorldVertexLayerData->layerVb, varGfxWorld->vld.data, layerDataSize);
#else"
    "headless world vertex layers must not link vertex-buffer realization")
require_source_contains(
    "database/db_load.cpp"
    "varGfxWorldVertexLayerData->layerVb = nullptr;"
    "headless world vertex layers must retain CPU data with an absent runtime buffer")

file(STRINGS
    "${SOURCE_ROOT}/src/database/db_load.cpp"
    _raw_array_loads
    REGEX "Load_Stream\\(atStreamStart,.*([0-9]+[ \t]*\\*[ \t]*count|count[ \t]*\\*[ \t]*[0-9]+)\\)")
if (_raw_array_loads)
    message(FATAL_ERROR
        "Unchecked generated fast-file array loads remain in db_load.cpp; use Load_StreamArray")
endif()

file(READ "${SOURCE_ROOT}/src/database/db_load.cpp" _db_load_source)
string(FIND
    "${_db_load_source}"
    "db::validation::MaterialPassLayoutValid("
    _material_pass_header_validation)
string(FIND
    "${_db_load_source}"
    "if (varMaterialPass->vertexDecl)"
    _material_pass_first_child_fixup)
if (_material_pass_header_validation EQUAL -1
    OR _material_pass_first_child_fixup EQUAL -1
    OR _material_pass_header_validation GREATER _material_pass_first_child_fixup)
    message(FATAL_ERROR
        "Material pass headers must be validated before child fixups")
endif()
string(FIND
    "${_db_load_source}"
    "CountInRange(varMaterialTechnique->passCount, 1, 4)"
    _material_technique_count_validation)
string(FIND
    "${_db_load_source}"
    "Load_MaterialPassArray(1, varMaterialTechnique->passCount)"
    _material_technique_pass_load)
if (_material_technique_count_validation EQUAL -1
    OR _material_technique_pass_load EQUAL -1
    OR _material_technique_count_validation GREATER _material_technique_pass_load)
    message(FATAL_ERROR
        "Material technique pass counts must be validated before array loading")
endif()
string(FIND
    "${_db_load_source}"
    "Load_MaterialShaderArgumentArray(1, static_cast<int32_t>(argumentCount))"
    _material_argument_load)
string(FIND
    "${_db_load_source}"
    "if (!DB_ValidateMaterialPassArguments(varMaterialPass, argumentCount))"
    _material_argument_validation)
if (_material_argument_load EQUAL -1
    OR _material_argument_validation EQUAL -1
    OR _material_argument_load GREATER _material_argument_validation)
    message(FATAL_ERROR
        "Material shader arguments must be validated after loading/fixups")
endif()
string(FIND
    "${_db_load_source}"
    "DB_RegisterPointerSlot(
                varMaterialPass->vertexDecl,
                DBAliasKind::MaterialVertexDeclaration)"
    _material_vertex_declaration_registration)
string(FIND
    "${_db_load_source}"
    "if (!Load_MaterialVertexDeclaration(1))"
    _material_vertex_declaration_load)
string(FIND
    "${_db_load_source}"
    "Load_BuildVertexDecl(&varMaterialPass->vertexDecl)"
    _material_vertex_declaration_build)
string(FIND
    "${_db_load_source}"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::MaterialVertexDeclaration"
    _material_vertex_declaration_completion)
if (_material_vertex_declaration_registration EQUAL -1
    OR _material_vertex_declaration_load EQUAL -1
    OR _material_vertex_declaration_build EQUAL -1
    OR _material_vertex_declaration_completion EQUAL -1
    OR _material_vertex_declaration_registration GREATER _material_vertex_declaration_load
    OR _material_vertex_declaration_load GREATER _material_vertex_declaration_build
    OR _material_vertex_declaration_build GREATER _material_vertex_declaration_completion)
    message(FATAL_ERROR
        "Material vertex declaration provenance must begin before loading and publish after construction")
endif()
string(FIND
    "${_db_load_source}"
    "DB_RegisterPointerSlot(
                *varMaterialTechniquePtr,
                DBAliasKind::MaterialTechnique)"
    _material_technique_registration)
string(FIND
    "${_db_load_source}"
    "if (!Load_MaterialTechnique(1))"
    _material_technique_load)
string(FIND
    "${_db_load_source}"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::MaterialTechnique"
    _material_technique_completion)
if (_material_technique_registration EQUAL -1
    OR _material_technique_load EQUAL -1
    OR _material_technique_completion EQUAL -1
    OR _material_technique_registration GREATER _material_technique_load
    OR _material_technique_load GREATER _material_technique_completion)
    message(FATAL_ERROR
        "Material technique provenance must begin before loading and publish after completion")
endif()
string(FIND
    "${_db_load_source}"
    "varMaterialVertexShader->prog.vs = nullptr"
    _material_vertex_shader_scrub)
string(FIND
    "${_db_load_source}"
    "Load_Stream(
        atStreamStart,
        (uint8_t *)varMaterialVertexShader,
        disk32::kMaterialVertexShaderBytes)"
    _material_vertex_shader_header_load)
string(FIND
    "${_db_load_source}"
    "varXString = &varMaterialVertexShader->name"
    _material_vertex_shader_name_load)
string(FIND
    "${_db_load_source}"
    "DB_RegisterPointerSlot(
                *varMaterialVertexShaderPtr,
                DBAliasKind::MaterialVertexShader)"
    _material_vertex_shader_registration)
string(FIND
    "${_db_load_source}"
    "if (!Load_MaterialVertexShader(1))"
    _material_vertex_shader_load)
string(FIND
    "${_db_load_source}"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::MaterialVertexShader"
    _material_vertex_shader_completion)
if (_material_vertex_shader_header_load EQUAL -1
    OR _material_vertex_shader_scrub EQUAL -1
    OR _material_vertex_shader_name_load EQUAL -1
    OR _material_vertex_shader_registration EQUAL -1
    OR _material_vertex_shader_load EQUAL -1
    OR _material_vertex_shader_completion EQUAL -1
    OR _material_vertex_shader_header_load GREATER _material_vertex_shader_scrub
    OR _material_vertex_shader_scrub GREATER _material_vertex_shader_name_load
    OR _material_vertex_shader_registration GREATER _material_vertex_shader_load
    OR _material_vertex_shader_load GREATER _material_vertex_shader_completion)
    message(FATAL_ERROR
        "Material vertex shaders must scrub runtime state and publish only after completion")
endif()
string(FIND
    "${_db_load_source}"
    "varMaterialPixelShader->prog.ps = nullptr"
    _material_pixel_shader_scrub)
string(FIND
    "${_db_load_source}"
    "Load_Stream(
        atStreamStart,
        (uint8_t *)varMaterialPixelShader,
        disk32::kMaterialPixelShaderBytes)"
    _material_pixel_shader_header_load)
string(FIND
    "${_db_load_source}"
    "varXString = &varMaterialPixelShader->name"
    _material_pixel_shader_name_load)
string(FIND
    "${_db_load_source}"
    "DB_RegisterPointerSlot(
                *varMaterialPixelShaderPtr,
                DBAliasKind::MaterialPixelShader)"
    _material_pixel_shader_registration)
string(FIND
    "${_db_load_source}"
    "if (!Load_MaterialPixelShader(1))"
    _material_pixel_shader_load)
string(FIND
    "${_db_load_source}"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::MaterialPixelShader"
    _material_pixel_shader_completion)
if (_material_pixel_shader_header_load EQUAL -1
    OR _material_pixel_shader_scrub EQUAL -1
    OR _material_pixel_shader_name_load EQUAL -1
    OR _material_pixel_shader_registration EQUAL -1
    OR _material_pixel_shader_load EQUAL -1
    OR _material_pixel_shader_completion EQUAL -1
    OR _material_pixel_shader_header_load GREATER _material_pixel_shader_scrub
    OR _material_pixel_shader_scrub GREATER _material_pixel_shader_name_load
    OR _material_pixel_shader_registration GREATER _material_pixel_shader_load
    OR _material_pixel_shader_load GREATER _material_pixel_shader_completion)
    message(FATAL_ERROR
        "Material pixel shaders must scrub runtime state and publish only after completion")
endif()
string(FIND
    "${_db_load_source}"
    "DB_RegisterPointerSlot(
                *varMaterialTextureDefInfo,
                DBAliasKind::MaterialWater)"
    _material_water_registration)
string(FIND
    "${_db_load_source}"
    "if (!Load_water_t(1))"
    _material_water_load)
string(FIND
    "${_db_load_source}"
    "if (!Load_PicmipWater(varMaterialTextureDefInfo))"
    _material_water_picmip)
string(FIND
    "${_db_load_source}"
    "if (!DB_CompleteObject(
                    completed,
                    DBAliasKind::MaterialWater,
                    *varMaterialTextureDefInfo,
                    disk32::kMaterialWaterBytes,
                    disk32::kMaterialWaterBytes))"
    _material_water_completion)
string(FIND
    "${_db_load_source}"
    "Load_Stream(
        1,
        (uint8_t *)varwater_t,
        disk32::kMaterialWaterBytes)"
    _material_water_header_load)
string(FIND
    "${_db_load_source}"
    "varwater_t->writable.floatTime = kWaterInitialTime"
    _material_water_runtime_scrub)
string(FIND
    "${_db_load_source}"
    "if (!DB_ValidateWaterHeader(varwater_t, &sampleCount))"
    _material_water_header_validation)
if (_material_water_registration EQUAL -1
    OR _material_water_load EQUAL -1
    OR _material_water_picmip EQUAL -1
    OR _material_water_completion EQUAL -1
    OR _material_water_header_load EQUAL -1
    OR _material_water_runtime_scrub EQUAL -1
    OR _material_water_header_validation EQUAL -1
    OR _material_water_registration GREATER _material_water_load
    OR _material_water_load GREATER _material_water_picmip
    OR _material_water_picmip GREATER _material_water_completion
    OR _material_water_header_load GREATER _material_water_runtime_scrub
    OR _material_water_runtime_scrub GREATER _material_water_header_validation)
    message(FATAL_ERROR
        "Material water must scrub runtime state and publish only after validated picmip completion")
endif()
file(READ "${SOURCE_ROOT}/src/gfx_d3d/r_water.cpp" _water_runtime_source)
string(FIND
    "${_water_runtime_source}"
    "DownsampleWaterGridInPlace(
            water->H0,
            sourceWidth,
            targetWidth)"
    _material_water_amplitude_picmip)
string(FIND
    "${_water_runtime_source}"
    "DownsampleWaterGridInPlace(
            water->wTerm,
            sourceWidth,
            targetWidth)"
    _material_water_frequency_picmip)
if (_material_water_amplitude_picmip EQUAL -1
    OR _material_water_frequency_picmip EQUAL -1)
    message(FATAL_ERROR
        "Material water picmip must compact paired amplitudes and frequencies")
endif()
string(FIND "${_water_runtime_source}" "std::fmod(phaseSource, 1024.0)" _water_bounded_phase)
string(FIND "${_water_runtime_source}" "if (!std::isfinite(phase))" _water_finite_phase)
string(FIND "${_water_runtime_source}" "std::hypot(sample.real, sample.imag)" _water_safe_magnitude)
string(FIND "${_water_runtime_source}" "iassert( fftIndexa < HCOUNT )" _water_second_fft_bound)
if (_water_bounded_phase EQUAL -1
    OR _water_finite_phase EQUAL -1
    OR _water_safe_magnitude EQUAL -1
    OR _water_second_fft_bound EQUAL -1)
    message(FATAL_ERROR
        "Material water runtime must bound numeric conversions and both FFT passes")
endif()
string(FIND
    "${_db_load_source}"
    "DB_RegisterPointerSlot(
                varMaterial->textureTable,
                DBAliasKind::MaterialTextureTable)"
    _material_texture_table_registration)
string(FIND
    "${_db_load_source}"
    "if (!Load_MaterialTextureDefArray(1, varMaterial->textureCount))"
    _material_texture_table_load)
string(FIND
    "${_db_load_source}"
    "if (!DB_CompleteObject(
                    completed,
                    DBAliasKind::MaterialTextureTable,
                    varMaterial->textureTable,
                    textureByteCount,
                    textureByteCount))"
    _material_texture_table_completion)
if (_material_texture_table_registration EQUAL -1
    OR _material_texture_table_load EQUAL -1
    OR _material_texture_table_completion EQUAL -1
    OR _material_texture_table_registration GREATER _material_texture_table_load
    OR _material_texture_table_load GREATER _material_texture_table_completion)
    message(FATAL_ERROR
        "Material texture-table provenance must begin before child loading and publish after completion")
endif()
string(FIND
    "${_db_load_source}"
    "DB_ConvertOffsetToPointer(
                (uint32_t*)&varMaterial->stateBitsTable,
                stateBitsByteCount"
    _material_state_table_resolution)
string(FIND
    "${_db_load_source}"
    "if (!DB_ValidateMaterialSemantics(varMaterial))
    {
        DB_PopStreamPos();
        return false;
    }
    DB_PopStreamPos();
    return true;
}"
    _material_semantic_validation)
string(FIND
    "${_db_load_source}"
    "if (!Load_Material(1))"
    _material_graph_load)
string(FIND
    "${_db_load_source}"
    "Load_MaterialAsset((XAssetHeader *)varMaterialHandle)"
    _material_asset_publication)
if (_material_state_table_resolution EQUAL -1
    OR _material_semantic_validation EQUAL -1
    OR _material_graph_load EQUAL -1
    OR _material_asset_publication EQUAL -1
    OR _material_state_table_resolution GREATER _material_semantic_validation
    OR _material_graph_load GREATER _material_asset_publication)
    message(FATAL_ERROR
        "Complete material graphs must pass semantic validation before asset publication")
endif()
string(FIND
    "${_db_load_source}"
    "const db::relocation::Status materialized = DB_ValidateStreamAddress("
    _material_shader_program_materialization)
string(FIND
    "${_db_load_source}"
    "if (!db::validation::D3D9ShaderBytecodeValid("
    _material_shader_bytecode_validation)
string(FIND
    "${_db_load_source}"
    "db::validation::D3D9ShaderStage::Vertex,
        varGfxVertexShaderLoadDef->loadForRenderer"
    _material_vertex_program_validation)
string(FIND
    "${_db_load_source}"
    "return Load_CreateMaterialVertexShader("
    _material_vertex_shader_creation)
string(FIND
    "${_db_load_source}"
    "db::validation::D3D9ShaderStage::Pixel,
        varGfxPixelShaderLoadDef->loadForRenderer"
    _material_pixel_program_validation)
string(FIND
    "${_db_load_source}"
    "return Load_CreateMaterialPixelShader("
    _material_pixel_shader_creation)
if (_material_shader_program_materialization EQUAL -1
    OR _material_shader_bytecode_validation EQUAL -1
    OR _material_vertex_program_validation EQUAL -1
    OR _material_vertex_shader_creation EQUAL -1
    OR _material_pixel_program_validation EQUAL -1
    OR _material_pixel_shader_creation EQUAL -1
    OR _material_shader_program_materialization GREATER _material_shader_bytecode_validation
    OR _material_vertex_program_validation GREATER _material_vertex_shader_creation
    OR _material_pixel_program_validation GREATER _material_pixel_shader_creation)
    message(FATAL_ERROR
        "Material shader programs must be materialized and validated before D3D creation")
endif()
file(STRINGS
    "${SOURCE_ROOT}/src/database/db_load.cpp"
    _legacy_direct_offsets
    REGEX "DB_ConvertOffsetToPointerLegacy")
list(LENGTH _legacy_direct_offsets _legacy_direct_offset_count)
if (NOT _legacy_direct_offset_count EQUAL 0)
    message(FATAL_ERROR
        "Expected no legacy direct fast-file offsets; found ${_legacy_direct_offset_count}. "
        "Migrations must update this debt gate.")
endif()

file(READ "${SOURCE_ROOT}/src/gfx_d3d/r_material.cpp" _material_runtime_source)
string(FIND
    "${_material_runtime_source}"
    "(IDirect3DVertexShader9 **)&mtlShader->prog"
    _untyped_vertex_shader_output)
string(FIND
    "${_material_runtime_source}"
    "(IDirect3DPixelShader9 **)&mtlShader->prog"
    _untyped_pixel_shader_output)
if (NOT _untyped_vertex_shader_output EQUAL -1
    OR NOT _untyped_pixel_shader_output EQUAL -1)
    message(FATAL_ERROR
        "Material shader creation must use typed COM output fields")
endif()
string(REGEX MATCH
    "(\\*inserted[ \\t]*=|inserted->)"
    _raw_alias_slot_write
    "${_db_load_source}")
if (_raw_alias_slot_write)
    message(FATAL_ERROR
        "Native pointer write to four-byte fast-file alias slot remains: ${_raw_alias_slot_write}")
endif()
string(REPLACE "->" "." _db_load_count_source "${_db_load_source}")
string(REGEX MATCH
    "Load_[A-Za-z0-9_]+Array[ \\t\\r\\n]*\\([ \\t\\r\\n]*1[ \\t\\r\\n]*,[^;]*(\\+|-|\\*|/|%|<<|>>|&|\\||\\^)[^;]*\\);"
    _raw_derived_count
    "${_db_load_count_source}")
if (_raw_derived_count)
    message(FATAL_ERROR
        "Unchecked derived fast-file array count remains in db_load.cpp: ${_raw_derived_count}")
endif()

set(_format_sensitive_sources
    "cgame/cg_hudelem.cpp"
    "cgame/cg_info.cpp"
    "client_mp/cl_cgame_mp.cpp"
    "client_mp/cl_main_mp.cpp"
    "client_mp/cl_parse_mp.cpp"
    "game/g_main.cpp"
    "game_mp/g_main_mp.cpp"
    "game_mp/g_scr_main_mp.cpp"
    "qcommon/com_bsp_load_obj.cpp"
    "server_mp/sv_client_mp.cpp"
    "win32/win_steam.cpp"
)

# These files contain network/content-facing diagnostics. Reject the dangerous
# two-pass pattern where preformatted or external text is supplied as the format
# argument with no variadic values. This source tripwire complements compiler
# format warnings, which cannot see through va() or decompiled temporary variables.
set(_bare_format_regex
    "Com_(Error|Printf|PrintError|PrintWarning|DPrintf)[ \t]*\\([^,\r\n]+,[ \t]*(va[ \t]*\\([^\r\n]*\\)|[A-Za-z_][A-Za-z0-9_]*)[ \t]*\\)[ \t]*;")

foreach(_relative_path IN LISTS _format_sensitive_sources)
    set(_path "${SOURCE_ROOT}/src/${_relative_path}")
    file(STRINGS "${_path}" _matches REGEX "${_bare_format_regex}")
    foreach(_line IN LISTS _matches)
        string(STRIP "${_line}" _stripped)
        if (NOT _stripped MATCHES "^//")
            message(FATAL_ERROR
                "Dynamic diagnostic format in src/${_relative_path}: ${_stripped}\n"
                "Pass a literal format and external text as a %s argument.")
        endif()
    endforeach()
endforeach()

message(STATUS "Security source invariants verified")
