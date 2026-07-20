foreach(required_variable IN ITEMS
    SEAL_EXECUTABLE
    PRODUCTION_OBJECTS
    CXX_COMPILER_ID)
    if(NOT DEFINED ${required_variable}
       OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR
            "Missing physical-memory production-seal input: ${required_variable}")
    endif()
endforeach()
string(REPLACE "|" ";" production_objects "${PRODUCTION_OBJECTS}")
if("${production_objects}" STREQUAL "")
    message(FATAL_ERROR "Physical-memory production object list is empty")
endif()

execute_process(
    COMMAND "${SEAL_EXECUTABLE}"
    RESULT_VARIABLE seal_result
    OUTPUT_VARIABLE seal_output
    ERROR_VARIABLE seal_error)
if(NOT seal_result EQUAL 0)
    message(FATAL_ERROR
        "Physical-memory macro-off compile seal failed (${seal_result}):\n"
        "${seal_output}${seal_error}")
endif()

if(CXX_COMPILER_ID STREQUAL "MSVC")
    if(NOT DEFINED LINKER_TOOL OR "${LINKER_TOOL}" STREQUAL "")
        message(FATAL_ERROR "MSVC production seal requires CMAKE_LINKER")
    endif()
    execute_process(
        COMMAND "${LINKER_TOOL}" /dump /symbols ${production_objects}
        RESULT_VARIABLE symbol_result
        OUTPUT_VARIABLE all_symbols
        ERROR_VARIABLE symbol_error)
    if(NOT symbol_result EQUAL 0)
        message(FATAL_ERROR
            "MSVC physical-memory symbol inspection failed (${symbol_result}):\n"
            "${all_symbols}${symbol_error}")
    endif()

    foreach(local_name IN ITEMS g_mem g_overAllocatedSize)
        string(REGEX MATCHALL "[^\n]*${local_name}[^\n]*" matching_lines
            "${all_symbols}")
        if("${matching_lines}" STREQUAL "")
            message(FATAL_ERROR
                "Macro-off PMem object omitted local state symbol ${local_name}")
        endif()
        foreach(symbol_line IN LISTS matching_lines)
            if(symbol_line MATCHES "External")
                message(FATAL_ERROR
                    "Macro-off PMem state escaped external linkage: ${symbol_line}")
            endif()
        endforeach()
    endforeach()
else()
    if(NOT DEFINED NM_TOOL OR "${NM_TOOL}" STREQUAL "")
        message(FATAL_ERROR "Production seal requires CMAKE_NM")
    endif()
    execute_process(
        COMMAND "${NM_TOOL}" -a -C ${production_objects}
        RESULT_VARIABLE symbol_result
        OUTPUT_VARIABLE all_symbols
        ERROR_VARIABLE symbol_error)
    if(NOT symbol_result EQUAL 0)
        message(FATAL_ERROR
            "Physical-memory symbol inspection failed (${symbol_result}):\n"
            "${all_symbols}${symbol_error}")
    endif()
    # Apple nm retains Mach-O's extra leading underscore when -C cannot
    # demangle a local Itanium symbol. AppleClang Release may instead coalesce
    # both zero-initialized locals into one local __MergedGlobals allocation.
    # All other objects must retain both exact anonymous-namespace names.
    set(missing_local_names)
    foreach(local_symbol IN ITEMS "g_mem|5" "g_overAllocatedSize|19")
        string(REPLACE "|" ";" local_fields "${local_symbol}")
        list(GET local_fields 0 local_name)
        list(GET local_fields 1 local_name_length)
        string(REGEX MATCHALL "[^\n\r]*${local_name}[^\n\r]*"
            matching_lines "${all_symbols}")
        if(NOT matching_lines MATCHES
               "\\(anonymous namespace\\)::${local_name}([^A-Za-z0-9_]|$)"
           AND NOT matching_lines MATCHES
               "_+ZN12_GLOBAL__N_1L?${local_name_length}${local_name}E([^A-Za-z0-9_]|$)")
            list(APPEND missing_local_names "${local_name}")
        endif()
    endforeach()
    set(apple_merged_state FALSE)
    list(LENGTH missing_local_names missing_local_count)
    if(missing_local_count GREATER 0)
        if(CXX_COMPILER_ID STREQUAL "AppleClang"
           AND missing_local_count EQUAL 2
           AND all_symbols MATCHES
               "(^|[\n\r])[0-9A-Fa-f]+[ \t]+b[ \t]+__MergedGlobals([\n\r]|$)")
            set(apple_merged_state TRUE)
        else()
            message(FATAL_ERROR
                "Macro-off PMem object omitted anonymous locals "
                "${missing_local_names}:\n${all_symbols}")
        endif()
    endif()

    execute_process(
        COMMAND "${NM_TOOL}" -g -C ${production_objects}
        RESULT_VARIABLE global_result
        OUTPUT_VARIABLE global_symbols
        ERROR_VARIABLE global_error)
    if(NOT global_result EQUAL 0)
        message(FATAL_ERROR
            "Physical-memory global-symbol inspection failed (${global_result}):\n"
            "${global_symbols}${global_error}")
    endif()
    foreach(local_name IN ITEMS g_mem g_overAllocatedSize)
        string(REGEX MATCHALL "[^\n\r]*${local_name}[^\n\r]*"
            escaped_global_lines "${global_symbols}")
        if(NOT "${escaped_global_lines}" STREQUAL "")
            message(FATAL_ERROR
                "Macro-off PMem mutable state retains external linkage:\n"
                "${escaped_global_lines}")
        endif()
    endforeach()
    if(apple_merged_state AND global_symbols MATCHES "__MergedGlobals")
        message(FATAL_ERROR
            "AppleClang PMem merged state escaped external linkage:\n"
            "${global_symbols}")
    endif()
endif()

if(all_symbols MATCHES "PhysicalMemoryGlobalStateTestAccess")
    message(FATAL_ERROR
        "Macro-off PMem object emitted test-access symbols:\n${all_symbols}")
endif()
