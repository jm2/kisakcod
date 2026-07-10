cmake_minimum_required(VERSION 3.16)

function(require_source_contains RELATIVE_PATH NEEDLE DESCRIPTION)
    file(READ "${SOURCE_ROOT}/src/${RELATIVE_PATH}" _source)
    string(FIND "${_source}" "${NEEDLE}" _position)
    if (_position EQUAL -1)
        message(FATAL_ERROR "Missing security invariant (${DESCRIPTION}) in src/${RELATIVE_PATH}")
    endif()
endfunction()

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
    "(uint32_t*)&varGfxAabbTree->smodelIndexes,
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
                return;
            Load_BuildVertexDecl"
    "material vertex declarations must validate before D3D construction")
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
                db::relocation::kMaterialVertexDeclarationDiskBytes)"
    "shared material vertex declarations must resolve through exact typed provenance")
require_source_contains(
    "database/db_load.cpp"
    "db::relocation::kMaterialVertexDeclarationDiskBytes"
    "material vertex declaration provenance must use its fixed disk32 extent")
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
    "DB_SetInsertedPointer(
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
file(STRINGS
    "${SOURCE_ROOT}/src/database/db_load.cpp"
    _legacy_direct_offsets
    REGEX "DB_ConvertOffsetToPointerLegacy")
list(LENGTH _legacy_direct_offsets _legacy_direct_offset_count)
if (NOT _legacy_direct_offset_count EQUAL 27)
    message(FATAL_ERROR
        "Expected exactly 27 explicitly legacy direct fast-file offsets; found ${_legacy_direct_offset_count}. "
        "Migrations must update this debt gate.")
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
