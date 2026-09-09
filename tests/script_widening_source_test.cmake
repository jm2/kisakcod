cmake_minimum_required(VERSION 3.16)

# script-widening-source-invariants (M4 ki-n1et): guards the widened
# runtime migration against regression. The rework replaced every
# literal-size allocation / frozen-stride scan / narrow-union copy in
# src/script with element-sized (sizeof) forms; this test pins the fixed
# forms and forbids the literal forms reappearing. It complements the
# behavioral tests (script-readstack-nested-contracts,
# script-widening-value-contracts) with mechanical source checks that run
# on every build target.

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

function(read_normalized RELATIVE_PATH OUT_VARIABLE DESCRIPTION)
    set(_path "${SOURCE_ROOT}/${RELATIVE_PATH}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR
            "Missing script source (${DESCRIPTION}): ${_path}")
    endif()
    file(READ "${_path}" _source)
    string(REGEX REPLACE "[ \t\r\n]+" " " _source "${_source}")
    set(${OUT_VARIABLE} "${_source}" PARENT_SCOPE)
endfunction()

function(require_contains RELATIVE_PATH NEEDLE DESCRIPTION)
    read_normalized("${RELATIVE_PATH}" _source "${DESCRIPTION}")
    string(FIND "${_source}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing widening invariant (${DESCRIPTION}): "
            "${RELATIVE_PATH} must contain '${NEEDLE}'")
    endif()
endfunction()

function(forbid_contains RELATIVE_PATH NEEDLE DESCRIPTION)
    read_normalized("${RELATIVE_PATH}" _source "${DESCRIPTION}")
    string(FIND "${_source}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden widening regression (${DESCRIPTION}): "
            "${RELATIVE_PATH} must not contain '${NEEDLE}'")
    endif()
endfunction()

# --- scr_readwrite.cpp: nested stack reader + object save/load unions ---
set(_rw "src/script/scr_readwrite.cpp")
require_contains("${_rw}"
    "frames[depth].pendingType = value.type;"
    "nested reader preserves the suspended record type")
require_contains("${_rw}"
    "value.type = frames[depth].pendingType;"
    "nested reader restores the suspended record type")
require_contains("${_rw}"
    "entryValue->u.u = value.u;"
    "Scr_ReadObject child payloads move the full union")
require_contains("${_rw}"
    "v18[0].u = v14->u.u;"
    "DoSaveObjectInfo child payloads move the full union")
require_contains("${_rw}"
    ".hash.id + VARIABLELIST_CHILD_BEGIN];"
    "variable-table children indexed by typed entry, not ROL4 stride")
forbid_contains("${_rw}"
    "__ROL4__(scrVarGlob.variableList["
    "frozen 16-byte entry stride must stay retired")
forbid_contains("${_rw}"
    "p_archive += 44;"
    "SourceBufferInfo scans must not advance by the 32-bit stride")
forbid_contains("${_rw}"
    "8 * scrParserGlob.saveSourceBufferLookupLen"
    "SaveSourceBufferInfo allocation must be element-sized")
require_contains("${_rw}"
    "sizeof(SaveSourceBufferInfo) * scrParserGlob.saveSourceBufferLookupLen"
    "SaveSourceBufferInfo allocation is element-sized")

# --- scr_evaluate.cpp: canonical string archive ---
set(_ev "src/script/scr_evaluate.cpp")
forbid_contains("${_ev}"
    "Hunk_AllocDebugMem(8 * scrVarPub.canonicalStrCount)"
    "canonical archive allocation must be element-sized")
require_contains("${_ev}"
    "sizeof(ArchivedCanonicalStringInfo) * scrVarPub.canonicalStrCount"
    "canonical archive allocation is element-sized")
require_contains("${_ev}"
    "sizeof(ArchivedCanonicalStringInfo),"
    "canonical archive sort stride is element-sized")
forbid_contains("${_ev}"
    "strcmp(arg1[1], arg2[1])"
    "canonical comparator must read typed members, not pointer-width slots")

# --- scr_parser.cpp: source buffer grow-copy ---
forbid_contains("src/script/scr_parser.cpp"
    "44 * scrParserPub.sourceBufferLookupLen"
    "source buffer grow-copy must use the native record size")
require_contains("src/script/scr_parser.cpp"
    "sizeof(SourceBufferInfo) * scrParserPub.sourceBufferLookupLen"
    "source buffer grow-copy uses the native record size")

# --- scr_debugger.cpp: watch/breakpoint/opcode allocations + sorting ---
set(_dbg "src/script/scr_debugger.cpp")
forbid_contains("${_dbg}"
    "Scr_AllocDebugMem(100"
    "watch element allocation must be record-sized")
forbid_contains("${_dbg}"
    "Scr_AllocDebugMem(8,"
    "debugger node allocations must be record-sized")
forbid_contains("${_dbg}"
    "Hunk_AllocDebugMem(393216)"
    "variable breakpoint table must be pointer-sized")
forbid_contains("${_dbg}"
    "Hunk_AllocDebugMem(8)"
    "opcode node allocation must be record-sized")
forbid_contains("${_dbg}"
    "100 * count"
    "remote watch array must be element-sized")
forbid_contains("${_dbg}"
    "elementList[newIndex] = (uint32_t)"
    "element sort slots must hold full pointers")
require_contains("${_dbg}"
    "sizeof(Scr_WatchElement_s) * count"
    "remote watch array is element-sized")
require_contains("${_dbg}"
    "98304 * sizeof(Scr_WatchElementDoubleNode_t *)"
    "variable breakpoint table is pointer-sized")
require_contains("${_dbg}"
    "sizeof(Scr_OpcodeList_s)"
    "opcode node allocation is record-sized")
require_contains("${_dbg}"
    "sizeof(Scr_WatchElement_s *) * count"
    "element sort array is pointer-sized")

# --- scr_compiler.cpp / scr_compiler2.cpp: compiler records ---
foreach(_compiler src/script/scr_compiler.cpp src/script/scr_compiler2.cpp)
    forbid_contains("${_compiler}"
        "Hunk_AllocateTempMemoryHigh(4096, "
        "block scratch arrays must be pointer-sized")
    require_contains("${_compiler}"
        "MAX_SWITCH_CASES, "
        "block scratch arrays sized by MAX_SWITCH_CASES")
    require_contains("${_compiler}"
        "sizeof(CaseStatementInfo), "
        "case records are record-sized")
    require_contains("${_compiler}"
        "sizeof(BreakStatementInfo), "
        "break records are record-sized")
    require_contains("${_compiler}"
        "sizeof(ContinueStatementInfo), "
        "continue records are record-sized")
endforeach()

# --- scr_parsetree.cpp: parse-node cells ---
set(_pt "src/script/scr_parsetree.cpp")
require_contains("${_pt}"
    "Scr_AllocDebugExpr(type, sizeof(sval_u), \"debugger_node0\")"
    "node0 allocates a full widened cell")
require_contains("${_pt}"
    "5 * sizeof(sval_u), \"debugger_node4\""
    "node4 allocates full widened cells")
forbid_contains("${_pt}"
    "result[1].intValue = (int)bufCopy"
    "buffer node pointer stores must use the pointer member")
require_contains("${_pt}"
    "result[1].debugString = reinterpret_cast<const char *>(bufCopy);"
    "buffer node pointer stored through the pointer member")

# --- scr_variable.cpp: dump records ---
set(_var "src/script/scr_variable.cpp")
forbid_contains("${_var}"
    "Z_TryVirtualAlloc(1572864"
    "variable dump array must be element-sized")
forbid_contains("${_var}"
    "Z_TryVirtualAlloc(140 * num"
    "thread dump array must be element-sized")
forbid_contains("${_var}"
    "qsort(infoArray, num, 0x10u,"
    "variable dump sort stride must be element-sized")
forbid_contains("${_var}"
    "qsort(infoArray, num, 0x8Cu,"
    "thread dump sort stride must be element-sized")
require_contains("${_var}"
    "sizeof(VariableDebugInfo) * 0x18000"
    "variable dump array is element-sized")
require_contains("${_var}"
    "sizeof(ThreadDebugInfo) * num"
    "thread dump array is element-sized")

# --- scr_vm.cpp: widened consumer retrievals ---
set(_vm "src/script/scr_vm.cpp")
forbid_contains("${_vm}"
    "(VariableStackBuffer*)GetVariableValueAddress"
    "stack retrievals must use the pointer member")
forbid_contains("${_vm}"
    "u.intValue - (uint32_t)scrVarPub.programBuffer"
    "code position offsets must come from the pointer member")
require_contains("${_vm}"
    "GetVariableValueAddress(stackId)->u.stackValue"
    "stack retrieval uses the pointer member")
require_contains("${_vm}"
    "scrAnimPub.xanim_lookup[1][treeIndex].anims"
    "anim tree lookup indexed by typed table")
message(STATUS "script widening source invariants: OK")
