cmake_minimum_required(VERSION 3.16)

set(SRC_DIR "${SOURCE_ROOT}/src")
set(DEPS_DIR "${SOURCE_ROOT}/deps")
set(SCRIPTS_DIR "${SOURCE_ROOT}/scripts")
set(KISAK_DEDI_HEADLESS ON)
set(KISAK_PLATFORM win32)

include("${SCRIPTS_DIR}/platform_override.cmake")
include("${SCRIPTS_DIR}/common_files.cmake")
include("${SCRIPTS_DIR}/mp/mp_files.cmake")
include("${SCRIPTS_DIR}/dedi/dedi_files.cmake")
include("${SCRIPTS_DIR}/platform/win32/platform.cmake")
include("${SCRIPTS_DIR}/dedi/dedi_sources.cmake")

kisakcod_require_platform_source_sets()

function(kisakcod_assert_source_lists_equal ACTUAL_VAR EXPECTED_VAR DESCRIPTION)
    list(JOIN ${ACTUAL_VAR} "|" _actual_sources)
    list(JOIN ${EXPECTED_VAR} "|" _expected_sources)
    if (NOT "${_actual_sources}" STREQUAL "${_expected_sources}")
        message(FATAL_ERROR "${DESCRIPTION} changed")
    endif()
endfunction()

set(_expected_win32_platform_sources
    "${SRC_DIR}/win32/win_configure.cpp"
    "${SRC_DIR}/win32/win_configure.h"
    "${SRC_DIR}/win32/win_input.cpp"
    "${SRC_DIR}/win32/win_input.h"
    "${SRC_DIR}/win32/win_local.h"
    "${SRC_DIR}/win32/win_localize.cpp"
    "${SRC_DIR}/win32/win_localize.h"
    "${SRC_DIR}/win32/win_main.cpp"
    "${SRC_DIR}/win32/win_net.cpp"
    "${SRC_DIR}/win32/win_net.h"
    "${SRC_DIR}/win32/win_net_debug.cpp"
    "${SRC_DIR}/win32/win_net_debug.h"
    "${SRC_DIR}/win32/win_storage.cpp"
    "${SRC_DIR}/win32/win_storage.h"
    "${SRC_DIR}/win32/win_syscon.cpp"
    "${SRC_DIR}/win32/win_voice.cpp"
    "${SRC_DIR}/win32/win_wndproc.cpp"
    "${SRC_DIR}/win32/win_steam.cpp"
    "${SRC_DIR}/win32/win_steam.h"
)
set(_expected_win32_headless_sources
    "${SRC_DIR}/win32/win_configure.cpp"
    "${SRC_DIR}/win32/win_configure.h"
    "${SRC_DIR}/win32/win_local.h"
    "${SRC_DIR}/win32/win_localize.cpp"
    "${SRC_DIR}/win32/win_localize.h"
    "${SRC_DIR}/win32/win_main.cpp"
    "${SRC_DIR}/win32/win_net.cpp"
    "${SRC_DIR}/win32/win_net.h"
    "${SRC_DIR}/win32/win_net_debug.cpp"
    "${SRC_DIR}/win32/win_net_debug.h"
    "${SRC_DIR}/win32/win_storage.h"
    "${SRC_DIR}/win32/win_syscon.cpp"
    "${SRC_DIR}/win32/win_steam.cpp"
    "${SRC_DIR}/win32/win_steam.h"
)
set(_expected_win32_service_sources
    "${SRC_DIR}/_platform/win32/sys_event.cpp"
    "${SRC_DIR}/_platform/win32/sys_sync.cpp"
    "${SRC_DIR}/_platform/win32/sys_thread.cpp"
    "${SRC_DIR}/_platform/win32/sys_time.cpp"
)
set(_expected_posix_service_sources
    "${SRC_DIR}/_platform/posix/sys_event.cpp"
    "${SRC_DIR}/_platform/posix/sys_sync.cpp"
    "${SRC_DIR}/_platform/posix/sys_thread.cpp"
    "${SRC_DIR}/_platform/posix/sys_time.cpp"
)

if (NOT KISAK_PLATFORM_SOURCE_SETS_COMPLETE)
    message(FATAL_ERROR "The selected Win32 platform source sets must be complete")
endif()
kisakcod_assert_source_lists_equal(
    KISAK_PLATFORM_SOURCES
    _expected_win32_platform_sources
    "Win32 engine platform source composition"
)
kisakcod_assert_source_lists_equal(
    KISAK_PLATFORM_DEDI_HEADLESS_SOURCES
    _expected_win32_headless_sources
    "Win32 headless platform source composition"
)
kisakcod_assert_source_lists_equal(
    KISAK_PLATFORM_SERVICE_SOURCES
    _expected_win32_service_sources
    "Win32 platform service source composition"
)

kisakcod_get_dedi_sources(_dedi_sources)
kisakcod_assert_headless_dedi_sources(${_dedi_sources})

foreach(_platform_source IN LISTS _expected_win32_headless_sources)
    set(_platform_source_count 0)
    foreach(_dedi_source IN LISTS _dedi_sources)
        if ("${_dedi_source}" STREQUAL "${_platform_source}")
            math(EXPR _platform_source_count "${_platform_source_count} + 1")
        endif()
    endforeach()
    if (NOT _platform_source_count EQUAL 1)
        message(FATAL_ERROR
            "Win32 headless platform source must occur exactly once: ${_platform_source}")
    endif()
endforeach()

foreach(_service_source IN LISTS _expected_win32_service_sources)
    set(_service_source_count 0)
    foreach(_dedi_source IN LISTS _dedi_sources)
        if ("${_dedi_source}" STREQUAL "${_service_source}")
            math(EXPR _service_source_count "${_service_source_count} + 1")
        endif()
    endforeach()
    if (NOT _service_source_count EQUAL 1)
        message(FATAL_ERROR
            "Win32 platform service source must occur exactly once: ${_service_source}")
    endif()
endforeach()

foreach(_contract_source
    "${SRC_DIR}/qcommon/threads.cpp"
    "${SRC_DIR}/qcommon/threads.h"
    "${SRC_DIR}/qcommon/sys_event.h"
    "${SRC_DIR}/qcommon/sys_sync.cpp"
    "${SRC_DIR}/qcommon/sys_sync.h"
    "${SRC_DIR}/qcommon/sys_thread.h"
    "${SRC_DIR}/qcommon/sys_time.h"
    "${SRC_DIR}/qcommon/sys_worker_gate.cpp"
    "${SRC_DIR}/qcommon/sys_worker_gate.h"
)
    set(_contract_source_count 0)
    foreach(_dedi_source IN LISTS _dedi_sources)
        if ("${_dedi_source}" STREQUAL "${_contract_source}")
            math(EXPR _contract_source_count "${_contract_source_count} + 1")
        endif()
    endforeach()
    if (NOT _contract_source_count EQUAL 1)
        message(FATAL_ERROR
            "Common platform contract source must occur exactly once: ${_contract_source}")
    endif()
endforeach()

if ("${SRC_DIR}/universal/win_shared.cpp" IN_LIST _dedi_sources)
    message(FATAL_ERROR "Retired universal/win_shared.cpp remains in dedicated composition")
endif()

function(kisakcod_assert_incomplete_platform_source_sets PLATFORM_NAME)
    set(KISAK_PLATFORM "${PLATFORM_NAME}")
    include("${SCRIPTS_DIR}/platform/${PLATFORM_NAME}/platform.cmake")
    kisakcod_require_platform_source_sets()

    if (KISAK_PLATFORM_SOURCE_SETS_COMPLETE)
        message(FATAL_ERROR
            "${PLATFORM_NAME} platform source sets must remain marked incomplete")
    endif()
    foreach(_source_set
        KISAK_PLATFORM_SOURCES
        KISAK_PLATFORM_DEDI_HEADLESS_SOURCES
    )
        if (NOT "${${_source_set}}" STREQUAL "")
            message(FATAL_ERROR
                "${PLATFORM_NAME} ${_source_set} must remain explicitly empty until its backend lands")
        endif()
    endforeach()
    kisakcod_assert_source_lists_equal(
        KISAK_PLATFORM_SERVICE_SOURCES
        _expected_posix_service_sources
        "${PLATFORM_NAME} platform service source composition"
    )
endfunction()

kisakcod_assert_incomplete_platform_source_sets(linux)
kisakcod_assert_incomplete_platform_source_sets(macos)

file(READ "${SOURCE_ROOT}/CMakeLists.txt" _root_cmake_source)
string(FIND "${_root_cmake_source}"
    "if (NOT KISAK_PLATFORM STREQUAL \"win32\""
    _non_windows_engine_gate)
string(FIND "${_root_cmake_source}"
    "backend is not buildable yet."
    _non_windows_engine_gate_message)
if (_non_windows_engine_gate EQUAL -1 OR _non_windows_engine_gate_message EQUAL -1)
    message(FATAL_ERROR
        "Linux/macOS engine targets must remain configuration-gated while their source sets are incomplete")
endif()

list(LENGTH _dedi_sources _source_count)
if (_source_count LESS 1)
    message(FATAL_ERROR "Headless dedicated source profile is empty")
endif()

foreach(_required
    "${SRC_DIR}/server_mp/sv_main_mp.cpp"
    "${SRC_DIR}/server_mp/sv_client_mp.cpp"
    "${SRC_DIR}/qcommon/msg_mp.cpp"
    "${SRC_DIR}/game_mp/g_main_mp.cpp"
    "${SRC_DIR}/cgame_mp/dedicated_cgame.cpp"
    "${SRC_DIR}/database/db_memory.cpp"
    "${SRC_DIR}/database/db_file_load.cpp"
    "${SRC_DIR}/script/scr_debugger.cpp"
)
    if (NOT _required IN_LIST _dedi_sources)
        message(FATAL_ERROR "Headless dedicated source profile is missing ${_required}")
    endif()
endforeach()

file(READ "${SRC_DIR}/script/scr_debugger.cpp" _debugger_source)
string(FIND "${_debugger_source}" "#ifdef KISAK_DEDI_HEADLESS" _headless_branch)
string(FIND "${_debugger_source}" "#else" _legacy_branch)
string(FIND "${_debugger_source}" "scrDebuggerGlob_t scrDebuggerGlob;" _legacy_global)
string(FIND "${_debugger_source}" "Script debugger is unavailable in KISAK_DEDI_HEADLESS builds." _disabled_message)
string(FIND "${_debugger_source}" "NET_RestartDebug();" _remote_shutdown)
string(FIND "${_debugger_source}" "return OP_NOP;" _breakpoint_noop)

if (_headless_branch EQUAL -1
    OR _legacy_branch EQUAL -1
    OR _legacy_global EQUAL -1
    OR NOT _headless_branch LESS _legacy_branch
    OR NOT _legacy_branch LESS _legacy_global)
    message(FATAL_ERROR
        "Headless debugger backend must be selected before the legacy UI-backed debugger global")
endif()

if (_disabled_message EQUAL -1 OR _remote_shutdown EQUAL -1 OR _breakpoint_noop EQUAL -1)
    message(FATAL_ERROR
        "Headless debugger backend must explicitly reject UI/remote debugging and neutralize debugger opcodes")
endif()

file(READ "${SRC_DIR}/database/db_memory.cpp" _db_memory_source)
string(REPLACE "\r\n" "\n" _db_memory_source "${_db_memory_source}")
string(FIND "${_db_memory_source}"
    "#ifndef KISAK_DEDI_HEADLESS\n#include <gfx_d3d/r_buffers.h>\n#endif"
    _db_memory_guarded_renderer_include)
string(REGEX MATCHALL
    "DB_ClearHeadlessGeometryBufferState\\(zoneMem\\);"
    _db_memory_headless_clears
    "${_db_memory_source}")
list(LENGTH _db_memory_headless_clears _db_memory_headless_clear_count)
string(FIND "${_db_memory_source}"
    "zoneMem->lockedVertexData = zoneMem->blocks[7].data"
    _db_memory_vertex_cpu_alias)
string(FIND "${_db_memory_source}"
    "zoneMem->lockedIndexData = zoneMem->blocks[8].data"
    _db_memory_index_cpu_alias)

if (_db_memory_guarded_renderer_include EQUAL -1
    OR _db_memory_headless_clear_count LESS 3)
    message(FATAL_ERROR
        "Headless zone-memory lifecycle must clear renderer state without compiling the renderer buffer API")
endif()

if (NOT _db_memory_vertex_cpu_alias EQUAL -1
    OR NOT _db_memory_index_cpu_alias EQUAL -1)
    message(FATAL_ERROR
        "Headless renderer lock pointers must not alias CPU-backed zone blocks")
endif()

file(READ "${SRC_DIR}/database/db_file_load.cpp" _db_file_load_source)
string(REPLACE "\r\n" "\n" _db_file_load_source "${_db_file_load_source}")
string(FIND "${_db_file_load_source}"
    "#ifndef KISAK_DEDI_HEADLESS\n#include <gfx_d3d/r_image.h>\n#include <gfx_d3d/r_buffers.h>\n#endif"
    _db_file_guarded_renderer_includes)
string(FIND "${_db_file_load_source}"
    "void __cdecl DB_FinalizeHeadlessDelayedImage(XAssetHeader header, void *data)"
    _db_file_headless_delayed_callback)
string(FIND "${_db_file_load_source}"
    "void __cdecl DB_FinishGeometryBlocks(XZoneMemory *zoneMem)\n{\n#ifdef KISAK_DEDI_HEADLESS"
    _db_file_headless_geometry_finish)
string(FIND "${_db_file_load_source}"
    "image->texture.basemap = nullptr;"
    _db_file_headless_null_texture)
string(FIND "${_db_file_load_source}"
    "DB_LoadedExternalData(externalDataSize);"
    _db_file_headless_external_progress)
string(FIND "${_db_file_load_source}"
    "if (externalDataSize < 0)\n    {\n        if (imageLoadFailed)\n            *imageLoadFailed = true;"
    _db_file_headless_external_size_check)
string(FIND "${_db_file_load_source}"
    "if (imageLoadFailed)\n        Com_Error(ERR_DROP, \"Invalid headless delayed image size\");"
    _db_file_headless_deferred_error)
string(FIND "${_db_file_load_source}"
    "ASSET_TYPE_IMAGE,\n        DB_FinalizeHeadlessDelayedImage,"
    _db_file_headless_asset_enumeration)
string(REGEX MATCHALL
    "DB_FinalizeHeadlessDelayedImage\\("
    _db_file_headless_delayed_visits
    "${_db_file_load_source}")
list(LENGTH _db_file_headless_delayed_visits _db_file_headless_delayed_visit_count)

if (_db_file_guarded_renderer_includes EQUAL -1
    OR _db_file_headless_delayed_callback EQUAL -1
    OR _db_file_headless_geometry_finish EQUAL -1
    OR _db_file_headless_null_texture EQUAL -1
    OR _db_file_headless_external_progress EQUAL -1
    OR _db_file_headless_external_size_check EQUAL -1
    OR _db_file_headless_deferred_error EQUAL -1
    OR _db_file_headless_asset_enumeration EQUAL -1
    OR _db_file_headless_delayed_visit_count LESS 2)
    message(FATAL_ERROR
        "Headless fast-file finalization must visit all images, account external bytes, and select null media resources")
endif()

string(FIND "${_db_file_load_source}"
    "void __cdecl DB_LoadXFileInternal()"
    _db_load_internal_start)
if (_db_load_internal_start EQUAL -1)
    message(FATAL_ERROR "Headless fast-file source is missing DB_LoadXFileInternal")
endif()
string(SUBSTRING "${_db_file_load_source}" ${_db_load_internal_start} -1 _db_load_internal_source)

string(FIND "${_db_load_internal_source}" "    DB_AllocXZoneMemory(" _db_load_alloc)
string(FIND "${_db_load_internal_source}" "    DB_InitStreams(g_load.zoneMem);" _db_load_init_streams)
string(FIND "${_db_load_internal_source}" "    Load_XAssetListCustom();" _db_load_asset_list)
string(FIND "${_db_load_internal_source}" "    DB_FinishGeometryBlocks(g_load.zoneMem);" _db_load_finish_geometry)
string(FIND "${_db_load_internal_source}" "    --g_loadingAssets;" _db_load_accounting)
string(FIND "${_db_load_internal_source}" "    Load_DelayStream();" _db_load_delayed_stream)
string(FIND "${_db_load_internal_source}" "    DB_LoadDelayedImages();" _db_load_delayed_images)
string(FIND "${_db_load_internal_source}" "    DB_CancelLoadXFile();" _db_load_cancel REVERSE)

foreach(_position
    _db_load_alloc
    _db_load_init_streams
    _db_load_asset_list
    _db_load_finish_geometry
    _db_load_accounting
    _db_load_delayed_stream
    _db_load_delayed_images
    _db_load_cancel
)
    if (${_position} EQUAL -1)
        message(FATAL_ERROR "Headless fast-file load sequence is missing ${_position}")
    endif()
endforeach()

if (NOT _db_load_alloc LESS _db_load_init_streams
    OR NOT _db_load_init_streams LESS _db_load_asset_list
    OR NOT _db_load_asset_list LESS _db_load_finish_geometry
    OR NOT _db_load_finish_geometry LESS _db_load_accounting
    OR NOT _db_load_accounting LESS _db_load_delayed_stream
    OR NOT _db_load_delayed_stream LESS _db_load_delayed_images
    OR NOT _db_load_delayed_images LESS _db_load_cancel)
    message(FATAL_ERROR
        "Headless fast-file load must preserve CPU allocation, stream draining, accounting, and cancellation order")
endif()

file(READ "${SRC_DIR}/database/db_registry.cpp" _db_registry_source)
string(REPLACE "\r\n" "\n" _db_registry_source "${_db_registry_source}")
foreach(_required_registry_boundary
    "void DB_MediaRemapTechniqueSet(MaterialTechniqueSet *techniqueSet)"
    "techniqueSet->remappedTechniqueSet = techniqueSet;"
    "void DB_MediaReleaseTechniqueSet(XAssetHeader, void *) {}"
    "void DB_MediaSaveSounds() {}"
    "void DB_MediaLoadSounds() {}"
    "#define DB_MediaRemapTechniqueSet Material_OriginalRemapTechniqueSet"
    "#define DB_MediaReleaseTechniqueSet Material_ReleaseTechniqueSet"
    "#define DB_MediaSaveSounds DB_SaveSounds"
    "#define DB_MediaLoadSounds DB_LoadSounds"
)
    string(FIND "${_db_registry_source}" "${_required_registry_boundary}" _registry_boundary_pos)
    if (_registry_boundary_pos EQUAL -1)
        message(FATAL_ERROR
            "Headless database media boundary is missing: ${_required_registry_boundary}")
    endif()
endforeach()

string(FIND "${_db_registry_source}"
    "techniqueSet->xmodelPieces = DB_AddXAsset(ASSET_TYPE_TECHNIQUE_SET, (XAssetHeader)techniqueSet->xmodelPieces).xmodelPieces;\n    DB_MediaRemapTechniqueSet(techniqueSet->techniqueSet);\n    DB_MediaUploadShaders(techniqueSet->techniqueSet);"
    _db_registry_technique_publish_order)
string(FIND "${_db_registry_source}"
    "g_archiveBuf = 1;\n        DB_MediaSyncRenderThread();\n        DB_MediaClearStaticModelCacheRefs();\n        DB_MediaSaveSounds();\n        DB_SaveDObjs();"
    _db_registry_archive_order)
string(FIND "${_db_registry_source}"
    "DB_MediaLoadSounds();\n    DB_LoadDObjs();\n    DB_ExternalInitAssets();"
    _db_registry_unarchive_order)
string(FIND "${_db_registry_source}"
    "DB_MediaDirtyTechniqueSetOverrides();\n    BG_FillInAllWeaponItems();"
    _db_registry_external_init_order)

if (_db_registry_technique_publish_order EQUAL -1
    OR _db_registry_archive_order EQUAL -1
    OR _db_registry_unarchive_order EQUAL -1
    OR _db_registry_external_init_order EQUAL -1)
    message(FATAL_ERROR
        "Headless database media wrappers must preserve publication, archive, DObj, and gameplay ordering")
endif()

file(READ "${SOURCE_ROOT}/.github/workflows/ci.yml" _ci_workflow)
string(FIND "${_ci_workflow}"
    "name: KisakCOD-windows-x86-headless-Release"
    _headless_artifact_name)
string(FIND "${_ci_workflow}"
    "path: bin/Release/KisakCOD-dedi.exe"
    _headless_artifact_path)

file(READ "${SOURCE_ROOT}/.github/workflows/licensed-smoke.yml" _licensed_smoke_workflow)
string(FIND "${_licensed_smoke_workflow}"
    "name: Windows x86 headless dedicated"
    _headless_smoke_job)
string(FIND "${_licensed_smoke_workflow}"
    "-HeadlessDedi -BuildDir build-headless -Clean"
    _headless_smoke_build)
string(FIND "${_licensed_smoke_workflow}"
    "-Executable ./bin/Release/KisakCOD-dedi.exe -Port 28962"
    _headless_smoke_probe)
string(FIND "${_licensed_smoke_workflow}"
    "if: github.ref == 'refs/heads/master'"
    _headless_smoke_trusted_ref)
string(FIND "${_licensed_smoke_workflow}"
    "ref: \${{ github.sha }}"
    _headless_smoke_immutable_ref)

file(READ "${SOURCE_ROOT}/scripts/ci/smoke-dedicated.ps1" _dedicated_smoke_script)
foreach(_required_smoke_boundary
    "[Guid]::NewGuid().ToString('N')"
    "$expectedIdentity = \"\\sv_hostname\\$serverIdentity\""
    "$text.Contains($expectedMap)"
    "$text.Contains($expectedIdentity)"
    "$reservation.Client.ExclusiveAddressUse = $true"
    "Remove-Item -LiteralPath $homePath -Recurse -Force"
)
    string(FIND "${_dedicated_smoke_script}" "${_required_smoke_boundary}" _smoke_boundary_pos)
    if (_smoke_boundary_pos EQUAL -1)
        message(FATAL_ERROR
            "Headless gameplay smoke is missing isolation boundary: ${_required_smoke_boundary}")
    endif()
endforeach()

if (_headless_artifact_name EQUAL -1
    OR _headless_artifact_path EQUAL -1
    OR _headless_smoke_job EQUAL -1
    OR _headless_smoke_build EQUAL -1
    OR _headless_smoke_probe EQUAL -1
    OR _headless_smoke_trusted_ref EQUAL -1
    OR _headless_smoke_immutable_ref EQUAL -1)
    message(FATAL_ERROR
        "A linked headless binary must be retained and covered by a trusted protected map/getstatus smoke gate")
endif()

foreach(_excluded
    "${SRC_DIR}/win32/win_input.cpp"
    "${SRC_DIR}/win32/win_input.h"
    "${SRC_DIR}/win32/win_wndproc.cpp"
    "${SRC_DIR}/win32/win_voice.cpp"
    "${SRC_DIR}/win32/win_storage.cpp"
)
    if (_excluded IN_LIST _dedi_sources)
        message(FATAL_ERROR "Headless dedicated source profile still contains client-only Win32 source ${_excluded}")
    endif()
endforeach()
