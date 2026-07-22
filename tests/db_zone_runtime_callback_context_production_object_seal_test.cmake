foreach(_required IN ITEMS
    SEAL_EXECUTABLE
    PRODUCTION_OBJECTS
    CXX_COMPILER_ID)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR
            "Missing callback-context object-seal input: ${_required}")
    endif()
endforeach()

string(REPLACE "|" ";" _production_objects "${PRODUCTION_OBJECTS}")
if("${_production_objects}" STREQUAL "")
    message(FATAL_ERROR "Callback-context production object list is empty")
endif()

execute_process(
    COMMAND "${SEAL_EXECUTABLE}"
    RESULT_VARIABLE _seal_result
    OUTPUT_VARIABLE _seal_output
    ERROR_VARIABLE _seal_error)
if(NOT _seal_result EQUAL 0)
    message(FATAL_ERROR
        "Callback-context macro-off compile seal failed (${_seal_result}):\n"
        "${_seal_output}${_seal_error}")
endif()

if(CXX_COMPILER_ID STREQUAL "MSVC")
    if(NOT DEFINED LINKER_TOOL OR "${LINKER_TOOL}" STREQUAL "")
        message(FATAL_ERROR "MSVC callback-context seal requires CMAKE_LINKER")
    endif()
    execute_process(
        COMMAND "${LINKER_TOOL}" /dump /symbols ${_production_objects}
        RESULT_VARIABLE _symbol_result
        OUTPUT_VARIABLE _all_symbols
        ERROR_VARIABLE _symbol_error)
    if(NOT _symbol_result EQUAL 0)
        message(FATAL_ERROR
            "MSVC callback-context symbol inspection failed "
            "(${_symbol_result}):\n${_all_symbols}${_symbol_error}")
    endif()

    string(REGEX MATCHALL "[^\n\r]*g_contextStore[^\n\r]*"
        _store_lines "${_all_symbols}")
    if("${_store_lines}" STREQUAL "")
        message(FATAL_ERROR
            "Macro-off callback object omitted local g_contextStore:\n"
            "${_all_symbols}")
    endif()
    foreach(_line IN LISTS _store_lines)
        if(_line MATCHES "External")
            message(FATAL_ERROR
                "Callback-context store escaped external linkage: ${_line}")
        endif()
    endforeach()

    foreach(_local_name IN ITEMS
        kWitnessSeed
        kInvalidStoreIndex
        initialized)
        string(REGEX MATCHALL "[^\n\r]*${_local_name}[^\n\r]*"
            _local_lines "${_all_symbols}")
        foreach(_line IN LISTS _local_lines)
            if(_line MATCHES "External")
                message(FATAL_ERROR
                    "Callback-context helper/state escaped external linkage: "
                    "${_line}")
            endif()
        endforeach()
    endforeach()
else()
    if(NOT DEFINED NM_TOOL OR "${NM_TOOL}" STREQUAL "")
        message(FATAL_ERROR "Callback-context seal requires CMAKE_NM")
    endif()
    execute_process(
        COMMAND "${NM_TOOL}" -a -C ${_production_objects}
        RESULT_VARIABLE _symbol_result
        OUTPUT_VARIABLE _all_symbols
        ERROR_VARIABLE _symbol_error)
    if(NOT _symbol_result EQUAL 0)
        message(FATAL_ERROR
            "Callback-context symbol inspection failed (${_symbol_result}):\n"
            "${_all_symbols}${_symbol_error}")
    endif()

    set(_apple_merged_store FALSE)
    if(CXX_COMPILER_ID STREQUAL "AppleClang"
       AND _all_symbols MATCHES
           "(^|[\n\r])[0-9A-Fa-f]+[ \t]+b[ \t]+__MergedGlobals([\n\r]|$)")
        set(_apple_merged_store TRUE)
    endif()
    if(NOT _all_symbols MATCHES
           "\\(anonymous namespace\\)::g_contextStore([^A-Za-z0-9_]|$)"
       AND NOT _all_symbols MATCHES
           "_+ZN12_GLOBAL__N_1L?14g_contextStoreE([^A-Za-z0-9_]|$)"
       AND NOT _apple_merged_store)
        message(FATAL_ERROR
            "Macro-off callback object omitted local g_contextStore:\n"
            "${_all_symbols}")
    endif()

    execute_process(
        COMMAND "${NM_TOOL}" -g -C ${_production_objects}
        RESULT_VARIABLE _global_result
        OUTPUT_VARIABLE _global_symbols
        ERROR_VARIABLE _global_error)
    if(NOT _global_result EQUAL 0)
        message(FATAL_ERROR
            "Callback-context global-symbol inspection failed "
            "(${_global_result}):\n${_global_symbols}${_global_error}")
    endif()
    foreach(_forbidden_global IN ITEMS
        g_contextStore
        ContextStore
        ExactStoreIndex
        kWitnessSeed
        kInvalidStoreIndex
        initialized
        ZoneRuntimeCallbackContextTestAccess)
        if(_global_symbols MATCHES "${_forbidden_global}")
            message(FATAL_ERROR
                "Callback-context local/test state escaped external linkage "
                "(${_forbidden_global}):\n${_global_symbols}")
        endif()
    endforeach()
    if(CXX_COMPILER_ID STREQUAL "AppleClang"
       AND _global_symbols MATCHES "__MergedGlobals")
        message(FATAL_ERROR
            "AppleClang callback merged state escaped external linkage:\n"
            "${_global_symbols}")
    endif()

    if(NOT CXX_COMPILER_ID STREQUAL "AppleClang")
        if(NOT DEFINED READELF_TOOL OR "${READELF_TOOL}" STREQUAL "")
            message(FATAL_ERROR "ELF callback-context seal requires readelf")
        endif()
        execute_process(
            COMMAND "${READELF_TOOL}" -sW ${_production_objects}
            RESULT_VARIABLE _readelf_result
            OUTPUT_VARIABLE _elf_symbols
            ERROR_VARIABLE _readelf_error)
        if(NOT _readelf_result EQUAL 0)
            message(FATAL_ERROR
                "Callback-context ELF symbol inspection failed "
                "(${_readelf_result}):\n${_elf_symbols}${_readelf_error}")
        endif()
        string(REGEX MATCHALL
            "[^\n\r]*_ZN2db12zone_runtime12_GLOBAL__N_1L?14g_contextStoreE[^\n\r]*"
            _elf_store_lines "${_elf_symbols}")
        if(NOT _elf_store_lines MATCHES "OBJECT[ \t]+LOCAL[ \t]+DEFAULT")
            message(FATAL_ERROR
                "ELF callback-context store is not a local object:\n"
                "${_elf_symbols}")
        endif()
    endif()
endif()

# Private owner methods intentionally remain normal cross-TU symbols so the
# two friend owners can call them. C++ access control and the source seal own
# that capability boundary; this object seal checks only mutable/local state.
foreach(_owner_operation IN ITEMS
    TryClassifyStorage
    TryBind
    TryResolve
    TryAdvance
    TryAuthenticate
    TryCapture
    SpanIsSeparated)
    if(NOT _all_symbols MATCHES
        "ZoneRuntimeCallbackContextOwner.*${_owner_operation}")
        message(FATAL_ERROR
            "Macro-off object omitted private owner operation "
            "${_owner_operation}:\n${_all_symbols}")
    endif()
endforeach()

if(_all_symbols MATCHES "ZoneRuntimeCallbackContextTestAccess")
    message(FATAL_ERROR
        "Macro-off object emitted TestAccess symbols:\n${_all_symbols}")
endif()
