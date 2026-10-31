# Builds the client's Direct3DCreate9 wrapper test (tests/posix_direct3d_tests.cpp)
# against a runtime prefix's dxvk-native (#970). The Linux runtime project adds
# it as an ExternalProject after dxvk and runs it with no Vulkan driver, so a
# prefix whose dxvk aborts the client instead of failing with a reason fails
# to build.
cmake_minimum_required(VERSION 3.20)
project(kisakcod_dxvk_check CXX)

set(KISAK_DXVK_PREFIX "" CACHE PATH "the runtime prefix holding dxvk-native's lib/ and include/")
if (NOT KISAK_DXVK_PREFIX)
    message(FATAL_ERROR "Set KISAK_DXVK_PREFIX to the runtime prefix")
endif()

# Only the prefix's dxvk-d3d9.pc, never a host install.
set(ENV{PKG_CONFIG_LIBDIR} "${KISAK_DXVK_PREFIX}/lib/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
find_package(PkgConfig REQUIRED)
pkg_check_modules(KISAK_DXVK_D3D9 REQUIRED IMPORTED_TARGET dxvk-d3d9)

# The release links dxvk's libstdc++ statically, which is what kept its error
# from reaching the client (#970). A dxvk on the shared libstdc++ passes the
# test below without exercising that, so it must not need one.
set(_kisak_dxvk_lib "${KISAK_DXVK_PREFIX}/lib/libdxvk_d3d9.so.0")
find_program(KISAK_READELF readelf REQUIRED)
execute_process(COMMAND ${KISAK_READELF} -d "${_kisak_dxvk_lib}"
    OUTPUT_VARIABLE _kisak_dxvk_dynamic RESULT_VARIABLE _kisak_readelf_result)
if (NOT _kisak_readelf_result EQUAL 0)
    message(FATAL_ERROR "readelf couldn't read ${_kisak_dxvk_lib}")
endif()
if (_kisak_dxvk_dynamic MATCHES "NEEDED[^\n]*libstdc\\+\\+")
    message(FATAL_ERROR "${_kisak_dxvk_lib} needs the shared libstdc++, so dxvk's -static-libstdc++ "
        "found no static libstdc++ and the release build isn't reproduced. Install it "
        "(libstdc++-static on Fedora and RHEL; Debian's and Ubuntu's libstdc++-dev has it) "
        "and rebuild the runtime.")
endif()

set(_kisak_repo "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
add_executable(posix_direct3d_check
    "${_kisak_repo}/tests/posix_direct3d_tests.cpp"
    "${_kisak_repo}/src/_platform/posix/posix_direct3d.cpp")
target_include_directories(posix_direct3d_check SYSTEM PRIVATE "${_kisak_repo}/src")
target_compile_features(posix_direct3d_check PRIVATE cxx_std_20)
target_link_libraries(posix_direct3d_check PRIVATE PkgConfig::KISAK_DXVK_D3D9)
set_target_properties(posix_direct3d_check PROPERTIES BUILD_RPATH "${KISAK_DXVK_PREFIX}/lib")
