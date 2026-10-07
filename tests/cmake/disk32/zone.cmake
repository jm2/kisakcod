# One synthetic zone through the real 64-bit load path: DB_TryLoadXFileInternal
# opens and inflates the .ff the test writes, the envelope dispatches every
# asset through Load_XAssetHeader to its family's loader, the pools publish
# them, and DB_ShutdownXAssets unloads the zone. Every database TU the Linux
# headless server builds is linked as it is; only the engine boundary is
# stubbed, and section GC drops what the load never reaches. The admission
# seam (db_asset_layout.h) is defined only here.
file(GLOB _zone_db_sources CONFIGURE_DEPENDS "${SRC_DIR}/database/*.cpp")
list(FILTER _zone_db_sources EXCLUDE REGEX "/(shader_cache|db_fx_zone_adapter_wiring|db_graph_hash)\\.cpp$")
set(_zone_zlib adler32 compress crc32 deflate infblock infcodes inffast inflate inftrees infutil trees zutil)
list(TRANSFORM _zone_zlib PREPEND "${DEPS_DIR}/zlib/")
list(TRANSFORM _zone_zlib APPEND ".c")
add_library(kisakcod-db-disk32-zone-engine OBJECT
    ${_zone_db_sources}
    ${_zone_zlib}
    ${SRC_DIR}/universal/physicalmemory.cpp
    ${SRC_DIR}/universal/physicalmemory_checked.cpp
    ${SRC_DIR}/qcommon/sys_sync.cpp
    ${SRC_DIR}/script/scr_stringlist.cpp
    ${SRC_DIR}/script/scr_memorytree.cpp
    ${SRC_DIR}/_platform/posix/sys_file.cpp
    ${SRC_DIR}/_platform/posix/sys_memory.cpp
)
add_executable(kisakcod-db-disk32-zone-tests db_disk32_zone_tests.cpp disk32_zone_harness.cpp
    $<TARGET_OBJECTS:kisakcod-db-disk32-zone-engine>)
# Retail asset-list shapes the main zone does not use, in a zone of their own.
add_executable(kisakcod-db-disk32-zone-retail-tests db_disk32_zone_retail_tests.cpp disk32_zone_harness.cpp
    $<TARGET_OBJECTS:kisakcod-db-disk32-zone-engine>)
set(_zone_tests kisakcod-db-disk32-zone-tests kisakcod-db-disk32-zone-retail-tests)
foreach(_target kisakcod-db-disk32-zone-engine ${_zone_tests})
    kisakcod_use_disk32_mirrors(${_target})
    target_include_directories(${_target} SYSTEM PRIVATE ${SRC_DIR} ${DEPS_DIR})
    target_compile_features(${_target} PRIVATE cxx_std_20)
    # As the Linux headless server compiles them, plus the admission seam.
    target_compile_definitions(${_target} PRIVATE
        KISAK_MP KISAK_DEDICATED DEDICATED KISAK_DEDI_HEADLESS UNIX KISAK_DB_ASSET_FAMILY_ADMISSION_TESTING)
    target_compile_options(${_target} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-fms-extensions> -ffunction-sections -fdata-sections)
endforeach()
target_compile_options(kisakcod-db-disk32-zone-engine PRIVATE -w)
if (CMAKE_CXX_FLAGS MATCHES "-fsanitize=[^ ]*address" AND NOT APPLE)
    # Otherwise ASan's global registration keeps every global alive.
    target_compile_options(kisakcod-db-disk32-zone-engine PRIVATE -fsanitize-address-globals-dead-stripping)
endif()
foreach(_target ${_zone_tests})
    target_compile_options(${_target} PRIVATE -Wall -Wextra -Werror)
    target_link_options(${_target} PRIVATE ${KISAK_TEST_GC_SECTIONS})
    if (CMAKE_CXX_FLAGS MATCHES "-fsanitize=[^ ]*address" AND NOT APPLE)
        target_compile_options(${_target} PRIVATE -fsanitize-address-globals-dead-stripping)
        target_link_options(${_target} PRIVATE -Wl,-z,start-stop-gc)
    endif()
    target_link_libraries(${_target} PRIVATE Threads::Threads)
    set_target_properties(${_target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endforeach()
add_test(NAME database-disk32-zone-load COMMAND kisakcod-db-disk32-zone-tests)
add_test(NAME database-disk32-zone-retail-shapes COMMAND kisakcod-db-disk32-zone-retail-tests)
set_tests_properties(database-disk32-zone-retail-shapes PROPERTIES TIMEOUT 60)
# With one family's guard left on, the zone fails closed naming it.
foreach(_family IN ITEMS rawfile stringtable physpreset localize map_ents game_map_mp com_map
        sndcurve loaded_sound sound image lightdef techset material font fx impactfx menu menufile
        xmodel xanim weapon)
    add_test(NAME database-disk32-zone-guard-${_family} COMMAND kisakcod-db-disk32-zone-tests guard ${_family})
    set_tests_properties(database-disk32-zone-guard-${_family} PROPERTIES TIMEOUT 60)
endforeach()
set_tests_properties(database-disk32-zone-load PROPERTIES TIMEOUT 60)
