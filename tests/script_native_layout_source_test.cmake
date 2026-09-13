cmake_minimum_required(VERSION 3.16)

# Script VM M4 (ki-n1et) native-width layout invariants. Pins every
# migrated RUNTIME_SIZE(T, n32, n64) declaration in the src/script
# headers to its exact constants, and pins the one intentionally-
# unmigrated scrDebuggerGlob_t debt entry (it embeds the UI component
# family, a separate widening project). The companion portable test
# (script_native64_layout_test) proves those constants are the natural
# compiler layout on both widths; this test proves the headers still
# declare them, so mirror and header cannot drift apart silently.

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

function(require_header_contains RELATIVE_PATH NEEDLE DESCRIPTION)
    set(_path "${SOURCE_ROOT}/${RELATIVE_PATH}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing script header (${DESCRIPTION}): ${_path}")
    endif()
    file(READ "${_path}" _source)
    string(REGEX REPLACE "[ \t\r\n]+" " " _source "${_source}")
    string(REGEX REPLACE "[ \t\r\n]+" " " _needle "${NEEDLE}")
    string(FIND "${_source}" "${_needle}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing script VM layout invariant (${DESCRIPTION}): "
            "${RELATIVE_PATH} must declare '${NEEDLE}'")
    endif()
endfunction()

set(_variable_invariants
    "RUNTIME_SIZE(VariableStackBuffer, 0xC, 0x10);"
    "RUNTIME_SIZE(VariableUnion, 0x4, 0x8);"
    "RUNTIME_SIZE(VariableValue, 0x8, 0x10);"
    "RUNTIME_SIZE(ObjectInfo_u, 0x2, 0x2);"
    "RUNTIME_SIZE(ObjectInfo, 0x4, 0x4);"
    "RUNTIME_SIZE(Variable_u, 0x2, 0x2);"
    "RUNTIME_SIZE(Variable, 0x4, 0x4);"
    "RUNTIME_SIZE(VariableValueInternal_u, 0x4, 0x8);"
    "RUNTIME_SIZE(VariableValueInternal_w, 0x4, 0x4);"
    "RUNTIME_SIZE(VariableValueInternal_v, 0x2, 0x2);"
    "RUNTIME_SIZE(VariableValueInternal, 0x10, 0x18);"
    "RUNTIME_SIZE(scrVarDebugPub_t, 0xE0004, 0x140008);"
    "RUNTIME_SIZE(scrVarGlob_t, 0x180000, 0x240000);"
    "RUNTIME_SIZE(scr_entref_t, 0x4, 0x4);"
    "RUNTIME_SIZE(scr_classStruct_t, 0xC, 0x10);"
    "RUNTIME_SIZE(VariableDebugInfo, 0x10, 0x20);"
    "RUNTIME_SIZE(ThreadDebugInfo, 0x8C, 0x110);")
foreach(_invariant IN LISTS _variable_invariants)
    require_header_contains(
        "src/script/scr_variable.h" "${_invariant}"
        "script variable-table native layout")
endforeach()

set(_parser_invariants
    "RUNTIME_SIZE(OpcodeLookup, 0x18, 0x20);"
    "RUNTIME_SIZE(Scr_SourcePos_t, 0xC, 0xC);"
    "RUNTIME_SIZE(SourceBufferInfo, 44, 56);"
    "RUNTIME_SIZE(SourceLookup, 8, 8);"
    "RUNTIME_SIZE(SaveSourceBufferInfo, 0x8, 0x10);"
    "RUNTIME_SIZE(scrParserGlob_t, 0x34, 0x50);"
    "RUNTIME_SIZE(scrParserPub_t, 0x10, 0x20);")
foreach(_invariant IN LISTS _parser_invariants)
    require_header_contains(
        "src/script/scr_parser.h" "${_invariant}"
        "script parser native layout")
endforeach()

set(_evaluate_invariants
    "RUNTIME_SIZE(ArchivedCanonicalStringInfo, 0x8, 0x10);"
    "RUNTIME_SIZE(scrEvaluateGlob_t, 0x10, 0x20);")
foreach(_invariant IN LISTS _evaluate_invariants)
    require_header_contains(
        "src/script/scr_evaluate.h" "${_invariant}"
        "script evaluate native layout")
endforeach()

set(_debugger_invariants
    "RUNTIME_SIZE(debugger_sval_s, 0x4, 0x8);"
    "RUNTIME_SIZE(scr_localVar_t, 0x8, 0x8);"
    "RUNTIME_SIZE(scr_block_s, 0x218, 0x218);"
    "RUNTIME_SIZE(sval_u, 0x4, 0x8);"
    "RUNTIME_SIZE(ScriptExpression_t, 0xC, 0x18);"
    "RUNTIME_SIZE(Scr_SelectedLineInfo, 0xC, 0xC);"
    "RUNTIME_SIZE(Scr_Breakpoint, 0x1C, 0x30);"
    "RUNTIME_SIZE(Scr_WatchElement_s, 0x64, 0xA0);"
    "RUNTIME_SIZE(Scr_OpcodeList_s, 0x8, 0x10);"
    "RUNTIME_SIZE(Scr_WatchElementNode_s, 0x8, 0x10);"
    "RUNTIME_SIZE(Scr_WatchElementDoubleNode_t, 0x8, 0x10);")
foreach(_invariant IN LISTS _debugger_invariants)
    require_header_contains(
        "src/script/scr_debugger.h" "${_invariant}"
        "script debugger native layout")
endforeach()
# scrDebuggerGlob_t embeds the UI component family (UI_ScrollPane,
# Scr_ScriptList, ...), which is not part of the script VM widening; its
# raw ILP32 assert stays in place and stays tracked in the ABI sizeof
# debt ledger.
require_header_contains(
    "src/script/scr_debugger.h"
    "static_assert(sizeof(scrDebuggerGlob_t) == 0x2B8);"
    "script debugger UI-bound debt")

set(_memorytree_invariants
    "RUNTIME_SIZE(MemoryNode, 12, 12);"
    "RUNTIME_SIZE(scrMemTreeGlob_t, 0xC0380, 0xC0380);")
foreach(_invariant IN LISTS _memorytree_invariants)
    require_header_contains(
        "src/script/scr_memorytree.h" "${_invariant}"
        "script memory tree native layout")
endforeach()

set(_compiler_invariants
    "RUNTIME_SIZE(CaseStatementInfo, 0x10, 0x20);"
    "RUNTIME_SIZE(BreakStatementInfo, 0xC, 0x18);"
    "RUNTIME_SIZE(ContinueStatementInfo, 0xC, 0x18);"
    "RUNTIME_SIZE(VariableCompileValue, 0xC, 0x18);"
    "RUNTIME_SIZE(scrCompileGlob_t, 0x1D8, 0x390);")
foreach(_invariant IN LISTS _compiler_invariants)
    require_header_contains(
        "src/script/scr_compiler.h" "${_invariant}"
        "script compiler native layout")
endforeach()

set(_animtree_invariants
    "RUNTIME_SIZE(scrAnimPub_t, 0x41C, 0x820);"
    "RUNTIME_SIZE(scrAnimGlob_t, 0x20C, 0x218);")
foreach(_invariant IN LISTS _animtree_invariants)
    require_header_contains(
        "src/script/scr_animtree.h" "${_invariant}"
        "script animtree native layout")
endforeach()

require_header_contains(
    "src/script/scr_const.h"
    "RUNTIME_SIZE(scr_const_t, 0x174, 0x174);"
    "script const-string handle table native layout")

set(_vm_invariants
    "RUNTIME_SIZE(Scr_StringNode_s, 0x8, 0x10);"
    "RUNTIME_SIZE(function_stack_t, 0x14, 0x20);"
    "RUNTIME_SIZE(function_frame_t, 0x18, 0x28);"
    "RUNTIME_SIZE(scrVmPub_t, 0x4328, 0x8540);"
    "RUNTIME_SIZE(FuncDebugData, 0x10, 0x18);"
    "RUNTIME_SIZE(scrVmDebugPub_t, 0x24210, 0x26410);"
    "RUNTIME_SIZE(scrVmGlob_t, 0x2028, 0x2048);")
foreach(_invariant IN LISTS _vm_invariants)
    require_header_contains(
        "src/script/scr_vm.h" "${_invariant}"
        "script VM native layout")
endforeach()

message(STATUS "script VM native-width layout invariants verified")
