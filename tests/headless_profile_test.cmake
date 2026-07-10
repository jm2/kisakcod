cmake_minimum_required(VERSION 3.16)

set(SRC_DIR "${SOURCE_ROOT}/src")
set(DEPS_DIR "${SOURCE_ROOT}/deps")
set(SCRIPTS_DIR "${SOURCE_ROOT}/scripts")
set(KISAK_DEDI_HEADLESS ON)

include("${SCRIPTS_DIR}/common_files.cmake")
include("${SCRIPTS_DIR}/mp/mp_files.cmake")
include("${SCRIPTS_DIR}/dedi/dedi_files.cmake")
include("${SCRIPTS_DIR}/dedi/dedi_sources.cmake")

kisakcod_get_dedi_sources(_dedi_sources)
kisakcod_assert_headless_dedi_sources(${_dedi_sources})

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
