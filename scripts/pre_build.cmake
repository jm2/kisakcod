# Common target setup. Platform-specific libraries remain isolated here until
# they can be replaced by the portable RHI/audio/platform layers.

add_dependencies(${PROJECT_NAME} update_build_number)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${SRC_DIR}
    ${DEPS_DIR}
)

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
        if (CICD)
            if (NOT DXSDK_DIR)
                message(FATAL_ERROR "DXSDK_DIR must point to Microsoft.DXSDK.D3DX/build/native")
            endif()
            set(DXSDK_INC_DIR "${DXSDK_DIR}/include")
            set(DXSDK_LIB_DIR "${DXSDK_DIR}/release/lib/x86")
            set(D3DX_LIB d3dx9.lib)
        else()
            if (NOT DXSDK_DIR)
                set(DXSDK_DIR "$ENV{DXSDK_DIR}")
            endif()
            if (NOT DXSDK_DIR)
                message(FATAL_ERROR "DXSDK_DIR is not set. Install the June 2010 DirectX SDK or pass -DDXSDK_DIR=...")
            endif()
            set(DXSDK_INC_DIR "${DXSDK_DIR}/include")
            set(DXSDK_LIB_DIR "${DXSDK_DIR}/lib/x86")
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
    if (KISAK_TARGET_NEEDS_CLIENT_MEDIA)
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
        target_link_libraries(${PROJECT_NAME} PUBLIC
            mss32.lib
            dsound.lib
            ${D3DX_LIB}
            d3d9.lib
            ddraw.lib
            binkw32.lib
            dxguid.lib
        )
    endif()
else()
    message(FATAL_ERROR "Portable target dependencies are not implemented for ${KISAK_PLATFORM}")
endif()
