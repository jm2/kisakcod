cmake_minimum_required(VERSION 3.16)

# Usage:
#   cmake -DSTACK_USAGE_FILES="path/to/fx_archive.su;path/to/more/*.su" \
#       -DSTACK_USAGE_LIMIT_BYTES=4096 \
#       -P tests/fx_stack_usage_report_test.cmake
#
# Each STACK_USAGE_FILES item is independently required to resolve. Direct
# paths are handled literally before wildcard expansion. A literal directory
# is searched recursively for .su files, which avoids relying on non-portable
# recursive-glob syntax. Every resolved file must contain at least one valid
# compiler -fstack-usage record.

if(NOT DEFINED STACK_USAGE_FILES OR "${STACK_USAGE_FILES}" STREQUAL "")
    message(FATAL_ERROR
        "STACK_USAGE_FILES must contain one or more .su paths or globs")
endif()

if(NOT DEFINED STACK_USAGE_LIMIT_BYTES)
    set(STACK_USAGE_LIMIT_BYTES 4096)
endif()
if(NOT "${STACK_USAGE_LIMIT_BYTES}" MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR
        "STACK_USAGE_LIMIT_BYTES must be a positive decimal integer")
endif()

function(kisakcod_normalize_decimal value out_value)
    if(NOT "${value}" MATCHES "^[0-9]+$")
        message(FATAL_ERROR "Invalid decimal stack size '${value}'")
    endif()
    string(REGEX REPLACE "^0+" "" _normalized "${value}")
    if(_normalized STREQUAL "")
        set(_normalized 0)
    endif()
    set(${out_value} "${_normalized}" PARENT_SCOPE)
endfunction()

function(kisakcod_decimal_exceeds value limit out_result)
    kisakcod_normalize_decimal("${value}" _value)
    kisakcod_normalize_decimal("${limit}" _limit)
    string(LENGTH "${_value}" _value_length)
    string(LENGTH "${_limit}" _limit_length)
    if(_value_length GREATER _limit_length)
        set(_exceeds TRUE)
    elseif(_value_length LESS _limit_length)
        set(_exceeds FALSE)
    elseif("${_value}" STRGREATER "${_limit}")
        set(_exceeds TRUE)
    else()
        set(_exceeds FALSE)
    endif()
    set(${out_result} "${_exceeds}" PARENT_SCOPE)
endfunction()

function(kisakcod_pop_physical_line content_var out_line out_has_line)
    set(_contents "${${content_var}}")
    if(_contents STREQUAL "")
        set(${out_line} "" PARENT_SCOPE)
        set(${out_has_line} FALSE PARENT_SCOPE)
        return()
    endif()

    string(FIND "${_contents}" "\n" _newline)
    if(_newline EQUAL -1)
        set(_line "${_contents}")
        set(_contents "")
    else()
        string(SUBSTRING "${_contents}" 0 ${_newline} _line)
        math(EXPR _next "${_newline} + 1")
        string(SUBSTRING "${_contents}" ${_next} -1 _contents)
    endif()
    string(REGEX REPLACE "\r$" "" _line "${_line}")

    set(${content_var} "${_contents}" PARENT_SCOPE)
    set(${out_line} "${_line}" PARENT_SCOPE)
    set(${out_has_line} TRUE PARENT_SCOPE)
endfunction()

kisakcod_normalize_decimal("${STACK_USAGE_LIMIT_BYTES}" _normalized_limit)
if(_normalized_limit STREQUAL "0")
    message(FATAL_ERROR "STACK_USAGE_LIMIT_BYTES must be greater than zero")
endif()

set(_stack_usage_files)
foreach(_input_spec IN LISTS STACK_USAGE_FILES)
    if("${_input_spec}" STREQUAL "")
        message(FATAL_ERROR "STACK_USAGE_FILES contains an empty input")
    endif()

    if(EXISTS "${_input_spec}")
        if(IS_DIRECTORY "${_input_spec}")
            file(GLOB_RECURSE _matches
                LIST_DIRECTORIES FALSE "${_input_spec}/*.su")
            if(NOT _matches)
                message(FATAL_ERROR
                    "Stack-usage input directory contains no .su files: ${_input_spec}")
            endif()
        else()
            set(_matches "${_input_spec}")
        endif()
    else()
        file(GLOB _matches LIST_DIRECTORIES FALSE "${_input_spec}")
        if(NOT _matches)
            message(FATAL_ERROR
                "Stack-usage input matched no files: ${_input_spec}")
        endif()
    endif()

    foreach(_match IN LISTS _matches)
        if(IS_DIRECTORY "${_match}")
            message(FATAL_ERROR
                "Stack-usage input resolved to a directory: ${_match}")
        endif()
        string(TOLOWER "${_match}" _match_lower)
        if(NOT "${_match_lower}" MATCHES "\\.su$")
            message(FATAL_ERROR
                "Stack-usage input does not have a .su suffix: ${_match}")
        endif()
        list(APPEND _stack_usage_files "${_match}")
    endforeach()
endforeach()
list(REMOVE_DUPLICATES _stack_usage_files)

if(NOT _stack_usage_files)
    message(FATAL_ERROR "No stack-usage report files were resolved")
endif()

string(ASCII 9 _tab)
set(_entry_pattern "^(.+)${_tab}([0-9]+)${_tab}([^${_tab}]+)$")
set(_total_entries 0)
set(_maximum_bytes 0)
set(_maximum_function "")
set(_maximum_file "")

foreach(_stack_usage_file IN LISTS _stack_usage_files)
    if(NOT EXISTS "${_stack_usage_file}")
        message(FATAL_ERROR
            "Stack-usage report disappeared: ${_stack_usage_file}")
    endif()
    file(SIZE "${_stack_usage_file}" _file_size)
    if(_file_size EQUAL 0)
        message(FATAL_ERROR
            "Stack-usage report is empty: ${_stack_usage_file}")
    endif()
    file(READ "${_stack_usage_file}" _contents)

    set(_file_entries 0)
    set(_line_number 0)
    while(TRUE)
        kisakcod_pop_physical_line(_contents _line _has_line)
        if(NOT _has_line)
            break()
        endif()
        math(EXPR _line_number "${_line_number} + 1")

        if("${_line}" STREQUAL "")
            message(FATAL_ERROR
                "Malformed empty entry at ${_stack_usage_file}:${_line_number}")
        endif()
        if(NOT "${_line}" MATCHES "${_entry_pattern}")
            message(FATAL_ERROR
                "Malformed stack-usage entry at ${_stack_usage_file}:${_line_number}: ${_line}")
        endif()
        set(_location_and_function "${CMAKE_MATCH_1}")
        set(_bytes "${CMAKE_MATCH_2}")
        set(_usage_kind "${CMAKE_MATCH_3}")

        if("${_location_and_function}" MATCHES
            "^(.+):([0-9]+):([0-9]+):(.+)$")
            set(_source "${CMAKE_MATCH_1}")
            set(_source_line "${CMAKE_MATCH_2}")
            set(_source_column "${CMAKE_MATCH_3}")
            set(_function "${CMAKE_MATCH_4}")
        elseif("${_location_and_function}" MATCHES
            "^(.+):([0-9]+):(.+)$")
            # GCC releases predating column-aware locations used this form.
            set(_source "${CMAKE_MATCH_1}")
            set(_source_line "${CMAKE_MATCH_2}")
            set(_source_column "?")
            set(_function "${CMAKE_MATCH_3}")
        elseif("${_location_and_function}" MATCHES "^(.+):([^:]+)$")
            # Clang emits source:function for synthesized TLS wrappers that
            # have no source line. The function remains explicitly attributed;
            # only its non-existent source coordinates are unavailable.
            set(_source "${CMAKE_MATCH_1}")
            set(_source_line "?")
            set(_source_column "?")
            set(_function "${CMAKE_MATCH_2}")
        else()
            message(FATAL_ERROR
                "Malformed source/function field at ${_stack_usage_file}:${_line_number}: ${_location_and_function}")
        endif()
        string(STRIP "${_source}" _source)
        string(STRIP "${_function}" _function)
        string(STRIP "${_usage_kind}" _usage_kind)
        string(TOLOWER "${_usage_kind}" _usage_kind_lower)

        if(_source STREQUAL "" OR _function STREQUAL "")
            message(FATAL_ERROR
                "Empty source or function at ${_stack_usage_file}:${_line_number}")
        endif()
        if(_usage_kind_lower MATCHES "(^|,)dynamic($|,)" OR
            _usage_kind_lower MATCHES "(^|,)unbounded($|,)")
            message(FATAL_ERROR
                "Dynamic or unbounded stack usage for '${_function}' at ${_source}:${_source_line}:${_source_column} (${_usage_kind})")
        endif()
        if(NOT _usage_kind_lower STREQUAL "static")
            message(FATAL_ERROR
                "Unsupported stack-usage qualifier '${_usage_kind}' for '${_function}' at ${_source}:${_source_line}:${_source_column}")
        endif()

        kisakcod_normalize_decimal("${_bytes}" _normalized_bytes)
        kisakcod_decimal_exceeds(
            "${_normalized_bytes}" "${_normalized_limit}" _over_limit)
        if(_over_limit)
            message(FATAL_ERROR
                "Stack usage ${_normalized_bytes} bytes exceeds the ${_normalized_limit}-byte limit for '${_function}' at ${_source}:${_source_line}:${_source_column}")
        endif()

        kisakcod_decimal_exceeds(
            "${_normalized_bytes}" "${_maximum_bytes}" _new_maximum)
        if(_new_maximum)
            set(_maximum_bytes "${_normalized_bytes}")
            set(_maximum_function "${_function}")
            set(_maximum_file "${_source}:${_source_line}:${_source_column}")
        endif()
        math(EXPR _file_entries "${_file_entries} + 1")
        math(EXPR _total_entries "${_total_entries} + 1")
    endwhile()

    if(_file_entries EQUAL 0)
        message(FATAL_ERROR
            "Stack-usage report contains no entries: ${_stack_usage_file}")
    endif()
    message(STATUS
        "Stack-usage report ${_stack_usage_file}: ${_file_entries} static entries")
endforeach()

list(LENGTH _stack_usage_files _file_count)
message(STATUS
    "FX stack-usage gate passed: ${_total_entries} entries in ${_file_count} file(s); maximum ${_maximum_bytes} bytes in '${_maximum_function}' at ${_maximum_file}; limit ${_normalized_limit} bytes")
