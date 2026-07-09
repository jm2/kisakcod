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

if (WIN32)
    target_compile_definitions(${PROJECT_NAME} PUBLIC WIN32 _CONSOLE _MBCS)

    set_target_properties(${PROJECT_NAME} PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${BIN_DIR}/$<CONFIG>"
        WIN32_EXECUTABLE TRUE
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )

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
    target_link_directories(${PROJECT_NAME} PUBLIC
        "${DXSDK_LIB_DIR}"
        "${DEPS_DIR}/msslib"
        "${DEPS_DIR}/steamsdk"
        "${DEPS_DIR}/binklib"
    )

    target_link_options(${PROJECT_NAME} PRIVATE
        "$<$<CONFIG:Release>:/DEBUG>"
        "$<$<CONFIG:Release>:/OPT:REF>"
        "$<$<CONFIG:Release>:/OPT:ICF>"
        "$<$<EQUAL:${CMAKE_SIZEOF_VOID_P},4>:/machine:x86>"
    )

    target_link_libraries(${PROJECT_NAME} PUBLIC
        mss32.lib
        dsound.lib
        ${D3DX_LIB}
        d3d9.lib
        ddraw.lib
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
        binkw32.lib
        steam_api.lib
        dxguid.lib
    )
else()
    message(FATAL_ERROR "Portable target dependencies are not implemented for ${KISAK_PLATFORM}")
endif()
