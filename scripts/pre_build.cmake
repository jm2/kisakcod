# Common target setup. Platform-specific libraries remain isolated here until
# they can be replaced by the portable RHI/audio/platform layers.

add_dependencies(${PROJECT_NAME} update_build_number)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${SRC_DIR}
    ${DEPS_DIR}
)

# 64-bit targets read retail records through the generated disk32 mirrors.
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    kisakcod_use_disk32_mirrors(${PROJECT_NAME})
endif()

if (KISAK_EXTENDED)
    target_compile_definitions(${PROJECT_NAME} PUBLIC KISAK_EXTENDED)
endif()

# Steam identity/auth is a capability, not a platform assumption. A genuinely
# headless dedicated target must never depend on the desktop Steam client API;
# it uses the cl_guid identity backend even when the client target in the same
# configure enables Steam.
set(KISAK_TARGET_ENABLE_STEAM ${KISAK_ENABLE_STEAM})
if (KISAK_DEDI_HEADLESS AND PROJECT_NAME STREQUAL "KisakCOD-dedi")
    set(KISAK_TARGET_ENABLE_STEAM OFF)
endif()

if (KISAK_TARGET_ENABLE_STEAM)
    target_compile_definitions(${PROJECT_NAME} PUBLIC KISAK_STEAM)
endif()

if (WIN32)
    if (NOT DEFINED KISAK_TARGET_NEEDS_CLIENT_MEDIA)
        set(KISAK_TARGET_NEEDS_CLIENT_MEDIA ON)
    endif()

    target_compile_definitions(${PROJECT_NAME} PUBLIC WIN32 _CONSOLE _MBCS)

    set_target_properties(${PROJECT_NAME} PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${BIN_DIR}/$<CONFIG>"
        WIN32_EXECUTABLE TRUE
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )

    if (KISAK_TARGET_NEEDS_CLIENT_MEDIA)
        if (CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(_kisak_dx_arch x64)
        else()
            set(_kisak_dx_arch x86)
        endif()
        if (CICD)
            if (NOT DXSDK_DIR)
                message(FATAL_ERROR "DXSDK_DIR must point to Microsoft.DXSDK.D3DX/build/native")
            endif()
            set(DXSDK_INC_DIR "${DXSDK_DIR}/include")
            set(DXSDK_LIB_DIR "${DXSDK_DIR}/release/lib/${_kisak_dx_arch}")
            set(D3DX_LIB d3dx9.lib)
        else()
            if (NOT DXSDK_DIR)
                set(DXSDK_DIR "$ENV{DXSDK_DIR}")
            endif()
            if (NOT DXSDK_DIR)
                message(FATAL_ERROR "DXSDK_DIR is not set. Install the June 2010 DirectX SDK or pass -DDXSDK_DIR=...")
            endif()
            set(DXSDK_INC_DIR "${DXSDK_DIR}/include")
            set(DXSDK_LIB_DIR "${DXSDK_DIR}/lib/${_kisak_dx_arch}")
            set(D3DX_LIB "$<$<CONFIG:Debug>:d3dx9d.lib>$<$<NOT:$<CONFIG:Debug>>:d3dx9.lib>")
        endif()

        target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC "${DXSDK_INC_DIR}")
        target_link_directories(${PROJECT_NAME} PUBLIC "${DXSDK_LIB_DIR}")
    endif()
    if (KISAK_TARGET_ENABLE_STEAM)
        target_link_directories(${PROJECT_NAME} PUBLIC
            "${DEPS_DIR}/steamsdk"
        )
    endif()
    if (KISAK_TARGET_NEEDS_CLIENT_MEDIA AND NOT KISAK_MEDIA_STUBS)
        target_link_directories(${PROJECT_NAME} PUBLIC
            "${DEPS_DIR}/msslib"
            "${DEPS_DIR}/binklib"
        )
    endif()

    target_link_options(${PROJECT_NAME} PRIVATE
        "$<$<CONFIG:Release>:/DEBUG>"
        "$<$<CONFIG:Release>:/OPT:REF>"
        "$<$<CONFIG:Release>:/OPT:ICF>"
        "$<$<EQUAL:${CMAKE_SIZEOF_VOID_P},4>:/machine:x86>"
    )

    target_link_libraries(${PROJECT_NAME} PUBLIC
        ws2_32.lib
        winmm.lib
        kernel32.lib
        user32.lib
        gdi32.lib
        winspool.lib
        comdlg32.lib
        advapi32.lib
        shell32.lib
        ole32.lib
        oleaut32.lib
        uuid.lib
        odbc32.lib
        odbccp32.lib
    )
    if (KISAK_TARGET_ENABLE_STEAM)
        target_link_libraries(${PROJECT_NAME} PUBLIC steam_api.lib)
    endif()
    if (KISAK_TARGET_NEEDS_CLIENT_MEDIA)
        if (KISAK_MEDIA_STUBS)
            # 32-bit-only Miles/Bink: link the silent stubs instead.
            target_sources(${PROJECT_NAME} PRIVATE ${SRC_DIR}/win32/win_media_stubs.cpp)
            # Engine code calls Miles/Bink through their dllimport declarations;
            # the linker binds those calls to the stub definitions (LNK4217/4286).
            target_link_options(${PROJECT_NAME} PRIVATE /IGNORE:4217,4286)
            set(_kisak_miles_lib "")
            set(_kisak_bink_lib "")
        else()
            set(_kisak_miles_lib mss32.lib)
            set(_kisak_bink_lib binkw32.lib)
        endif()
        target_link_libraries(${PROJECT_NAME} PUBLIC
            ${_kisak_miles_lib}
            dsound.lib
            ${D3DX_LIB}
            d3d9.lib
            ddraw.lib
            ${_kisak_bink_lib}
            dxguid.lib
        )
    endif()
else()
    # POSIX (linux/macos) target dependencies. The engine links the system
    # POSIX layers directly; there is no DirectX/Miles/Bink/Steam dependency
    # because the headless source profile excludes client/media sources.
    find_package(Threads REQUIRED)
    target_compile_definitions(${PROJECT_NAME} PUBLIC
        UNIX
        $<$<PLATFORM_ID:Darwin>:__APPLE__>
    )
    target_link_libraries(${PROJECT_NAME} PUBLIC
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )
    # Speex's integer types: off Windows, speex_types.h includes the
    # configure-generated <speex/speex_config_types.h>.
    set(SIZE16 short)
    set(SIZE32 int)
    configure_file(${DEPS_DIR}/speex/speex_config_types.h.in
        ${CMAKE_BINARY_DIR}/generated/speex/speex_config_types.h @ONLY)
    target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR}/generated)
    # A POSIX client renders through dxvk-native's D3D9 (docs/design/CLIENT.md).
    # Point PKG_CONFIG_PATH at its install (lib*/pkgconfig/dxvk-d3d9.pc).
    if (KISAK_TARGET_NEEDS_CLIENT_MEDIA)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(KISAK_DXVK_D3D9 REQUIRED IMPORTED_TARGET dxvk-d3d9)
        target_link_libraries(${PROJECT_NAME} PUBLIC PkgConfig::KISAK_DXVK_D3D9)
        target_compile_definitions(${PROJECT_NAME} PUBLIC KISAK_DXVK_NATIVE)
        # Miles and Bink exist here only as headers: the client compiles
        # against msslib/binklib and links silent stubs (KISAK_MEDIA_STUBS).
        target_compile_definitions(${PROJECT_NAME} PUBLIC KISAK_POSIX_CLIENT_MEDIA)
    endif()
    if (KISAK_TARGET_ENABLE_STEAM)
        message(FATAL_ERROR
            "POSIX engine targets build with the cl_guid identity backend; "
            "the desktop Steam client API is not linked.")
    endif()
endif()
