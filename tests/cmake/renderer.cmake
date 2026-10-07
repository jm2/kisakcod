# Renderer value-encoding and shader cache tests.
# Included from tests/CMakeLists.txt.

# r_d3d9types.h ABI: the non-Windows D3D9 stand-in the shared asset headers
# compile against (KPI K5). GfxImageLoadDef.format is a disk field, so the
# stand-in's enumerator values and 4-byte width are pinned here.
add_executable(kisakcod-renderer-d3d9types-tests
    renderer_d3d9types_tests.cpp
)
target_include_directories(kisakcod-renderer-d3d9types-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-renderer-d3d9types-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-renderer-d3d9types-tests)
set_target_properties(kisakcod-renderer-d3d9types-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME renderer-d3d9types-abi
    COMMAND kisakcod-renderer-d3d9types-tests
)

# Headless seam for the IDirect3D* reach in shared database code (KPI K5).
# The shader-load failure paths in db_load.cpp release and null opaque COM
# shader pointers through the engine helper DB_ReleaseVertexShader /
# DB_ReleasePixelShader; behind KISAK_DEDI_HEADLESS the Release() call is
# compiled out. This test compiles that engine TU and drives it through its
# production declaration, so a seam regression in the engine's cleanup fails
# this test instead of a mirror of it.
add_executable(kisakcod-renderer-headless-seam-tests
    renderer_headless_seam_tests.cpp
    ${SRC_DIR}/database/db_shader_release.cpp
)
target_include_directories(kisakcod-renderer-headless-seam-tests PRIVATE ${SRC_DIR})
target_compile_definitions(kisakcod-renderer-headless-seam-tests PRIVATE KISAK_DEDI_HEADLESS)
target_compile_features(kisakcod-renderer-headless-seam-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-renderer-headless-seam-tests)
set_target_properties(kisakcod-renderer-headless-seam-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME renderer-headless-seam-cleanup
    COMMAND kisakcod-renderer-headless-seam-tests
)

add_executable(kisakcod-renderer-reservation-atomic-tests
    renderer_reservation_atomic_tests.cpp
)
target_include_directories(kisakcod-renderer-reservation-atomic-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-renderer-reservation-atomic-tests PRIVATE cxx_std_20)
target_link_libraries(kisakcod-renderer-reservation-atomic-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-renderer-reservation-atomic-tests)
set_target_properties(kisakcod-renderer-reservation-atomic-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME renderer-bounded-reservation-protocols
    COMMAND kisakcod-renderer-reservation-atomic-tests
)
set_tests_properties(renderer-bounded-reservation-protocols PROPERTIES TIMEOUT 20)

include("${CMAKE_CURRENT_SOURCE_DIR}/renderer_enum_contracts.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/renderer_image_contracts.cmake")
add_executable(kisakcod-renderer-value-encoding-tests
    renderer_value_encoding_tests.cpp
    renderer_enum_contracts.cpp
    renderer_image_contracts.cpp
)
target_include_directories(kisakcod-renderer-value-encoding-tests PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/renderer-enums")
target_include_directories(
    kisakcod-renderer-value-encoding-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-renderer-value-encoding-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-renderer-value-encoding-tests)
set_target_properties(kisakcod-renderer-value-encoding-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME renderer-value-encoding-contracts
    COMMAND kisakcod-renderer-value-encoding-tests
)

# A09 / #131 (ki-y49k): content-addressed derived shader cache/sidecar core.
# Original retail bytecode is identified by content hash, the sidecar is
# versioned by format and converter identity, and stale or corrupt evidence is
# regenerated from the untouched original inputs. The portable core is
# exercised on the Linux host. Build enrollment is the database/engine source
# manifest; wiring it into the D3D9 shader creation boundary is future
# renderer integration.
add_executable(kisakcod-shader-cache-tests
    shader_cache_tests.cpp
    ${SRC_DIR}/database/shader_cache.cpp
    ${SRC_DIR}/database/db_graph_hash.cpp)
target_include_directories(
    kisakcod-shader-cache-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-shader-cache-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-shader-cache-tests)
set_target_properties(kisakcod-shader-cache-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(
    NAME database-derived-shader-cache-contracts
    COMMAND kisakcod-shader-cache-tests)
set_tests_properties(database-derived-shader-cache-contracts PROPERTIES TIMEOUT 20)

kisakcod_ilp32(kisakcod-shader-cache-tests
    database-derived-shader-cache-contracts)

kisakcod_ilp32(kisakcod-renderer-value-encoding-tests
    renderer-value-encoding-contracts)

# Static-model groups in the prim draw-surf stream: written through the
# engine's R_AllocDrawSurf / R_AddDelayedStaticModelDrawSurf
# (gfx_d3d/r_add_cmdbuf.cpp) and read back through R_ReadStaticModelGroup,
# which both static-model draw readers use. On 64-bit the XSurface pointer
# takes two dwords. The renderer headers need the Windows SDK's D3D9 types.
if (WIN32)
    # r_add_cmdbuf.cpp is decompiled engine code and not /W4 clean, so it
    # compiles at the engine's /W3 in an object library of its own.
    add_library(kisakcod-renderer-smodel-draw-stream-objects OBJECT ${SRC_DIR}/gfx_d3d/r_add_cmdbuf.cpp)
    target_include_directories(kisakcod-renderer-smodel-draw-stream-objects SYSTEM PUBLIC ${SRC_DIR} ${DEPS_DIR})
    target_compile_features(kisakcod-renderer-smodel-draw-stream-objects PUBLIC cxx_std_20)
    target_compile_definitions(kisakcod-renderer-smodel-draw-stream-objects PUBLIC KISAK_MP)
    target_compile_options(kisakcod-renderer-smodel-draw-stream-objects PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W3>)
    add_executable(kisakcod-renderer-smodel-draw-stream-tests renderer_smodel_draw_stream_tests.cpp)
    target_link_libraries(kisakcod-renderer-smodel-draw-stream-tests PRIVATE kisakcod-renderer-smodel-draw-stream-objects)
    kisakcod_test_warnings(kisakcod-renderer-smodel-draw-stream-tests)
    set_target_properties(kisakcod-renderer-smodel-draw-stream-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
    add_test(NAME renderer-smodel-draw-stream COMMAND kisakcod-renderer-smodel-draw-stream-tests)
    kisakcod_ilp32(kisakcod-renderer-smodel-draw-stream-tests renderer-smodel-draw-stream)
endif()
