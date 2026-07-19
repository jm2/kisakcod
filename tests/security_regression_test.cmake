cmake_minimum_required(VERSION 3.16)

function(require_source_contains RELATIVE_PATH NEEDLE DESCRIPTION)
    file(READ "${SOURCE_ROOT}/src/${RELATIVE_PATH}" _source)
    string(FIND "${_source}" "${NEEDLE}" _position)
    if (_position EQUAL -1)
        message(FATAL_ERROR "Missing security invariant (${DESCRIPTION}) in src/${RELATIVE_PATH}")
    endif()
endfunction()

function(require_source_not_contains RELATIVE_PATH NEEDLE DESCRIPTION)
    file(READ "${SOURCE_ROOT}/src/${RELATIVE_PATH}" _source)
    string(FIND "${_source}" "${NEEDLE}" _position)
    if (NOT _position EQUAL -1)
        message(FATAL_ERROR "Forbidden security regression (${DESCRIPTION}) in src/${RELATIVE_PATH}")
    endif()
endfunction()

function(require_repository_contains RELATIVE_PATH NEEDLE DESCRIPTION)
    file(READ "${SOURCE_ROOT}/${RELATIVE_PATH}" _source)
    string(FIND "${_source}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing security invariant (${DESCRIPTION}) in ${RELATIVE_PATH}")
    endif()
endfunction()

function(require_source_not_matches RELATIVE_PATH PATTERN DESCRIPTION)
    file(READ "${SOURCE_ROOT}/src/${RELATIVE_PATH}" _source)
    string(REGEX MATCH "${PATTERN}" _match "${_source}")
    if (NOT "${_match}" STREQUAL "")
        message(FATAL_ERROR "Forbidden security regression (${DESCRIPTION}) in src/${RELATIVE_PATH}")
    endif()
endfunction()

function(require_source_matches RELATIVE_PATH PATTERN DESCRIPTION)
    file(READ "${SOURCE_ROOT}/src/${RELATIVE_PATH}" _source)
    string(REGEX MATCH "${PATTERN}" _match "${_source}")
    if ("${_match}" STREQUAL "")
        message(FATAL_ERROR "Missing security invariant (${DESCRIPTION}) in src/${RELATIVE_PATH}")
    endif()
endfunction()

function(require_source_ordered RELATIVE_PATH FIRST SECOND DESCRIPTION)
    file(READ "${SOURCE_ROOT}/src/${RELATIVE_PATH}" _source)
    string(FIND "${_source}" "${FIRST}" _first_position)
    string(FIND "${_source}" "${SECOND}" _second_position)
    if (_first_position EQUAL -1
        OR _second_position EQUAL -1
        OR _first_position GREATER _second_position)
        message(FATAL_ERROR
            "Missing or unordered security invariant (${DESCRIPTION}) in src/${RELATIVE_PATH}")
    endif()
endfunction()

function(require_source_match_count RELATIVE_PATH PATTERN EXPECTED_COUNT DESCRIPTION)
    file(READ "${SOURCE_ROOT}/src/${RELATIVE_PATH}" _source)
    set(_remaining "${_source}")
    set(_match_count 0)
    while(TRUE)
        string(REGEX MATCH "${PATTERN}" _match "${_remaining}")
        if("${_match}" STREQUAL "")
            break()
        endif()
        math(EXPR _match_count "${_match_count} + 1")
        string(FIND "${_remaining}" "${_match}" _match_position)
        string(LENGTH "${_match}" _match_length)
        math(EXPR _next_position "${_match_position} + ${_match_length}")
        string(SUBSTRING "${_remaining}" ${_next_position} -1 _remaining)
    endwhile()
    if (NOT _match_count EQUAL EXPECTED_COUNT)
        message(FATAL_ERROR
            "Unexpected security invariant count (${DESCRIPTION}) in src/${RELATIVE_PATH}: "
            "expected ${EXPECTED_COUNT}, found ${_match_count}")
    endif()
endfunction()

function(extract_security_slice
    SOURCE_VAR START_MARKER END_MARKER OUT_VAR DESCRIPTION)
    set(_source "${${SOURCE_VAR}}")
    string(FIND "${_source}" "${START_MARKER}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR
            "Missing start of security invariant (${DESCRIPTION})")
    endif()

    string(SUBSTRING "${_source}" ${_start} -1 _tail)
    string(FIND "${_tail}" "${END_MARKER}" _relative_end)
    if(_relative_end LESS_EQUAL 0)
        message(FATAL_ERROR
            "Missing ordered end of security invariant (${DESCRIPTION})")
    endif()

    string(SUBSTRING "${_tail}" 0 ${_relative_end} _slice)
    set(${OUT_VAR} "${_slice}" PARENT_SCOPE)
endfunction()

function(require_security_slice_contains SLICE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SLICE_VAR}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR "Missing security invariant (${DESCRIPTION})")
    endif()
endfunction()

function(forbid_security_slice_contains SLICE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SLICE_VAR}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR "Forbidden security regression (${DESCRIPTION})")
    endif()
endfunction()

function(require_security_slice_ordered SLICE_VAR FIRST SECOND DESCRIPTION)
    string(FIND "${${SLICE_VAR}}" "${FIRST}" _first_position)
    string(FIND "${${SLICE_VAR}}" "${SECOND}" _second_position)
    if(_first_position EQUAL -1
        OR _second_position EQUAL -1
        OR _first_position GREATER_EQUAL _second_position)
        message(FATAL_ERROR
            "Missing or unordered security invariant (${DESCRIPTION})")
    endif()
endfunction()

function(require_all_occurrences_wrapped RELATIVE_PATH OCCURRENCE_PATTERN WRAPPED_PATTERN DESCRIPTION)
    file(READ "${SOURCE_ROOT}/src/${RELATIVE_PATH}" _source)
    string(REGEX MATCHALL "${OCCURRENCE_PATTERN}" _all_occurrences "${_source}")
    string(REGEX MATCHALL "${WRAPPED_PATTERN}" _wrapped_occurrences "${_source}")
    list(LENGTH _all_occurrences _all_count)
    list(LENGTH _wrapped_occurrences _wrapped_count)
    if (_all_count EQUAL 0 OR NOT _all_count EQUAL _wrapped_count)
        message(FATAL_ERROR
            "Unwrapped security-sensitive access (${DESCRIPTION}) in src/${RELATIVE_PATH}: "
            "${_wrapped_count}/${_all_count} occurrences use the required boundary")
    endif()
endfunction()

file(GLOB_RECURSE _callback_sources "${SOURCE_ROOT}/src/*.cpp")
foreach(_callback_source IN LISTS _callback_sources)
    file(READ "${_callback_source}" _callback_text)
    foreach(_forbidden_cast
        "(void(__cdecl *)(XAssetHeader, void *))"
        "(void(__cdecl*)(XAssetHeader, void*))"
        "(void(*)(XAssetHeader, void*))")
        string(FIND "${_callback_text}" "${_forbidden_cast}" _callback_cast)
        if (NOT _callback_cast EQUAL -1)
            message(FATAL_ERROR
                "XAsset enumeration callback cast remains in ${_callback_source}")
        endif()
    endforeach()
endforeach()

require_source_contains(
    "bgame/bg_local.h"
    "constexpr int32_t MAX_WEAPONS = 128"
    "weapon-table width must be owned by shared game code")
require_source_contains(
    "bgame/bg_local.h"
    "return itemIndex / MAX_WEAPONS;"
    "weapon item indices must decode their model without applying the table stride")
require_source_contains(
    "bgame/bg_misc.cpp"
    "BG_GetItemWeaponModel(ent->index.item) * MAX_WEAPONS + weapIdx"
    "weapon item lookup must apply the model stride exactly once")
require_source_not_contains(
    "bgame/bg_misc.cpp"
    "ITEM_WEAPMODEL"
    "legacy weapon-model macro could multiply the item-table stride twice")
require_source_contains(
    "win32/win_syscon.cpp"
    "const size_t inputCapacity = capacity - currentLength - 1;"
    "console input must reserve space for its command separator and terminator")
require_source_not_contains(
    "win32/win_syscon.cpp"
    "strncat("
    "console input must not use underflow-prone remaining-size concatenation")
require_source_contains(
    "win32/win_syscon.cpp"
    "static char s_headlessConsoleHistory[0x4000];"
    "headless startup diagnostics must remain available when the console is created late")
require_source_contains(
    "win32/win_syscon.cpp"
    "Conbuf_AppendHeadlessHistory(msg);"
    "headless diagnostics must enter bounded history before console-window creation")
require_source_contains(
    "win32/win_syscon.cpp"
    "SysConsoleOutputStream::StandardOutput,
			msg,
			std::strlen(msg))"
    "headless diagnostics must reach inherited process output")
require_source_contains(
    "win32/win_syscon.cpp"
    "#ifdef KISAK_DEDI_HEADLESS
	// Headless servers keep inherited standard handles"
    "headless console display must remain non-interactive")
require_source_contains(
    "win32/win_main.cpp"
    "I_strncpyz(b, s, static_cast<int32_t>(v2 + 1));"
    "console events must preserve their final character and trailing terminator")
require_source_contains(
    "win32/win_main.cpp"
    "ExitProcess(EXIT_FAILURE);"
    "headless fatal errors must terminate unattended servers with a failure code")
require_source_contains(
    "win32/win_main.cpp"
    "while (GetMessageA(&Msg, 0, 0, 0))"
    "interactive Windows fatal errors must retain their crash message loop")
require_source_matches(
    "win32/win_main.cpp"
    "while[ 	]*\\(GetMessageA\\(&Msg,[ 	]*0,[ 	]*0,[ 	]*0\\)\\)[^{]*\\{[^}]*\\}[ 	\r\n]*exit\\(EXIT_FAILURE\\);"
    "interactive Windows fatal errors must exit with failure after the message loop")
require_source_contains(
    "win32/win_main.cpp"
    "Sys_SetErrorText(string);
	exit(EXIT_FAILURE);"
    "non-main interactive Windows fatal errors must exit with failure")
require_source_contains(
    "win32/win_main.cpp"
    "#ifndef KISAK_DEDI_HEADLESS
	if (!Sys_ConsoleIsRedirected(SysConsoleOutputStream::StandardOutput)
		&& !Sys_ConsoleIsRedirected(SysConsoleOutputStream::StandardError))"
    "headless startup must preserve inherited standard handles instead of reopening CONOUT")
require_source_contains(
    "win32/win_main.cpp"
    "if (Sys_ConsoleIsRedirected(SysConsoleOutputStream::StandardError))
		Win_TerminateOnFatalError(string);"
    "redirected Windows fatal errors must not enter the modal GUI path")
require_source_not_contains(
    "win32/win_main.cpp"
    "Win_IsRedirectedHandle"
    "Windows redirected-stream checks must not bypass the portable service")
require_source_not_contains(
    "win32/win_syscon.cpp"
    "Conbuf_WriteProcessHandle"
    "Windows console writes must not bypass the portable service")
require_source_contains(
    "win32/win_main.cpp"
    "if (com_dedicated && com_dedicated->current.integer)
		Win_TerminateOnFatalError(string);"
    "legacy dedicated fatal errors must also be non-modal and fail the process")
require_source_contains(
    "win32/win_main.cpp"
    "Sys_Error(\"Out of memory: filename '%s', line %d\", filename, line);"
    "headless allocation failure must not open a modal dialog")
require_source_contains(
    "win32/win_main.cpp"
    "Sys_Error(\"Less than 128 MB of virtual address space remains\");"
    "headless low-memory startup must fail without a modal prompt")
foreach(_sys_error_contract IN ITEMS
    "qcommon/qcommon.h"
    "win32/win_local.h"
    "win32/win_main.cpp")
    require_source_matches(
        "${_sys_error_contract}"
        "\\[\\[noreturn\\]\\][ \t]+void[ \t]+Sys_Error[ \t]*\\("
        "the cross-platform fatal-error contract must be declared and defined noreturn")
endforeach()
require_source_contains(
    "win32/win_main.cpp"
    "[[noreturn]] static void Win_TerminateOnFatalError("
    "Windows non-modal fatal termination must preserve the noreturn contract")
require_source_contains(
    "win32/win_main.cpp"
    "message ? message : \"Unknown fatal error\""
    "Windows last-resort fatal output must tolerate a missing message")
require_source_contains(
    "qcommon/threads.h"
    "#include <qcommon/sys_event.h>"
    "public thread services must use the opaque event contract")
require_source_not_contains(
    "qcommon/threads.h"
    "#include <Windows.h>"
    "public thread declarations must remain Windows-independent")
require_source_contains(
    "qcommon/threads.h"
    "#include <qcommon/sys_thread.h>"
    "shared thread orchestration must use the opaque native-thread contract")
foreach(_legacy_thread_registry "qcommon/threads.cpp" "qcommon/threads.h")
    require_source_not_matches(
        "${_legacy_thread_registry}"
        "threadId[ \\t\\r\\n]*\\["
        "shared thread orchestration must not retain a numeric native-thread registry")
endforeach()
require_source_not_matches(
    "qcommon/threads.h"
    "threadHandle[ \\t\\r\\n]*\\["
    "opaque thread handles must remain private to their implementation")
foreach(_native_thread_registry_token
    "HANDLE"
    "DWORD"
    "pthread_t"
    "void *threadHandle"
    "void* threadHandle"
)
    require_source_not_contains(
        "qcommon/threads.h"
        "${_native_thread_registry_token}"
        "public thread orchestration must not expose a native or untyped handle registry")
endforeach()
foreach(_native_thread_token
    "Windows.h"
    "windows.h"
    "pthread"
    "HANDLE"
    "DWORD"
    "cpu_set_t"
    "sched_param"
    "GROUP_AFFINITY"
    "KAFFINITY"
    "std::thread"
    "CreateThread"
    "_beginthread"
    "SetThreadAffinityMask"
    "SuspendThread"
    "ResumeThread"
)
    require_source_not_contains(
        "qcommon/sys_thread.h"
        "${_native_thread_token}"
        "opaque thread contract must not expose native thread types")
endforeach()
foreach(_native_worker_suspend_token
    "Windows.h"
    "windows.h"
    "SuspendThread"
    "ResumeThread"
    "pthread"
    "HANDLE"
    "DWORD"
)
    require_source_not_contains(
        "qcommon/sys_worker_gate.h"
        "${_native_worker_suspend_token}"
        "cooperative worker gate must not expose native suspension APIs")
endforeach()
require_source_contains(
    "gfx_d3d/r_workercmds.h"
    "void KISAK_CDECL R_WorkerThread(uint32_t threadContext);"
    "renderer worker entry must accept its fixed-width thread context without an ABI cast")
require_source_contains(
    "gfx_d3d/r_workercmds.cpp"
    "void KISAK_CDECL R_WorkerThread(uint32_t threadContext)"
    "renderer worker implementation must retain its exact fixed-width entry signature")
require_source_contains(
    "gfx_d3d/r_workercmds.cpp"
    "Sys_SpawnWorkerThread(R_WorkerThread, workerThreadIndexa)"
    "renderer worker creation must pass its exactly typed entry point")
require_source_not_contains(
    "gfx_d3d/r_workercmds.cpp"
    "(void(__cdecl *)(uint32_t))R_WorkerThread"
    "renderer worker creation must not cast an incompatible no-argument entry point")
require_source_not_contains(
    "qcommon/threads.h"
    "Sys_SuspendThread("
    "shared thread orchestration must not re-expose arbitrary worker suspension")
foreach(_renderer_worker_source
    "gfx_d3d/r_init.cpp"
    "gfx_d3d/r_workercmds.cpp"
)
    foreach(_legacy_worker_control Sys_SuspendThread Sys_ResumeThread)
        require_source_not_contains(
            "${_renderer_worker_source}"
            "${_legacy_worker_control}("
            "renderer workers must use the cooperative active-state controller")
    endforeach()
endforeach()
foreach(_raw_thread_api
    CreateThread
    GetCurrentThreadId
    DuplicateHandle
    SetThreadAffinityMask
    SetThreadPriority
    SuspendThread
    ResumeThread
)
    require_source_not_matches(
        "qcommon/threads.cpp"
        "(^|[^A-Za-z0-9_])${_raw_thread_api}\\("
        "shared thread orchestration must not bypass the opaque native-thread backend")
endforeach()
foreach(_native_thread_source_token
    "#include <Windows"
    "#include <win32/"
    "PVOID"
    "Interlocked"
    "RaiseException"
    "MS_VC_EXCEPTION"
    "LPTHREAD_START_ROUTINE"
)
    require_source_not_contains(
        "qcommon/threads.cpp"
        "${_native_thread_source_token}"
        "shared thread orchestration must not regain native Windows coupling")
endforeach()
foreach(_required_thread_service_call
    "Sys_ThreadCaptureCurrent("
    "Sys_ThreadCreateSuspended("
    "Sys_ThreadStart("
    "Sys_ThreadIsCurrent("
    "Sys_ThreadGetEligibleProcessorCount("
    "Sys_ThreadSetPriority("
    "Sys_ThreadPinToEligibleProcessor("
    "Sys_ThreadClearAffinity("
    "Sys_ThreadForceSuspendForCrash("
)
    require_source_contains(
        "qcommon/threads.cpp"
        "${_required_thread_service_call}"
        "engine thread policy must remain behind the opaque native-thread service")
endforeach()
require_source_not_contains(
    "qcommon/threads.h"
    "Sys_StartThread("
    "one-shot native thread start must remain private to thread orchestration")
foreach(_retired_thread_topology_token
    "Win_InitThreads"
    "s_affinityMaskForProcess"
    "s_affinityMaskForCpu"
    "s_cpuCount"
)
    require_source_not_contains(
        "universal/win_common.cpp"
        "${_retired_thread_topology_token}"
        "Win32 filesystem code must not regain ownership of thread topology")
endforeach()
require_source_contains(
    "win32/win_main.cpp"
    "Sys_FreezeOtherThreadsForCrash();"
    "the Windows fatal path must use the explicitly crash-only thread freeze")
require_source_not_contains(
    "win32/win_main.cpp"
    "Sys_SuspendOtherThreads();"
    "the Windows fatal path must not use the legacy generic suspension name")
foreach(_crash_freeze_owner
    "win32/win_main.cpp"
    "qcommon/threads.cpp"
)
    file(READ "${SOURCE_ROOT}/src/${_crash_freeze_owner}" _crash_freeze_owner_text)
    string(REGEX MATCHALL
        "Sys_FreezeOtherThreadsForCrash\\("
        _crash_freeze_owner_matches
        "${_crash_freeze_owner_text}")
    list(LENGTH _crash_freeze_owner_matches _crash_freeze_owner_count)
    if (NOT _crash_freeze_owner_count EQUAL 1)
        message(FATAL_ERROR
            "Expected exactly one crash-freeze occurrence in ${_crash_freeze_owner}, found ${_crash_freeze_owner_count}")
    endif()
endforeach()
file(GLOB_RECURSE _crash_freeze_sources "${SOURCE_ROOT}/src/*.cpp")
foreach(_crash_freeze_source IN LISTS _crash_freeze_sources)
    file(RELATIVE_PATH _crash_freeze_relative
        "${SOURCE_ROOT}/src"
        "${_crash_freeze_source}")
    if (NOT _crash_freeze_relative STREQUAL "win32/win_main.cpp"
        AND NOT _crash_freeze_relative STREQUAL "qcommon/threads.cpp")
        file(READ "${_crash_freeze_source}" _crash_freeze_text)
        string(FIND
            "${_crash_freeze_text}"
            "Sys_FreezeOtherThreadsForCrash("
            _crash_freeze_position)
        if (NOT _crash_freeze_position EQUAL -1)
            message(FATAL_ERROR
                "Crash-only thread freeze escaped into ${_crash_freeze_relative}")
        endif()
    endif()
endforeach()
require_source_contains(
    "client/cl_main.cpp"
    "void __cdecl CL_startMultiplayer_f()
{
    Com_PrintError(
        14,
        \"startMultiplayer is unavailable because executable handoff is not implemented\\n\");
    return;
}"
    "the incomplete executable-handoff command must fail safely before changing runtime state")
foreach(_forbidden_start_multiplayer_action
    "Sys_FreezeOtherThreadsForCrash("
    "Sys_SuspendOtherThreads("
)
    require_source_not_contains(
        "client/cl_main.cpp"
        "${_forbidden_start_multiplayer_action}"
        "the incomplete executable-handoff command must not freeze threads or tear down rendering")
endforeach()
foreach(_typed_thread_entry_source
    "database/db_registry.cpp"
    "gfx_d3d/r_rendercmds.cpp"
    "server/sv_demo.cpp"
)
    foreach(_forbidden_thread_entry_cast
        "(void(__cdecl *)(uint32_t))"
        "(void(__cdecl *)(unsigned int))"
    )
        require_source_not_contains(
            "${_typed_thread_entry_source}"
            "${_forbidden_thread_entry_cast}"
            "typed engine thread callbacks must not be hidden behind ABI casts")
    endforeach()
endforeach()
foreach(_raw_event_api CreateEventA SetEvent ResetEvent WaitForSingleObject)
    require_source_not_matches(
        "qcommon/threads.cpp"
        "(^|[^A-Za-z0-9_])${_raw_event_api}\\("
        "thread orchestration must not bypass the native event backend")
endforeach()
require_source_contains(
    "physics/ode/error.cpp"
    "#if !defined(_WIN32) || defined(KISAK_DEDI_HEADLESS)"
    "headless ODE failures must use the stderr/nonzero-exit backend")
require_source_ordered(
    "physics/ode/ode.cpp"
    "new (std::nothrow) dxBody;"
    "if (!b)\n        return nullptr;"
    "ODE body allocation failure must be checked before initialization")
file(READ "${SOURCE_ROOT}/src/physics/ode/ode.cpp" _ode_body_source)
extract_security_slice(
    _ode_body_source
    "dxBody *dBodyCreate(dxWorld *w)"
    "poolmutationstatus_t ODE_TryBodyCreateNoReport("
    _diagnostic_body_create
    "diagnostic ODE body creation")
require_security_slice_ordered(
    _diagnostic_body_create
    "if (!b)\n        return nullptr;"
    "return ODE_InitializeAllocatedBody(w, b);"
    "failed diagnostic ODE bodies must not enter their world list")

extract_security_slice(
    _ode_body_source
    "poolmutationstatus_t ODE_TryBodyCreateNoReport("
    "dxJointGroup *__cdecl dGetContactJointGroup("
    _silent_body_create
    "silent ODE body creation")
require_security_slice_ordered(
    _silent_body_create
    "*outBody = nullptr;"
    "ODE_NoReportBodyAllocationCandidateHasNoAliases("
    "silent body output clears before free-head alias validation")
require_security_slice_ordered(
    _silent_body_create
    "ODE_NoReportBodyAllocationCandidateHasNoAliases("
    "Pool_TryAllocNoReport("
    "prospective body ownership validates before pool mutation")
require_security_slice_ordered(
    _silent_body_create
    "if (allocation.status != poolmutationstatus_t::Success)"
    "*outBody = ODE_InitializeAllocatedBody(world, body);"
    "failed silent ODE bodies must not enter their world list")
require_source_not_contains(
    "physics/ode/ode.cpp"
    "free(b);"
    "heap-backed ODE bodies must use matching C++ deallocation")
require_source_match_count(
    "physics/ode/ode.cpp"
    "delete[ \t]+b[ \t]*;"
    2
    "both ODE body destruction paths must match the fallback new-expression")
# Production physics no longer composes callback resource pairs. The body,
# user-data, primary geom, and optional transform now use status-bearing silent
# fixed-pool operations, with explicit rollback and one unified destroy core.
foreach(_obsolete_pair_use
    "physics::allocation::TryCreateResourcePair("
    "ResourcePairCallbacks"
    "PhysBodyDestroyPlan")
    require_source_not_contains(
        "physics/phys_ode.cpp"
        "${_obsolete_pair_use}"
        "production physics must not retain divergent callback/manual transactions")
endforeach()
foreach(_silent_transaction_api
    "ODE_TryBodyCreateNoReport("
    "Pool_TryAllocNoReport("
    "ODE_TryCreateGeomTransformNoReport("
    "ODE_TryGeomTransformSetGeomNoReport("
    "ODE_TryGeomDestructNoReport("
    "ODE_TryBodyDestroyNoReport("
    "Pool_TryFreeNoReport(")
    require_source_contains(
        "physics/phys_ode.cpp"
        "${_silent_transaction_api}"
        "physics mutations must use the silent fixed-pool transaction layer")
endforeach()
require_source_ordered(
    "physics/phys_ode.cpp"
    "geom->spaceRemove();"
    "geom->spaceAdd(&space->first);"
    "successful COM adjustment must preserve the simple-space dirty prefix")
require_source_ordered(
    "physics/phys_ode.cpp"
    "ODE_TryBodyCreateNoReport("
    "ODE_TryBodyDestroyNoReport(body)"
    "user-data exhaustion must silently return its newly created body")
require_source_not_contains(
    "physics/phys_ode.cpp"
    "iassert(userData);\n        memset((uint8_t *)userData"
    "physics user-data exhaustion must not reach memset through an assertion")
require_source_contains(
    "physics/phys_ode.cpp"
    "!Phys_RollbackBodyStateIsValid(*state)"
    "body construction must reject nonpositive or nonfinite mass before allocation")
require_source_contains(
    "physics/phys_ode.cpp"
    "geomState.u.brushState.u.brush = brush;"
    "brush geometry state must retain its native-width typed pointer")
require_source_not_contains(
    "physics/phys_ode.cpp"
    "(int)brush"
    "brush geometry state must not truncate pointers through its x86 union overlay")
require_source_contains(
    "physics/ode/collision_kernel.h"
    "alignas(physics::ode::kUserGeomClassDataAlignment)\n        unsigned char user_data[physics::ode::kUserGeomClassDataBytes]"
    "ODE user-geom storage must scale and align with native brush-pointer width")
require_source_contains(
    "physics/ode/collision_kernel.h"
    "sizeof(dxUserGeom) <= sizeof(dxGeomTransform)"
    "native user geoms must continue to fit the shared ODE geom pool")
require_source_contains(
    "physics/ode/collision_kernel.h"
    "alignof(dxUserGeom) <= alignof(dxGeomTransform)"
    "native user geoms must satisfy the shared ODE geom-pool alignment")
require_source_contains(
    "physics/ode/odeext.h"
    "alignas(dxGeomTransform)\n        char geoms["
    "ODE geom-pool backing storage must encode its placement-new alignment")
require_source_contains(
    "physics/phys_world_collision.cpp"
    "physics::ode::UserGeomClassDataMatches<BrushInfo>"
    "production BrushInfo size and alignment must match ODE class storage")
require_source_contains(
    "physics/phys_local.h"
    "RUNTIME_SIZE(BrushInfo, 0x10, 0x18);"
    "production BrushInfo must retain its exact x86/native64 sizes")
require_source_match_count(
    "physics/phys_world_collision.cpp"
    "gclass\\.bytes = static_cast<int>\\(sizeof\\(BrushInfo\\)\\);"
    2
    "both brush user classes must register their native runtime payload size")
require_source_not_contains(
    "physics/phys_world_collision.cpp"
    "gclass.bytes = 16;"
    "brush user-class payloads must not retain their x86-only byte width")
require_source_not_matches(
    "physics/phys_ode.cpp"
    "geomState\\.u\\.cylinderState\\.(radius|halfHeight)[ \t]*=[ \t]*physMass"
    "brush inertia must use named native-layout members instead of cylinder overlays")
require_source_match_count(
    "physics/phys_ode.cpp"
    "geomState->u\\.boxState\\.extent\\[[012]\\]"
    3
    "box geometry creation must use typed box extents")
foreach(_box_mass_component x y z)
    require_source_contains(
        "physics/phys_ode.cpp"
        "const float ${_box_mass_component} = geomState.u.boxState.extent["
        "silent box mass must snapshot each typed extent before arithmetic")
endforeach()
require_source_not_contains(
    "physics/phys_ode.cpp"
    "geomState->u.boxState.extent[0],\n            geomState->u.cylinderState.radius"
    "box construction must not alias dimensions through cylinder state")
require_source_contains(
    "physics/phys_ode.cpp"
    "physGlob.worldData[bodyWorldIndex].timeLastUpdate;"
    "physics wake timestamps must come from typed world data")
require_source_not_contains(
    "physics/phys_ode.cpp"
    "(int)physGlob.space["
    "physics wake timestamps must not truncate an unrelated space pointer")
require_source_contains(
    "physics/phys_local.h"
    "Phys_TryCreateBodyFromStateAndXModel("
    "fresh body-plus-model construction must expose a checked API")
require_source_contains(
    "physics/phys_local.h"
    "void __cdecl Phys_ObjSetCollisionFromXModel("
    "legacy collision callers must retain their source-compatible wrapper")
file(READ "${SOURCE_ROOT}/src/physics/phys_ode.cpp" _phys_transaction_source)
extract_security_slice(
    _phys_transaction_source
    "static PhysBodyModelCreateStatus\nPhys_TryCreateBodyFromStateAndXModelInternal("
    "PhysBodyModelCreateStatus __cdecl Phys_TryCreateBodyFromStateAndXModel("
    _complete_model_create
    "complete body/model construction")
require_security_slice_ordered(
    _complete_model_create
    "*outBody = nullptr;"
    "Phys_TryBuildCollisionFromXModel("
    "checked construction must clear output ownership before building collision")
require_security_slice_ordered(
    _complete_model_create
    "Phys_TryBuildCollisionFromXModel("
    "Phys_TryDestroyBodyAndUserDataLockedNoReport(worldIndex, body)"
    "checked collision failure must silently destroy the complete fresh body")
require_security_slice_ordered(
    _complete_model_create
    "return collisionStatus;"
    "*outBody = body;"
    "checked construction must publish ownership only after every failure exit")
require_source_contains(
    "EffectsCore/fx_archive.cpp"
    "Phys_TryCreateBodyFromStateAndXModelLockedNoReport("
    "FX archive restore must use complete non-reporting collision construction while PHYSICS is held")
require_source_not_contains(
    "EffectsCore/fx_archive.cpp"
    "Phys_TryCreateBodyFromStateAndXModel("
    "FX archive restore must not call the diagnostic body constructor while PHYSICS is held")
require_source_not_contains(
    "EffectsCore/fx_archive.cpp"
    "Phys_CreateBodyFromState(PHYS_WORLD_FX"
    "FX archive restore must use the checked body-plus-model transaction")
require_source_contains(
    "cgame_mp/dedicated_cgame.cpp"
    "{ \"code_post_gfx_mp\", 2, 0 }"
    "headless startup must load authoritative MP code assets")
require_source_contains(
    "cgame_mp/dedicated_cgame.cpp"
    "{ \"localized_code_post_gfx_mp\", 0, 0 }"
    "headless startup must load localized MP code assets")
require_source_contains(
    "cgame_mp/dedicated_cgame.cpp"
    "{ \"common_mp\", 4, 0 }"
    "headless startup must load authoritative MP common assets")
require_source_contains(
    "cgame_mp/dedicated_cgame.cpp"
    "{ \"localized_common_mp\", 1, 0 }"
    "headless startup must load localized MP common assets")
require_source_not_contains(
    "cgame_mp/dedicated_cgame.cpp"
    "ui_mp"
    "headless startup must not load client UI assets")
require_source_contains(
    "universal/q_shared.h"
    "#ifdef KISAK_DEDI_HEADLESS
\treturn true;"
    "headless builds must be compile-time fast-file-only")
require_source_contains(
    "qcommon/common.cpp"
    "#ifdef KISAK_DEDI_HEADLESS
        DVAR_ROM,
#else
        DVAR_INIT,"
    "headless useFastFile must not be disabled from the command line")
require_source_contains(
    "bgame/bg_public.h"
    "static const pmoveHandler_t pmoveHandlers[2] = { { G_TraceCapsule, NULL}, {G_TraceCapsule, G_PlayerEvent} };"
    "headless movement tables must not retain a client collision callback")
require_source_contains(
    "universal/com_files.cpp"
    "#ifndef KISAK_DEDI_HEADLESS
    SND_StopSounds(SND_STOP_STREAMED);
#endif"
    "headless filesystem shutdown must not link the sound backend")

foreach(_iwd_atomic_source
    "universal/com_files.cpp"
    "universal/com_files.h"
)
    require_source_not_contains(
        "${_iwd_atomic_source}"
        "Interlocked"
        "IWD ownership must use the portable fixed-width atomic boundary")
    require_source_not_matches(
        "${_iwd_atomic_source}"
        "(^|[^A-Za-z0-9_])LONG([^A-Za-z0-9_]|$)"
        "IWD ownership must not depend on a native Windows word")
endforeach()
foreach(_iwd_layout_contract
    "RUNTIME_SIZE(directory_t, 0x200, 0x200);"
    "volatile uint32_t hasOpenFile;"
    "volatile uint32_t referenced;"
    "RUNTIME_SIZE(fileInIwd_s, 0xC, 0x18);"
    "RUNTIME_SIZE(iwd_t, 0x324, 0x330);"
    "RUNTIME_OFFSET(iwd_t, hasOpenFile, 0x30C, 0x310);"
    "RUNTIME_OFFSET(iwd_t, referenced, 0x314, 0x318);"
    "RUNTIME_SIZE(searchpath_s, 0x1C, 0x28);"
    "RUNTIME_SIZE(qfile_gus, 0x4, 0x8);"
    "RUNTIME_SIZE(qfile_us, 0x8, 0x10);"
    "RUNTIME_SIZE(fileHandleData_t, 0x11C, 0x130);"
)
    require_source_contains(
        "universal/com_files.h"
        "${_iwd_layout_contract}"
        "IWD and file-handle layouts must remain frozen on both pointer widths")
endforeach()
require_all_occurrences_wrapped(
    "universal/com_files.cpp"
    "->[ \t\r\n]*hasOpenFile"
    "Sys_Atomic(Load|Store|Exchange|CompareExchange)[ \t\r\n]*\\([ \t\r\n]*&[^,;()]*->[ \t\r\n]*hasOpenFile"
    "every canonical IWD-handle ownership access must be atomic")
require_all_occurrences_wrapped(
    "universal/com_files.cpp"
    "->[ \t\r\n]*referenced"
    "Sys_Atomic(Load|Store|Exchange|CompareExchange)[ \t\r\n]*\\([ \t\r\n]*&[^,;()]*->[ \t\r\n]*referenced"
    "every IWD reference-publication access must be atomic")
require_source_not_contains(
    "universal/com_files.cpp"
    "unzReOpen("
    "contended IWD readers must not clone mutable live unzip state")
foreach(_retired_unzip_reopen_source
    "qcommon/unzip.cpp"
    "qcommon/unzip.h"
)
    require_source_not_contains(
        "${_retired_unzip_reopen_source}"
        "unzReOpen"
        "the mutable unzip-state clone API must remain retired")
endforeach()
require_source_not_contains(
    "universal/com_files.cpp"
    "Com_Memcpy((char *)zfi, (char *)iwd->handle"
    "selected IWD readers must not copy the canonical live unzip cursor")
require_source_contains(
    "universal/com_files.cpp"
    "fsh[*file].handleFiles.file.z = unzOpen(iwd->iwdFilename);"
    "contended IWD readers must open an independent archive handle")
require_source_contains(
    "universal/com_files.cpp"
    "Sys_AtomicCompareExchange(&iwd->hasOpenFile, 1u, 0u)"
    "the canonical unzip handle must be claimed atomically")
require_source_contains(
    "universal/com_files.cpp"
    "Sys_AtomicExchange(&fsh[h].zipFile->hasOpenFile, 0u)"
    "the canonical unzip handle must be released and validated atomically")
require_source_contains(
    "universal/com_files.cpp"
    "unzSetCurrentFileInfoPosition(
            fsh[*file].handleFiles.file.z, iwdFile->pos)"
    "IWD positioning must target the selected canonical or clone handle")
require_source_contains(
    "universal/com_files.cpp"
    "selectedArchiveInfo.number_entry
                != static_cast<unsigned long>(iwd->numfiles)"
    "reopened IWDs must preserve their central-directory entry count")
require_source_contains(
    "universal/com_files.cpp"
    "selectedFileInfo.size_filename >= sizeof(selectedFileName)
                || memchr("
    "selected IWD entries must reject unterminated and embedded-NUL names")
require_source_contains(
    "universal/com_files.cpp"
    "FS_FilenameCompare(selectedFileName, iwdFile->name)"
    "reopened IWDs must verify that a cached position still names the selected entry")
require_source_contains(
    "universal/com_files.cpp"
    "Com_Memset(&fsh[h], 0, sizeof(fsh[h]));"
    "file-handle cleanup must clear the native runtime record size")
require_source_contains(
    "universal/com_files.cpp"
    "h < 0 || h >= static_cast<int32_t>(ARRAY_COUNT(fsh))"
    "file close must reject handles before indexing the handle table")
require_source_match_count(
    "universal/com_files.cpp"
    "file_info\\.size_filename >= sizeof\\(filename_inzip\\)[ \t\r\n]*\\|\\| memchr\\("
    2
    "both IWD construction passes must reject embedded NUL names")
require_source_contains(
    "universal/com_files.cpp"
    "if (unzGoToFirstFile(uf) != UNZ_OK)"
    "IWD construction must reject an invalid first central-directory entry")
require_source_contains(
    "universal/com_files.cpp"
    "i + 1u < entryCount && unzGoToNextFile(uf) != UNZ_OK"
    "IWD construction must reject partial central-directory traversal")
require_source_contains(
    "universal/com_files.cpp"
    "nameBytes > static_cast<size_t>(namesEnd - namePtr)"
    "IWD names must remain inside their checked aggregate allocation")
require_source_contains(
    "universal/com_files.cpp"
    "if (namePtr != namesEnd)"
    "both IWD traversal passes must agree on the exact name allocation extent")
require_source_contains(
    "universal/com_files.cpp"
    "qsort(s0, static_cast<size_t>(numfiles), sizeof(s0[0]), iwdsort);"
    "IWD pointer sorting must use the native pointer element width")
require_source_contains(
    "universal/com_files.cpp"
    "Z_Malloc(sizeof(*search), \"FS_AddIwdFilesForGameDirectory\", 3)"
    "IWD search paths must allocate their native runtime record size")
require_source_contains(
    "universal/com_files.cpp"
    "Z_Malloc(sizeof(*search), \"FS_AddGameDirectory\", 3)"
    "directory search paths must allocate their native runtime record size")
require_source_not_contains(
    "universal/com_files.cpp"
    "Z_Malloc(28,"
    "search-path allocation must not retain the x86-only byte count")
require_source_not_contains(
    "universal/com_files.cpp"
    "(searchpath_s **)*pSearch"
    "search-path insertion must not type-pun its first member")
require_source_contains(
    "universal/com_files.cpp"
    "memcpy(v2, \"          \", 10);"
    "localized IWD sorting must not write character storage through aliased words")
require_source_contains(
    "universal/com_files.cpp"
    "fs_iwdFileCount = 0;"
    "filesystem shutdown must reset its per-search-path IWD count")

require_source_contains(
    "qcommon/unzip.cpp"
    "uint8_t bytes[2];"
    "ZIP 16-bit scalars must decode from exact little-endian bytes")
require_source_contains(
    "qcommon/unzip.cpp"
    "uint8_t bytes[4];"
    "ZIP 32-bit scalars must decode from exact little-endian bytes")
foreach(_host_word_zip_decoder
    "LittleShort"
    "LittleLong"
)
    require_source_not_contains(
        "qcommon/unzip.cpp"
        "${_host_word_zip_decoder}"
        "ZIP parsing must not depend on host-word endian macros")
endforeach()
require_source_contains(
    "qcommon/unzip.cpp"
    "else if (uL != 0x06054b50u)"
    "ZIP opening must validate the end-of-central-directory signature")
require_source_contains(
    "qcommon/unzip.cpp"
    "us.size_central_dir <= central_pos - us.offset_central_dir"
    "ZIP central-directory bounds must avoid addition overflow")
require_source_contains(
    "qcommon/unzip.cpp"
    "if (file==NULL || pos==NULL)"
    "ZIP position queries must validate both arguments")
require_source_contains(
    "qcommon/unzip.cpp"
    "s->current_file_ok = (err == UNZ_OK);
\treturn err;"
    "ZIP repositioning must return its actual parser result")
require_source_contains(
    "qcommon/unzip.cpp"
    "if (pfile_in_zip_read_info->read_buffer == NULL)"
    "ZIP entry opening must reject buffer allocation failure")
require_source_contains(
    "qcommon/unzip.cpp"
    "unzlocal_FreeCurrentFile(pfile_in_zip_read_info);
\t    return err;"
    "ZIP inflate initialization failures must tear down partial state")

foreach(_loopback_atomic_source
    "qcommon/net_chan_mp.cpp"
    "qcommon/net_chan_mp.h"
)
    require_source_not_contains(
        "${_loopback_atomic_source}"
        "Interlocked"
        "loopback queues must use the portable fixed-width atomic boundary")
    require_source_not_matches(
        "${_loopback_atomic_source}"
        "(^|[^A-Za-z0-9_])LONG([^A-Za-z0-9_]|$)"
        "loopback queues must not depend on a native Windows word")
endforeach()
foreach(_loopback_layout_contract
    "RUNTIME_SIZE(loopmsg_t, 0x580, 0x580);"
    "RUNTIME_SIZE(loopback_t, 0x5808, 0x5808);"
)
    require_source_contains(
        "qcommon/net_chan_mp.h"
        "${_loopback_layout_contract}"
        "loopback queue records must remain fixed-width on every target")
endforeach()
foreach(_loopback_cursor "send" "get")
    require_all_occurrences_wrapped(
        "qcommon/net_chan_mp.cpp"
        "loop->[ \t\r\n]*${_loopback_cursor}"
        "Sys_Atomic(Load|Store|Exchange|CompareExchange)[ \t\r\n]*\\([ \t\r\n]*&loop->[ \t\r\n]*${_loopback_cursor}"
        "every loopback queue cursor access must use canonical atomics")
endforeach()
require_source_contains(
    "qcommon/net_chan_mp.cpp"
    "Sys_AtomicCompareExchange(
               &s_loopbackLocks[queueIndex], 1u, 0u)"
    "loopback slot payloads must be protected against wrapped reuse")
require_source_contains(
    "qcommon/net_chan_mp.cpp"
    "Sys_Sleep(0);"
    "contended loopback publishers must yield")
require_source_contains(
    "qcommon/net_chan_mp.cpp"
    "message.datalen = static_cast<int32_t>(length);
    message.port = returnPort;

    // Publish only after the complete slot payload is visible.
    Sys_AtomicStore(&loop->send, send + 1u);"
    "loopback producers must publish only complete slot metadata and payload")
require_source_contains(
    "qcommon/net_chan_mp.cpp"
    "// Finish copying before the slot can be reused by a wrapped producer.
    Sys_AtomicStore(&loop->get, get + 1u);"
    "loopback consumers must release a slot only after copying it")
require_source_contains(
    "qcommon/net_chan_mp.cpp"
    "static_cast<uint32_t>(dataLength) > sizeof(message.data)
        || dataLength > net_message->maxsize"
    "loopback consumers must validate stored length and destination capacity")
require_source_contains(
    "qcommon/net_chan_mp.cpp"
    "length > sizeof(loopbacks[0].msgs[0].data)"
    "loopback producers must reject oversized packets")
require_source_contains(
    "qcommon/net_chan_mp.cpp"
    "memcpy(&packetMarker, data, sizeof(packetMarker));"
    "packet logging must read its marker without alignment or short-buffer UB")
require_source_not_contains(
    "qcommon/net_chan_mp.cpp"
    "*(uint32_t *)data"
    "packet logging must not dereference an unaligned or undersized marker")
require_source_not_contains(
    "qcommon/net_chan_mp.cpp"
    "*(uint32_t *)net_from"
    "loopback source addresses must not be cleared through type-punned stores")
require_source_contains(
    "qcommon/net_chan_mp.cpp"
    "const int32_t destinationCapacity = net_message->maxsize;"
    "fake-lag delivery must preserve the caller's destination capacity")
require_source_contains(
    "qcommon/net_chan_mp.cpp"
    "queued.length > static_cast<uint32_t>(destinationCapacity)"
    "fake-lag delivery must reject packets larger than the caller's buffer")
require_source_contains(
    "qcommon/net_chan_mp.cpp"
    "static_cast<uint32_t>(queuedSize) != queued.length"
    "fake-lag delivery must reject inconsistent queued metadata")
require_source_not_contains(
    "qcommon/net_chan_mp.cpp"
    "net_message->maxsize = laggedPackets"
    "fake-lag delivery must not replace the caller's capacity with queued metadata")
require_source_contains(
    "qcommon/net_chan_mp.cpp"
    "if (length <= 0 || !data)"
    "fake-lag outbound input must use a release-active runtime check")
require_source_contains(
    "qcommon/net_chan_mp.cpp"
    "msg->cursize <= 0
        || msg->maxsize < msg->cursize"
    "fake-lag incoming input must use release-active message bounds")
require_source_contains(
    "qcommon/net_chan_mp.cpp"
    "if (!queuedData)
            return static_cast<uint32_t>(-2);"
    "fake-lag outbound allocation failure must leave no live queue slot")
require_source_contains(
    "qcommon/net_chan_mp.cpp"
    "if (!queuedData)
        return static_cast<uint32_t>(-1);"
    "fake-lag incoming allocation failure must leave no live queue slot")
require_source_contains(
    "qcommon/net_chan_mp.cpp"
    "memset(&laggedPackets[packet], 0, sizeof(laggedPackets[packet]));"
    "fake-lag slot destruction must clear all stale metadata")
require_source_contains(
    "qcommon/cmd.cpp"
    "dumpraw is unavailable because headless builds do not realize media resources"
    "headless media extraction must not dereference canonical null audio resources")
require_source_contains(
    "qcommon/cmd.cpp"
    "#ifndef KISAK_DEDI_HEADLESS
    Cmd_AddCommandInternal(\"dumpraw\", Cmd_Dumpraw_f, &Cmd_Dumpraw_f_VAR);
#endif"
    "headless builds must not advertise the unsupported media extraction command")
require_source_contains(
    "qcommon/cm_load_obj.cpp"
    "#if defined(KISAK_MP) && !defined(KISAK_DEDI_HEADLESS)"
    "headless load-object collision code must not link client dynamic entities")
require_source_contains(
    "universal/com_sndalias.cpp"
    "Load-object sound aliases are unavailable in a headless fast-file build"
    "headless builds must explicitly reject the load-object sound path")
require_source_contains(
    "universal/com_sndalias_load_obj.cpp"
    "void __cdecl Com_AddLoadedSoundFile(SoundFile *soundFile, char *fileName)
{
#ifdef KISAK_DEDI_HEADLESS"
    "headless load-object sound construction must compile out SND_LoadSoundFile")
require_source_contains(
    "universal/com_sndalias_load_obj.cpp"
    "int __cdecl Com_LoadSoundAliasSounds(SoundFileInfo *soundFileInfo)
{
#ifdef KISAK_DEDI_HEADLESS"
    "headless load-object sound verification must compile out sound dvars")
require_source_contains(
    "xanim/xmodel_load_obj.cpp"
    "XModel *__cdecl XModelLoadFile(char *name, void *(__cdecl *Alloc)(int), void *(__cdecl *AllocColl)(int))
{
#ifdef KISAK_DEDI_HEADLESS"
    "headless load-object model construction must compile out renderer dependencies")
require_source_contains(
    "qcommon/sv_msg_write_mp.cpp"
    "checkValue = ~static_cast<uint32_t>(value);"
    "signed bit-width checks must handle INT_MIN without negation overflow")
require_source_not_contains(
    "qcommon/sv_msg_write_mp.cpp"
    "_BitScanReverse"
    "shared message serialization must not depend on a Win32 compiler intrinsic")
require_source_contains(
    "qcommon/files.cpp"
    "ARRAY_COUNT(fs_serverReferencedFFCheckSums)"
    "fast-file reference capacity must be derived from the destination array")
require_source_contains(
    "qcommon/files.cpp"
    "return server_file_compare::IsServerOnlyIwdName(pak);"
    "server-only IWD detection must use the shared non-copying policy")
require_source_contains(
    "qcommon/files.cpp"
    "strlen(iwd) >= sizeof(szFile)"
    "IWD names must be bounded before copying")

# Referenced IWD/fast-file names are remote SYSTEMINFO input. Preflight the
# complete paired list before allocating names or publishing checksums so a
# malicious later token cannot create traversal paths or partial state.
file(READ "${SOURCE_ROOT}/src/qcommon/files.cpp" _qcommon_files_security_source)
file(READ
    "${SOURCE_ROOT}/src/qcommon/server_file_compare.h"
    _server_file_compare_security_source)
file(READ
    "${SOURCE_ROOT}/src/server_mp/ucmds.cpp"
    _server_download_command_security_source)
file(READ
    "${SOURCE_ROOT}/src/server_mp/sv_snapshot_mp.cpp"
    _server_download_snapshot_security_source)
file(READ
    "${SOURCE_ROOT}/src/universal/com_files.cpp"
    _server_file_open_security_source)
extract_security_slice(
    _server_file_compare_security_source
    "inline DownloadKind ClassifyServerDownloadRequest("
    "inline bool TokenListContainsExact("
    _server_download_classification
    "server download request classification")
extract_security_slice(
    _server_file_compare_security_source
    "inline bool IsPermittedServerDownloadRequest("
    "// Preflights the complete append"
    _server_download_admission
    "server download request admission")
extract_security_slice(
    _server_file_compare_security_source
    "inline const char *GameDirectorySuffix("
    "inline std::size_t BoundedLength("
    _game_directory_suffix
    "native game-directory membership")
extract_security_slice(
    _server_file_compare_security_source
    "inline bool AppendParts("
    "inline bool AppendFileName("
    _server_file_append_parts
    "atomic server-file list append")
extract_security_slice(
    _server_file_compare_security_source
    "inline Result CompareAll("
    "\n}\n}"
    _server_file_compare_all
    "transactional combined server-file comparison")
extract_security_slice(
    _qcommon_files_security_source
    "bool ServerReferenceCountsAreValid()"
    "server_file_compare::IwdReferences ServerIwdReferences()"
    _server_reference_count_validation
    "server referenced-file count validation")
extract_security_slice(
    _qcommon_files_security_source
    "int __cdecl FS_CompareIwds("
    "int __cdecl FS_CompareFFs("
    _server_iwd_compare_wrapper
    "production IWD comparison")
extract_security_slice(
    _qcommon_files_security_source
    "int __cdecl FS_CompareFFs("
    "FS_SERVER_COMPARE_RESULT __cdecl FS_CompareWithServerFiles("
    _server_fastfile_compare_wrapper
    "production fast-file comparison")
extract_security_slice(
    _qcommon_files_security_source
    "FS_SERVER_COMPARE_RESULT __cdecl FS_CompareWithServerFiles("
    "void __cdecl FS_ShutdownServerFileReferences"
    _server_file_compare_wrapper
    "production combined server-file comparison")
extract_security_slice(
    _server_download_command_security_source
    "bool __cdecl SV_IsDownloadRequestAuthorized("
    "void __cdecl SV_BeginDownload_f("
    _server_download_authorization
    "published server download authorization")
extract_security_slice(
    _server_download_command_security_source
    "void __cdecl SV_BeginDownload_f("
    "void __cdecl SV_VerifyIwds_f("
    _server_download_begin
    "server download command admission")
extract_security_slice(
    _server_download_snapshot_security_source
    "void __cdecl SV_Download_Clear("
    "void __cdecl SV_WriteDownloadErrorMessage("
    _server_download_clear
    "server download resource cleanup")
extract_security_slice(
    _server_download_snapshot_security_source
    "int __cdecl SV_WWWRedirectClient("
    "void __cdecl SV_WriteDownloadToClient("
    _server_download_redirect
    "server download redirect")
extract_security_slice(
    _server_download_snapshot_security_source
    "void __cdecl SV_WriteDownloadToClient("
    "void __cdecl SV_EndClientSnapshot("
    _server_download_writer
    "server download writer")
extract_security_slice(
    _server_file_open_security_source
    "bool FS_TryBuildServerOSPath("
    "int __cdecl FS_SV_FOpenFileRead("
    _server_path_builder
    "nonfatal server path builder")
extract_security_slice(
    _server_file_open_security_source
    "int __cdecl FS_SV_FOpenFileRead("
    "int __cdecl FS_SV_FOpenFileWrite("
    _server_file_reader
    "server file reader")
extract_security_slice(
    _server_file_open_security_source
    "char *__cdecl FS_ReferencedIwdNames()"
    "char info5[8192];"
    _referenced_iwd_name_producer
    "referenced IWD name publication")
extract_security_slice(
    _server_file_open_security_source
    "char *__cdecl FS_ReferencedIwdChecksums()"
    "char info3[8192];"
    _referenced_iwd_checksum_producer
    "referenced IWD checksum publication")

foreach(_marker IN ITEMS
    "if (!request)"
    "BoundedLength(request, kServerDownloadNameCapacity)"
    "requestLength == kServerDownloadNameCapacity"
    "constexpr char iwdExtension[] = \".iwd\";"
    "constexpr char fastFileExtension[] = \".ff\";"
    "return DownloadKind::Invalid;")
    require_security_slice_contains(
        _server_download_classification
        "${_marker}"
        "download requests are classified only when fully representable")
endforeach()
foreach(_marker IN ITEMS
    "inline constexpr std::size_t kServerDownloadNameCapacity = 64;"
    "!CanStoreServerDownloadName(name, \".iwd\")"
    "!CanStoreServerDownloadName(name, \".ff\")")
    require_source_contains(
        "qcommon/server_file_compare.h"
        "${_marker}"
        "the client cannot advertise a filename the server would truncate")
endforeach()
foreach(_marker IN ITEMS
    "ClassifyServerDownloadRequest(request) != kind"
    "kind == DownloadKind::Invalid"
    "kind == DownloadKind::Iwd"
    "IsServerOnlyIwdName(request)"
    "info_string::IsSafeUnquotedPathTokenComponent(request)"
    "info_string::IsSafeUnquotedPathTokenComponent(gameDirectory)"
    "GameDirectorySuffix(request, gameDirectory)"
    "TokenListContainsExact("
    "referencedNamesLength"
    "requestLength - extensionLength")
    require_security_slice_contains(
        _server_download_admission
        "${_marker}"
        "download admission requires a safe exact advertised mod child")
endforeach()
require_source_contains(
    "qcommon/files.cpp"
    "return server_file_compare::IsServerOnlyIwdName(pak);"
    "comparison and request admission share the server-only IWD policy")

foreach(_marker IN ITEMS
    "ClassifyServerDownloadRequest(request)"
    "!sv.configstrings[1]"
    "SL_ConvertToString(SV_GetConfigstringConst(1))"
    "info_string::TryGetExactValueView("
    "\"fs_game\""
    "gameDirectoryLength >= ARRAY_COUNT(gameDirectory)"
    "std::memcpy("
    "\"sv_referencedIwdNames\""
    "\"sv_referencedFFNames\""
    "referencedNamesLength"
    "IsPermittedServerDownloadRequest(")
    require_security_slice_contains(
        _server_download_authorization
        "${_marker}"
        "authorization uses the exact published SYSTEMINFO snapshot")
endforeach()
foreach(_forbidden IN ITEMS
    "sv_referencedIwdNames->current"
    "sv_referencedFFNames->current"
    "fs_serverReferenced"
    "Info_ValueForKey")
    forbid_security_slice_contains(
        _server_download_authorization
        "${_forbidden}"
        "authorization cannot trust unpublished or client-side metadata")
endforeach()

foreach(_marker IN ITEMS
    "SV_Cmd_Argc() != 2"
    "!SV_IsDownloadRequestAuthorized(request)"
    "SV_DropClient(cl, \"invalid download request\", 1);"
    "ARRAY_COUNT(cl->downloadName)"
    "kServerDownloadNameCapacity"
    "const std::size_t requestLength = std::strlen(request);"
    "SV_CloseDownload(cl);"
    "cl->downloadSize = 0;"
    "cl->downloadCount = 0;"
    "cl->downloadClientBlock = 0;"
    "cl->downloadCurrentBlock = 0;"
    "cl->downloadXmitBlock = 0;"
    "for (int &blockSize : cl->downloadBlockSize)"
    "cl->downloadEOF = 0;"
    "cl->downloadSendTime = 0;"
    "cl->downloadingWWW = 0;"
    "cl->clientDownloadingWWW = 0;"
    "std::memcpy(cl->downloadName, request, requestLength + 1);"
    "cl->downloading = 1;")
    require_security_slice_contains(
        _server_download_begin
        "${_marker}"
        "a remote path is authorized and copied without truncation")
endforeach()
require_security_slice_ordered(
    _server_download_begin
    "!SV_IsDownloadRequestAuthorized(request)"
    "SV_CloseDownload(cl);"
    "authorization precedes mutation of prior download state")
require_security_slice_ordered(
    _server_download_begin
    "cl->downloadClientBlock = 0;"
    "std::memcpy(cl->downloadName, request, requestLength + 1);"
    "download transfer counters reset before a WWW fallback can reuse the open file")
require_security_slice_ordered(
    _server_download_begin
    "std::memcpy(cl->downloadName, request, requestLength + 1);"
    "cl->downloading = 1;"
    "the canonical request publishes before the relaxed downloading state")
forbid_security_slice_contains(
    _server_download_begin
    "I_strncpyz"
    "download requests cannot be truncated into a different path")
forbid_security_slice_contains(
    _server_download_begin
    "cl->wwwFallback = 0"
    "a failed WWW redirect must retain its one-shot in-band fallback flag")

require_security_slice_contains(
    _server_download_clear
    "SV_CloseDownload(cl);"
    "download clearing closes the file and frees queued blocks")
require_security_slice_contains(
    _server_download_clear
    "return;"
    "a null client cannot reach download cleanup dereferences")
require_security_slice_ordered(
    _server_download_clear
    "SV_CloseDownload(cl);"
    "cl->downloading = 0;"
    "resource cleanup precedes state clearing")

foreach(_marker IN ITEMS
    "const int downloadSize = cl->downloadSize;"
    "cl->download && downloadSize > 0"
    "baseLength > ARRAY_COUNT(cl->downloadURL) - 2"
    "nameLength > ARRAY_COUNT(cl->downloadURL) - baseLength - 2"
    "Com_sprintf("
    "SV_Download_Clear(cl);")
    require_security_slice_contains(
        _server_download_redirect
        "${_marker}"
        "redirects reuse the authorized open file and preflight their URL")
endforeach()
require_security_slice_ordered(
    _server_download_redirect
    "nameLength > ARRAY_COUNT(cl->downloadURL) - baseLength - 2"
    "Com_sprintf("
    "the redirect URL is bounded before formatting")
forbid_security_slice_contains(
    _server_download_redirect
    "FS_SV_FOpenFileRead"
    "redirects cannot reopen and leak the in-band file handle")

foreach(_marker IN ITEMS
    "!SV_IsDownloadRequestAuthorized(cl->downloadName)"
    "SV_WriteDownloadErrorMessage(cl, msg, errorMessage);"
    "FS_SV_FOpenFileRead(cl->downloadName, &cl->download)")
    require_security_slice_contains(
        _server_download_writer
        "${_marker}"
        "authorization is revalidated before the first server file open")
endforeach()
require_security_slice_ordered(
    _server_download_writer
    "!SV_IsDownloadRequestAuthorized(cl->downloadName)"
    "FS_SV_FOpenFileRead(cl->downloadName, &cl->download)"
    "stale authorization fails before filesystem access")

foreach(_marker IN ITEMS
    "!base || !*base"
    "!filename || !*filename"
    "FS_BuildOSPathForThread("
    "FS_THREAD_SERVER"
    "const std::size_t length = std::strlen(osPath);"
    "length == 0"
    "osPath[length - 1] = '\\0';")
    require_security_slice_contains(
        _server_path_builder
        "${_marker}"
        "oversized server paths return failure instead of terminating the process")
endforeach()
foreach(_marker IN ITEMS
    "if (!fp)"
    "*fp = 0;"
    "if (!filename || !*filename)"
    "fs_homepath->current.string, filename, ospath"
    "fs_basepath->current.string, filename, ospath"
    "fs_cdpath->current.string, filename, ospath")
    require_security_slice_contains(
        _server_file_reader
        "${_marker}"
        "server file opens validate outputs and use the nonfatal path builder")
endforeach()
forbid_security_slice_contains(
    _server_file_reader
    "FS_BuildOSPath("
    "remote server file opens cannot hit the fatal path-length builder")

foreach(_marker IN ITEMS
    "char staged[sizeof(info8)];"
    "FS_TryAppendReferencedIwdName("
    "info8[0] = 0;"
    "Invalid or oversized referenced IWD name list"
    "std::memcpy(info8, staged, stagedLength + 1);")
    require_security_slice_contains(
        _referenced_iwd_name_producer
        "${_marker}"
        "referenced IWD names publish only a complete validated list")
endforeach()
foreach(_marker IN ITEMS
    "char staged[sizeof(info5)];"
    "FS_TryAppendReferencedIwdChecksum("
    "info5[0] = 0;"
    "Invalid or oversized referenced IWD checksum list"
    "std::memcpy(info5, staged, stagedLength + 1);")
    require_security_slice_contains(
        _referenced_iwd_checksum_producer
        "${_marker}"
        "referenced IWD checksums publish only a complete validated list")
endforeach()
foreach(_producer IN ITEMS
    _referenced_iwd_name_producer
    _referenced_iwd_checksum_producer)
    forbid_security_slice_contains(
        ${_producer}
        "I_strncat"
        "referenced IWD publication cannot silently truncate")
endforeach()
foreach(_marker IN ITEMS
    "std::memchr(component, '\\0', Capacity)"
    "info_string::IsSafeUnquotedPathTokenComponent(component)"
    "if (available == 0)"
    "--available;"
    "std::to_chars("
    "available - checksumLength < 1")
    require_source_contains(
        "universal/com_files.cpp"
        "${_marker}"
        "referenced IWD formatting uses checked portable boundaries")
endforeach()

require_source_contains(
    "client_mp/cl_parse_mp.cpp"
    "cl_updatefiles->current.string"
    "legacy update filenames use their native string pointer")
require_source_not_contains(
    "client_mp/cl_parse_mp.cpp"
    "(char *)cl_updatefiles->current.integer"
    "legacy update filenames cannot truncate pointers")
require_source_contains(
    "client_mp/cl_main_mp.cpp"
    "if (autoupdateFilename[0])"
    "legacy update completion tests filename content rather than array address")

foreach(_marker IN ITEMS
    "if (!name || !IsModGameDirectory(gameDirectory))"
    "const std::size_t gameDirectoryLength = std::strlen(gameDirectory);"
    "const std::size_t nameLength = std::strlen(name);"
    "nameLength <= gameDirectoryLength"
    "name[gameDirectoryLength] != '/'"
    "name[gameDirectoryLength + 1] == '\\0'"
    "FoldAscii(static_cast<unsigned char>(name[index]))"
    "FoldAscii(static_cast<unsigned char>(gameDirectory[index]))"
    "return name + gameDirectoryLength + 1;")
    require_security_slice_contains(
        _game_directory_suffix
        "${_marker}"
        "mod membership uses an exact native-width case-insensitive component boundary")
endforeach()
foreach(_forbidden IN ITEMS
    "static_cast<int>(gameDirectoryLength)"
    "uint32_t"
    "_DWORD")
    forbid_security_slice_contains(
        _game_directory_suffix
        "${_forbidden}"
        "game-directory membership cannot narrow native lengths or pointers")
endforeach()

foreach(_marker IN ITEMS
    "const std::size_t outputLength = BoundedLength(output, capacity);"
    "if (outputLength == capacity)"
    "const std::size_t remaining = capacity - finalLength - 1;"
    "if (partLength > remaining)"
    "std::memcpy(cursor, parts[index], partLength);"
    "*cursor = '\\0';")
    require_security_slice_contains(
        _server_file_append_parts
        "${_marker}"
        "download-list fields require complete bounded preflight")
endforeach()
require_security_slice_ordered(
    _server_file_append_parts
    "if (partLength > remaining)"
    "std::memcpy(cursor, parts[index], partLength);"
    "every complete download-list append is sized before its first write")
foreach(_forbidden IN ITEMS
    "I_strncat"
    "strcat("
    "strncat(")
    forbid_security_slice_contains(
        _server_file_append_parts
        "${_forbidden}"
        "atomic download-list construction cannot use truncating concatenation")
endforeach()

foreach(_marker IN ITEMS
    "const std::size_t stagingCapacity = capacity < kAggregateCapacity"
    "std::array<char, kAggregateCapacity> staging{};"
    "const Outcome iwdOutcome = CompareIwds("
    "const Outcome fastFileOutcome = CompareFastFiles("
    "finalOutcome.failure == Failure::OutputCapacity"
    "finalOutcome.failure == Failure::InvalidInput"
    "BoundedLength(staging.data(), stagingCapacity)"
    "std::memcpy(output, staging.data(), stagedLength + 1);")
    require_security_slice_contains(
        _server_file_compare_all
        "${_marker}"
        "the combined IWD/fast-file result publishes only a complete staged aggregate")
endforeach()
require_security_slice_ordered(
    _server_file_compare_all
    "finalOutcome.failure == Failure::OutputCapacity"
    "std::memcpy(output, staging.data(), stagedLength + 1);"
    "capacity or invalid-input failure returns before aggregate publication")

foreach(_marker IN ITEMS
    "fs_numServerReferencedIwds >= 0"
    "static_cast<std::size_t>(fs_numServerReferencedIwds)"
    "<= ARRAY_COUNT(fs_serverReferencedIwds)"
    "fs_numServerReferencedFFs >= 0"
    "static_cast<std::size_t>(fs_numServerReferencedFFs)"
    "<= ARRAY_COUNT(fs_serverReferencedFFCheckSums)")
    require_security_slice_contains(
        _server_reference_count_validation
        "${_marker}"
        "signed remote reference counts are bounded before native conversion")
endforeach()
require_security_slice_ordered(
    _server_reference_count_validation
    "fs_numServerReferencedIwds >= 0"
    "static_cast<std::size_t>(fs_numServerReferencedIwds)"
    "the IWD count is nonnegative before conversion")
require_security_slice_ordered(
    _server_reference_count_validation
    "fs_numServerReferencedFFs >= 0"
    "static_cast<std::size_t>(fs_numServerReferencedFFs)"
    "the fast-file count is nonnegative before conversion")

foreach(_marker IN ITEMS
    "if (!neededFiles || len <= 0)"
    "neededFiles[0] = '\\0';"
    "if (!ServerReferenceCountsAreValid())"
    "server_file_compare::CompareAll("
    "static_cast<std::size_t>(len)"
    "CurrentGameDirectory()"
    "ServerIwdReferences()"
    "ServerFastFileReferences()")
    require_security_slice_contains(
        _server_file_compare_wrapper
        "${_marker}"
        "production comparison routes bounded inputs through the tested transactional helper")
endforeach()
require_security_slice_ordered(
    _server_file_compare_wrapper
    "neededFiles[0] = '\\0';"
    "if (!ServerReferenceCountsAreValid())"
    "the production destination becomes a valid empty string before count failure")
require_security_slice_ordered(
    _server_file_compare_wrapper
    "if (!ServerReferenceCountsAreValid())"
    "server_file_compare::CompareAll("
    "signed reference counts are validated before helper publication")
foreach(_wrapper_and_destination IN ITEMS
    "_server_iwd_compare_wrapper|needediwds"
    "_server_fastfile_compare_wrapper|neededFFs")
    string(REPLACE "|" ";" _wrapper_fields "${_wrapper_and_destination}")
    list(GET _wrapper_fields 0 _wrapper)
    list(GET _wrapper_fields 1 _destination)
    require_security_slice_contains(
        ${_wrapper}
        "${_destination}[0] = '\\0';"
        "direct server-file comparison initializes its caller output")
    require_security_slice_ordered(
        ${_wrapper}
        "${_destination}[0] = '\\0';"
        "if (!ServerReferenceCountsAreValid())"
        "direct server-file output is valid before count failure")
    require_security_slice_ordered(
        ${_wrapper}
        "if (!ServerReferenceCountsAreValid())"
        "server_file_compare::CompareAll("
        "direct server-file comparison validates counts before publication")
endforeach()
foreach(_forbidden IN ITEMS
    "I_strncat"
    "fs_gameDirVar->current.integer"
    "(uint32_t)&"
    "_DWORD")
    forbid_security_slice_contains(
        _server_file_compare_wrapper
        "${_forbidden}"
        "production server-file comparison cannot truncate pointers or partial fields")
endforeach()
extract_security_slice(
    _qcommon_files_security_source
    "int __cdecl FS_ServerSetReferencedFiles("
    "void __cdecl FS_ServerSetReferencedIwds"
    _server_referenced_files
    "server referenced-file list ingestion")
require_source_contains(
    "qcommon/files.cpp"
    "#include <universal/info_string.h>"
    "referenced-file ingestion uses the shared path-token policy")
require_source_contains(
    "universal/info_string.h"
    "std::strchr(value, '@')"
    "path tokens cannot inject download-list framing fields")
require_source_contains(
    "universal/info_string.h"
    "*cursor == static_cast<unsigned char>(0x7f)"
    "path tokens cannot contain the ASCII delete control byte")
require_source_contains(
    "universal/info_string.h"
    "std::strpbrk(value, \":<>|?*\")"
    "download paths cannot contain Windows namespaces or wildcard metacharacters")
require_source_contains(
    "universal/info_string.h"
    "detail::IsWindowsDosDevicePathComponent(component, cursor)"
    "download paths cannot resolve through Windows DOS device aliases")
foreach(_marker IN ITEMS
    "ComponentStemEquals(begin, baseEnd, \"CONIN$\", 6)"
    "ComponentStemEquals(begin, baseEnd, \"CONOUT$\", 7)"
    "ComponentStemEquals(begin, baseEnd, \"CLOCK$\", 6)"
    "suffix == 0xB9u"
    "suffix == 0xC2u"
    "componentLength == 1 && component[0] == '.'"
    "component[componentLength - 1] == '.'")
    require_source_contains(
        "universal/info_string.h"
        "${_marker}"
        "download paths reject Win32 device and dot-normalization aliases")
endforeach()
foreach(_marker IN ITEMS
    "std::array<int, ARRAY_COUNT(fs_serverReferencedIwds)> parsedChecksums{};"
    "const int checksumCount = Cmd_Argc();"
    "info_string::TryParseSignedDecimalToken("
    "Invalid referenced-file checksum at index %d"
    "const int nameCount = Cmd_Argc();"
    "if (checksumCount != nameCount)"
    "!info_string::IsSafeUnquotedPathTokenComponent(name)"
    "Invalid referenced-file name at index %d"
    "fs_names[i] = CopyString(Cmd_Argv(i));"
    "fs_sums[i] = parsedChecksums[static_cast<std::size_t>(i)];")
    require_security_slice_contains(
        _server_referenced_files
        "${_marker}"
        "remote referenced-file names require fail-closed paired preflight")
endforeach()
require_security_slice_ordered(
    _server_referenced_files
    "info_string::TryParseSignedDecimalToken("
    "!info_string::IsSafeUnquotedPathTokenComponent(name)"
    "every checksum is validated before the paired name preflight")
require_security_slice_ordered(
    _server_referenced_files
    "!info_string::IsSafeUnquotedPathTokenComponent(name)"
    "fs_names[i] = CopyString(Cmd_Argv(i));"
    "every remote name is validated before any allocation")
require_security_slice_ordered(
    _server_referenced_files
    "!info_string::IsSafeUnquotedPathTokenComponent(name)"
    "fs_sums[i] = parsedChecksums[static_cast<std::size_t>(i)];"
    "every remote name is validated before checksum publication")
require_security_slice_ordered(
    _server_referenced_files
    "fs_names[i] = CopyString(Cmd_Argv(i));"
    "fs_sums[i] = parsedChecksums[static_cast<std::size_t>(i)];"
    "paired checksum publication follows successful name preflight")
forbid_security_slice_contains(
    _server_referenced_files
    "atoi("
    "remote checksums cannot use undefined-overflow or prefix parsing")
require_source_contains(
    "database/db_stream_load.cpp"
    "CheckedArrayBytes(count, stride, &byteCount)"
    "generated fast-file arrays must use checked count multiplication")
require_source_contains(
    "database/db_stream_load.cpp"
    "DB_ResolveInsertedPointer"
    "fast-file aliases must resolve through registered provenance")
require_source_contains(
    "database/db_stream_load.cpp"
    "DB_ResolveOffsetBytes"
    "migrated direct offsets must resolve through materialized-range provenance")
require_source_contains(
    "database/db_stream_load.cpp"
    "requiredBytes ? requiredBytes : 1"
    "non-null zero-count direct offsets must still reference materialized storage")
require_source_contains(
    "database/db_stream_load.cpp"
    "DB_ResolveOffsetCString"
    "direct fast-file strings must scan only bounded materialized storage")
require_source_contains(
    "database/db_stream_load.cpp"
    "DB_RegisterStreamCString"
    "inline fast-file strings must register exact start and extent provenance")
require_source_contains(
    "database/db_stream_load.cpp"
    "SL_GetStringOfSize"
    "direct temporary strings must be interned with their validated extent")
require_source_contains(
    "database/db_validation.h"
    "kMaxInternedStringBytes = 65531"
    "temporary strings must remain below the script-memory allocation ceiling")
require_source_contains(
    "database/db_stream_load.cpp"
    "db::validation::CanInternString(byteCount)"
    "inline and direct temporary strings must enforce the allocation ceiling")
require_source_contains(
    "database/db_load.cpp"
    "varMaterial->textureCount,
            \"material textures\")"
    "material textures must reject missing nonempty spans")
require_source_contains(
    "database/db_load.cpp"
    "varMaterial->constantCount,
            \"material constants\")"
    "material constants must reject missing nonempty spans")
require_source_contains(
    "database/db_load.cpp"
    "varMaterial->stateBitsCount,
            \"material state bits\")"
    "material state bits must reject missing nonempty spans")
require_source_contains(
    "database/db_load.cpp"
    "varGfxAabbTree->smodelIndexCount,
            \"world AABB static-model indices\")"
    "world AABB indices must reject missing nonempty spans")
require_source_contains(
    "database/db_load.cpp"
    "varGfxWorld->planeCount,
            \"world planes\")"
    "world planes must reject missing nonempty spans")
require_source_contains(
    "database/db_load.cpp"
    "db::validation::AllU16Below"
    "world AABB static-model indices must be bounded before runtime use")
require_source_contains(
    "database/db_validation.h"
    "ValidateWorldAabbTopology("
    "world AABB topology must use the portable linear-time validator")
require_source_contains(
    "database/db_load.cpp"
    "GfxCellLayoutValid(*varGfxCell, &extents)"
    "world cell child arrays must validate checked extents before loading")
require_source_contains(
    "database/db_load.cpp"
    "!DB_ValidateWorldAabbTrees(varGfxWorld)"
    "completed fast-file worlds must validate every owning AABB tree")
require_source_contains(
    "database/db_load.cpp"
    "if (!DB_ValidateWorldAabbCell(varGfxWorld, varGfxCell))"
    "every owning fast-file cell must validate its AABB topology")
require_source_contains(
    "database/db_validation.h"
    "kMaxGfxWorldCells = 1024"
    "world cells must fit fixed renderer bitsets and traversal lists")
require_source_contains(
    "database/db_validation.h"
    "kMaxGfxPortalVertices = 64"
    "portal geometry must fit fixed renderer hull workspaces")
require_source_contains(
    "database/db_validation.h"
    "kMaxGfxReflectionProbes = 254"
    "world reflection-probe iteration must not wrap its uint8 index")
require_source_contains(
    "database/db_validation.h"
    "aabbTreeCount < 1"
    "every world cell must retain the AABB root dereferenced by runtime queries")
require_source_contains(
    "database/db_validation.h"
    "GfxReflectionProbeRuntimeValid("
    "world reflection probes must validate finite origins and required images")
require_source_contains(
    "database/db_validation.h"
    "GfxCullGroupRuntimeValid("
    "world cull groups must validate bounds and sorted-surface spans")
require_source_contains(
    "database/db_load.cpp"
    "GfxWorldCellBitsValid(
            cellCount,
            varGfxWorld->cellBitsCount)"
    "world cell bit buffers must match the renderer's fixed chunk layout")
require_source_contains(
    "database/db_validation.h"
    "SerializedArrayElementIndex("
    "serialized member references must use explicit disk strides")
require_source_ordered(
    "database/db_load.cpp"
    "Load_StreamArray(
        atStreamStart,
        (uint8_t *)varGfxCell,
        count,
        disk32::kGfxCellBytes)"
    "DB_ResolveDirectPointer(
            &varGfxPortal->cell"
    "the complete cell header array must materialize before portal targets resolve")
require_source_ordered(
    "database/db_load.cpp"
    "varGfxPortal->writable = {};"
    "DB_ResolveDirectPointer(
            &varGfxPortal->cell"
    "serialized portal runtime pointers must be scrubbed before target fixups")
require_source_ordered(
    "database/db_load.cpp"
    "DB_ResolveDirectPointer(
            &varGfxPortal->cell"
    "SerializedArrayElementIndex(
            varGfxWorld->cells"
    "portal targets must resolve a bounded span before exact cell membership")
require_source_ordered(
    "database/db_load.cpp"
    "GfxWorldCellGraphValid(
            *varGfxWorld"
    "if (!Load_GfxWorld(1))"
    "completed world cell graphs must validate before returning to asset publication")
require_source_ordered(
    "database/db_load.cpp"
    "DB_ValidateMaterializedSpan(
            varGfxWorld->reflectionProbeTextures"
    "GfxWorldCellGraphValid(
            *varGfxWorld"
    "reflection texture scratch storage must materialize before world publication")
require_source_ordered(
    "database/db_load.cpp"
    "DB_ValidateMaterializedSpan(
            varGfxWorld->cellCasterBits"
    "GfxWorldCellGraphValid(
            *varGfxWorld"
    "cell-caster storage must materialize before world publication")
require_source_ordered(
    "database/db_load.cpp"
    "DB_ValidateMaterializedBlock4Span(
            varGfxWorld->dpvs.sortedSurfIndex"
    "GfxWorldCellGraphValid(
            *varGfxWorld"
    "sorted surfaces must materialize before graph validation dereferences them")
require_source_contains(
    "database/db_load.cpp"
    "db::validation::CheckedCountSum(
            varGfxWorld->dpvs.staticSurfaceCountNoDecal,
            varGfxWorld->dpvs.staticSurfaceCount,
            &sortedSurfaceCount)"
    "sorted-surface overflow must fail instead of collapsing to a zero-byte span")
require_source_ordered(
    "database/db_load.cpp"
    "DB_ValidateMaterializedBlock4Span(
            varGfxWorld->models"
    "!DB_ValidateWorldAabbTrees(varGfxWorld)"
    "world brush models must materialize before AABB validation dereferences model zero")
require_source_contains(
    "database/db_load.cpp"
    "!DB_IsStreamRangeValid(
                    *varGfxWorldPtr,
                    disk32::kGfxWorldBytes)"
    "world loading must preflight its complete top-level header allocation")
require_source_ordered(
    "database/db_load.cpp"
    "inserted = DB_InsertPointer(DBAliasKind::GfxWorld);
                if (!inserted)"
    "if (!Load_GfxWorld(1))"
    "shared world alias-slot failure must stop loading before asset publication")
require_source_ordered(
    "database/db_load.cpp"
    "!DB_IsStreamRangeValid(
                    varGfxWorld->sunLight,
                    disk32::kGfxLightBytes)"
    "DB_ValidateSunLight(varGfxWorld->sunLight)"
    "inline world sunlight must preflight its full schema before validation")
require_source_ordered(
    "database/db_load.cpp"
    "if (!Load_GfxWorld(1))"
    "Load_GfxWorldAsset((XAssetHeader *)varGfxWorldPtr)"
    "invalid portal graphs must stop world asset publication")
require_source_not_contains(
    "database/db_load.cpp"
    "varGfxPortal->cell = (GfxCell *)AllocLoad_FxElemVisStateSample()"
    "portal targets must not allocate cells outside the owned world array")
require_source_contains(
    "database/db_validation.h"
    "kMaxPathTreeDepth = 64"
    "path-tree traversal must reject recursion-depth denial of service")
require_source_contains(
    "database/db_validation.h"
    "kMaxPathChainDepth = 256"
    "path-chain parents must bound recursive gameplay traversal depth")
require_source_contains(
    "database/db_validation.h"
    "PathNodeTypeValid("
    "path-node types must be bounded before runtime table lookup and bit shifts")
require_source_ordered(
    "database/db_load.cpp"
    "&varpathnode_t->dynamic.pOwner"
    "&varpathnode_t->transient"
    "PathNodeTypeValid("
    "runtime-only path-node state must be scrubbed before type validation")
require_source_contains(
    "database/db_validation.h"
    "PathDataLayoutValid("
    "path-data child arrays must derive checked disk32 extents")
require_source_contains(
    "database/db_validation.h"
    "PathVisibilityBytes("
    "path visibility must cover every runtime-indexed directed node pair")
require_source_contains(
    "database/db_validation.h"
    "PathLinksRuntimeValid("
    "path links must reject out-of-range runtime node indices")
require_source_ordered(
    "database/db_load.cpp"
    "PathNodesRuntimeValid(
            varPathData->nodes"
    "PathChainMapsRuntimeValid(
            varPathData->chainNodeForNode"
    "PathTreeGraphValid(
            varPathData->nodeTree"
    "path runtime indices and parent topology must validate before tree publication")
require_source_ordered(
    "database/db_load.cpp"
    "Load_StreamArray(
        atStreamStart,
        (uint8_t *)varpathnode_tree_t,
        count,
        disk32::kPathTreeBytes)"
    "if (!Load_pathnode_tree_t(0))"
    "the complete flat path-tree header array must materialize before child fixups")
require_source_ordered(
    "database/db_load.cpp"
    "DB_ResolveDirectPointer(
            varpathnode_tree_ptr"
    "SerializedArrayElementIndex(
            varPathData->nodeTree"
    "path-tree children must resolve a full header before exact owner membership")
require_source_ordered(
    "database/db_load.cpp"
    "PathTreeGraphValid(
            varPathData->nodeTree"
    "if (!Load_GameWorldSp(1))"
    "completed path trees must validate before returning toward SP-world publication")
require_source_ordered(
    "database/db_load.cpp"
    "if (!Load_GameWorldSp(1))"
    "Load_GameWorldSpAsset((XAssetHeader *)varGameWorldSpPtr)"
    "invalid path graphs must stop SP-world asset publication")
require_source_not_contains(
    "database/db_load.cpp"
    "*varpathnode_tree_ptr = (pathnode_tree_t *)AllocLoad_FxElemVisStateSample()"
    "path-tree children must not allocate nodes outside the owned flat array")
require_source_not_contains(
    "database/db_stream_load.cpp"
    "DB_ConvertOffsetToPointerLegacy"
    "all direct fast-file offsets must use bounded typed resolution")
require_source_contains(
    "database/db_load.cpp"
    "void __cdecl Load_XAssetHeader(bool atStreamStart)
{
    if (varXAsset->type < 0 || varXAsset->type >= ASSET_TYPE_COUNT)"
    "top-level asset loading must validate the serialized type before dispatch")
require_source_contains(
    "database/db_load.cpp"
    "if (!DB_IsXAssetTypeSupportedForBuild(varXAsset->type))
    {
        Com_Error(
            ERR_DROP,
            \"Fast-file asset type '%s' is not supported by this build\""
    "top-level asset loading must reject disallowed build-mode and unavailable types before dispatch")
require_source_contains(
    "database/db_load.cpp"
    "void __cdecl Mark_XAssetHeader()
{
    if (varXAsset->type < 0 || varXAsset->type >= ASSET_TYPE_COUNT)"
    "asset marking must validate the type before dispatch")
require_source_contains(
    "database/db_load.cpp"
    "\"Cannot mark asset type '%s' in this build\""
    "asset marking must reject disallowed build-mode and unavailable types before dispatch")
require_source_contains(
    "database/db_registry.cpp"
    "if (!DB_IsXAssetTypeSupportedForBuild(type))
    {
        Sys_UnlockWrite(&db_hashCritSect);
        Com_Error(
            ERR_DROP,
            \"Cannot allocate asset type %d in this build\""
    "asset allocation must reject invalid, unavailable, and wrong-mode types before pool indexing")
require_source_contains(
    "database/db_registry.cpp"
    "\"Cannot publish asset type %d in this build\""
    "asset publication must reject invalid, unavailable, and wrong-mode types before insertion")
require_source_contains(
    "database/db_registry.cpp"
    "\"Cannot mark asset type %d in this build\""
    "registry marking must reject invalid, unavailable, and wrong-mode types before name lookup")
require_source_contains(
    "database/db_load.cpp"
    "world->models[0].surfaceCount
            != world->dpvs.staticSurfaceCount"
    "world AABB metadata must match the world brush-model surface partition")
require_source_contains(
    "database/db_load.cpp"
    "db::validation::CoverageComplete("
    "world AABB cell roots must cover each sorted-surface partition exactly once")
require_source_contains(
    "database/db_load.cpp"
    "varGfxWorldDpvsStatic->smodelCount
        > db::validation::kMaxWorldAabbStaticModels"
    "fast-file static-model counts must fit uint16 AABB indices before payload loading")
require_source_contains(
    "database/db_load.cpp"
    "Fast-file world has an invalid sorted surface index"
    "world sorted-surface values must be bounded before renderer traversal")
require_source_contains(
    "gfx_d3d/r_gfx.h"
    "GfxAabbTree_GetChildren("
    "world AABB child offsets must have one byte-displacement accessor")
require_source_contains(
    "gfx_d3d/r_gfx.h"
    "GfxAabbTree_SetChildren("
    "world AABB producers must range-check native byte displacements")
require_source_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "ValidateImplicitWorldAabbForest("
    "raw BSP world AABB forests must validate before recursive reconstruction")
require_source_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "const std::uint32_t staticSurfaceCount = s_world.models[0].surfaceCount"
    "raw BSP AABB ranges must use the static sorted-surface partition")
require_source_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "R_ValidateBrushModelSurfaceRanges(&s_world)"
    "raw BSP brush-model surface ranges must validate before AABB consumption")
require_source_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "if (tree->smodelIndexCount == UINT16_MAX)"
    "load-object AABB static-model lists must reject count wraparound")
require_source_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "if (smodelIndex < 0 || smodelIndex > UINT16_MAX)"
    "load-object AABB static-model indices must validate before uint16 storage")
require_source_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "if (tree->childCount == UINT16_MAX)"
    "load-object AABB child appends must reject count wraparound")
require_source_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "if (smodelCount > UINT16_MAX)"
    "raw BSP entity parsing must reject unrepresentable static-model indices before allocation")
require_source_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "static_cast<std::uint32_t>(sizeof(GfxStaticModelCombinedInst))"
    "load-object static-model sorting must allocate the native combined stride")
require_source_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "&& depth < db::validation::kMaxWorldAabbDepth"
    "load-object AABB sorting must stop subdividing at the runtime recursion budget")
require_source_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "R_PreflightNoDecalAabbTree("
    "raw BSP AABB no-decal output must preflight capacity and field widths")
require_source_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "R_ValidateLoadedWorldAabbTrees("
    "flattened load-object world AABB trees must validate before publication")
require_source_not_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "tree + tree->childrenOffset"
    "world AABB byte offsets must not be scaled as element offsets")
require_source_not_contains(
    "gfx_d3d/r_marks.cpp"
    "(char *)tree + tree->childrenOffset"
    "renderer mark traversal must use the canonical AABB child accessor")
require_source_not_contains(
    "gfx_d3d/r_dpvs_static.cpp"
    "(char *)tree + tree->childrenOffset"
    "renderer visibility traversal must use the canonical AABB child accessor")

file(READ
    "${SOURCE_ROOT}/src/gfx_d3d/r_bsp_load_obj.cpp"
    _world_aabb_load_obj_source)
string(FIND
    "${_world_aabb_load_obj_source}"
    "if (!R_PreflightNoDecalAabbTree("
    _world_aabb_preflight_call)
string(FIND
    "${_world_aabb_load_obj_source}"
    "startSurfIndex = R_BuildNoDecalAabbTree_r("
    _world_aabb_no_decal_build_call)
if (_world_aabb_preflight_call EQUAL -1
    OR _world_aabb_no_decal_build_call EQUAL -1
    OR _world_aabb_preflight_call GREATER _world_aabb_no_decal_build_call)
    message(FATAL_ERROR
        "Raw BSP AABB no-decal output must preflight before writing")
endif()
require_source_contains(
    "database/db_load.cpp"
    "db::validation::CountInRange(varFont->glyphCount, 96, 65536)"
    "font glyph tables must cover direct ASCII indexing without oversized counts")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varMaterialArgumentDef->literalConst,
                16,
                4,
                kDirectBlock4"
    "literal material constants must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varMaterial->constantTable,
                constantByteCount,
                16,
                kDirectBlock4"
    "material constant tables must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varMaterial->stateBitsTable,
                stateBitsByteCount,
                4,
                kDirectBlock4"
    "material state-bit tables must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "DB_ResolveDirectPointer(
                &varGfxAabbTree->smodelIndexes,
                smodelIndexByteCount,
                2,
                kDirectBlock4"
    "world AABB indices must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varGfxWorldDpvsPlanes->planes,
                planeByteCount,
                4,
                kDirectBlock4"
    "world planes must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varXSurfaceVertexInfo->vertsBlend,
                blendByteCount,
                2,
                kDirectBlock4"
    "surface blend weights must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varXModel->boneNames,
                boneNameByteCount,
                2,
                kDirectBlock4"
    "model bone names must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varcNode_t->plane,
                20,
                4,
                kDirectBlock4"
    "collision-node planes must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varcLeafBrushNodeLeaf_t->brushes,
                brushIndexByteCount,
                2,
                kDirectBlock4"
    "leaf-brush indices must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "Load_CollisionBorderArray(1, varCollisionPartition->borderCount)"
    "inline collision partitions must load their complete border array")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varCollisionPartition->borders,
                borderByteCount,
                4,
                kDirectBlock4"
    "collision partition borders must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varFont->glyphs,
                glyphByteCount,
                4,
                kDirectBlock4"
    "font glyphs must use their full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "OptionalMirroredCountInRange(count, originalCount, 2, 16)"
    "weapon accuracy graphs must fit fixed runtime buffers and use matching counts")
require_source_contains(
    "database/db_load.cpp"
    "if (!DB_ValidateWeaponAccuracyGraph(
                varWeaponDef,"
    "weapon definitions must run accuracy-graph validation before loading payloads")
require_source_contains(
    "database/db_load.cpp"
    "db::validation::NormalizedGraphKnots("
    "weapon accuracy graphs must validate normalized interpolation inputs")
require_source_contains(
    "database/db_load.cpp"
    "if (!DB_ValidateWeaponAccuracyGraphKnots(varWeaponDef, 0))"
    "first weapon accuracy graph must be validated after materialization")
require_source_contains(
    "database/db_load.cpp"
    "if (!DB_ValidateWeaponAccuracyGraphKnots(varWeaponDef, 1))"
    "second weapon accuracy graph must be validated after materialization")
require_source_contains(
    "database/db_load.cpp"
    "Load_vec2_tArray(1, varWeaponDef->originalAccuracyGraphKnotCount[0])"
    "first original accuracy graph must load with the count used by its runtime consumer")
require_source_contains(
    "database/db_load.cpp"
    "Load_vec2_tArray(1, varWeaponDef->originalAccuracyGraphKnotCount[1])"
    "second original accuracy graph must load with the count used by its runtime consumer")
require_source_contains(
    "bgame/bg_weapons_load_obj.cpp"
    "CountInRange(declaredKnotCount, 2, 16)"
    "load-object weapon graphs must reject counts outside their stack-buffer capacity")
require_source_contains(
    "bgame/bg_weapons_load_obj.cpp"
    "if (knotCountIndex >= 16)"
    "load-object weapon graphs must check capacity before storing a knot")
require_source_contains(
    "bgame/bg_weapons_load_obj.cpp"
    "db::validation::NormalizedGraphKnots("
    "load-object weapon graphs must enforce runtime interpolation invariants")
require_source_contains(
    "database/db_load.cpp"
    "if (!Load_MaterialVertexDeclaration(1))
                return false;"
    "material vertex declarations must validate before runtime realization")
require_source_contains(
    "gfx_d3d/r_material.cpp"
    "db::validation::MaterialVertexRoutingValid"
    "all material vertex-declaration construction paths must validate routing indices")
require_source_contains(
    "gfx_d3d/r_material.cpp"
    "memset((*mtlVertDecl)->routing.decl, 0"
    "material vertex declarations must discard serialized runtime handles")
require_source_contains(
    "database/db_load.cpp"
    "uint16_t destinationMask = 0"
    "material vertex declarations must reject duplicate destination semantics")
require_source_contains(
    "database/db_load.cpp"
    "db::validation::MaterialPassLayoutValid("
    "material passes must bound serialized argument partitions and custom flags")
require_source_contains(
    "database/db_load.cpp"
    "db::validation::MaterialArgumentShapeValid("
    "material shader arguments must bound runtime register and source indices")
require_source_contains(
    "database/db_load.cpp"
    "MaterialCodeConstantAllowedInSegment("
    "material code constants must match their serialized update-frequency partition")
require_source_contains(
    "database/db_load.cpp"
    "MaterialCodeSamplerAllowedInSegment("
    "material code samplers must match their serialized update-frequency partition")
require_source_contains(
    "database/db_load.cpp"
    "if (!DB_ValidateMaterialPassArguments(varMaterialPass, argumentCount))"
    "material arguments must be validated after their literal fixups")
require_source_contains(
    "database/db_load.cpp"
    "uint32_t vertexRegisterMask = 0"
    "material arguments must reject overlapping vertex constant registers")
require_source_contains(
    "database/db_load.cpp"
    "uint16_t pixelSamplerMask = 0"
    "material arguments must reject stored/custom pixel sampler collisions")
require_source_contains(
    "database/db_load.cpp"
    "uint32_t pixelConstantMask[8] = {}"
    "material arguments must reject overlapping pixel constant registers")
require_source_contains(
    "database/db_load.cpp"
    "CountInRange(varMaterialTechnique->passCount, 1, 4)"
    "material techniques must fit the four-pass authored/runtime bound")
require_source_contains(
    "database/db_load.cpp"
    "varMaterialTechnique->flags &= UINT16_C(0x3F)"
    "material techniques must clear the serialized runtime upload bit")
require_source_contains(
    "database/db_load.cpp"
    "varMaterialTechniqueSet->worldVertFormat,
            0,
            11"
    "material world vertex formats must not index past declaration variants")
require_source_contains(
    "database/db_load.cpp"
    "varMaterialTechniqueSet->hasBeenUploaded = false"
    "material technique sets must not trust serialized upload state")
require_source_contains(
    "database/db_load.cpp"
    "varMaterialTechniqueSet->remappedTechniqueSet = nullptr"
    "material technique sets must not trust serialized remap pointers")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "consts->count >= ARRAY_COUNT(consts->dest)"
    "pixel literal collection must enforce its runtime capacity")
require_source_contains(
    "gfx_d3d/rb_shade.cpp"
    "static_cast<uint32_t>(primState->vertDeclType) >= VERTDECL_COUNT"
    "material binding must bound the runtime vertex declaration type")
require_source_contains(
    "gfx_d3d/r_shade.cpp"
    "static_cast<uint32_t>(state->prim.vertDeclType) >= VERTDECL_COUNT"
    "material updates must bound the runtime vertex declaration type")
require_source_contains(
    "gfx_d3d/rb_uploadshaders.cpp"
    "static_cast<uint32_t>(vertDeclType) >= VERTDECL_COUNT"
    "material uploads must bound the runtime vertex declaration type")

file(READ
    "${SOURCE_ROOT}/src/bgame/bg_weapons_load_obj.cpp"
    _weapon_graph_load_obj_source)
string(FIND
    "${_weapon_graph_load_obj_source}"
    "if (knotCountIndex >= 16)"
    _weapon_graph_capacity_check)
string(FIND
    "${_weapon_graph_load_obj_source}"
    "knots[knotCountIndex][0] = x"
    _weapon_graph_first_store)
if (_weapon_graph_capacity_check EQUAL -1
    OR _weapon_graph_first_store EQUAL -1
    OR _weapon_graph_capacity_check GREATER _weapon_graph_first_store)
    message(FATAL_ERROR
        "Load-object weapon graph capacity must be checked before the first knot store")
endif()
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varWeaponDef->accuracyGraphKnots[0],
                accuracyGraphByteCount[0],
                4,
                kDirectBlock4"
    "first working accuracy graph must use its full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varWeaponDef->originalAccuracyGraphKnots[0],
                accuracyGraphByteCount[0],
                4,
                kDirectBlock4"
    "first original accuracy graph must use its full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varWeaponDef->accuracyGraphKnots[1],
                accuracyGraphByteCount[1],
                4,
                kDirectBlock4"
    "second working accuracy graph must use its full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varWeaponDef->originalAccuracyGraphKnots[1],
                accuracyGraphByteCount[1],
                4,
                kDirectBlock4"
    "second original accuracy graph must use its full aligned block-4 span")
require_source_contains(
    "database/db_load.cpp"
    "DBAliasKind::XStringPointerSlot"
    "direct string-holder references must use completed-object provenance")
require_source_contains(
    "database/db_relocation.h"
    "CompletedSharedObjectSchemaValid("
    "completed shared objects must use a portable tested disk32 schema")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varsnd_alias_t->soundFile,
                DBAliasKind::SoundFile,
                disk32::kSoundFileBytes"
    "shared sound files must resolve through exact typed provenance")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varsnd_alias_t->speakerMap,
                DBAliasKind::SpeakerMap,
                disk32::kSpeakerMapBytes"
    "shared speaker maps must resolve through exact typed provenance")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varsnd_alias_list_t->head,
                DBAliasKind::SndAliasArray,
                aliasByteCount"
    "shared sound-alias arrays must resolve through exact typed provenance")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varWeaponDef->bounceSound,
                DBAliasKind::WeaponBounceSoundTable,
                disk32::kWeaponBounceSoundTableBytes"
    "weapon bounce-sound tables must resolve through exact typed provenance")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varGfxWorld->sunLight,
                DBAliasKind::GfxLight,
                disk32::kGfxLightBytes"
    "shared world sun lights must resolve through exact typed provenance")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)varStringTablePtr,
                DBAliasKind::StringTable,
                disk32::kStringTableBytes"
    "shared string tables must resolve through exact typed provenance")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)varXModelPiecesPtr,
                DBAliasKind::XModelPieces,
                disk32::kXModelPiecesBytes"
    "shared model-pieces headers must resolve through exact typed provenance")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varXRigidVertList->collisionTree,
                DBAliasKind::XSurfaceCollisionTree,
                disk32::kXSurfaceCollisionTreeBytes"
    "shared surface collision trees must resolve through exact typed provenance")
require_source_contains(
    "database/db_load.cpp"
    "(uint32_t*)&varXSurface->vertList,
                DBAliasKind::XRigidVertListArray,
                rigidListBytes"
    "shared rigid-vertex lists must resolve through exact typed provenance")
require_source_contains(
    "database/db_load.cpp"
    "db::validation::SoundFileHeaderValid("
    "sound-file union tags and existence flags must validate before dispatch")
require_source_contains(
    "database/db_load.cpp"
    "DB_ValidateSpeakerMap(varSpeakerMap)"
    "speaker-map fixed arrays and levels must validate before completion")
require_source_contains(
    "database/db_load.cpp"
    "CountInRange(varSndCurve->knotCount, 2, 8)"
    "sound falloff curves must fit their fixed knot array")
require_source_contains(
    "database/db_load.cpp"
    "NormalizedGraphKnots(
            varSndCurve->knots"
    "sound falloff curves must be finite, normalized, and strictly ordered")
require_source_contains(
    "database/db_load.cpp"
    "if (!DB_ValidateSoundAlias(varsnd_alias_t))"
    "sound aliases must validate required completed children before array publication")
require_source_contains(
    "database/db_load.cpp"
    "if (!varsnd_alias_list_t->aliasName || !*varsnd_alias_list_t->aliasName)"
    "sound-alias lists must have a completed name before asset publication")
require_source_contains(
    "database/db_load.cpp"
    "if (!varStringTable->name || !*varStringTable->name)"
    "string tables must have a completed name before asset publication")
require_source_contains(
    "database/db_load.cpp"
    "varStringTable->values,
            valueCount,
            \"string-table values\""
    "string-table values must match their validated dimensions")
require_source_contains(
    "database/db_load.cpp"
    "if (!DB_ValidateSunLight(varGfxWorld->sunLight))"
    "world sun-light values must validate before exact completion")
require_source_contains(
    "database/db_load.cpp"
    "XModelPiecesLayoutValid(
            pieces->name != nullptr,
            pieces->pieces != nullptr,
            pieces->numpieces"
    "model-pieces arrays must preserve pointer/count and fit the uint16 source-format extent")
require_source_contains(
    "database/db_load.cpp"
    "XModelPieceRuntimeValid("
    "model pieces must have finite offsets and completed model pointers")
require_source_contains(
    "database/db_load.cpp"
    "DB_IsStreamRangeValid(
            varXModelPiece,
            static_cast<uint32_t>(pieceBytes))"
    "model-pieces child spans must fit before element fixups begin")
require_source_contains(
    "database/db_load.cpp"
    "ValidateXSurfaceCollisionTopology("
    "surface collision nodes, leaves, and triangle spans must validate before publication")
require_source_contains(
    "database/db_load.cpp"
    "XSurfaceRigidPartitionValid("
    "rigid surface partitions must validate against completed geometry")
require_source_contains(
    "database/db_load.cpp"
    "XSurfaceTriangleIndicesValid("
    "surface triangle indices must remain inside the completed vertex span")
require_source_contains(
    "database/db_validation.h"
    "kMaxBrushNonaxialSides = 26"
    "model physics brushes must retain the source builder side budget")
require_source_contains(
    "database/db_validation.h"
    "kMaxBrushAdjacencyEntries = 32 * 12"
    "model physics brush adjacency must retain the source builder edge budget")
require_source_contains(
    "database/db_validation.h"
    "side.plane != &brush.planes[sideIndex]"
    "completed physics brush sides must reference their indexed owned planes")
require_source_contains(
    "database/db_validation.h"
    "brush.axialMaterialNum[direction][axis] != 0"
    "model physics brushes must retain their source-generated zero axial materials")
require_source_contains(
    "database/db_validation.h"
    "side.materialNum != 0"
    "model physics brush sides must retain their source-generated zero materials")
require_source_contains(
    "database/db_validation.h"
    "BrushMaterialIndex("
    "runtime brush materials must use a portable bounds-checked selector")
require_source_contains(
    "physics/phys_world_collision.cpp"
    "if (!cm.materials
        || !db::validation::BrushMaterialIndex("
    "physics collision must validate brush and material indices before table access")
require_source_contains(
    "database/db_validation.h"
    "PhysGeomInfoRuntimeValid("
    "physics geometry tags, primitive dimensions, and nested brushes must validate")
require_source_contains(
    "database/db_validation.h"
    "PhysMassFinite(list.mass)"
    "physics mass properties must be finite before publication")
require_source_contains(
    "database/db_load.cpp"
    "disk32::PointerToken sidePlaneTokens["
    "physics brush-side forward tokens must be preserved outside native pointer fields")
require_source_ordered(
    "database/db_load.cpp"
    "Load_cplane_tArray(
                1,
                static_cast<int32_t>(varBrushWrapper->numsides));"
    "const db::relocation::Status status = DB_ResolveOffsetBytes(
            token,"
    "physics brush-side forward references must wait for the wrapper plane array")
require_source_ordered(
    "database/db_load.cpp"
    "DB_RegisterPointerSlot(
                varPhysGeomInfo->brush,
                DBAliasKind::BrushWrapper)"
    "if (!Load_BrushWrapper(1))"
    "physics brush provenance must register before child fixups")
require_source_ordered(
    "database/db_load.cpp"
    "if (!Load_BrushWrapper(1))"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::BrushWrapper"
    "physics brush provenance must publish after graph validation")
require_source_contains(
    "database/db_load.cpp"
    "DB_ResolveCompletedPointer(
                &varPhysGeomInfo->brush,
                DBAliasKind::BrushWrapper,
                disk32::kBrushWrapperBytes"
    "shared physics brushes must resolve through exact typed provenance")
require_source_ordered(
    "database/db_load.cpp"
    "DB_RegisterPointerSlot(
                varXModel->physGeoms,
                DBAliasKind::PhysGeomList)"
    "if (!Load_PhysGeomList(1)"
    "physics geometry-list provenance must register before nested brush loading")
require_source_ordered(
    "database/db_load.cpp"
    "if (!Load_PhysGeomList(1)"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::PhysGeomList"
    "physics geometry lists must publish only after nested validation")
require_source_contains(
    "database/db_load.cpp"
    "DB_ResolveCompletedPointer(
                &varXModel->physGeoms,
                DBAliasKind::PhysGeomList,
                disk32::kPhysGeomListBytes"
    "shared physics geometry lists must resolve through exact typed provenance")
require_source_contains(
    "database/db_validation.h"
    "kMaxClipMapBrushNonaxialSides = 250"
    "clipmap brushes must fit fixed 256-plane collision workspaces")
require_source_contains(
    "database/db_validation.h"
    "ClipMapPlaneValid("
    "clipmap plane metadata and finite values must validate before use")
require_source_contains(
    "database/db_validation.h"
    "ClipMapBrushGraphValid("
    "clipmap brush sides and adjacency must form an exact global partition")
require_source_ordered(
    "database/db_load.cpp"
    "ClipMapBrushLayoutValid(
            *varclipMap_t,
            &brushExtents)"
    "varXString = &varclipMap_t->name;"
    "clipmap array extents must validate before child materialization")
require_source_contains(
    "database/db_load.cpp"
    "Invalid inline fast-file clipmap brush sides"
    "clipmap brush sides must use bounded global direct references")
require_source_ordered(
    "database/db_load.cpp"
    "ClipMapBrushAdjacencyPrefixExtent(
            *varcbrush_t,
            &adjacencyBytes)"
    "DB_ResolveDirectPointer(
                &varcbrush_t->baseAdjacentSide"
    "clipmap adjacency extents must be derived before resolving their token")
require_source_ordered(
    "database/db_load.cpp"
    "DB_ResolveDirectPointer(
                &varcbrush_t->baseAdjacentSide"
    "if (!DB_GetClipBrushAdjacencyBytes(
            varcbrush_t,
            &validatedAdjacencyBytes)"
    "clipmap adjacency contents must not be read before bounded resolution")
require_source_ordered(
    "database/db_load.cpp"
    "DB_RegisterPointerSlot(
                varclipMap_t->box_brush,
                DBAliasKind::ClipMapBoxBrush)"
    "if (!Load_cbrush_t(1))"
    "clipmap box-brush provenance must register before child fixups")
require_source_contains(
    "database/db_load.cpp"
    "DB_ResolveCompletedPointer(
                &varclipMap_t->box_brush,
                DBAliasKind::ClipMapBoxBrush,
                disk32::kCBrushBytes"
    "shared clipmap box brushes must resolve through exact typed provenance")
require_source_ordered(
    "database/db_load.cpp"
    "ClipMapBrushGraphValid(*varclipMap_t)"
    "DB_CompleteObject(
            completedBoxBrush,
            DBAliasKind::ClipMapBoxBrush"
    "clipmap box brushes must publish only after whole-graph validation")
require_source_ordered(
    "database/db_load.cpp"
    "if (!Load_clipMap_t(1))"
    "Load_ClipMapAsset((XAssetHeader *)varclipMap_ptr)"
    "invalid clipmap graphs must stop asset publication")
foreach(_clipmap_collision_source
    "physics/phys_coll_boxbrush.cpp"
    "physics/phys_coll_capsulebrush.cpp"
    "physics/phys_coll_cylinderbrush.cpp")
    require_source_contains(
        "${_clipmap_collision_source}"
        "db::validation::kMaxClipMapBrushNonaxialSides"
        "clipmap collision workspaces must reject oversized brush graphs")
endforeach()
require_source_ordered(
    "xanim/xmodel.cpp"
    "if (nextNodeQueueEnd == locals->nodeQueueBegin)"
    "locals->nodeQueue[locals->nodeQueueEnd].beginIndex = childBeginIndex"
    "surface collision traversal must reject a full node queue before writing")
require_source_not_contains(
    "xanim/xmodel.cpp"
    "0xFFFFFF80"
    "surface prefetch stubs must not truncate native-width addresses")
require_source_ordered(
    "DynEntity/DynEntity_pieces.cpp"
    "if (!model->physPreset)"
    "model->physPreset->piecesUpwardVelocity"
    "piece spawning must reject missing physics presets before dereference")
require_source_ordered(
    "database/db_load.cpp"
    "DB_RegisterPointerSlot(
                varsnd_alias_t->soundFile,
                DBAliasKind::SoundFile)"
    "if (!Load_SoundFile(1))"
    "sound-file provenance must register before child fixups")
require_source_ordered(
    "database/db_load.cpp"
    "if (!Load_SoundFile(1))"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::SoundFile"
    "sound-file provenance must publish after child fixups")
require_source_ordered(
    "database/db_load.cpp"
    "DB_RegisterPointerSlot(
                varsnd_alias_t->speakerMap,
                DBAliasKind::SpeakerMap)"
    "if (!Load_SpeakerMap(1))"
    "speaker-map provenance must register before child fixups")
require_source_ordered(
    "database/db_load.cpp"
    "if (!Load_SpeakerMap(1))"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::SpeakerMap"
    "speaker-map provenance must publish after validation")
require_source_ordered(
    "database/db_load.cpp"
    "DB_RegisterPointerSlot(
                varsnd_alias_list_t->head,
                DBAliasKind::SndAliasArray)"
    "if (!Load_snd_alias_tArray(1, varsnd_alias_list_t->count))"
    "sound-alias array provenance must register before element fixups")
require_source_ordered(
    "database/db_load.cpp"
    "if (!Load_snd_alias_tArray(1, varsnd_alias_list_t->count))"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::SndAliasArray"
    "sound-alias array provenance must publish after element fixups")
require_source_ordered(
    "database/db_load.cpp"
    "DB_RegisterPointerSlot(
                varWeaponDef->bounceSound,
                DBAliasKind::WeaponBounceSoundTable)"
    "Load_snd_alias_list_nameArray(
                1,
                disk32::kWeaponBounceSoundCount)"
    "bounce-sound provenance must register before holder fixups")
require_source_ordered(
    "database/db_load.cpp"
    "Load_snd_alias_list_nameArray(
                1,
                disk32::kWeaponBounceSoundCount)"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::WeaponBounceSoundTable"
    "bounce-sound provenance must publish after holder fixups")
require_source_ordered(
    "database/db_load.cpp"
    "DB_RegisterPointerSlot(
                serializedStringTable,
                DBAliasKind::StringTable)"
    "Load_StringTable(1)"
    "string-table provenance must register before child fixups")
require_source_ordered(
    "database/db_load.cpp"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::StringTable"
    "Load_StringTableAsset((XAssetHeader *)varStringTablePtr)"
    "string tables must publish their serialized identity before asset canonicalization")
require_source_ordered(
    "database/db_load.cpp"
    "DB_RegisterPointerSlot(
                varGfxWorld->sunLight,
                DBAliasKind::GfxLight)"
    "Load_GfxLight(1)"
    "sun-light provenance must register before child fixups")
require_source_ordered(
    "database/db_load.cpp"
    "if (!DB_ValidateSunLight(varGfxWorld->sunLight))"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::GfxLight"
    "sun-light provenance must publish after semantic validation")
require_source_ordered(
    "database/db_load.cpp"
    "DB_RegisterPointerSlot(
                *varXModelPiecesPtr,
                DBAliasKind::XModelPieces)"
    "if (!Load_XModelPieces(1))"
    "model-pieces provenance must register before child fixups")
require_source_ordered(
    "database/db_load.cpp"
    "if (!Load_XModelPieces(1))"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::XModelPieces"
    "model-pieces provenance must publish after child validation")
require_source_ordered(
    "database/db_load.cpp"
    "return DB_ValidateXModelPieces("
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::XModelPieces"
    "model-pieces semantic validation must precede publication")
require_source_contains(
    "database/db_load.cpp"
    "if (!Load_XModelPiecesPtr(0))
        return;"
    "dynamic-entity loading must stop after model-pieces rejection")
require_source_ordered(
    "database/db_load.cpp"
    "DB_RegisterPointerSlot(
                varXRigidVertList->collisionTree,
                DBAliasKind::XSurfaceCollisionTree)"
    "if (!Load_XSurfaceCollisionTree(1))"
    "surface collision-tree provenance must register before child loading")
require_source_ordered(
    "database/db_load.cpp"
    "if (!Load_XSurfaceCollisionTree(1))"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::XSurfaceCollisionTree"
    "surface collision trees must publish after topology validation")
require_source_ordered(
    "database/db_load.cpp"
    "completedRigidLists = DB_RegisterPointerSlot(
                varXSurface->vertList,
                DBAliasKind::XRigidVertListArray)"
    "if (!Load_XRigidVertListArray("
    "rigid-list provenance must register before nested tree loading")
require_source_ordered(
    "database/db_load.cpp"
    "Load_r_index16_tArray(1, indexCount)"
    "if (!DB_ValidateLoadedXSurface(
            varXSurface,
            deformed,
            varXModel ? varXModel->numBones : 0))"
    "rigid-list validation must wait for surface triangle loading")
require_source_ordered(
    "database/db_load.cpp"
    "if (!DB_ValidateLoadedXSurface(
            varXSurface,
            deformed,
            varXModel ? varXModel->numBones : 0))"
    "DB_CompleteObject(
            completedRigidLists,
            DBAliasKind::XRigidVertListArray"
    "rigid lists must publish only after consumer-specific validation")
require_source_ordered(
    "database/db_load.cpp"
    "if (!Load_XModel(1))"
    "Load_XModelAsset((XAssetHeader *)varXModelPtr)"
    "invalid surface graphs must stop model asset publication")
require_source_contains(
    "database/db_stream.cpp"
    "DB_ValidateStreamCString"
    "completed string holders must validate registered pointee provenance")
require_source_contains(
    "database/db_relocation.cpp"
    "resolvedAddress != aliasBlock.base + record.offset"
    "completed string holders must publish their exact registered slot")
require_source_contains(
    "database/db_relocation.cpp"
    "RequiresExactStartPublication(expectedKind)"
    "completed material objects must publish their exact registered start")
require_source_contains(
    "database/db_stream.cpp"
    "DB_DirectResolver().ValidateAddress("
    "completed material objects must cover a fully materialized stream span")
require_source_contains(
    "database/db_zone_stream_ownership.cpp"
    "if (receipt->phase() == ZoneStreamGenerationPhase::Invalidated)
        return ZoneStreamOwnershipStatus::AlreadyComplete;
    if (receipt->phase() == ZoneStreamGenerationPhase::UnsafeFailure)"
    "terminal stream receipts must return before inspecting a newer singleton")
require_source_contains(
    "database/db_zone_stream_ownership.cpp"
    "if (hasPointer != hasSize)"
    "stream bindings must reject pointer/size mismatches")
require_source_contains(
    "database/db_zone_stream_ownership.cpp"
    "block.size
                > (std::numeric_limits<std::uintptr_t>::max)() - block.base"
    "stream block ends must reject native-address overflow")
require_source_contains(
    "database/db_zone_stream_ownership.cpp"
    "&& SpansOverlap(
                    block.base,
                    blockEnds[i],
                    blocks[prior].base,
                    blockEnds[prior])"
    "stream blocks must be pairwise disjoint")
require_source_contains(
    "database/db_zone_stream_ownership.h"
    "const XZoneMemory *zoneIdentity"
    "zone identity must use a typed public boundary")
require_source_not_contains(
    "database/db_zone_stream_ownership.h"
    "const void *zoneIdentity"
    "zone identity must not regress to an opaque dereference boundary")
require_source_contains(
    "database/db_zone_stream_ownership.cpp"
    "ObjectIsAligned(zoneIdentity, alignof(XZoneMemory))"
    "zone identity alignment must be checked before descriptor access")
require_repository_contains(
    "tests/db_zone_stream_ownership_source_test.cmake"
    "string(CONCAT _qualified_using_bypass"
    "zone-stream production-neutrality seal must retain using-bypass coverage")
require_repository_contains(
    "tests/db_zone_stream_ownership_source_test.cmake"
    "function(normalize_zone_stream_phase2"
    "zone-stream production-neutrality seal must normalize line splices")
require_repository_contains(
    "tests/db_zone_stream_ownership_source_test.cmake"
    "set(_zone_stream_comment_gap"
    "zone-stream production-neutrality seal must recognize comment token gaps")
require_repository_contains(
    "tests/db_zone_stream_ownership_source_test.cmake"
    "string(CONCAT _public_header_bypass"
    "zone-stream production-neutrality seal must retain header-splice coverage")
require_repository_contains(
    "tests/db_zone_stream_ownership_source_test.cmake"
    "string(CONCAT _unqualified_pointer_bypass"
    "zone-stream production-neutrality seal must retain pointer-bypass coverage")
require_repository_contains(
    "tests/db_zone_stream_ownership_source_test.cmake"
    "string(CONCAT _private_pointer_bypass"
    "zone-stream production-neutrality seal must retain private-pointer coverage")
require_repository_contains(
    "tests/db_zone_stream_ownership_source_test.cmake"
    "set(_compact_namespace_bypass"
    "zone-stream production-neutrality seal must retain namespace-bypass coverage")
require_repository_contains(
    "tests/db_zone_stream_ownership_production_seal_tests.cpp"
    "SplicedBindPointer"
    "zone-stream bypass probes must remain compiler-validated")
require_repository_contains(
    "tests/db_zone_stream_ownership_production_seal_tests.cpp"
    "CommentQualifiedBindPointer"
    "comment-separated qualified access must remain compiler-validated")
require_repository_contains(
    "tests/db_zone_stream_ownership_production_seal_tests.cpp"
    "CommentNamespaceProbe"
    "comment-separated namespace declarations must remain compiler-validated")
require_source_contains(
    "database/db_zone_stream_ownership.cpp"
    "for (StreamDelayInfo &delay : g_streamDelayArray)"
    "all delayed stream pointers must be scrubbed")
require_source_contains(
    "database/db_zone_stream_ownership.cpp"
    "for (StreamPosInfo &saved : g_streamPosStack)"
    "all saved stream cursors must be scrubbed")
require_source_contains(
    "database/db_zone_stream_ownership.cpp"
    "for (std::uint8_t *&position : g_streamPosArray)"
    "all block cursors must be scrubbed")
require_source_contains(
    "database/db_zone_stream_ownership.cpp"
    "g_aliasRegistry.Invalidate();
    g_directResolver.Invalidate();
    ScrubStreamScalars();
    ScrubStreamArrays();"
    "relocation state must invalidate before every stream global")
require_source_not_contains(
    "database/db_zone_stream_ownership.cpp"
    "Com_Error("
    "stream receipt/controller must remain report-free")
require_source_not_contains(
    "database/db_zone_stream_ownership.cpp"
    "memset("
    "stream teardown must explicitly enumerate pointer-bearing state")
require_source_contains(
    "database/db_relocation.cpp"
    "volatile std::uintptr_t *const resolvedAddress"
    "alias invalidation scrubs must remain observable before release")
require_source_contains(
    "database/db_relocation.cpp"
    "*resolvedAddress = 0;"
    "alias invalidation must overwrite published native addresses")
require_source_contains(
    "database/db_relocation.cpp"
    "std::vector<Record>{}.swap(records_);"
    "alias invalidation must release retained record capacity")
require_source_contains(
    "database/db_relocation.cpp"
    "return Status::GenerationExhausted;"
    "alias generation exhaustion must fail closed")
require_source_contains(
    "database/db_stream.cpp"
    "std::extent_v<decltype(XZoneMemory::blocks)>"
    "legacy stream loops must pin the canonical block count")
require_source_contains(
    "database/db_zone_stream_ownership.cpp"
    "db::relocation::kBlockCount == 9"
    "stream ownership storage must pin the legacy block count")
require_source_contains(
    "qcommon/com_error.h"
    "__attribute__((format(__printf__, 2, 3)))"
    "portable Com_Error declarations must retain format checking")
require_source_contains(
    "database/db_load.cpp"
    "DBAliasKind::MaterialVertexDeclaration"
    "material vertex declaration references must use completed-object provenance")
require_source_contains(
    "database/db_load.cpp"
    "DB_ConvertOffsetToAlias(
                (uint32_t*)&varMaterialPass->vertexDecl,
                DBAliasKind::MaterialVertexDeclaration,
                disk32::kMaterialVertexDeclarationBytes)"
    "shared material vertex declarations must resolve through exact typed provenance")
require_source_contains(
    "database/db_load.cpp"
    "disk32::kMaterialVertexDeclarationBytes"
    "material vertex declaration provenance must use its fixed disk32 extent")
require_source_contains(
    "database/db_load.cpp"
    "DB_ConvertOffsetToAlias(
                (uint32_t*)varMaterialTechniquePtr,
                DBAliasKind::MaterialTechnique,
                disk32::kMaterialTechniqueSchema)"
    "shared material techniques must resolve through exact typed provenance")
require_source_contains(
    "database/db_validation.h"
    "MaterialTechniqueDiskBytes("
    "material technique extents must be derived from their bounded pass count")
require_source_contains(
    "database/db_stream.cpp"
    "expectedKind == DBAliasKind::MaterialTechnique"
    "completed material techniques must recheck their serialized pass count")
require_source_contains(
    "database/db_stream.cpp"
    "Completed fast-file objects require DB_CompleteObject validation"
    "generic alias publication must reject completed material object kinds")
require_source_contains(
    "database/db_load.cpp"
    "D3D9ShaderBytecodeValid("
    "material shader bytecode must be structurally bounded before D3D creation")
require_source_contains(
    "database/db_load.cpp"
    "DB_ValidateStreamAddress("
    "material shader bytecode must cover a fully materialized stream span")
require_source_contains(
    "database/db_load.cpp"
    "DB_ConvertOffsetToAlias(
                (uint32_t*)varMaterialVertexShaderPtr,
                DBAliasKind::MaterialVertexShader,
                disk32::kMaterialVertexShaderBytes)"
    "shared material vertex shaders must resolve through stage-specific provenance")
require_source_contains(
    "database/db_load.cpp"
    "DB_ConvertOffsetToAlias(
                (uint32_t*)varMaterialPixelShaderPtr,
                DBAliasKind::MaterialPixelShader,
                disk32::kMaterialPixelShaderBytes)"
    "shared material pixel shaders must resolve through stage-specific provenance")
require_source_contains(
    "database/db_load.cpp"
    "Fast-file material pass mixes renderer shader variants"
    "material passes must pair vertex and pixel shaders for the same renderer")
require_source_contains(
    "database/db_load.cpp"
    "Fast-file material technique mixes renderer shader variants"
    "all material technique passes must target the same renderer")
require_source_contains(
    "database/db_load.cpp"
    "Fast-file material technique set mixes renderer variants"
    "all techniques in a set must target the same renderer")
require_source_contains(
    "database/db_load.cpp"
    "DB_ConvertOffsetToAlias(
                (uint32_t*)varMaterialTextureDefInfo,
                DBAliasKind::MaterialWater,
                disk32::kMaterialWaterBytes)"
    "shared material water must resolve through exact typed provenance")
require_source_contains(
    "database/db_load.cpp"
    "FiniteComplexArray(
            varwater_t->H0,"
    "material water amplitudes must be finite")
require_source_contains(
    "database/db_load.cpp"
    "FiniteNonnegativeFloatArray(
            varwater_t->wTerm,"
    "material water frequencies must be finite and nonnegative")
require_source_contains(
    "database/db_load.cpp"
    "FiniteFloatArray(water->codeConstant, 4)"
    "material water code constants must be finite")
require_source_contains(
    "gfx_d3d/r_water.cpp"
    "water->image->width != expectedImageWidth
        || water->image->height != expectedImageHeight"
    "loaded material water must target its mode-specific water image dimensions")
require_source_contains(
    "gfx_d3d/r_water.cpp"
    "Com_Error(ERR_DROP, \"Invalid material water image contract\")"
    "loaded material water image contract failures must reject publication")
require_source_contains(
    "database/db_stream.cpp"
    "case DBAliasKind::MaterialWater:
        if (metadata != disk32::kMaterialWaterBytes
            || materializedBytes != disk32::kMaterialWaterBytes)"
    "completed material water must use its fixed disk32 schema")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "water_t setup = {}"
    "raw material water must initialize serialized and runtime state")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "(const char*)material + texdef->u.waterDefOffset"
    "raw material water offsets must use byte arithmetic")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "(const char*)mtlRaw
                            + textureTableRaw[texIndex].u.waterDefOffset"
    "raw material construction must use byte offsets for water definitions")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "setup.writable.floatTime = kWaterInitialTime"
    "raw material water input must initialize runtime time state")
require_source_contains(
    "gfx_d3d/r_water_load_obj.cpp"
    "destination->writable.floatTime = kWaterInitialTime"
    "cached raw material water must scrub runtime time state")
require_source_contains(
    "database/db_load.cpp"
    "return Load_MaterialTextureDefInfo(0)"
    "material texture definitions must propagate nested payload failure")
require_source_contains(
    "database/db_load.cpp"
    "if (!Load_MaterialTextureDef(0))"
    "material texture arrays must propagate element failure")
require_source_contains(
    "database/db_load.cpp"
    "if (!Load_MaterialTextureDefArray(1, varMaterial->textureCount))"
    "materials must propagate texture-table failure")
require_source_contains(
    "database/db_load.cpp"
    "if (!Load_Material(1))"
    "material handles must reject failed child graphs before publication")
require_source_contains(
    "database/db_load.cpp"
    "if (varMaterialTextureDefInfo && *varMaterialTextureDefInfo)"
    "material water marking must test the payload rather than its holder")
require_source_contains(
    "database/db_load.cpp"
    "StrictlyIncreasingNameHashes(
            material->constantTable"
    "material constant tables must be strictly hash ordered")
require_source_contains(
    "database/db_load.cpp"
    "FiniteFloatArray(
                material->constantTable[constantIndex].literal"
    "material constants must contain only finite values")
require_source_contains(
    "database/db_load.cpp"
    "if (argument.type == 2)
                {
                    if (!db::validation::SortedNameHashContains(
                            material->textureTable,
                            material->textureCount,
                            argument.u.nameHash))"
    "named material sampler arguments must resolve within the texture table")
require_source_contains(
    "database/db_load.cpp"
    "else if (argument.type == 0 || argument.type == 6)
                {
                    if (!db::validation::SortedNameHashContains(
                            material->constantTable,
                            material->constantCount,
                            argument.u.nameHash))"
    "named material constant arguments must resolve within the constant table")
require_source_contains(
    "database/db_load.cpp"
    "MaterialTechniqueStateSpanValid("
    "material technique pass states must fit the state table")
require_source_contains(
    "database/db_load.cpp"
    "MaterialRemapSlotValid("
    "material technique remaps must preserve occupied pass spans")
require_source_contains(
    "database/db_load.cpp"
    "MaterialStateBitsDecodeSafe("
    "material render states must not index beyond D3D decode tables")
require_source_contains(
    "database/db_load.cpp"
    "!db::validation::FiniteFloatArray(
                        argument.u.literalConst,
                        4)"
    "inline material shader literals must be finite")
require_source_contains(
    "database/db_load.cpp"
    "varMaterial->stateBitsTable = nullptr"
    "present-empty material state tables must be canonicalized")
require_source_contains(
    "database/db_load.cpp"
    "varMaterial->constantTable = nullptr"
    "present-empty material constant tables must be canonicalized")
require_source_contains(
    "database/db_load.cpp"
    "material->info.sortKey >= 64"
    "material sort keys must fit the renderer's fixed tables")
require_source_contains(
    "database/db_load.cpp"
    "candidate->worldVertFormat != original->worldVertFormat"
    "loaded material remaps must preserve the world vertex format")
require_source_contains(
    "database/db_load.cpp"
    "if (!DB_ValidateMaterialNamedInputs(material, original)
        || (candidate != original
            && !DB_ValidateMaterialNamedInputs(material, candidate)))"
    "both original and initially remapped techniques must resolve named inputs")
require_source_contains(
    "gfx_d3d/r_material_override.cpp"
    "bool __cdecl Material_ValidateRemappedTechniqueSet"
    "runtime material technique remaps must use release-build validation")
require_source_contains(
    "gfx_d3d/r_material_override.cpp"
    "else if (!Material_ValidateRemappedTechniqueSet(techSet))
    {
        techSet->remappedTechniqueSet = techSet;"
    "invalid dynamic material remaps must fall back to the original set")
require_source_contains(
    "gfx_d3d/r_material_override.cpp"
    "(void)Material_ValidateRemappedTechniqueSet(techSet);"
    "initial renderer remaps must remain visible to material graph validation")
require_source_contains(
    "gfx_d3d/r_shade.cpp"
    "texDef = db::validation::FindSortedNameHash(
        material->textureTable,
        material->textureCount,
        arg->u.nameHash);
    if (!texDef)"
    "runtime named texture lookup must fail within its material table")
require_source_contains(
    "gfx_d3d/r_shade.cpp"
    "constDef = db::validation::FindSortedNameHash(
            material->constantTable,
            material->constantCount,
            arg->u.nameHash);
        if (!constDef)"
    "runtime named shader constants must fail within their material table")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "constDef = db::validation::FindSortedNameHash(
                mtl->constantTable,
                mtl->constantCount,
                arg->u.nameHash);
            if (!constDef)"
    "material sorting must bound named pixel-constant lookup")
require_source_not_contains(
    "gfx_d3d/r_shade.cpp"
    "while (texDef->nameHash !="
    "runtime texture lookup must not walk beyond the material table")
require_source_not_contains(
    "gfx_d3d/r_shade.cpp"
    "while (constDef->nameHash !="
    "runtime constant lookup must not walk beyond the material table")
require_source_not_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "while (constDef->nameHash !="
    "material sorting must not walk beyond the constant table")
require_source_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "if (texdef->semantic == TS_WATER_MAP)
            {
                Com_Error("
    "sky water rejection must occur before interpreting the texture union as an image")
require_source_not_contains(
    "gfx_d3d/r_bsp_load_obj.cpp"
    "texdef->u.image->name"
    "sky errors must not dereference a water payload as an image")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "mtl->textureTable[texIndex].semantic != TS_NORMAL_MAP
        || !mtl->textureTable[texIndex].u.image
        || !mtl->textureTable[texIndex].u.image->name"
    "normal-map inspection must validate the image union member before use")
require_source_contains(
    "gfx_d3d/r_shade.cpp"
    "R_UploadWaterTexture(texDef->u.water, floatTime);
        image = texDef->u.water->image;"
    "runtime water validation must precede image dereference")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "material->textureTable[texIndex].u.water =
                    Material_RegisterWaterImage("
    "raw water construction must use the active water union member")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "newTexEntry->u = v5->u;"
    "layered material texture payloads must copy the complete union")
require_source_contains(
    "gfx_d3d/r_material.cpp"
    "const Material *material = header.material;"
    "material picmip enumeration must use the typed asset-header member")
require_source_contains(
    "gfx_d3d/r_material.cpp"
    "material->textureCount && !material->textureTable"
    "material picmip enumeration must validate pointer/count consistency")
require_source_contains(
    "gfx_d3d/r_material.cpp"
    "DB_EnumXAssets(ASSET_TYPE_MATERIAL, Material_UpdatePicmipSingle, 0, 1);"
    "material picmip enumeration must use a native-width callback")
require_source_not_contains(
    "gfx_d3d/r_material.cpp"
    "header.xmodelPieces["
    "material picmip enumeration must not depend on 32-bit header layout tricks")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "worldVertFormat = s_worldVertFormatForLayerCount[layerCount - 1];"
    "layered world vertex formats must use their dedicated bounded table")
require_source_not_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "s_stateMapDstStencilBitGroup[9].stateBitsMask[layerCount + 1]"
    "layered world vertex formats must not read beyond an unrelated stencil table")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "worldVertFormatIndex >= MTL_WORLDVERT_COUNT"
    "layered material world vertex formats must fit the renderer table")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "normalMapCount > 3"
    "layered material normal maps must fit the world vertex declarations")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "CheckedMaterialTableCountSum("
    "layered texture and constant counts must reject uint8 overflow")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "layerMtl[layerIndex]->textureCount
                && !layerMtl[layerIndex]->textureTable"
    "layered materials must validate source texture pointer/count consistency")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "layerMtl[layerIndex]->constantCount
                && !layerMtl[layerIndex]->constantTable"
    "layered materials must validate source constant pointer/count consistency")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "return (hash0 > hash1) - (hash0 < hash1);"
    "material hash comparators must report equality")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "if (mtl0 == mtl1)
        return 0;"
    "material qsort comparison must report equality for identical entries")
require_source_not_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "return comparison < 0;"
    "material qsort comparison must return a three-way result")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "StrictlyIncreasingNameHashes(
            newMtl->textureTable"
    "layered material tables must reject ambiguous duplicate hashes")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "memset(material, 0, sizeof(*material));"
    "raw runtime materials must initialize absent tables to null")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "StrictlyIncreasingNameHashes(
            material->textureTable"
    "raw material tables must reject ambiguous duplicate hashes")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "static_cast<uint64_t>(sizeof(Material))"
    "layered allocation layout must use native object size with checked arithmetic")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "stateBitsTable + *stateBitsCount,
        stateBitsForPass + partialMatchCount,
        sizeof(*stateBitsTable)"
    "layered render-state merging must use row-pointer arithmetic")
require_source_not_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "&(*stateBitsTable)[2 *"
    "layered render-state merging must not index beyond an inner row")
require_source_contains(
    "database/database.h"
    "using DBEnumXAssetCallback = void(__cdecl *)(XAssetHeader, void *);"
    "database enumeration must use one native-width callback type")
require_source_contains(
    "database/db_registry.cpp"
    "DB_EnumXAssets_LoadObj(type, func, inData);"
    "load-object enumeration must preserve the canonical callback signature")
require_source_contains(
    "database/db_registry.cpp"
    "asset.material = header;"
    "load-object material enumeration must activate the typed header member")
require_source_contains(
    "database/db_registry.cpp"
    "asset.techniqueSet = header;"
    "load-object technique enumeration must activate the typed header member")
require_source_contains(
    "database/db_registry.cpp"
    "asset.image = header;"
    "load-object image enumeration must activate the typed header member")
require_source_contains(
    "database/db_registry.cpp"
    "asset.model = static_cast<XModel *>(fileData->data);"
    "load-object model enumeration must activate the typed header member")
require_source_contains(
    "database/db_registry.cpp"
    "asset.parts = static_cast<XAnimParts *>(fileData->data);"
    "load-object animation enumeration must activate the typed header member")
require_source_not_contains(
    "database/db_registry.cpp"
    "(void(*)(void *, void *))func"
    "load-object enumeration must not cast canonical callbacks")
require_source_contains(
    "universal/com_memory.cpp"
    "AssetList *assetList = static_cast<AssetList *>(data);"
    "Hunk asset enumeration must use the typed native-width context")
require_source_contains(
    "universal/com_memory.cpp"
    "assetList->assets[assetList->assetCount] = header;"
    "Hunk asset enumeration must use native array indexing")
require_source_contains(
    "universal/com_memory.cpp"
    "++assetList->assetCount;"
    "count-only Hunk asset enumeration must retain the required count")
require_source_not_contains(
    "universal/com_memory.cpp"
    "_DWORD *data"
    "Hunk asset enumeration must not reinterpret its context as 32-bit words")
require_source_not_contains(
    "universal/com_memory.cpp"
    "data[2]"
    "Hunk asset enumeration must not truncate the output pointer")
require_source_contains(
    "database/db_registry.cpp"
    "AssetOutputWriteAllowed(
                        assets != nullptr,
                        assetCount,
                        maxCount)"
    "fast-file asset enumeration must bound every output write")
require_source_contains(
    "database/db_registry.cpp"
    "Sys_UnlockRead(&db_hashCritSect);
    if (assets && assetCount > maxCount)"
    "asset capacity failure must occur after releasing the database read lock")
require_source_not_contains(
    "database/db_registry.cpp"
    "db_hashCritSect.readCount"
    "database readers must not bypass the shared atomic lock helpers")
require_source_not_contains(
    "database/db_registry.cpp"
    "db_hashCritSect.writeCount"
    "database writer-state queries must be atomic")
require_source_not_contains(
    "universal/dvar.cpp"
    "g_dvarCritSect.readCount"
    "dvar readers must not bypass the shared atomic lock helpers")
require_source_not_contains(
    "universal/dvar.cpp"
    "g_dvarCritSect.writeCount"
    "dvar writer-state polling must be atomic")
foreach(_native_fast_lock_token
    "Windows.h"
    "windows.h"
    "Interlocked"
    "reinterpret_cast<volatile long"
)
    require_source_not_contains(
        "qcommon/sys_sync.cpp"
        "${_native_fast_lock_token}"
        "the shared fast lock must use the fixed-width atomic boundary")
endforeach()
require_source_contains(
    "qcommon/sys_sync.cpp"
    "return Sys_AtomicLoad(count);"
    "fast-lock readers must use the canonical atomic load")
require_source_contains(
    "qcommon/sys_sync.cpp"
    "Sys_AtomicIncrement(&critSect->readCount);"
    "fast-lock reader acquisition must use the canonical atomic increment")
require_source_contains(
    "qcommon/sys_sync.cpp"
    "Sys_AtomicDecrement(&critSect->writeCount);"
    "fast-lock writer release must use the canonical atomic decrement")
require_source_not_contains(
    "universal/sys_atomic.h"
    "#include <Windows.h>"
    "the fixed-width atomic leaf must not import Windows SDK types or macros")
require_source_contains(
    "universal/sys_atomic.h"
    "#include <intrin.h>"
    "the MSVC atomic backend must use compiler intrinsics without Windows.h")
require_source_contains(
    "universal/sys_atomic.h"
    "static_assert(std::numeric_limits<unsigned long>::digits == 32"
    "the centralized MSVC intrinsic bridge must freeze its native word width")
require_source_contains(
    "universal/sys_atomic.h"
    "std::is_same_v<Word, std::int32_t>"
    "the atomic boundary must reject LP64 long storage")

foreach(_diagnostic_counter_source
    "cgame/cg_localents.cpp"
    "EffectsCore/fx_marks.cpp"
)
    require_source_contains(
        "${_diagnostic_counter_source}"
        "#include <universal/sys_atomic.h>"
        "diagnostic overlap counters must use the fixed-width atomic boundary")
    require_source_contains(
        "${_diagnostic_counter_source}"
        "Diagnostic overlap counter only; this does not serialize"
        "overlap counters must not be mistaken for mutual-exclusion locks")
    require_source_not_contains(
        "${_diagnostic_counter_source}"
        "Interlocked"
        "diagnostic overlap counters must not use native Windows atomics")
    require_source_not_matches(
        "${_diagnostic_counter_source}"
        "(^|[^A-Za-z0-9_])LONG([^A-Za-z0-9_]|$)"
        "diagnostic overlap counters must not use native Windows word types")
endforeach()
require_source_contains(
    "cgame/cg_localents.cpp"
    "volatile int32_t g_localEntThread;"
    "the local-entity overlap counter must remain an exact 32-bit atomic word")
require_source_match_count(
    "cgame/cg_localents.cpp"
    "if[ \t\r\n]*\\([ \t\r\n]*Sys_AtomicIncrement"
    2
    "each local-entity diagnostic entry must use the canonical increment")
require_source_match_count(
    "cgame/cg_localents.cpp"
    "if[ \t\r\n]*\\([ \t\r\n]*Sys_AtomicDecrement"
    2
    "each local-entity diagnostic entry must retain its balancing decrement")
require_source_contains(
    "EffectsCore/fx_marks.cpp"
    "static volatile int32_t g_markThread[1];"
    "the mark-generation overlap counter must remain an exact 32-bit atomic word")
require_source_match_count(
    "EffectsCore/fx_marks.cpp"
    "if[ \t\r\n]*\\([ \t\r\n]*Sys_AtomicIncrement"
    4
    "each mark-generation diagnostic entry must use the canonical increment")
require_source_match_count(
    "EffectsCore/fx_marks.cpp"
    "if[ \t\r\n]*\\([ \t\r\n]*Sys_AtomicDecrement"
    4
    "each mark-generation diagnostic entry must retain its balancing decrement")

foreach(_script_atomic_source
    "script/scr_stringlist.cpp"
    "script/scr_variable.cpp"
    "script/scr_string_atomic.h"
)
    foreach(_native_script_atomic_token
        "win32/win_local.h"
        "Interlocked"
    )
        require_source_not_contains(
            "${_script_atomic_source}"
            "${_native_script_atomic_token}"
            "script lifetime accounting must not depend on Windows atomics or bit macros")
    endforeach()
    require_source_not_matches(
        "${_script_atomic_source}"
        "#[ \t]*include[ \t]*[<\"]([Ww][Ii][Nn][Dd][Oo][Ww][Ss]\\.[Hh]|win32/win_local\\.h)[>\"]"
        "script lifetime accounting must not import Windows headers")
    require_source_not_matches(
        "${_script_atomic_source}"
        "HI(WORD|BYTE)[ \t\r\n]*\\("
        "script lifetime accounting must not use Windows bit macros")
    foreach(_native_script_atomic_type "LONG" "BOOL" "BYTE")
        require_source_not_matches(
            "${_script_atomic_source}"
            "(^|[^A-Za-z0-9_])${_native_script_atomic_type}([^A-Za-z0-9_]|$)"
            "script lifetime accounting must use fixed-width types")
    endforeach()
endforeach()

require_source_contains(
    "script/scr_string_atomic.h"
    "inline constexpr std::uint32_t kRefCountMask = UINT32_C(0x0000ffff);"
    "the packed string reference mask must remain explicit")
require_source_contains(
    "script/scr_string_atomic.h"
    "inline constexpr std::uint32_t kUserMask = UINT32_C(0x00ff0000);"
    "the packed string user mask must remain explicit")
require_source_contains(
    "script/scr_string_atomic.h"
    "inline constexpr std::uint32_t kByteLengthMask = UINT32_C(0xff000000);"
    "the packed string byte-length mask must remain explicit")
require_source_contains(
    "script/scr_string_atomic.h"
    "Sys_AtomicCompareExchange(value, desired, observed);"
    "packed string transitions must retry through the canonical atomic CAS")
require_source_contains(
    "script/scr_string_atomic.h"
    "if (refCount == 0 || refCount == kMaxRefCount)"
    "packed string additions must reject underflowed and overflowing lifetimes")
require_source_contains(
    "script/scr_string_atomic.h"
    "user != 0 && (User(observed) & user) != 0"
    "same-user claims must be decided from the CAS snapshot")
require_source_contains(
    "script/scr_string_atomic.h"
    "alreadyPresent && refCount == 1"
    "duplicate user transfers must not consume their last reference")
require_source_contains(
    "script/scr_string_atomic.h"
    "inline RemoveUserRefResult RemoveUserRef("
    "user-bit removal and its owned reference decrement must share one CAS")
require_source_contains(
    "script/scr_string_atomic.h"
    "inline RemoveRefAttempt TryRemoveRefUnlessLast("
    "the unlocked removal path must reserve zero publication for the hash-lock owner")
require_source_contains(
    "script/scr_string_atomic.h"
    "if (refCount == 1 && User(observed) != 0)"
    "generic removal must not consume the final user-owned reference")
require_source_contains(
    "script/scr_string_atomic.h"
    "enum class TransferUserResult : std::uint8_t"
    "user transfers must report duplicate-owner reference merges")
require_source_not_contains(
    "script/scr_string_atomic.h"
    "*value"
    "the packed helper must not dereference shared words outside Sys_Atomic")
require_source_not_contains(
    "script/scr_string_atomic.h"
    "value["
    "the packed helper must not index shared words outside Sys_Atomic")

require_source_contains(
    "script/scr_stringlist.h"
    "struct RefString;"
    "RefString must remain opaque outside its authenticated owner")
require_source_not_contains(
    "script/scr_stringlist.h"
    "volatile uint32_t data;"
    "the public header must not expose RefString packed ownership")
require_source_not_contains(
    "script/scr_stringlist.h"
    "SL_RefStringWord("
    "the public header must not expose raw packed-word access")
require_source_contains(
    "script/scr_stringlist.cpp"
    "volatile std::uint32_t data;"
    "the private RefString layout must retain one fixed-width packed word")
require_source_not_matches(
    "script/scr_stringlist.h"
    "refCount[ \t\r\n]*:[ \t\r\n]*16"
    "RefString readers must not alias its atomic word through bitfields")
require_source_contains(
    "script/scr_stringlist.cpp"
    "RUNTIME_OFFSET(RefString, data, 0, 0);"
    "the packed string word offset must remain frozen")
require_source_contains(
    "script/scr_stringlist.cpp"
    "RUNTIME_OFFSET(RefString, str, 4, 4);"
    "the script string payload offset must remain frozen")
require_source_match_count(
    "script/scr_stringlist.cpp"
    "return[ \t\r\n]+&refString[ \t\r\n]*->[ \t\r\n]*data;"
    2
    "the two private RefString word accessors must remain the sole packed owners")
require_source_contains(
    "script/scr_stringlist.h"
    "uint16_t refCount;"
    "serialized RefVector lifetime state must have an explicit fixed width")
require_source_contains(
    "script/scr_stringlist.h"
    "RUNTIME_SIZE(RefVector, 0x10, 0x10);"
    "the serialized vector header size must remain frozen")
foreach(_ref_vector_layout
    "RUNTIME_OFFSET(RefVector, refCount, 0, 0);"
    "RUNTIME_OFFSET(RefVector, user, 2, 2);"
    "RUNTIME_OFFSET(RefVector, byteLen, 3, 3);"
    "RUNTIME_OFFSET(RefVector, vec, 4, 4);"
)
    require_source_contains(
        "script/scr_stringlist.h"
        "${_ref_vector_layout}"
        "every serialized vector header field offset must remain frozen")
endforeach()
require_source_contains(
    "script/scr_stringlist.h"
    "RUNTIME_SIZE(scrStringDebugGlob_t, 0x40008, 0x40008);"
    "the script string debug layout must remain frozen")
require_source_contains(
    "script/scr_stringlist.h"
    "RUNTIME_OFFSET(scrStringDebugGlob_t, totalRefCount, 0x40000, 0x40000);"
    "the string debug aggregate counter offset must remain frozen")
require_source_contains(
    "script/scr_stringlist.h"
    "RUNTIME_OFFSET(scrStringDebugGlob_t, ignoreLeaks, 0x40004, 0x40004);"
    "the string debug leak-policy offset must remain frozen")
require_source_contains(
    "script/scr_main.h"
    "RUNTIME_SIZE(scrVarPub_t, 0x2007C, 0x200A0);"
    "the script public runtime layout must remain frozen on both pointer widths")
require_source_contains(
    "script/scr_main.h"
    "RUNTIME_OFFSET(scrVarPub_t, totalVectorRefCount, 0x20078, 0x20098);"
    "the vector reference counter offset must be frozen on 32- and 64-bit runtimes")
require_source_contains(
    "script/scr_main.h"
    "RUNTIME_SIZE(PrecacheEntry, 0x8, 0x8);"
    "the script precache record must retain its width-independent layout")

require_source_contains(
    "script/scr_stringlist.cpp"
    "scr_string_atomic::AddUserRef(SL_RefStringWord(refStr), userByte);"
    "user claims and their reference increment must be one packed transition")
require_source_contains(
    "script/scr_stringlist.cpp"
    "scr_string_atomic::TransferRefToUser(
			SL_RefStringWord(refStr), userByte);"
    "reference-to-user transfer races must be resolved by one packed transition")
require_source_contains(
    "script/scr_stringlist.cpp"
    "scr_string_atomic::RemoveUserRef(
					SL_RefStringWord(refStr), userByte);"
    "system shutdown must clear a user and its reference in one packed transition")
require_source_contains(
    "script/scr_stringlist.cpp"
    "result == scr_string_atomic::TransferUserResult::ReleasedDuplicate)
				SL_DebugRemoveRefNoReport(stringValue);"
    "merging an existing destination user must remove its duplicate debug reference")
require_source_contains(
    "script/scr_stringlist.cpp"
    "Com_Memset(&scrStringDebugGlobBuf, 0, sizeof(scrStringDebugGlobBuf));"
    "debug leak initialization must reset counters and ignoreLeaks together")
require_source_contains(
    "script/scr_stringlist.cpp"
    "return user ? (user & (user - 1)) == 0 : allowZero;"
    "each nonzero script-string user bit must own exactly one reference")
require_source_contains(
    "script/scr_stringlist.cpp"
    "SL_IsDebugOwnershipExactNoReport(stringValue, packed)"
    "complete ownership validation must authenticate debug references per ID")
require_source_contains(
    "script/scr_memorytree.h"
    "return &admission == &Canonical();"
    "allocator lease operations must require the exact canonical capability")
require_source_contains(
    "script/scr_memorytree.h"
    "static_assert(!std::is_trivially_copyable_v<MT_ValidationLeaseAdmission>);"
    "allocator lease capability must not be forgeable by bitwise construction")
require_source_contains(
    "script/scr_memorytree.h"
    "~MT_ValidationLeaseAdmission() noexcept = default;"
    "allocator lease capability must not register a shutdown destructor")
require_source_contains(
    "script/scr_memorytree.h"
    "MT_ValidationLeaseAdmission(
        const MT_ValidationLeaseAdmission &) noexcept
    {
    }"
    "allocator lease capability must remain non-trivially copyable without dynamic destruction")
foreach(_marker IN ITEMS
    "bool isCanonicalClearNoLock() const noexcept;"
    "void activateNoLock(uint64_t serial) noexcept;"
    "void poisonNoLock() noexcept;"
    "void clearNoLock() noexcept;")
    require_source_contains(
        "script/scr_memorytree.h"
        "${_marker}"
        "lease token mutation must remain private member authority")
endforeach()
require_source_not_contains(
    "script/scr_memorytree.h"
    "MT_ValidationLeaseAccess"
    "production headers must not expose a reproducible lease authority shim")
require_source_not_contains(
    "script/scr_memorytree.cpp"
    "MT_ValidationLeaseAccess"
    "allocator implementation must not define a reproducible lease authority shim")
file(READ
    "${SOURCE_ROOT}/src/script/scr_memorytree.cpp"
    _memory_lease_view_security_source)
extract_security_slice(
    _memory_lease_view_security_source
    "namespace
{
enum class MT_ValidationPolicy"
    "constexpr const char *mt_type_names[22]"
    _memory_lease_view_security_slice
    "translation-private allocator lease view")
require_security_slice_contains(
    _memory_lease_view_security_slice
    "struct MT_ValidationLeaseView final"
    "generic allocator helpers must receive only anonymous-namespace lease state")
require_source_contains(
    "script/scr_memorytree.cpp"
    "return MT_TryAllocIndexImpl(
        numBytes, type, outIndex, MT_ValidationPolicy::Leased, &view);"
    "leased allocation must delegate through the translation-private view")
require_source_contains(
    "script/scr_memorytree.cpp"
    "return MT_TryGetAllocationInfoImpl(
        nodeNum, outInfo, MT_ValidationPolicy::Leased, &view);"
    "leased query must delegate through the translation-private view")
require_source_contains(
    "script/scr_memorytree.cpp"
    "return MT_TryFreeIndexImpl(
        nodeNum, numBytes, MT_ValidationPolicy::Leased, &view);"
    "leased free must delegate through the translation-private view")
file(READ
    "${SOURCE_ROOT}/tests/script_memorytree_lease_api_seal_compile_tests.cpp"
    _memory_lease_seal_security_source)
foreach(_marker IN ITEMS
    "static_assert(!HasUngatedBegin<MT_ValidationLease>);"
    "static_assert(!HasUngatedFinish<MT_ValidationLease>);"
    "static_assert(!HasUngatedAllocation<MT_ValidationLease>);"
    "static_assert(!HasUngatedQuery<MT_ValidationLease>);"
    "static_assert(!HasUngatedFree<MT_ValidationLease>);"
    "static_assert(!HasCanonicalTestingAdmission<MT_ValidationLeaseAdmission>);"
    "static_assert(!HasInvalidTestingAdmission<MT_ValidationLeaseAdmission>);"
    "static_assert(!HasLeaseAuthenticationSetter<MT_ValidationLease>);"
    "static_assert(!HasLeaseMutationSetter<MT_ValidationLease>);"
    "static_assert(!HasLeaseCanonicalInspection<MT_ValidationLease>);"
    "static_assert(!HasLeaseActivationAuthority<MT_ValidationLease>);"
    "static_assert(!HasLeasePoisonAuthority<MT_ValidationLease>);"
    "static_assert(!HasLeaseClearAuthority<MT_ValidationLease>);"
    "static_assert(\n    !HasBatchAuthenticationSetter<script_string::OwnershipBatch>);"
    "static_assert(!HasBatchLeaseAccessor<script_string::OwnershipBatch>);"
    "static_assert(!HasBatchActivationSetter<script_string::OwnershipBatch>);"
    "static_assert(!HasBatchOperationSetter<script_string::OwnershipBatch>);"
    "static_assert(\n    !HasBatchMemoryMutationSetter<script_string::OwnershipBatch>);")
    string(FIND
        "${_memory_lease_seal_security_source}"
        "${_marker}"
        _memory_lease_seal_authority_position)
    if(_memory_lease_seal_authority_position EQUAL -1)
        message(FATAL_ERROR
            "Missing security invariant (production code must not reach private no-lock lease authority)")
    endif()
endforeach()
require_source_not_contains(
    "script/scr_string_transaction.h"
    "friend struct OwnershipBatchAccess;"
    "namespace-visible code must not be able to define a batch authority friend")
require_source_not_contains(
    "script/scr_stringlist.cpp"
    "OwnershipBatchAccess"
    "namespace-visible batch authority shim must remain removed")
require_source_match_count(
    "script/scr_memorytree.cpp"
    "MT_ValidationLeaseAdmission::Authenticates\\(admission\\)"
    5
    "every allocator lease entry must authenticate its operation capability")
file(READ
    "${SOURCE_ROOT}/src/script/scr_stringlist.cpp"
    _ownership_capability_security_source)
extract_security_slice(
    _ownership_capability_security_source
    "MT_AllocIndexStatus SL_TryAllocateStringMemoryNoReport("
    "MT_FreeIndexStatus SL_TryFreeStringMemoryNoReport("
    _leased_allocation_security_slice
    "leased string allocation capability flow")
extract_security_slice(
    _ownership_capability_security_source
    "MT_FreeIndexStatus SL_TryFreeStringMemoryNoReport("
    "SL_InternStatus SL_TryInternStringOfSizeWithValidation("
    _leased_free_security_slice
    "leased string release capability flow")
extract_security_slice(
    _ownership_capability_security_source
    "MT_AllocationInfoStatus SL_TryGetAllocationInfoForScopeNoReport("
    "bool SL_TryGetAllocatedStringByteCountForScopeNoReport("
    _leased_query_security_slice
    "leased string query capability flow")
foreach(_slice IN ITEMS
    _leased_allocation_security_slice
    _leased_free_security_slice
    _leased_query_security_slice)
    require_security_slice_ordered(
        ${_slice}
        "SL_IsValidationAuthorityWellFormed("
        "case SL_ValidationScope::Leased:"
        "leased string operations must validate the capability tuple before dispatch")
    require_security_slice_contains(
        ${_slice}
        "*admission);"
        "leased string operations must pass the private exact-address capability")
endforeach()
require_security_slice_contains(
    _leased_allocation_security_slice
    "MT_TryAllocIndexLeased("
    "leased string allocation must call the gated allocator entry")
require_security_slice_contains(
    _leased_free_security_slice
    "MT_TryFreeIndexLeased("
    "leased string release must call the gated allocator entry")
require_security_slice_contains(
    _leased_query_security_slice
    "MT_TryGetAllocationInfoLeased("
    "leased string query must call the gated allocator entry")
require_source_contains(
    "script/scr_stringlist.cpp"
    "Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
			return SL_InternStatus::PrimaryTableCapacityNoChange;"
    "primary string-table exhaustion must release the hash lock and return without reporting")
require_source_contains(
    "script/scr_stringlist.cpp"
    "Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
				return SL_InternStatus::RelocatedTableCapacityNoChange;"
    "relocated string-table exhaustion must release the hash lock and return without reporting")
require_source_contains(
    "script/scr_stringlist.cpp"
    "case SL_InternStatus::PrimaryTableCapacityNoChange:
		Com_Error(ERR_DROP, \"exceeded maximum number of script strings (increase STRINGLIST_SIZE)\");"
    "the legacy wrapper must retain the primary-table exhaustion report")
require_source_contains(
    "script/scr_stringlist.cpp"
    "case SL_InternStatus::RelocatedTableCapacityNoChange:
		Com_Error(ERR_DROP, \"exceeded maximum number of script strings\");"
    "the legacy wrapper must retain the relocated-table exhaustion report")
file(READ
    "${SOURCE_ROOT}/src/script/scr_stringlist.cpp"
    _legacy_string_release_security_source)
extract_security_slice(
    _legacy_string_release_security_source
    "void SL_ShutdownSystem(uint32_t user)"
    "int SL_IsLowercaseString("
    _legacy_shutdown_security_slice
    "legacy shutdown physical-entry sweep")
require_security_slice_ordered(
    _legacy_shutdown_security_slice
    "SL_IsCompleteSystemSweepStateValidNoReport()"
    "for (uint32_t owningHash = 1;"
    "shutdown must authenticate the complete table before mutation")
require_security_slice_ordered(
    _legacy_shutdown_security_slice
    "SL_TryGetAllocatedStringByteCountForScopeNoReport("
    "scr_string_atomic::RemoveUserRef("
    "shutdown must authenticate allocation/debug ownership before removal")
require_security_slice_contains(
    _legacy_shutdown_security_slice
    "SL_TryFreeSystemSweepEntryNoReport("
    "shutdown must use the authenticated constant-work chain splice")
forbid_security_slice_contains(
    _legacy_shutdown_security_slice
    "SL_FreeString("
    "shutdown must not rewalk each collision chain during final free")
extract_security_slice(
    _legacy_string_release_security_source
    "void SL_TransferSystem(uint32_t from, uint32_t to)"
    "uint32_t SL_GetString_("
    _legacy_transfer_security_slice
    "legacy transfer physical-entry sweep")
require_security_slice_ordered(
    _legacy_transfer_security_slice
    "SL_IsCompleteSystemSweepStateValidNoReport()"
    "for (uint32_t hash = 1;"
    "transfer must authenticate the complete table before mutation")
require_security_slice_ordered(
    _legacy_transfer_security_slice
    "SL_TryGetAllocatedStringByteCountForScopeNoReport("
    "scr_string_atomic::TransferUser("
    "transfer must authenticate allocation/debug ownership before mutation")
extract_security_slice(
    _legacy_string_release_security_source
    "const scr_string_atomic::TransferUserResult result ="
    "if (invalidTransition)"
    _legacy_transfer_commit_security_slice
    "legacy transfer committed transition")
require_security_slice_ordered(
    _legacy_transfer_commit_security_slice
    "SL_DebugRemoveRefNoReport(stringValue);"
    "Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);"
    "duplicate transfer debug accounting must remain report-free under lock")
require_source_contains(
    "script/scr_stringlist.cpp"
    "return (entry.status_next & ~kHashEntryBits) == 0;"
    "hash entries must reject reserved high-bit corruption")
require_source_not_contains(
    "script/scr_stringlist.cpp"
    "memset(sl_hashChainVisited"
    "bounded hash validation must not clear whole-table entry scratch")
require_source_not_contains(
    "script/scr_stringlist.cpp"
    "memset(sl_stringIdVisited"
    "bounded hash validation must not clear whole-table string-ID scratch")
extract_security_slice(
    _legacy_string_release_security_source
    "namespace
{
bool SL_TryRemoveRefToStringLockedNoReport("
    "void __cdecl SL_AddUser("
    _legacy_string_release_security_slice
    "legacy script-string release")
require_security_slice_ordered(
    _legacy_string_release_security_slice
    "SL_IsDebugOwnershipExactNoReport(stringValue, packed);"
    "SL_DebugRemoveRefNoReport(stringValue);"
    "debug ownership must be authenticated before decrement")
require_security_slice_ordered(
    _legacy_string_release_security_slice
    "SL_DebugRemoveRefNoReport(stringValue);"
    "SL_FreeString(stringValue, refStr, byteCount);"
    "old debug ownership must be removed before slot reuse")
require_security_slice_ordered(
    _legacy_string_release_security_slice
    "SL_FreeString(stringValue, refStr, byteCount);"
    "Sys_AtomicStore(SL_RefStringWord(refStr), packed);"
    "failed final free must restore the packed ownership word")
require_security_slice_ordered(
    _legacy_string_release_security_slice
    "Sys_AtomicStore(SL_RefStringWord(refStr), packed);"
    "SL_DebugAddRefNoReport(stringValue);"
    "failed final free must restore debug ownership")
require_source_ordered(
    "script/scr_stringlist.cpp"
    "Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);"
    "SL_TryRemoveRefToStringLockedNoReport("
    "the legacy decrement must own the hash lock before authenticated removal")
require_source_not_matches(
    "script/scr_stringlist.cpp"
    "(refStr|refString)[ \t\r\n]*->[ \t\r\n]*(refCount|user|byteLen)"
    "packed RefString fields must be decoded from one atomic snapshot")
require_source_match_count(
    "script/scr_stringlist.cpp"
    "->[ \t\r\n]*data"
    2
    "only private accessors may lexically touch RefString packed data")
require_source_not_contains(
    "script/scr_stringlist.h"
    "SL_AddUserInternal("
    "opaque RefString pointers must not authorize public mutation")

extract_security_slice(
    _legacy_string_release_security_source
    "static bool SL_AddUserInternal("
    "void SL_AddRefToString(uint32_t stringValue)"
    _legacy_opaque_user_mutator_security_slice
    "opaque RefString user mutation")
require_security_slice_ordered(
    _legacy_opaque_user_mutator_security_slice
    "const bool addressValid = refStr != nullptr"
    "SL_TryResolveLegacyTransferTargetNoReport("
    "opaque RefString mutation must range-check before exact resolution")
require_security_slice_ordered(
    _legacy_opaque_user_mutator_security_slice
    "&& resolvedRefString == refStr"
    "&& SL_TryAddUserInternalNoReport(resolvedRefString, user);"
    "opaque RefString mutation must prove exact identity before atomic access")
forbid_security_slice_contains(
    _legacy_opaque_user_mutator_security_slice
    "SL_RefStringWord(refStr)"
    "opaque RefString mutation must not directly access caller storage")

extract_security_slice(
    _legacy_string_release_security_source
    "void SL_AddRefToString(uint32_t stringValue)"
    "void SL_CheckExists(uint32_t stringValue)"
    _legacy_add_ref_security_slice
    "legacy ID ref mutation")
require_security_slice_ordered(
    _legacy_add_ref_security_slice
    "SL_TryResolveLegacyTransferTargetNoReport("
    "SL_TryAddUserInternalNoReport(refStr, 0)"
    "legacy ID ref mutation must authenticate before atomic access")

extract_security_slice(
    _legacy_string_release_security_source
    "void __cdecl SL_AddUser(uint32_t stringValue, uint32_t user)"
    "void __cdecl Scr_SetString(uint16_t *to, uint32_t from)"
    _legacy_add_user_security_slice
    "legacy ID user mutation")
require_security_slice_ordered(
    _legacy_add_user_security_slice
    "SL_TryResolveLegacyTransferTargetNoReport("
    "SL_TryAddUserInternalNoReport(refString, user)"
    "legacy ID user mutation must authenticate before atomic access")

extract_security_slice(
    _legacy_string_release_security_source
    "uint32_t __cdecl SL_ConvertToLowercase("
    "void __cdecl CreateCanonicalFilename("
    _legacy_lowercase_security_slice
    "legacy lowercase conversion")
require_security_slice_ordered(
    _legacy_lowercase_security_slice
    "SL_TryResolveLegacyTransferTargetNoReport("
    "static_cast<unsigned char>(refString->str[index])"
    "lowercase conversion must authenticate before bounded ctype access")

extract_security_slice(
    _legacy_string_release_security_source
    "static uint32_t GetLowercaseStringOfSize("
    "uint32_t SL_GetLowercaseString_("
    _legacy_lowercase_intern_security_slice
    "legacy lowercase intern")
require_security_slice_contains(
    _legacy_lowercase_intern_security_slice
    "static_cast<unsigned char>(str[i])"
    "lowercase intern must use unsigned-char ctype input")

extract_security_slice(
    _legacy_string_release_security_source
    "uint32_t SL_FindLowercaseString(const char* str)"
    "bool SL_TryRemoveRefToStringLockedNoReport("
    _legacy_lowercase_find_security_slice
    "legacy lowercase lookup")
require_security_slice_contains(
    _legacy_lowercase_find_security_slice
    "static_cast<unsigned char>(str[i])"
    "lowercase lookup must use unsigned-char ctype input")

extract_security_slice(
    _legacy_string_release_security_source
    "void __cdecl CreateCanonicalFilename("
    "uint32_t __cdecl Scr_CreateCanonicalFilename("
    _legacy_canonical_filename_security_slice
    "canonical filename folding")
require_security_slice_contains(
    _legacy_canonical_filename_security_slice
    "c = static_cast<unsigned char>(*filename++);"
    "canonical filename must read unsigned input bytes")
require_security_slice_contains(
    _legacy_canonical_filename_security_slice
    "static_cast<unsigned char>(c)"
    "canonical filename must use unsigned-char ctype input")

require_all_occurrences_wrapped(
    "script/scr_stringlist.cpp"
    "scrStringDebugGlob[ \t\r\n]*->[ \t\r\n]*refCount"
    "Sys_Atomic(Load|Increment|Decrement)[ \t\r\n]*\\([ \t\r\n]*&scrStringDebugGlob[ \t\r\n]*->[ \t\r\n]*refCount[ \t\r\n]*\\[[^]]+\\]"
    "string debug reference counters must use canonical atomic operations")
require_all_occurrences_wrapped(
    "script/scr_stringlist.cpp"
    "scrStringDebugGlob[ \t\r\n]*->[ \t\r\n]*totalRefCount"
    "Sys_Atomic(Load|Increment|Decrement)[ \t\r\n]*\\([ \t\r\n]*&scrStringDebugGlob[ \t\r\n]*->[ \t\r\n]*totalRefCount"
    "the aggregate string debug count must use canonical atomic operations")
require_all_occurrences_wrapped(
    "script/scr_variable.cpp"
    "scrStringDebugGlob[ \t\r\n]*->[ \t\r\n]*refCount"
    "Sys_Atomic(Load|Increment|Decrement)[ \t\r\n]*\\([ \t\r\n]*&scrStringDebugGlob[ \t\r\n]*->[ \t\r\n]*refCount[ \t\r\n]*\\[[^]]+\\]"
    "vector debug reference counters must use canonical atomic operations")
require_all_occurrences_wrapped(
    "script/scr_variable.cpp"
    "scrVarPub[ \t\r\n]*\\.[ \t\r\n]*totalVectorRefCount"
    "Sys_Atomic(Load|Store|Increment|Decrement)[ \t\r\n]*\\([ \t\r\n]*&scrVarPub[ \t\r\n]*\\.[ \t\r\n]*totalVectorRefCount"
    "the global vector lifetime count must use canonical atomic operations")
require_source_contains(
    "script/scr_variable.cpp"
    "refVector->refCount < (std::numeric_limits<uint16_t>::max)()"
    "serialized vector reference increments must reject 16-bit overflow")
require_source_contains(
    "script/scr_variable.cpp"
    "offset % nodeSize != 0 || offset / nodeSize >= SL_MAX_STRING_INDEX"
    "vector debug accounting must validate its memory-tree index")
foreach(_legacy_vector_header_access
    "_BYTE*)vectorValue"
    "_WORD*)vectorValue"
    "vectorValue - 1"
)
    require_source_not_contains(
        "script/scr_variable.cpp"
        "${_legacy_vector_header_access}"
        "RefVector headers must not use untyped pointer arithmetic")
endforeach()
require_source_contains(
    "script/scr_variable.cpp"
    "Sys_AtomicStore(&scrVarPub.totalVectorRefCount, 0u);"
    "vector lifetime initialization must use the canonical atomic store")
require_source_contains(
    "script/scr_variable.cpp"
    "Sys_AtomicDecrement(&scrVarPub.totalVectorRefCount);"
    "vector lifetime release must use the canonical atomic decrement")
require_source_contains(
    "script/scr_variable.cpp"
    "Sys_AtomicIncrement(&scrStringDebugGlob->refCount[debugIndex]);"
    "vector allocation debug accounting must use the canonical atomic increment")

foreach(_xanim_atomic_source
    "xanim/xanim.cpp"
    "xanim/xanim_calc.cpp"
    "xanim/dobj.cpp"
    "xanim/dobj_utils.cpp"
)
    require_source_contains(
        "${_xanim_atomic_source}"
        "#include <universal/sys_atomic.h>"
        "XAnim and DObj synchronization must use the fixed-width atomic boundary")
    foreach(_native_xanim_atomic_token
        "Interlocked"
        "win32/win_local.h"
        "Windows.h"
        "windows.h"
    )
        require_source_not_contains(
            "${_xanim_atomic_source}"
            "${_native_xanim_atomic_token}"
            "XAnim and DObj synchronization must remain platform-independent")
    endforeach()
endforeach()

require_source_contains(
    "xanim/xanim.h"
    "volatile int32_t calcRefCount;"
    "the animation calculation counter must remain an exact 32-bit atomic word")
require_source_contains(
    "xanim/xanim.h"
    "volatile int32_t modifyRefCount;"
    "the animation modification counter must remain an exact 32-bit atomic word")
foreach(_xanim_tree_layout
    "RUNTIME_SIZE(XAnimTree_s, 0x14, 0x18);"
    "RUNTIME_OFFSET(XAnimTree_s, anims, 0x0, 0x0);"
    "RUNTIME_OFFSET(XAnimTree_s, info_usage, 0x4, 0x8);"
    "RUNTIME_OFFSET(XAnimTree_s, calcRefCount, 0x8, 0xC);"
    "RUNTIME_OFFSET(XAnimTree_s, modifyRefCount, 0xC, 0x10);"
    "RUNTIME_OFFSET(XAnimTree_s, children, 0x10, 0x14);"
    "static_assert(std::is_same_v<decltype(XAnimTree_s::calcRefCount), volatile int32_t>);"
    "static_assert(std::is_same_v<decltype(XAnimTree_s::modifyRefCount), volatile int32_t>);"
    "static_assert(std::is_standard_layout_v<XAnimTree_s>);"
)
    require_source_contains(
        "xanim/xanim.h"
        "${_xanim_tree_layout}"
        "the XAnim tree layout must remain frozen on both pointer widths")
endforeach()

foreach(_xanim_counter_source "xanim/xanim.cpp" "xanim/xanim_calc.cpp")
    foreach(_xanim_counter "calcRefCount" "modifyRefCount")
        require_all_occurrences_wrapped(
            "${_xanim_counter_source}"
            "->[ \t\r\n]*${_xanim_counter}"
            "Sys_Atomic(Load|Store|Increment|Decrement|FetchAdd|Exchange|CompareExchange)[ \t\r\n]*\\([ \t\r\n]*&[^,;()]*->[ \t\r\n]*${_xanim_counter}"
            "every XAnim overlap-counter access must use a canonical atomic operation")
    endforeach()
endforeach()
require_source_contains(
    "xanim/xanim.cpp"
    "iassert(tree);\n    iassert(infoIndex && (infoIndex < 4096));"
    "XAnimFreeInfo must validate its tree before touching overlap counters")
require_source_ordered(
    "xanim/xanim.cpp"
    "iassert(tree);\n    iassert(infoIndex && (infoIndex < 4096));"
    "Sys_AtomicIncrement(&tree->modifyRefCount);"
    "XAnimFreeInfo must validate its tree before incrementing its modification counter")

require_source_contains(
    "xanim/dobj.h"
    "volatile uint32_t locked;"
    "the DObj lock must remain an exact 32-bit atomic word")
foreach(_dobj_layout
    "RUNTIME_SIZE(DObj_s, 0x64, 0x78);"
    "RUNTIME_OFFSET(DObj_s, locked, 0x10, 0x14);"
    "RUNTIME_OFFSET(DObj_s, skel, 0x14, 0x18);"
    "RUNTIME_OFFSET(DObj_s, models, 0x60, 0x70);"
    "static_assert(std::is_same_v<decltype(DObj_s::locked), volatile uint32_t>);"
    "static_assert(std::is_standard_layout_v<DObj_s>);"
)
    require_source_contains(
        "xanim/dobj.h"
        "${_dobj_layout}"
        "the DObj runtime layout must remain frozen on both pointer widths")
endforeach()
foreach(_dobj_atomic_source "xanim/dobj.cpp" "xanim/dobj_utils.cpp")
    require_all_occurrences_wrapped(
        "${_dobj_atomic_source}"
        "->[ \t\r\n]*locked"
        "Sys_Atomic(Load|Store|Increment|Decrement|FetchAdd|Exchange|CompareExchange)[ \t\r\n]*\\([ \t\r\n]*&[^,;()]*->[ \t\r\n]*locked"
        "every DObj lock access must use a canonical atomic operation")
endforeach()
require_source_contains(
    "xanim/dobj_utils.cpp"
    "Sys_AtomicCompareExchange(&obj->locked, 1u, 0u)"
    "DObj lock acquisition must use a fixed-width compare-exchange")
require_source_contains(
    "xanim/dobj_utils.cpp"
    "Sys_AtomicExchange(&obj->locked, 0u)"
    "DObj unlock must atomically validate and release its ownership word")
require_source_contains(
    "xanim/dobj_utils.cpp"
    "Sys_Sleep(0);"
    "contended DObj locking must yield instead of spinning without pause")

require_source_contains(
    "xanim/dobj.cpp"
    "DObjLock(mutableFrom);"
    "DObj cloning must hold the source lock while taking its snapshot")
require_source_contains(
    "xanim/dobj.cpp"
    "DObjUnlock(mutableFrom);\n    if (snapshotError)"
    "DObj cloning must release its source lock before reporting snapshot failures")
require_source_ordered(
    "xanim/dobj.cpp"
    "DObjUnlock(mutableFrom);\n    if (snapshotError)"
    "preparedDuplicateParts = SL_GetStringOfSize("
    "DObj cloning must release its source lock before fallible string interning")
require_source_not_contains(
    "xanim/dobj.cpp"
    "SL_AddRefToString(duplicateParts)"
    "DObj cloning must not call a longjmp-capable ref increment while locked")
foreach(_whole_dobj_copy_pattern
    "mem(cpy|move)[ \t\r\n]*\\([ \t\r\n]*obj[ \t\r\n]*,[ \t\r\n]*from([^A-Za-z0-9_]|$)"
    "mem(cpy|move)[ \t\r\n]*\\([^;]*sizeof[ \t\r\n]*\\([ \t\r\n]*DObj_s[ \t\r\n]*\\)"
    "\\*obj[ \t\r\n]*=[ \t\r\n]*\\*from"
    "DObj_s[ \t\r\n]+[A-Za-z_][A-Za-z0-9_]*[ \t\r\n]*=[ \t\r\n]*\\*from"
)
    require_source_not_matches(
        "xanim/dobj.cpp"
        "${_whole_dobj_copy_pattern}"
        "live DObj snapshots must never copy the lock word")
endforeach()

foreach(_saved_dobj_layout
    "RUNTIME_SIZE(SavedDObj, 0x60, 0x68);"
    "RUNTIME_OFFSET(SavedDObj, models, 0x40, 0x40);"
    "RUNTIME_OFFSET(SavedDObj, tree, 0x4C, 0x50);"
    "RUNTIME_OFFSET(SavedDObj, hidePartBits, 0x50, 0x58);"
)
    require_source_contains(
        "xanim/dobj.cpp"
        "${_saved_dobj_layout}"
        "the lock-free archived DObj record must have an exact native-width layout")
endforeach()
require_source_match_count(
    "xanim/dobj.cpp"
    "SavedDObj[ \t\r\n]+savedObj[ \t\r\n]*\\{[ \t\r\n]*\\}"
    2
    "archive snapshots must be zero-initialized before exact-size copies")
require_source_match_count(
    "xanim/dobj.cpp"
    "memcpy[ \t\r\n]*\\([^;]*savedObj[^;]*\\)"
    2
    "only the exact archive and unarchive snapshot copies may involve SavedDObj")
require_source_match_count(
    "xanim/dobj.cpp"
    "memcpy[ \t\r\n]*\\([ \t\r\n]*obj[ \t\r\n]*,[ \t\r\n]*&savedObj[ \t\r\n]*,[ \t\r\n]*sizeof[ \t\r\n]*\\([ \t\r\n]*savedObj[ \t\r\n]*\\)[ \t\r\n]*\\)"
    1
    "DObjArchive must copy exactly the initialized SavedDObj record")
require_source_match_count(
    "xanim/dobj.cpp"
    "memcpy[ \t\r\n]*\\([ \t\r\n]*&savedObj[ \t\r\n]*,[ \t\r\n]*obj[ \t\r\n]*,[ \t\r\n]*sizeof[ \t\r\n]*\\([ \t\r\n]*savedObj[ \t\r\n]*\\)[ \t\r\n]*\\)"
    1
    "DObjUnarchive must read exactly the SavedDObj record")
require_source_not_matches(
    "xanim/dobj.cpp"
    "sizeof[ \t\r\n]*\\([ \t\r\n]*DObj_s[ \t\r\n]*\\)[ \t\r\n]*-[ \t\r\n]*sizeof"
    "archive copies must not derive disk size from the native DObj layout")

require_source_contains(
    "xanim/dobj.cpp"
    "numModels * (sizeof(XModel *) + sizeof(uint8_t))"
    "DObj model storage must account for native pointer width")
require_source_contains(
    "xanim/dobj.cpp"
    "if constexpr (alignof(XModel *) > alignof(MemoryNode))"
    "DObj model storage must compensate when memory-tree nodes under-align pointers")
require_source_contains(
    "xanim/dobj.cpp"
    "reinterpret_cast<uintptr_t>(models) % alignof(XModel *) == 0"
    "DObj clone allocation must verify native pointer alignment")
require_source_contains(
    "xanim/dobj.cpp"
    "memcpy(models, modelData, modelDataSize);"
    "DObj cloning must copy only pointer-width model data, not allocator padding")

require_source_not_contains(
    "cgame/cg_modelpreviewer.h"
    "char objBuf[100]"
    "model-preview DObj storage must not retain the x86-only object size")
require_source_match_count(
    "cgame/cg_modelpreviewer.h"
    "alignas[ \t\r\n]*\\([ \t\r\n]*DObj_s[ \t\r\n]*\\)[ \t\r\n]*char[ \t\r\n]+objBuf[ \t\r\n]*\\[[ \t\r\n]*sizeof[ \t\r\n]*\\([ \t\r\n]*DObj_s[ \t\r\n]*\\)[ \t\r\n]*\\]"
    2
    "both model-preview DObj buffers must use native size and alignment")
require_source_match_count(
    "cgame/cg_modelpreviewer.cpp"
    "const[ \t\r\n]+uint64_t[ \t\r\n]+allocationBytes[ \t\r\n]*="
    2
    "model-preview pointer table sizes must be computed without 32-bit overflow")
require_source_match_count(
    "cgame/cg_modelpreviewer.cpp"
    "allocationBytes[ \t\r\n]*>[ \t\r\n]*UINT32_MAX"
    2
    "both model-preview pointer tables must reject truncating Hunk sizes")
require_source_match_count(
    "cgame/cg_modelpreviewer.cpp"
    "static_cast<uint32_t>[ \t\r\n]*\\([ \t\r\n]*allocationBytes[ \t\r\n]*\\)"
    2
    "both model-preview pointer tables must narrow only after validation")
require_source_match_count(
    "cgame/cg_modelpreviewer.cpp"
    "alignof[ \t\r\n]*\\([ \t\r\n]*const[ \t\r\n]+char[ \t\r\n]*\\*[ \t\r\n]*\\)"
    2
    "both model-preview pointer tables must use native pointer alignment")
require_source_contains(
    "cgame/cg_modelpreviewer.cpp"
    "sizeof(*g_mdlprv.system.modelNames)\n            * (static_cast<uint64_t>(g_mdlprv.system.modelCount) + 2)"
    "model-preview model enumeration must allocate a native-width pointer table")
require_source_contains(
    "cgame/cg_modelpreviewer.cpp"
    "sizeof(*g_mdlprv.system.animNames)\n            * (static_cast<uint64_t>(g_mdlprv.system.animCount) + 2)"
    "model-preview animation enumeration must allocate a native-width pointer table")

foreach(_xanim_table_layout
    "RUNTIME_SIZE(XAnimEntry, 0x8, 0x10);"
    "RUNTIME_OFFSET(XAnimEntry, parts, 0x4, 0x8);"
    "RUNTIME_SIZE(XAnim_s, 0x14, 0x28);"
    "RUNTIME_OFFSET(XAnim_s, debugName, 0x0, 0x0);"
    "RUNTIME_OFFSET(XAnim_s, size, 0x4, 0x8);"
    "RUNTIME_OFFSET(XAnim_s, debugAnimNames, 0x8, 0x10);"
    "RUNTIME_OFFSET(XAnim_s, entries, 0xC, 0x18);"
)
    require_source_contains(
        "xanim/xanim.h"
        "${_xanim_table_layout}"
        "variable-sized XAnim table layouts must remain native-width exact")
endforeach()
require_source_contains(
    "xanim/xanim.cpp"
    "const std::size_t headerSize = offsetof(XAnim_s, entries);"
    "XAnim table allocation must derive its header from the native layout")
require_source_contains(
    "xanim/xanim.cpp"
    "size > (maxSize - headerSize) / sizeof(XAnimEntry)"
    "XAnim table allocation must reject native-size overflow")
require_source_contains(
    "xanim/xanim.cpp"
    "static_cast<uint64_t>(size) * sizeof(*anims->debugAnimNames)"
    "XAnim debug pointer tables must use native pointer width")
foreach(_legacy_xanim_table_formula
    "8 * size + 12"
    "4 * size"
    "8 * animIndex"
)
    require_source_not_contains(
        "xanim/xanim.cpp"
        "${_legacy_xanim_table_formula}"
        "XAnim tables must not retain x86-only element arithmetic")
endforeach()
require_source_contains(
    "xanim/xanim.cpp"
    "reinterpret_cast<uintptr_t>(tree) % alignof(XAnimTree_s) != 0"
    "XAnim tree allocation must validate native alignment")
require_source_contains(
    "cgame/cg_modelpreviewer.cpp"
    "Hunk_UserAlloc(v2, size, alignof(XAnimTree_s))"
    "model-preview XAnim allocations must request native alignment")
require_source_contains(
    "cgame/cg_main.cpp"
    "alignof(XAnimTree_s)"
    "SP client XAnim allocations must request native alignment")
require_source_contains(
    "universal/com_memory.cpp"
    "Hunk_AllocAlign(size, alignof(void *), \"XAnimPrecache\", 11)"
    "XAnim precache allocations must request native pointer alignment")
require_source_contains(
    "universal/com_memory.cpp"
    "Hunk_UserAlloc(g_debugUser, size, alignof(void *))"
    "debug pointer tables must receive native pointer alignment")

require_source_contains(
    "xanim/dobj.cpp"
    "Sys_AtomicCompareExchange(&obj->locked, 1u, 0u)"
    "DObj create and clone paths must reserve construction state atomically")
require_source_contains(
    "xanim/dobj.cpp"
    "DObjLock(obj);\n    if (obj->numModels > DOBJ_MAX_SUBMODELS"
    "DObj teardown must claim the live object lock before validation and release")
require_source_contains(
    "xanim/dobj.cpp"
    "DObjApplyCreatePlanLocked(plan, obj);\n    Sys_AtomicStore(&obj->locked, 0u);"
    "DObj create and clone publication must apply a complete plan before release")
require_source_contains(
    "xanim/dobj.cpp"
    "DObjPrepareCreateInternal("
    "DObj unarchive must prepare fallible state before rebuilding live fields")
require_source_contains(
    "xanim/dobj.cpp"
    "DObjApplyCreatePlanLocked(&plan, obj);\n    Sys_AtomicStore(&obj->locked, 0u);"
    "DObj unarchive must publish only after restoring hide-part state")
require_source_ordered(
    "xanim/dobj.cpp"
    "XAnimResetAnimMap(&treeModelView, tree->children);"
    "bool DObjTryCommitCreatePlan(DObjCreatePlan *plan, DObj_s *obj)"
    "fallible animation-map preparation must precede DObj construction locks")
require_source_contains(
    "xanim/dobj.cpp"
    "DObj duplicate-bone map exceeds its encoded size"
    "DObj duplicate maps must reject their one-byte encoded-size overflow")
require_source_not_contains(
    "xanim/dobj.cpp"
    "1 <<"
    "DObj model masks must not use signed shifts at model index 31")
require_source_contains(
    "xanim/dobj.cpp"
    "if (!model)"
    "DObj creation must reject null models before claiming construction state")

require_source_contains(
    "qcommon/dobj_management.cpp"
    "track_static_alloc_internal(objBuf, sizeof(objBuf), \"objBuf\", 11);"
    "DObj pool tracking must use the native object-array size")
require_source_not_contains(
    "qcommon/dobj_management.cpp"
    "204800"
    "DObj pool tracking must not retain the x86-only byte count")
require_source_contains(
    "qcommon/dobj_management.cpp"
    "DObjTryCommitCreatePlan(&plan, &objBuf[index])"
    "client DObjs must be fully constructed before map publication")
require_source_contains(
    "qcommon/dobj_management.cpp"
    "DObjTryCommitCreatePlan(&plan, &objBuf[index])"
    "server DObjs must be fully constructed before map publication")
require_source_contains(
    "qcommon/dobj_management.cpp"
    "DObjTryCommitCreatePlan(&plan, &objBuf[FreeDObjIndex])"
    "buffered DObj clones must be complete before map publication")
require_source_ordered(
    "qcommon/dobj_management.cpp"
    "DObjPrepareCreate(dobjModels, numModels, tree, 0, &plan);"
    "index = Com_GetFreeDObjIndex();"
    "fallible client DObj preparation must precede pool reservation")
require_source_ordered(
    "qcommon/dobj_management.cpp"
    "DObjPrepareClone(&objBuf[v4], &plan);"
    "FreeDObjIndex = Com_GetFreeDObjIndex();"
    "fallible DObj clone preparation must precede pool reservation")

require_source_contains(
    "cgame/cg_modelpreviewer.h"
    "inline constexpr int MDLPRV_CLONE_COUNT = 10;"
    "model-preview active clone traversal must have a typed bound")
foreach(_legacy_model_preview_stride
    "p_obj += 78"
    "right += 78"
    "memcpy(pClone, &g_mdlprv.model.currentEntity, 0x7Cu)"
)
    require_source_not_contains(
        "cgame/cg_modelpreviewer.cpp"
        "${_legacy_model_preview_stride}"
        "model-preview clone traversal must not use x86-only structure strides")
endforeach()
require_source_contains(
    "cgame/cg_modelpreviewer.cpp"
    "track_static_alloc_internal(&g_mdlprv, sizeof(g_mdlprv), \"g_mdlprv\", 0);"
    "model-preview tracking must use the native aggregate size")
require_source_contains(
    "cgame/cg_modelpreviewer.cpp"
    "pClone->ent = g_mdlprv.model.currentEntity;"
    "model-preview cloning must copy the typed entity rather than a frozen byte prefix")

require_source_not_contains(
    "game/g_main.cpp"
    "(int)XAnimCreateTree"
    "SP corpse XAnim trees must not truncate pointers")
require_source_contains(
    "game/g_main.cpp"
    "corpseInfo.tree = XAnimCreateTree(anims, Hunk_AllocActorXAnimServer);"
    "SP corpse XAnim trees must be assigned through their typed field")
require_source_contains(
    "game/g_main.cpp"
    "level.cgData_actorProneInfo[specialIndex] = corpseInfo.proneInfo;"
    "SP corpse metadata traversal must use typed native-width records")

foreach(_native_dvar_sort_token
    "Windows.h"
    "windows.h"
    "win_net"
    "Interlocked"
    "LONG"
)
    require_source_not_contains(
        "universal/dvar.cpp"
        "${_native_dvar_sort_token}"
        "dvar sorting must not depend on Windows atomics or networking")
endforeach()
require_source_contains(
    "universal/dvar.cpp"
    "static std::atomic<bool> s_areDvarsSorted{false};"
    "the sorted-dvar publication flag must be an initialized C++ atomic")
require_source_contains(
    "universal/dvar.cpp"
    "static std::atomic<bool> s_isSortingDvars{false};"
    "the dvar sort-owner flag must be an initialized C++ atomic")
require_source_contains(
    "universal/dvar.cpp"
    "s_isSortingDvars.compare_exchange_strong("
    "exactly one thread must claim each dvar sort")
require_source_contains(
    "universal/dvar.cpp"
    "s_isSortingDvars.load("
    "dvar sort waiters must observe the atomic owner flag")
require_source_contains(
    "universal/dvar.cpp"
    "s_isSortingDvars.store(false,"
    "the dvar sort owner must release its atomic claim")
require_source_contains(
    "universal/dvar.cpp"
    "s_areDvarsSorted.load("
    "dvar readers must observe sorted publication atomically")
require_source_contains(
    "universal/dvar.cpp"
    "s_areDvarsSorted.store(true,"
    "a completed dvar sort must publish its result atomically")
require_source_contains(
    "universal/dvar.cpp"
    "s_areDvarsSorted.store(false,"
    "dvar registration must invalidate sorted publication atomically")
require_source_ordered(
    "universal/dvar.cpp"
    "std::sort(sortedDvars, sortedDvars + dvarCount, CompareDvars);"
    "s_areDvarsSorted.store(true,"
    "dvar sort results must be complete before publication")
require_source_ordered(
    "universal/dvar.cpp"
    "s_areDvarsSorted.store(true,"
    "s_isSortingDvars.store(false,"
    "sorted publication must precede releasing waiting sort callers")

file(READ "${SOURCE_ROOT}/src/universal/dvar.cpp" _dvar_atomic_source)
set(_dvar_sorted_flag_accesses "${_dvar_atomic_source}")
string(REPLACE
    "static std::atomic<bool> s_areDvarsSorted{false};"
    ""
    _dvar_sorted_flag_accesses
    "${_dvar_sorted_flag_accesses}")
string(REGEX REPLACE
    "s_areDvarsSorted[ \t\r\n]*\\.[ \t\r\n]*(load|store)[ \t\r\n]*\\("
    ""
    _dvar_sorted_flag_accesses
    "${_dvar_sorted_flag_accesses}")
string(FIND "${_dvar_sorted_flag_accesses}" "s_areDvarsSorted" _raw_dvar_sorted_access)
if (NOT _raw_dvar_sorted_access EQUAL -1)
    message(FATAL_ERROR
        "Dvar sorted-publication flag has a non-atomic load, store, alias, or assignment")
endif()

set(_dvar_sort_owner_accesses "${_dvar_atomic_source}")
string(REPLACE
    "static std::atomic<bool> s_isSortingDvars{false};"
    ""
    _dvar_sort_owner_accesses
    "${_dvar_sort_owner_accesses}")
string(REGEX REPLACE
    "s_isSortingDvars[ \t\r\n]*\\.[ \t\r\n]*(compare_exchange_(strong|weak)|load|store)[ \t\r\n]*\\("
    ""
    _dvar_sort_owner_accesses
    "${_dvar_sort_owner_accesses}")
string(FIND "${_dvar_sort_owner_accesses}" "s_isSortingDvars" _raw_dvar_sort_owner_access)
if (NOT _raw_dvar_sort_owner_access EQUAL -1)
    message(FATAL_ERROR
        "Dvar sort-owner flag has a non-atomic load, store, alias, or assignment")
endif()
require_source_contains(
    "database/db_registry.cpp"
    "Sys_UnlockRead(&db_hashCritSect);
    if (!alwaysfails)
        MyAssertHandler(\".\\\\database\\\\db_registry.cpp\", 2849, 0, \"unreachable\");"
    "default-asset lookup must release its read lock on the no-match path")
require_source_contains(
    "database/db_registry.cpp"
    "DB_RemoveTechniqueSetAsset"
    "technique-set removal must use an exact one-argument adapter")
require_source_contains(
    "database/db_registry.cpp"
    "DB_RemoveImageAsset"
    "image removal must use an exact one-argument adapter")
require_source_contains(
    "universal/com_sndalias.cpp"
    "SndCurve *curve = header.sndCurve;"
    "sound-curve enumeration must use its typed asset member")
require_source_contains(
    "gfx_d3d/r_image.cpp"
    "GfxImage *image = header.image;"
    "delayed image enumeration must use its typed asset member")
require_source_not_contains(
    "gfx_d3d/r_image.cpp"
    "header.xmodelPieces["
    "image enumeration must not depend on x86 header layout")
require_source_contains(
    "cgame/cg_visionsets.cpp"
    "header.rawfile->name"
    "vision-set enumeration must use the raw-file asset member")
require_source_contains(
    "cgame/cg_modelpreviewer.cpp"
    "context->type == ASSET_TYPE_XMODEL && header.model"
    "model-preview enumeration must discriminate model headers")
require_source_contains(
    "cgame/cg_modelpreviewer.cpp"
    "context->type == ASSET_TYPE_XANIMPARTS && header.parts"
    "model-preview enumeration must discriminate animation headers")
require_source_contains(
    "cgame/cg_modelpreviewer.cpp"
    "alignof(const char *)"
    "model-preview name tables must use native pointer alignment")
require_source_contains(
    "gfx_d3d/r_model.cpp"
    "context->entries[context->count++] = header.model;"
    "model enumeration must use an explicit bounded context")
require_source_contains(
    "gfx_d3d/r_material.cpp"
    "entry.material = header.material;"
    "material enumeration must use an explicit native-width context")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "XAssetHeader materialHeaders[ARRAY_COUNT(rgp.sortedMaterials)];"
    "material sorting must not alias a Material pointer array as asset headers")
require_source_contains(
    "gfx_d3d/r_material.cpp"
    "Material_ForEachTechniqueSet(Material_ReloadTechniqueSetResources, true);"
    "technique reloads must occur after database enumeration releases its read count")
require_source_contains(
    "gfx_d3d/r_material.cpp"
    "Material_ForEachTechniqueSet(Material_ReleaseTechniqueSetResources, true);"
    "technique releases must occur after database enumeration releases its read count")
require_source_contains(
    "gfx_d3d/r_material_load_obj.cpp"
    "Material_ForEachTechniqueSet(Material_ReleaseTechniqueSetResources, true);"
    "load-object technique cleanup must occur after database enumeration")
file(READ
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive.cpp"
    _fx_archive_save_source)
extract_security_slice(
    _fx_archive_save_source
    "void __cdecl FX_CaptureEffectTableEntry_LoadObj("
    "fx::archive::EffectTableSaveStatus FX_CaptureEffectTableNoReport("
    _fx_effect_table_capture_callbacks
    "bounded FX effect-table capture callbacks")
require_security_slice_contains(
    _fx_effect_table_capture_callbacks
    "AppendEffectTableSaveDefinitionNoReport("
    "FX archive database callbacks must only append bounded records")
foreach(_forbidden_effect_table_callback_call
    "MemFile_"
    "Com_"
    "Z_Malloc("
    "Z_Free("
    "ValidateEffectTableSaveSnapshotNoReport("
    "WriteEffectTableSaveSnapshotNoReport(")
    forbid_security_slice_contains(
        _fx_effect_table_capture_callbacks
        "${_forbidden_effect_table_callback_call}"
        "FX archive database callbacks must not allocate, report, validate, or write")
endforeach()
extract_security_slice(
    _fx_archive_save_source
    "fx::archive::EffectTableSaveStatus FX_CaptureEffectTableNoReport("
    "bool FX_WriteEffectTableSaveBytes("
    _fx_effect_table_capture_transaction
    "FX effect-table capture and post-enumeration validation")
require_security_slice_ordered(
    _fx_effect_table_capture_transaction
    "FX_ForEachEffectDef("
    "ValidateEffectTableSaveSnapshotNoReport("
    "FX archive effect-table validation must occur after enumeration returns")
extract_security_slice(
    _fx_archive_save_source
    "bool FX_ValidateArchiveEffectDefinitionReferences("
    "bool FX_ValidateArchiveCopiedSnapshot("
    _fx_effect_table_graph_admission
    "FX copied effect-definition admission")
require_security_slice_contains(
    _fx_effect_table_graph_admission
    "FindEffectTableSaveDefinitionKey("
    "copied effect-definition pointers must be represented by the staged table")
require_security_slice_contains(
    _fx_effect_table_graph_admission
    "reinterpret_cast<std::uintptr_t>(effect->def)"
    "effect-definition admission must compare full pointer values")
foreach(_forbidden_unadmitted_definition_read
    "effect->def->"
    "effect->def."
    "FX_GetArchiveEffectDefCount("
    "FX_ValidateArchiveEffectDefTiming(")
    forbid_security_slice_contains(
        _fx_effect_table_graph_admission
        "${_forbidden_unadmitted_definition_read}"
        "unadmitted copied effect definitions must not be dereferenced")
endforeach()

file(READ "${SOURCE_ROOT}/src/universal/memfile.cpp" _memfile_source)
extract_security_slice(
    _memfile_source
    "MemFileReadStatus MemFile_ReadEncodedByteNoReport("
    "} // namespace"
    _memfile_silent_byte
    "silent MemoryFile encoded-byte reader")
extract_security_slice(
    _memfile_source
    "MemFileReadStatus MemFile_TryReadDataNoReport("
    "uint8_t __cdecl MemFile_ReadByteInternal("
    _memfile_silent_public
    "silent MemoryFile public readers")
foreach(_forbidden_memfile_read_report
    "MyAssertHandler("
    "Com_Error("
    "Com_Printf("
    "MemFile_ReadData("
    "MemFile_ReadByteInternal("
    "AssertStreamMode(")
    forbid_security_slice_contains(
        _memfile_silent_byte
        "${_forbidden_memfile_read_report}"
        "silent encoded-byte reads cannot report or enter legacy readers")
    forbid_security_slice_contains(
        _memfile_silent_public
        "${_forbidden_memfile_read_report}"
        "silent public reads cannot report or enter legacy readers")
endforeach()
require_security_slice_contains(
    _memfile_silent_public
    "MemFile_ReadEncodedByteNoReport(memFile, &value)"
    "silent data reads must use the bounded encoded-byte primitive")
extract_security_slice(
    _memfile_source
    "void MemFile_AbandonCurrentThreadForError()"
    "uint8_t __cdecl MemFile_ReadByteInternal("
    _memfile_error_abandon
    "MemoryFile error abandonment")
require_security_slice_contains(
    _memfile_error_abandon
    "MemoryFile* const owner = g_currentThreadStreamOwner;"
    "MemoryFile error abandonment must only release the calling thread's stream")
foreach(_forbidden_memfile_abandon_dependency
    "g_streamOwner"
    "streamModeThread"
    "GetThreadID("
    "MyAssertHandler("
    "Com_Error("
    "Com_Printf(")
    forbid_security_slice_contains(
        _memfile_error_abandon
        "${_forbidden_memfile_abandon_dependency}"
        "MemoryFile error abandonment must be TLS-first and report-free")
endforeach()

file(READ "${SOURCE_ROOT}/src/qcommon/common.cpp" _common_error_source)
extract_security_slice(
    _common_error_source
    "ERR_JMP:"
    "    if (code == ERR_SCRIPT_DROP)"
    _com_error_jump
    "Com_Error longjmp cleanup")
extract_security_slice(
    _common_error_source
    "void Com_CheckError()"
    "#ifdef KISAK_SP"
    _com_check_error
    "Com_CheckError longjmp cleanup")
foreach(_common_error_jump_slice _com_error_jump _com_check_error)
    require_security_slice_contains(
        ${_common_error_jump_slice}
        "MemFile_AbandonCurrentThreadForError();"
        "error longjmp paths must abandon the calling thread's MemoryFile stream")
    require_security_slice_ordered(
        ${_common_error_jump_slice}
        "MemFile_AbandonCurrentThreadForError();"
        "longjmp("
        "MemoryFile state must be abandoned before error control leaves the stack")
endforeach()
require_security_slice_ordered(
    _com_error_jump
    "Sys_LeaveCriticalSection(CRITSECT_COM_ERROR);"
    "MemFile_AbandonCurrentThreadForError();"
    "Com_Error must release its error lock before abandoning the MemoryFile stream")
require_source_match_count(
    "qcommon/common.cpp"
    "MemFile_AbandonCurrentThreadForError\\(\\);"
    2
    "both error longjmp paths must abandon active MemoryFile state")
require_source_match_count(
    "qcommon/common.cpp"
    "MemFile_AbandonCurrentThreadForError\\(\\);[ \t\r\n]*#ifndef[ \t]+KISAK_DEDI_HEADLESS"
    2
    "MemoryFile error abandonment must remain outside client-only cleanup guards")

file(READ
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive_reader_disk32.cpp"
    _fx_archive_read_source)
extract_security_slice(
    _fx_archive_read_source
    "[[nodiscard]] FxArchiveDisk32ReaderStatus ReadExact("
    "struct DefinitionResolverContext"
    _fx_archive_silent_read
    "portable FX archive silent read adapter")
require_security_slice_contains(
    _fx_archive_silent_read
    "MemFile_TryReadDataNoReport("
    "FX archive reads must use the status-bearing silent MemoryFile boundary")
foreach(_fx_archive_read_status
    "MemFileReadStatus::Success"
    "MemFileReadStatus::InvalidArgument"
    "MemFileReadStatus::InvalidState"
    "MemFileReadStatus::Overflow"
    "MemFileReadStatus::OutputTooSmall"
    "FxArchiveDisk32ReaderStatus::TruncatedInput")
    require_security_slice_contains(
        _fx_archive_silent_read
        "${_fx_archive_read_status}"
        "portable FX archive reads must preserve complete status classification")
endforeach()
foreach(_forbidden_fx_archive_read_behavior
    "MemFile_ReadData("
    "errorOnOverflow"
    "MyAssertHandler("
    "Com_Error("
    "Com_Printf(")
    forbid_security_slice_contains(
        _fx_archive_silent_read
        "${_forbidden_fx_archive_read_behavior}"
        "FX archive silent reads cannot toggle diagnostics or report")
endforeach()
require_source_contains(
    "database/db_load.cpp"
    "DB_ConvertOffsetToAlias(
                (uint32_t*)&varMaterial->textureTable,
                DBAliasKind::MaterialTextureTable,
                textureByteCount)"
    "shared material texture tables must resolve through exact typed provenance")
require_source_contains(
    "database/db_load.cpp"
    "db::validation::MaterialTextureHeaderValid("
    "material texture headers must be bounded before payload fixup")
require_source_contains(
    "database/db_load.cpp"
    "var[-1].nameHash >= var->nameHash"
    "material texture tables must be strictly hash ordered")
require_source_contains(
    "database/db_load.cpp"
    "Present-empty spans are legal in disk32"
    "empty material texture tables must be explicitly canonicalized")
require_source_contains(
    "database/db_load.cpp"
    "if (textureToken == disk32::kInline)"
    "empty inline material texture tables must preserve stream alignment")
require_source_contains(
    "database/db_load.cpp"
    "DB_ConvertOffsetToPointer(
                    (uint32_t*)&varMaterial->textureTable,
                    0,
                    4,
                    kDirectBlock4)"
    "empty direct material texture tables must still validate their token")
require_source_contains(
    "database/db_stream.cpp"
    "case DBAliasKind::MaterialTextureTable:
        if (metadata != materializedBytes
            || metadata < disk32::kMaterialTextureDefBytes"
    "completed material texture tables must validate their variable disk32 extent")
require_source_contains(
    "database/db_validation.h"
    "major != loadForRenderer + 2"
    "material shader bytecode models must match their renderer variant")
require_source_contains(
    "gfx_d3d/r_material.cpp"
    "const HRESULT result = dx.device->CreateVertexShader("
    "material vertex shader creation must inspect the D3D result")
require_source_contains(
    "gfx_d3d/r_material.cpp"
    "const HRESULT result = dx.device->CreatePixelShader("
    "material pixel shader creation must inspect the D3D result")
require_source_contains(
    "gfx_d3d/r_material.cpp"
    "partialShader->Release()"
    "failed material shader creation must release a partial COM output")
require_source_contains(
    "database/db_load.cpp"
    "disk32::kMaterialPassBytes"
    "material pass stream reads must use their fixed disk32 layout")
require_source_contains(
    "database/db_file_load.cpp"
    "DB_MarkStreamRangeMaterialized(pos, size)"
    "successful fast-file output must be recorded as materialized")
require_source_contains(
    "database/db_file_load.cpp"
    "db::load_atomic::PublishFileRead(
        &g_fileRead,"
    "asynchronous fast-file reads must preserve the actual completion byte count")
require_source_contains(
    "database/db_file_load.cpp"
    "initialReadSize < 12"
    "fast-file magic and version reads must reject truncated headers")
require_source_contains(
    "database/db_file_load.cpp"
    "db::validation::CanAppendBytes"
    "fast-file ring-buffer accounting must reject invalid accumulated input")
require_source_contains(
    "database/db_file_load.cpp"
    "if (cancelError == ERROR_NOT_FOUND)"
    "a completion that races cancellation must drain through another alertable wait")
foreach(_native_db_atomic_token
    "Interlocked"
    "LPOVERLAPPED_COMPLETION_ROUTINE"
)
    require_source_not_contains(
        "database/db_file_load.cpp"
        "${_native_db_atomic_token}"
        "the fast-file adapter must use the fixed-width atomic protocol without callback casts")
endforeach()
require_source_not_matches(
    "database/db_file_load.cpp"
    "(^|[^A-Za-z0-9_])LONG([^A-Za-z0-9_]|$)"
    "fast-file atomic storage must not use a native Windows word")
require_source_contains(
    "database/db_file_load.cpp"
    "static db::load_atomic::ProgressState g_loadProgress;"
    "fast-file progress must use one coherent fixed-width record")
require_source_contains(
    "database/db_file_load.cpp"
    "static db::load_atomic::FileReadState g_fileRead;"
    "asynchronous reads must use one ordered fixed-width result slot")
require_source_contains(
    "database/db_file_load.cpp"
    "static VOID CALLBACK DB_FileReadCompletion(
    DWORD dwErrorCode,
    DWORD dwNumberOfBytesTransfered,
    LPOVERLAPPED lpOverlapped)"
    "the Windows completion adapter must retain its exact native callback ABI")
require_source_contains(
    "database/db_file_load.cpp"
    "db::load_atomic::RebaseProgress("
    "header-time progress rebasing must be transactional")
require_source_contains(
    "database/db_file_load.cpp"
    "db::load_atomic::LoadedFraction(snapshot)"
    "progress readers must consume a coherent bounded snapshot")

foreach(_retired_db_progress_word
    "g_fileReadComplete"
    "g_fileReadError"
    "g_fileReadBytes"
    "g_loadProgressSequence"
    "g_loadedSize"
    "g_loadedExternalBytes"
    "g_totalSize"
    "g_totalExternalBytes"
)
    require_source_not_contains(
        "database/db_file_load.cpp"
        "${_retired_db_progress_word}"
        "fast-file state must not bypass its portable atomic record")
endforeach()

foreach(_native_db_helper_token
    "Windows.h"
    "windows.h"
    "Interlocked"
    "LONG"
    "OVERLAPPED"
)
    require_source_not_contains(
        "database/db_load_atomic.h"
        "${_native_db_helper_token}"
        "the portable database atomic protocol must not import Windows concepts")
endforeach()
require_source_contains(
    "database/db_load_atomic.h"
    "Sys_AtomicStore(&state->complete, 0u);
    Sys_AtomicStore(&state->error, 0u);
    Sys_AtomicStore(&state->bytes, 0u);"
    "read-slot reset must unpublish completion before clearing its payload")
require_source_contains(
    "database/db_load_atomic.h"
    "Sys_AtomicStore(&state->bytes, valid ? bytes : 0u);
    Sys_AtomicStore(&state->complete, 1u);"
    "read-slot completion must publish its payload before the ready flag")
require_source_contains(
    "database/db_load_atomic.h"
    "inline ProgressUpdateResult RebaseProgress("
    "the progress protocol must own header-time rebasing")
require_source_contains(
    "database/db_load_atomic.h"
    "std::this_thread::yield();"
    "contended database atomic protocols must yield")

foreach(_native_registry_atomic_token
    "Interlocked"
    "FILE_FLAG_NO_BUFFERING"
    "0x60000000"
)
    require_source_not_contains(
        "database/db_registry.cpp"
        "${_native_registry_atomic_token}"
        "database queue atomics and buffered reads must retain portable-safe semantics")
endforeach()
require_source_not_matches(
    "database/db_registry.cpp"
    "(^|[^A-Za-z0-9_])LONG([^A-Za-z0-9_]|$)"
    "database queue state must not use a native Windows word")
require_source_contains(
    "database/db_load_atomic.h"
    "std::atomic<bool> assetsSafe_{true};"
    "lost-device recovery must begin in an idle/safe state")
require_source_contains(
    "database/db_registry.cpp"
    "static db::load_atomic::AssetRecoveryGate g_assetRecoveryGate;"
    "database recovery state must use the standalone-tested gate")
require_source_contains(
    "database/db_load_atomic.h"
    "assetsSafe_.store(true, std::memory_order_seq_cst);
            while (recoveryRequested_.load(std::memory_order_seq_cst))
                std::this_thread::yield();

            assetsSafe_.store(false, std::memory_order_seq_cst);
            if (!recoveryRequested_.load(std::memory_order_seq_cst))
                return;"
    "asset safe points must advertise through back-to-back recoveries and recheck after reclaiming")
require_source_contains(
    "database/db_load_atomic.h"
    "void EndRecovery() noexcept
    {
        // Do not clear assetsSafe_ here."
    "recovery completion must leave the safe publication owned by the database thread")
require_source_contains(
    "database/db_registry.cpp"
    "DB_WaitForRecoveryAndClaimAssets();

        zone = &g_zones[g_zoneIndex];"
    "database asset-use ownership must precede publication of a new zone")
require_source_contains(
    "database/db_registry.cpp"
    "DB_LOADING_ASSET_QUEUE_RESERVED"
    "zone producers must reserve the queue before populating shared entries")
require_source_contains(
    "database/db_registry.cpp"
    "static volatile uint32_t g_loadingAssets;"
    "loading-asset accounting must remain private fixed-width atomic storage")
require_source_not_contains(
    "database/database.h"
    "g_loadingAssets"
    "public database headers must not expose mutable loading-asset storage")
require_source_contains(
    "database/db_registry.cpp"
    "Sys_AtomicExchange(&g_zoneInfoCount, 0u)"
    "the database thread must atomically claim each published zone batch")
require_source_contains(
    "database/db_registry.cpp"
    "DB_CompleteLoadingAsset();"
    "zone completion must use checked atomic accounting")
require_source_not_contains(
    "database/db_registry.cpp"
    "--g_loadingAssets"
    "zone completion must not underflow through a raw decrement")
require_source_contains(
    "database/db_registry.cpp"
    "FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN"
    "fast-file reads must use buffered overlapped sequential I/O")
foreach(_retired_database_pointer_pattern
    "(uint32_t)indices"
    "(char *)zoneFile + 1"
    "(void *)-1"
)
    require_source_not_contains(
        "database/db_registry.cpp"
        "${_retired_database_pointer_pattern}"
        "database buffer and handle validation must not use truncated or invalid pointer arithmetic")
endforeach()
require_source_contains(
    "database/db_registry.cpp"
    "const uintptr_t indexAddress = reinterpret_cast<uintptr_t>(indices);"
    "index-buffer offsets must be calculated at native pointer width")
require_source_contains(
    "database/db_registry.cpp"
    "byteOffset > indexBlock.size || (byteOffset & 1u)"
    "index-buffer offsets must stay inside the owning zone block and remain element-aligned")
require_source_contains(
    "database/db_registry.cpp"
    "byteOffset > vertexBlock.size
        || byteOffset > static_cast<uintptr_t>(INT32_MAX)"
    "vertex-buffer offsets must fit both the owning block and public result type")
require_source_contains(
    "database/db_registry.cpp"
    "alignas(uint32_t) uint8_t g_fileBuf[524288];"
    "the buffered fast-file ring must satisfy its word-alignment contract")
require_source_contains(
    "database/db_stringtable_load.cpp"
    "*var >= static_cast<uint32_t>(varXAssetList->stringList.count)"
    "script-string tokens must be range-checked before indexing")
require_source_contains(
    "qcommon/com_bsp_load_obj.cpp"
    "comBspGlob.fileSize > INT32_MAX"
    "BSP allocation sizes must fit the signed zone allocator")
require_source_contains(
    "universal/physicalmemory.cpp"
    "PMem_TryAlloc"
    "zone allocations must report checked failure before generic fatal PMem OOM handling")
require_source_contains(
    "database/db_load.cpp"
    "#ifndef KISAK_DEDI_HEADLESS
    SND_SetData(mssSound, *data);
#else
    (void)data;"
    "headless loaded sounds must not link the playback data realizer")
require_source_contains(
    "database/db_load.cpp"
    "DB_SetInsertedPointer(
                    inserted,
                    DBAliasKind::SoundData,
                    sharedData,
                    sharedDataSize);"
    "headless loaded sounds must retain raw sound alias provenance")
require_source_contains(
    "database/db_load.cpp"
    "DB_ClearHeadlessSoundRuntimeData(varMssSound);"
    "headless loaded sounds must canonicalize playback-owned pointers before publication")
require_source_contains(
    "database/db_load.cpp"
    "#ifndef KISAK_DEDI_HEADLESS
            Load_Texture(varGfxTextureLoad, varGfxImage);
#else"
    "headless image loading must not link the D3D texture realizer")
require_source_contains(
    "database/db_load.cpp"
    "varGfxTextureLoad->basemap = nullptr;"
    "headless images must publish a canonical absent runtime texture")
require_source_contains(
    "database/db_load.cpp"
    "else if (!image->delayLoadPixels)"
    "headless non-delayed external images must be finalized immediately")
require_source_contains(
    "database/db_load.cpp"
    "DB_LoadedExternalData(externalDataSize);"
    "headless immediate external image loads must advance progress accounting")
require_source_contains(
    "database/db_file_load.cpp"
    "if (externalDataSize < 0)
    {
        if (imageLoadFailed)
            *imageLoadFailed = true;"
    "headless delayed external images must reject negative progress sizes")
require_source_contains(
    "database/db_file_load.cpp"
    "if (imageLoadFailed)
        Com_Error(ERR_DROP, \"Invalid headless delayed image size\");"
    "headless delayed image failures must be raised after database enumeration")
require_source_contains(
    "database/db_load.cpp"
    "if (!DB_FinalizeHeadlessTextureLoad(varGfxImageLoadDef, varGfxImage))"
    "headless texture loading must select embedded, delayed, or immediate data-only realization")
require_source_contains(
    "database/db_registry.cpp"
    "techniqueSet->remappedTechniqueSet = techniqueSet;"
    "headless technique sets must retain a canonical self-remap for material validation")
require_source_contains(
    "database/db_registry.cpp"
    "if (fs_basepath && fs_basepath->current.string && *fs_basepath->current.string)
        return fs_basepath->current.string;"
    "fast-file lookup must honor the configured filesystem base path")
require_source_contains(
    "database/db_registry.cpp"
    "Com_sprintf(filename, size, \"%s\\\\%s\\\\%s.ff\", DB_GetFastFileBasePath(), string, zoneName);"
    "mod fast-file lookup must use the configured filesystem base path")
require_source_contains(
    "database/db_registry.cpp"
    "Com_sprintf(filename, size, \"%s\\\\zone\\\\%s\\\\%s.ff\", DB_GetFastFileBasePath(), Language, zoneName);"
    "base fast-file lookup must use the configured filesystem base path")
require_source_contains(
    "database/db_load.cpp"
    "#ifndef KISAK_DEDI_HEADLESS
    return Load_CreateMaterialVertexShader("
    "headless material loading must not link vertex-shader creation")
require_source_contains(
    "database/db_load.cpp"
    "#ifndef KISAK_DEDI_HEADLESS
    return Load_CreateMaterialPixelShader("
    "headless material loading must not link pixel-shader creation")
require_source_contains(
    "database/db_load.cpp"
    "varMaterialVertexShaderProgram->vs = nullptr;"
    "headless vertex shaders must retain bytecode with an absent runtime handle")
require_source_contains(
    "database/db_load.cpp"
    "varMaterialPixelShaderProgram->ps = nullptr;"
    "headless pixel shaders must retain bytecode with an absent runtime handle")
require_source_contains(
    "database/db_load.cpp"
    "#ifndef KISAK_DEDI_HEADLESS
            Load_BuildVertexDecl(&varMaterialPass->vertexDecl);
#endif"
    "headless material loading must not link vertex-declaration construction")
require_source_contains(
    "database/db_load.cpp"
    "sizeof(varMaterialVertexDeclaration->routing.decl));"
    "headless vertex declarations must clear serialized COM handles")
require_source_contains(
    "database/db_load.cpp"
    "varMaterialVertexDeclaration->isLoaded = true;"
    "headless vertex declarations must record completed data-only realization")
require_source_contains(
    "database/db_load.cpp"
    "#ifndef KISAK_DEDI_HEADLESS
            if (!Load_PicmipWater(varMaterialTextureDefInfo))
                return false;
#else
            // Dedicated simulation keeps the validated source-resolution CPU
            // grids; renderer picmip would destructively compact both arrays.
            if (!DB_ValidateHeadlessWaterContract(*varMaterialTextureDefInfo))"
    "headless water must validate its CPU/image contract without renderer picmip")
require_source_contains(
    "database/db_load.cpp"
    "water->image->width != water->M
        || water->image->height != water->N"
    "headless water must preserve source-resolution image dimensions")
require_source_contains(
    "database/db_load.cpp"
    "#ifndef KISAK_DEDI_HEADLESS
    Load_VertexBuffer("
    "headless world loading must not link vertex-buffer realization")
require_source_contains(
    "database/db_load.cpp"
    "varGfxWorldVertexData->worldVb = nullptr;"
    "headless world vertices must retain CPU data with an absent runtime buffer")
require_source_contains(
    "database/db_load.cpp"
    "#ifndef KISAK_DEDI_HEADLESS
    Load_VertexBuffer(&varGfxWorldVertexLayerData->layerVb, varGfxWorld->vld.data, layerDataSize);
#else"
    "headless world vertex layers must not link vertex-buffer realization")
require_source_contains(
    "database/db_load.cpp"
    "varGfxWorldVertexLayerData->layerVb = nullptr;"
    "headless world vertex layers must retain CPU data with an absent runtime buffer")

file(STRINGS
    "${SOURCE_ROOT}/src/database/db_load.cpp"
    _raw_array_loads
    REGEX "Load_Stream\\(atStreamStart,.*([0-9]+[ \t]*\\*[ \t]*count|count[ \t]*\\*[ \t]*[0-9]+)\\)")
if (_raw_array_loads)
    message(FATAL_ERROR
        "Unchecked generated fast-file array loads remain in db_load.cpp; use Load_StreamArray")
endif()

file(READ "${SOURCE_ROOT}/src/database/db_load.cpp" _db_load_source)
string(FIND
    "${_db_load_source}"
    "db::validation::MaterialPassLayoutValid("
    _material_pass_header_validation)
string(FIND
    "${_db_load_source}"
    "if (varMaterialPass->vertexDecl)"
    _material_pass_first_child_fixup)
if (_material_pass_header_validation EQUAL -1
    OR _material_pass_first_child_fixup EQUAL -1
    OR _material_pass_header_validation GREATER _material_pass_first_child_fixup)
    message(FATAL_ERROR
        "Material pass headers must be validated before child fixups")
endif()
string(FIND
    "${_db_load_source}"
    "CountInRange(varMaterialTechnique->passCount, 1, 4)"
    _material_technique_count_validation)
string(FIND
    "${_db_load_source}"
    "Load_MaterialPassArray(1, varMaterialTechnique->passCount)"
    _material_technique_pass_load)
if (_material_technique_count_validation EQUAL -1
    OR _material_technique_pass_load EQUAL -1
    OR _material_technique_count_validation GREATER _material_technique_pass_load)
    message(FATAL_ERROR
        "Material technique pass counts must be validated before array loading")
endif()
string(FIND
    "${_db_load_source}"
    "Load_MaterialShaderArgumentArray(1, static_cast<int32_t>(argumentCount))"
    _material_argument_load)
string(FIND
    "${_db_load_source}"
    "if (!DB_ValidateMaterialPassArguments(varMaterialPass, argumentCount))"
    _material_argument_validation)
if (_material_argument_load EQUAL -1
    OR _material_argument_validation EQUAL -1
    OR _material_argument_load GREATER _material_argument_validation)
    message(FATAL_ERROR
        "Material shader arguments must be validated after loading/fixups")
endif()
string(FIND
    "${_db_load_source}"
    "DB_RegisterPointerSlot(
                varMaterialPass->vertexDecl,
                DBAliasKind::MaterialVertexDeclaration)"
    _material_vertex_declaration_registration)
string(FIND
    "${_db_load_source}"
    "if (!Load_MaterialVertexDeclaration(1))"
    _material_vertex_declaration_load)
string(FIND
    "${_db_load_source}"
    "Load_BuildVertexDecl(&varMaterialPass->vertexDecl)"
    _material_vertex_declaration_build)
string(FIND
    "${_db_load_source}"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::MaterialVertexDeclaration"
    _material_vertex_declaration_completion)
if (_material_vertex_declaration_registration EQUAL -1
    OR _material_vertex_declaration_load EQUAL -1
    OR _material_vertex_declaration_build EQUAL -1
    OR _material_vertex_declaration_completion EQUAL -1
    OR _material_vertex_declaration_registration GREATER _material_vertex_declaration_load
    OR _material_vertex_declaration_load GREATER _material_vertex_declaration_build
    OR _material_vertex_declaration_build GREATER _material_vertex_declaration_completion)
    message(FATAL_ERROR
        "Material vertex declaration provenance must begin before loading and publish after construction")
endif()
string(FIND
    "${_db_load_source}"
    "DB_RegisterPointerSlot(
                *varMaterialTechniquePtr,
                DBAliasKind::MaterialTechnique)"
    _material_technique_registration)
string(FIND
    "${_db_load_source}"
    "if (!Load_MaterialTechnique(1))"
    _material_technique_load)
string(FIND
    "${_db_load_source}"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::MaterialTechnique"
    _material_technique_completion)
if (_material_technique_registration EQUAL -1
    OR _material_technique_load EQUAL -1
    OR _material_technique_completion EQUAL -1
    OR _material_technique_registration GREATER _material_technique_load
    OR _material_technique_load GREATER _material_technique_completion)
    message(FATAL_ERROR
        "Material technique provenance must begin before loading and publish after completion")
endif()
string(FIND
    "${_db_load_source}"
    "varMaterialVertexShader->prog.vs = nullptr"
    _material_vertex_shader_scrub)
string(FIND
    "${_db_load_source}"
    "Load_Stream(
        atStreamStart,
        (uint8_t *)varMaterialVertexShader,
        disk32::kMaterialVertexShaderBytes)"
    _material_vertex_shader_header_load)
string(FIND
    "${_db_load_source}"
    "varXString = &varMaterialVertexShader->name"
    _material_vertex_shader_name_load)
string(FIND
    "${_db_load_source}"
    "DB_RegisterPointerSlot(
                *varMaterialVertexShaderPtr,
                DBAliasKind::MaterialVertexShader)"
    _material_vertex_shader_registration)
string(FIND
    "${_db_load_source}"
    "if (!Load_MaterialVertexShader(1))"
    _material_vertex_shader_load)
string(FIND
    "${_db_load_source}"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::MaterialVertexShader"
    _material_vertex_shader_completion)
if (_material_vertex_shader_header_load EQUAL -1
    OR _material_vertex_shader_scrub EQUAL -1
    OR _material_vertex_shader_name_load EQUAL -1
    OR _material_vertex_shader_registration EQUAL -1
    OR _material_vertex_shader_load EQUAL -1
    OR _material_vertex_shader_completion EQUAL -1
    OR _material_vertex_shader_header_load GREATER _material_vertex_shader_scrub
    OR _material_vertex_shader_scrub GREATER _material_vertex_shader_name_load
    OR _material_vertex_shader_registration GREATER _material_vertex_shader_load
    OR _material_vertex_shader_load GREATER _material_vertex_shader_completion)
    message(FATAL_ERROR
        "Material vertex shaders must scrub runtime state and publish only after completion")
endif()
string(FIND
    "${_db_load_source}"
    "varMaterialPixelShader->prog.ps = nullptr"
    _material_pixel_shader_scrub)
string(FIND
    "${_db_load_source}"
    "Load_Stream(
        atStreamStart,
        (uint8_t *)varMaterialPixelShader,
        disk32::kMaterialPixelShaderBytes)"
    _material_pixel_shader_header_load)
string(FIND
    "${_db_load_source}"
    "varXString = &varMaterialPixelShader->name"
    _material_pixel_shader_name_load)
string(FIND
    "${_db_load_source}"
    "DB_RegisterPointerSlot(
                *varMaterialPixelShaderPtr,
                DBAliasKind::MaterialPixelShader)"
    _material_pixel_shader_registration)
string(FIND
    "${_db_load_source}"
    "if (!Load_MaterialPixelShader(1))"
    _material_pixel_shader_load)
string(FIND
    "${_db_load_source}"
    "DB_CompleteObject(
                    completed,
                    DBAliasKind::MaterialPixelShader"
    _material_pixel_shader_completion)
if (_material_pixel_shader_header_load EQUAL -1
    OR _material_pixel_shader_scrub EQUAL -1
    OR _material_pixel_shader_name_load EQUAL -1
    OR _material_pixel_shader_registration EQUAL -1
    OR _material_pixel_shader_load EQUAL -1
    OR _material_pixel_shader_completion EQUAL -1
    OR _material_pixel_shader_header_load GREATER _material_pixel_shader_scrub
    OR _material_pixel_shader_scrub GREATER _material_pixel_shader_name_load
    OR _material_pixel_shader_registration GREATER _material_pixel_shader_load
    OR _material_pixel_shader_load GREATER _material_pixel_shader_completion)
    message(FATAL_ERROR
        "Material pixel shaders must scrub runtime state and publish only after completion")
endif()
string(FIND
    "${_db_load_source}"
    "DB_RegisterPointerSlot(
                *varMaterialTextureDefInfo,
                DBAliasKind::MaterialWater)"
    _material_water_registration)
string(FIND
    "${_db_load_source}"
    "if (!Load_water_t(1))"
    _material_water_load)
string(FIND
    "${_db_load_source}"
    "if (!Load_PicmipWater(varMaterialTextureDefInfo))"
    _material_water_picmip)
string(FIND
    "${_db_load_source}"
    "if (!DB_CompleteObject(
                    completed,
                    DBAliasKind::MaterialWater,
                    *varMaterialTextureDefInfo,
                    disk32::kMaterialWaterBytes,
                    disk32::kMaterialWaterBytes))"
    _material_water_completion)
string(FIND
    "${_db_load_source}"
    "Load_Stream(
        1,
        (uint8_t *)varwater_t,
        disk32::kMaterialWaterBytes)"
    _material_water_header_load)
string(FIND
    "${_db_load_source}"
    "varwater_t->writable.floatTime = kWaterInitialTime"
    _material_water_runtime_scrub)
string(FIND
    "${_db_load_source}"
    "if (!DB_ValidateWaterHeader(varwater_t, &sampleCount))"
    _material_water_header_validation)
if (_material_water_registration EQUAL -1
    OR _material_water_load EQUAL -1
    OR _material_water_picmip EQUAL -1
    OR _material_water_completion EQUAL -1
    OR _material_water_header_load EQUAL -1
    OR _material_water_runtime_scrub EQUAL -1
    OR _material_water_header_validation EQUAL -1
    OR _material_water_registration GREATER _material_water_load
    OR _material_water_load GREATER _material_water_picmip
    OR _material_water_picmip GREATER _material_water_completion
    OR _material_water_header_load GREATER _material_water_runtime_scrub
    OR _material_water_runtime_scrub GREATER _material_water_header_validation)
    message(FATAL_ERROR
        "Material water must scrub runtime state and publish only after validated picmip completion")
endif()
file(READ "${SOURCE_ROOT}/src/gfx_d3d/r_water.cpp" _water_runtime_source)
string(FIND
    "${_water_runtime_source}"
    "DownsampleWaterGridInPlace(
            water->H0,
            sourceWidth,
            targetWidth)"
    _material_water_amplitude_picmip)
string(FIND
    "${_water_runtime_source}"
    "DownsampleWaterGridInPlace(
            water->wTerm,
            sourceWidth,
            targetWidth)"
    _material_water_frequency_picmip)
if (_material_water_amplitude_picmip EQUAL -1
    OR _material_water_frequency_picmip EQUAL -1)
    message(FATAL_ERROR
        "Material water picmip must compact paired amplitudes and frequencies")
endif()
string(FIND "${_water_runtime_source}" "std::fmod(phaseSource, 1024.0)" _water_bounded_phase)
string(FIND "${_water_runtime_source}" "if (!std::isfinite(phase))" _water_finite_phase)
string(FIND "${_water_runtime_source}" "std::hypot(sample.real, sample.imag)" _water_safe_magnitude)
string(FIND "${_water_runtime_source}" "iassert( fftIndexa < HCOUNT )" _water_second_fft_bound)
if (_water_bounded_phase EQUAL -1
    OR _water_finite_phase EQUAL -1
    OR _water_safe_magnitude EQUAL -1
    OR _water_second_fft_bound EQUAL -1)
    message(FATAL_ERROR
        "Material water runtime must bound numeric conversions and both FFT passes")
endif()
string(FIND
    "${_db_load_source}"
    "DB_RegisterPointerSlot(
                varMaterial->textureTable,
                DBAliasKind::MaterialTextureTable)"
    _material_texture_table_registration)
string(FIND
    "${_db_load_source}"
    "if (!Load_MaterialTextureDefArray(1, varMaterial->textureCount))"
    _material_texture_table_load)
string(FIND
    "${_db_load_source}"
    "if (!DB_CompleteObject(
                    completed,
                    DBAliasKind::MaterialTextureTable,
                    varMaterial->textureTable,
                    textureByteCount,
                    textureByteCount))"
    _material_texture_table_completion)
if (_material_texture_table_registration EQUAL -1
    OR _material_texture_table_load EQUAL -1
    OR _material_texture_table_completion EQUAL -1
    OR _material_texture_table_registration GREATER _material_texture_table_load
    OR _material_texture_table_load GREATER _material_texture_table_completion)
    message(FATAL_ERROR
        "Material texture-table provenance must begin before child loading and publish after completion")
endif()
string(FIND
    "${_db_load_source}"
    "DB_ConvertOffsetToPointer(
                (uint32_t*)&varMaterial->stateBitsTable,
                stateBitsByteCount"
    _material_state_table_resolution)
string(FIND
    "${_db_load_source}"
    "if (!DB_ValidateMaterialSemantics(varMaterial))
    {
        DB_PopStreamPos();
        return false;
    }
    DB_PopStreamPos();
    return true;
}"
    _material_semantic_validation)
string(FIND
    "${_db_load_source}"
    "if (!Load_Material(1))"
    _material_graph_load)
string(FIND
    "${_db_load_source}"
    "Load_MaterialAsset((XAssetHeader *)varMaterialHandle)"
    _material_asset_publication)
if (_material_state_table_resolution EQUAL -1
    OR _material_semantic_validation EQUAL -1
    OR _material_graph_load EQUAL -1
    OR _material_asset_publication EQUAL -1
    OR _material_state_table_resolution GREATER _material_semantic_validation
    OR _material_graph_load GREATER _material_asset_publication)
    message(FATAL_ERROR
        "Complete material graphs must pass semantic validation before asset publication")
endif()
string(FIND
    "${_db_load_source}"
    "const db::relocation::Status materialized = DB_ValidateStreamAddress("
    _material_shader_program_materialization)
string(FIND
    "${_db_load_source}"
    "if (!db::validation::D3D9ShaderBytecodeValid("
    _material_shader_bytecode_validation)
string(FIND
    "${_db_load_source}"
    "db::validation::D3D9ShaderStage::Vertex,
        varGfxVertexShaderLoadDef->loadForRenderer"
    _material_vertex_program_validation)
string(FIND
    "${_db_load_source}"
    "return Load_CreateMaterialVertexShader("
    _material_vertex_shader_creation)
string(FIND
    "${_db_load_source}"
    "db::validation::D3D9ShaderStage::Pixel,
        varGfxPixelShaderLoadDef->loadForRenderer"
    _material_pixel_program_validation)
string(FIND
    "${_db_load_source}"
    "return Load_CreateMaterialPixelShader("
    _material_pixel_shader_creation)
if (_material_shader_program_materialization EQUAL -1
    OR _material_shader_bytecode_validation EQUAL -1
    OR _material_vertex_program_validation EQUAL -1
    OR _material_vertex_shader_creation EQUAL -1
    OR _material_pixel_program_validation EQUAL -1
    OR _material_pixel_shader_creation EQUAL -1
    OR _material_shader_program_materialization GREATER _material_shader_bytecode_validation
    OR _material_vertex_program_validation GREATER _material_vertex_shader_creation
    OR _material_pixel_program_validation GREATER _material_pixel_shader_creation)
    message(FATAL_ERROR
        "Material shader programs must be materialized and validated before D3D creation")
endif()
file(STRINGS
    "${SOURCE_ROOT}/src/database/db_load.cpp"
    _legacy_direct_offsets
    REGEX "DB_ConvertOffsetToPointerLegacy")
list(LENGTH _legacy_direct_offsets _legacy_direct_offset_count)
if (NOT _legacy_direct_offset_count EQUAL 0)
    message(FATAL_ERROR
        "Expected no legacy direct fast-file offsets; found ${_legacy_direct_offset_count}. "
        "Migrations must update this debt gate.")
endif()

file(READ "${SOURCE_ROOT}/src/gfx_d3d/r_material.cpp" _material_runtime_source)
string(FIND
    "${_material_runtime_source}"
    "(IDirect3DVertexShader9 **)&mtlShader->prog"
    _untyped_vertex_shader_output)
string(FIND
    "${_material_runtime_source}"
    "(IDirect3DPixelShader9 **)&mtlShader->prog"
    _untyped_pixel_shader_output)
if (NOT _untyped_vertex_shader_output EQUAL -1
    OR NOT _untyped_pixel_shader_output EQUAL -1)
    message(FATAL_ERROR
        "Material shader creation must use typed COM output fields")
endif()
string(REGEX MATCH
    "(\\*inserted[ \\t]*=|inserted->)"
    _raw_alias_slot_write
    "${_db_load_source}")
if (_raw_alias_slot_write)
    message(FATAL_ERROR
        "Native pointer write to four-byte fast-file alias slot remains: ${_raw_alias_slot_write}")
endif()
string(REPLACE "->" "." _db_load_count_source "${_db_load_source}")
string(REGEX MATCH
    "Load_[A-Za-z0-9_]+Array[ \\t\\r\\n]*\\([ \\t\\r\\n]*1[ \\t\\r\\n]*,[^;]*(\\+|-|\\*|/|%|<<|>>|&|\\||\\^)[^;]*\\);"
    _raw_derived_count
    "${_db_load_count_source}")
if (_raw_derived_count)
    message(FATAL_ERROR
        "Unchecked derived fast-file array count remains in db_load.cpp: ${_raw_derived_count}")
endif()

# cpose_t::cullIn is shared by the renderer and client threads.  Keep its
# frozen engine layout exact while requiring every producer and consumer to use
# one protocol boundary; a plain read or assignment can otherwise lose a
# concurrent request.
require_source_match_count(
    "bgame/bg_local.h"
    "volatile[ \\t]+uint32_t[ \\t]+cullIn"
    2
    "both MP and SP pose state words must remain fixed-width atomic storage")
require_source_contains(
    "bgame/bg_local.h"
    "RUNTIME_OFFSET(cpose_t, cullIn, 0x8, 0x8);"
    "the MP pose atomic word must retain its x86 and native offsets")
require_source_contains(
    "bgame/bg_local.h"
    "RUNTIME_SIZE(cpose_t, 0x64, 0x68);"
    "the MP pose layout must retain its x86 and native sizes")
require_source_contains(
    "bgame/bg_local.h"
    "RUNTIME_OFFSET(cpose_t, cullIn, 0x10, 0x18);"
    "the SP pose atomic word must retain its x86 and native offsets")
require_source_contains(
    "bgame/bg_local.h"
    "RUNTIME_SIZE(cpose_t, 0x54, 0x60);"
    "the SP pose layout must retain its x86 and native sizes")

foreach(_native_pose_atomic_token
    "Windows.h"
    "windows.h"
    "Interlocked"
    "0xFFFFFFF0"
)
    require_source_not_contains(
        "cgame/cg_pose_atomic.h"
        "${_native_pose_atomic_token}"
        "the pose atomic protocol must remain platform-neutral")
endforeach()
require_source_not_matches(
    "cgame/cg_pose_atomic.h"
    "(^|[^A-Za-z0-9_])LONG([^A-Za-z0-9_]|$)"
    "the pose atomic protocol must not use a native Windows word")
require_source_contains(
    "cgame/cg_pose_atomic.h"
    "(void)Sys_AtomicCompareExchange(state, kUsed, kIdle);"
    "a used notification must only claim idle state and never downgrade culled")
require_source_contains(
    "cgame/cg_pose_atomic.h"
    "Sys_AtomicStore(state, kCulled);"
    "a culled notification must publish the strongest pending state")
require_source_contains(
    "cgame/cg_pose_atomic.h"
    "return Sys_AtomicExchange(state, kIdle);"
    "pose consumers must atomically claim and clear one pending request")
require_source_contains(
    "cgame/cg_pose_atomic.h"
    "return Sys_AtomicLoad(state);"
    "non-consuming pose readers must use an atomic load")
require_source_match_count(
    "cgame/cg_pose_atomic.h"
    "Sys_AtomicStore\\(state, kIdle\\)"
    1
    "pose reset must atomically restore idle state")

file(GLOB_RECURSE _pose_access_sources
    "${SOURCE_ROOT}/src/*.cpp"
    "${SOURCE_ROOT}/src/*.h")
set(_pose_field_access_count 0)
set(_wrapped_pose_field_access_count 0)
foreach(_pose_access_source IN LISTS _pose_access_sources)
    file(READ "${_pose_access_source}" _pose_access_text)
    string(REGEX MATCHALL
        "(\\.|->)cullIn"
        _pose_field_accesses
        "${_pose_access_text}")
    string(REGEX MATCHALL
        "cg::pose_atomic::(MarkUsed|MarkCulled|Consume|Peek|Reset)[ \\t\\r\\n]*\\([^;]*(\\.|->)cullIn"
        _wrapped_pose_field_accesses
        "${_pose_access_text}")
    list(LENGTH _pose_field_accesses _pose_source_access_count)
    list(LENGTH _wrapped_pose_field_accesses _wrapped_pose_source_access_count)
    math(EXPR _pose_field_access_count
        "${_pose_field_access_count} + ${_pose_source_access_count}")
    math(EXPR _wrapped_pose_field_access_count
        "${_wrapped_pose_field_access_count} + ${_wrapped_pose_source_access_count}")
endforeach()
if (NOT _pose_field_access_count EQUAL 10
    OR NOT _pose_field_access_count EQUAL _wrapped_pose_field_access_count)
    message(FATAL_ERROR
        "Every cpose_t::cullIn field access must use cg::pose_atomic: "
        "${_wrapped_pose_field_access_count}/${_pose_field_access_count} wrapped "
        "(expected 10 current consumers). Update this topology gate only for an "
        "intentional, helper-wrapped consumer change.")
endif()

# Skeleton caches use a fixed-width cursor and generation word embedded in
# frozen engine structures.  The shared helper owns arithmetic and atomic
# transitions without importing a native ABI-sized atomic type.
foreach(_skel_layout_header
    "client/client.h"
    "client_mp/client_mp.h"
    "server/server.h"
    "server_mp/server_mp.h"
)
    require_source_match_count(
        "${_skel_layout_header}"
        "volatile[ \\t]+uint32_t[ \\t]+skelTimeStamp"
        1
        "skeleton epochs must remain exact-width storage")
    require_source_match_count(
        "${_skel_layout_header}"
        "volatile[ \\t]+uint32_t[ \\t]+skelMemPos"
        1
        "skeleton arena cursors must remain exact-width storage")
endforeach()

foreach(_native_skel_atomic_token
    "Windows.h"
    "windows.h"
    "Interlocked"
    "0xFFFFFFF0"
)
    require_source_not_contains(
        "qcommon/skel_memory_atomic.h"
        "${_native_skel_atomic_token}"
        "the skeleton arena protocol must remain platform-neutral")
endforeach()
require_source_not_matches(
    "qcommon/skel_memory_atomic.h"
    "(^|[^A-Za-z0-9_])LONG([^A-Za-z0-9_]|$)"
    "the skeleton arena protocol must not use a native Windows word")
require_source_contains(
    "qcommon/skel_memory_atomic.h"
    "class ResetGuard"
    "skeleton resetters must be serialized across the complete publication scope")
require_source_contains(
    "qcommon/skel_memory_atomic.h"
    "while (Sys_AtomicCompareExchange(gate_, 1u, 0u) != 0u)"
    "the skeleton reset guard must claim one fixed-width owner")
require_source_contains(
    "qcommon/skel_memory_atomic.h"
    "Sys_AtomicStore(gate_, 0u);"
    "the skeleton reset guard must release ownership atomically")
require_source_contains(
    "qcommon/skel_memory_atomic.h"
    "value != 0u && (value & (value - 1u)) == 0u"
    "arena alignment must be a nonzero power of two")
require_source_contains(
    "qcommon/skel_memory_atomic.h"
    "if (value > kInvalidOffset - mask)"
    "aligned reservation sizes must reject uint32 overflow")
require_source_contains(
    "qcommon/skel_memory_atomic.h"
    "if (!storage || storageBytes > kInvalidOffset
        || !IsPowerOfTwo(alignment))"
    "arena views must reject null, oversized, and invalidly aligned storage")
require_source_contains(
    "qcommon/skel_memory_atomic.h"
    "static_cast<std::uint32_t>(storageBytes) - padding"
    "arena capacity must account for base-alignment padding")
require_source_contains(
    "qcommon/skel_memory_atomic.h"
    "if ((observed & (alignment - 1u)) != 0u
            || observed > capacity
            || reservedBytes > capacity - observed)"
    "reservations must reject corrupt alignment, invalid cursors, and overflow-safe exhaustion")
require_source_contains(
    "qcommon/skel_memory_atomic.h"
    "const std::uint32_t previous = Sys_AtomicCompareExchange(
            cursor,
            desired,
            observed);"
    "arena cursor publication must use a checked compare/exchange")
require_source_contains(
    "qcommon/skel_memory_atomic.h"
    "if (desired == 0u)
        {
            if (onWrap)
                onWrap();
            desired = 1u;
        }"
    "zero must remain reserved when the skeleton epoch rolls over")
require_source_contains(
    "qcommon/skel_memory_atomic.h"
    "void (*const onWrap)() = nullptr"
    "skeleton epoch advancement must accept an invalidation callback for full-cycle reuse")
require_source_ordered(
    "qcommon/skel_memory_atomic.h"
    "onWrap();"
    "const std::uint32_t previous = Sys_AtomicCompareExchange(
            epoch,"
    "full-cycle invalidation must run before epoch one can be published")
require_source_contains(
    "qcommon/skel_memory_atomic.h"
    "return std::bit_cast<std::int32_t>(epoch);"
    "engine timestamps must preserve the unsigned epoch bit pattern")
require_source_contains(
    "qcommon/skel_memory_atomic.h"
    "const std::uint32_t previous = Sys_AtomicCompareExchange(
            warnedEpoch,
            epoch,
            observed);"
    "skeleton exhaustion warnings must be claimed once per epoch")

set(_skel_migrated_sources
    "client/cl_cgame.cpp"
    "client/cl_main.cpp"
    "client/cl_pose.cpp"
    "client_mp/cl_cgame_mp.cpp"
    "client_mp/cl_main_mp.cpp"
    "client_mp/cl_pose_mp.cpp"
    "server/sv_game.cpp"
    "server/sv_init.cpp"
)
foreach(_skel_migrated_source IN LISTS _skel_migrated_sources)
    foreach(_retired_skel_token
        "Interlocked"
        "0xFFFFFFF0"
        "warnCount"
    )
        require_source_not_contains(
            "${_skel_migrated_source}"
            "${_retired_skel_token}"
            "migrated skeleton code must not restore native atomics, hardcoded masks, or racy warning counters")
    endforeach()
    require_source_not_matches(
        "${_skel_migrated_source}"
        "(^|[^A-Za-z0-9_])LONG([^A-Za-z0-9_]|$)"
        "migrated skeleton code must not use a native Windows word")
endforeach()
require_source_not_contains(
    "client_mp/cl_pose_mp.cpp"
    "0x3FFF0"
    "MP skeleton allocation must use exact aligned arena capacity instead of a hardcoded under-allocation")

foreach(_skel_arena_consumer
    "client/cl_main.cpp"
    "client/cl_pose.cpp"
    "client_mp/cl_main_mp.cpp"
    "client_mp/cl_pose_mp.cpp"
    "server/sv_game.cpp"
)
    require_all_occurrences_wrapped(
        "${_skel_arena_consumer}"
        "skel_memory_atomic::MakeArenaView"
        "skel_memory_atomic::MakeArenaView[ \\t\\r\\n]*\\([^;]*sizeof[ \\t\\r\\n]*\\("
        "every skeleton arena view must derive capacity from its actual backing array")
endforeach()
foreach(_skel_allocator
    "client/cl_pose.cpp"
    "client_mp/cl_pose_mp.cpp"
    "server/sv_game.cpp"
)
    require_source_contains(
        "${_skel_allocator}"
        "alignedSize <= arena.capacity"
        "skeleton allocation must reject requests larger than exact aligned capacity")
    require_all_occurrences_wrapped(
        "${_skel_allocator}"
        "skel_memory_atomic::ReserveAligned"
        "skel_memory_atomic::ReserveAligned[ \\t\\r\\n]*\\([^;]*skelMemPos[^;]*arena.capacity"
        "skeleton reservations must use their embedded atomic cursor and exact arena capacity")
endforeach()

# ResetGuard owns the complete arena publication scope.  Within it, base and
# cursor publication precede generation advancement, whose wrap callback
# invalidates old DObj skeletons before epoch one becomes observable.
require_source_ordered(
    "client/cl_main.cpp"
    "const skel_memory_atomic::ResetGuard resetGuard(&s_skelResetGate);"
    "clients[0].skelMemoryStart = arena.base;"
    "SP client reset ownership must cover arena publication")
require_source_ordered(
    "client/cl_main.cpp"
    "clients[0].skelMemoryStart = arena.base;"
    "Sys_AtomicStore(&clients[0].skelMemPos, 0u);"
    "SP client skeleton reset must publish its aligned base before its empty cursor")
require_source_ordered(
    "client/cl_main.cpp"
    "Sys_AtomicStore(&clients[0].skelMemPos, 0u);"
    "skel_memory_atomic::AdvanceEpoch("
    "SP client skeleton reset must empty its cursor before advancing its epoch")
require_source_contains(
    "client/cl_main.cpp"
    "skel_memory_atomic::AdvanceEpoch(
        &clients[0].skelTimeStamp,
        Com_ClientDObjClearAllSkel);"
    "SP client epoch reuse must atomically couple client DObj invalidation")
require_source_ordered(
    "client_mp/cl_main_mp.cpp"
    "const skel_memory_atomic::ResetGuard resetGuard(&s_skelResetGate);"
    "skelGlob->skelMemoryStart = arena.base;"
    "MP client reset ownership must cover arena publication")
require_source_ordered(
    "client_mp/cl_main_mp.cpp"
    "skelGlob->skelMemoryStart = arena.base;"
    "Sys_AtomicStore(&skelGlob->skelMemPos, 0u);"
    "MP client skeleton reset must publish its aligned base before its empty cursor")
require_source_ordered(
    "client_mp/cl_main_mp.cpp"
    "Sys_AtomicStore(&skelGlob->skelMemPos, 0u);"
    "skel_memory_atomic::AdvanceEpoch("
    "MP client skeleton reset must empty its cursor before advancing its epoch")
require_source_contains(
    "client_mp/cl_main_mp.cpp"
    "skel_memory_atomic::AdvanceEpoch(
        &skelGlob->skelTimeStamp,
        Com_ClientDObjClearAllSkel);"
    "MP client epoch reuse must atomically couple client DObj invalidation")
require_source_ordered(
    "server/sv_game.cpp"
    "const skel_memory_atomic::ResetGuard resetGuard(&s_skelResetGate);"
    "g_sv_skel_memory_start = arena.base;"
    "server reset ownership must cover arena publication")
require_source_ordered(
    "server/sv_game.cpp"
    "g_sv_skel_memory_start = arena.base;"
    "Sys_AtomicStore(&sv.skelMemPos, 0u);"
    "server skeleton reset must publish its aligned base before its empty cursor")
require_source_ordered(
    "server/sv_game.cpp"
    "Sys_AtomicStore(&sv.skelMemPos, 0u);"
    "skel_memory_atomic::AdvanceEpoch("
    "server skeleton reset must empty its cursor before advancing its epoch")
require_source_contains(
    "server/sv_game.cpp"
    "skel_memory_atomic::AdvanceEpoch(
        &sv.skelTimeStamp,
        Com_ServerDObjClearAllSkel);"
    "server epoch reuse must atomically couple server DObj invalidation")
require_source_contains(
    "server/sv_init.cpp"
    "Sys_AtomicStore(&sv.skelTimeStamp, 0u);"
    "server startup must initialize its atomic skeleton epoch through the portable boundary")

require_source_contains(
    "qcommon/qcommon.h"
    "void __cdecl Com_ServerDObjClearAllSkel();"
    "the full-cycle server skeleton invalidation routine must be publicly declared")
require_source_contains(
    "qcommon/dobj_management.cpp"
    "void __cdecl Com_ServerDObjClearAllSkel()"
    "the full-cycle server skeleton invalidation routine must be implemented")
require_source_contains(
    "qcommon/dobj_management.cpp"
    "for (int handle = 0; handle < SERVER_DOBJ_HANDLE_MAX; ++handle)"
    "server full-cycle invalidation must visit every possible DObj handle")
require_source_contains(
    "qcommon/dobj_management.cpp"
    "if (serverObjMap[handle])
            DObjSkelClear(&objBuf[serverObjMap[handle]]);"
    "server full-cycle invalidation must clear every live mapped DObj skeleton")

# Allocation exhaustion can reset and advance the server arena, so both DObj
# creation paths must reload the generation immediately before publishing a
# skeleton.  The only two DObjCreateSkel calls in this allocator unit follow
# their checked SV_AllocSkelMemory calls.
file(READ "${SOURCE_ROOT}/src/server/sv_game.cpp" _sv_skel_protocol_source)
string(REPLACE ";" "" _sv_skel_protocol_source "${_sv_skel_protocol_source}")
string(REGEX MATCHALL
    "timeStamp = skel_memory_atomic::LoadTimestamp\\(&sv.skelTimeStamp\\)
    DObjCreateSkel\\(obj, buf, timeStamp\\)"
    _sv_post_allocation_epoch_reloads
    "${_sv_skel_protocol_source}")
list(LENGTH _sv_post_allocation_epoch_reloads _sv_post_allocation_epoch_reload_count)
if (NOT _sv_post_allocation_epoch_reload_count EQUAL 2)
    message(FATAL_ERROR
        "Server DObj creation must reload the skeleton epoch immediately "
        "before publication: expected 2 paths, found "
        "${_sv_post_allocation_epoch_reload_count}")
endif()
require_source_match_count(
    "server/sv_game.cpp"
    "Com_Error\\([^;]*invalid skeleton allocation"
    2
    "server skeleton allocation failures must drop instead of reporting a usable matrix")

# Catch any new raw field access anywhere in production code.  Epoch reads and
# transitions remain inside skel_memory_atomic, while startup initialization
# retains the same fixed-width Sys_AtomicStore boundary.
file(GLOB_RECURSE _skel_access_sources
    "${SOURCE_ROOT}/src/*.cpp"
    "${SOURCE_ROOT}/src/*.h")
set(_skel_cursor_access_count 0)
set(_wrapped_skel_cursor_access_count 0)
set(_skel_epoch_access_count 0)
set(_wrapped_skel_epoch_access_count 0)
foreach(_skel_access_source IN LISTS _skel_access_sources)
    file(READ "${_skel_access_source}" _skel_access_text)
    string(REGEX MATCHALL
        "(\\.|->)skelMemPos"
        _skel_cursor_accesses
        "${_skel_access_text}")
    string(REGEX MATCHALL
        "(Sys_AtomicStore|skel_memory_atomic::[A-Za-z0-9_]+)[ \\t\\r\\n]*\\([^;]*(\\.|->)skelMemPos"
        _wrapped_skel_cursor_accesses
        "${_skel_access_text}")
    string(REGEX MATCHALL
        "(\\.|->)skelTimeStamp"
        _skel_epoch_accesses
        "${_skel_access_text}")
    string(REGEX MATCHALL
        "(Sys_AtomicStore|skel_memory_atomic::[A-Za-z0-9_]+)[ \\t\\r\\n]*\\([^;]*(\\.|->)skelTimeStamp"
        _wrapped_skel_epoch_accesses
        "${_skel_access_text}")
    list(LENGTH _skel_cursor_accesses _skel_source_cursor_count)
    list(LENGTH _wrapped_skel_cursor_accesses _wrapped_skel_source_cursor_count)
    list(LENGTH _skel_epoch_accesses _skel_source_epoch_count)
    list(LENGTH _wrapped_skel_epoch_accesses _wrapped_skel_source_epoch_count)
    math(EXPR _skel_cursor_access_count
        "${_skel_cursor_access_count} + ${_skel_source_cursor_count}")
    math(EXPR _wrapped_skel_cursor_access_count
        "${_wrapped_skel_cursor_access_count} + ${_wrapped_skel_source_cursor_count}")
    math(EXPR _skel_epoch_access_count
        "${_skel_epoch_access_count} + ${_skel_source_epoch_count}")
    math(EXPR _wrapped_skel_epoch_access_count
        "${_wrapped_skel_epoch_access_count} + ${_wrapped_skel_source_epoch_count}")
endforeach()
if (_skel_cursor_access_count EQUAL 0
    OR NOT _skel_cursor_access_count EQUAL _wrapped_skel_cursor_access_count)
    message(FATAL_ERROR
        "Every skeleton cursor field access must use the portable boundary: "
        "${_wrapped_skel_cursor_access_count}/${_skel_cursor_access_count} wrapped")
endif()
if (_skel_epoch_access_count EQUAL 0
    OR NOT _skel_epoch_access_count EQUAL _wrapped_skel_epoch_access_count)
    message(FATAL_ERROR
        "Every skeleton epoch field access must use the portable boundary: "
        "${_wrapped_skel_epoch_access_count}/${_skel_epoch_access_count} wrapped")
endif()

# Renderer worker producers reserve fixed-array ranges through one portable,
# bounded CAS protocol.  Exact-width storage prevents C long from widening and
# shifting the scene/back-end layouts on Linux and macOS.
require_source_not_contains(
    "gfx_d3d/r_reservation_atomic.h"
    "Interlocked"
    "renderer reservations must remain independent of native Windows atomics")
require_source_not_matches(
    "gfx_d3d/r_reservation_atomic.h"
    "(^|[^A-Za-z0-9_])LONG([^A-Za-z0-9_]|$)"
    "renderer reservations must not use a native Windows word")
require_source_contains(
    "gfx_d3d/r_reservation_atomic.h"
    "observed > capacity || count > capacity - observed"
    "renderer reservation capacity checks must not use overflow-prone addition")
require_source_contains(
    "gfx_d3d/r_reservation_atomic.h"
    "Sys_AtomicCompareExchange("
    "renderer reservations must atomically claim unique ranges")
require_source_not_contains(
    "gfx_d3d/r_drawsurf.cpp"
    "Interlocked"
    "draw-surface reservations must use the portable bounded protocol")
require_source_match_count(
    "gfx_d3d/r_drawsurf.cpp"
    "gfx::reservation_atomic::TryReserve(Index)?[ \t\r\n]*\\("
    4
    "all draw-surface counter reservations must use the bounded protocol")
require_source_contains(
    "gfx_d3d/r_drawsurf.cpp"
    "region >= static_cast<uint32_t>(DRAW_SURF_TYPE_COUNT)"
    "FX draw-surface regions must be validated before indexing scene arrays")
require_source_contains(
    "gfx_d3d/r_drawsurf.cpp"
    "region >= static_cast<uint32_t>(DRAW_SURF_FX_CAMERA_LIT)"
    "atomic draw-surface reservations must remain limited to FX-owned regions")
require_source_contains(
    "gfx_d3d/r_drawsurf.cpp"
    "if (!argOffsetOut || argCount < 0 || argCount > CODE_MESH_ARG_COUNT)"
    "code-mesh argument reservations must validate release-build inputs")
require_source_contains(
    "gfx_d3d/r_drawsurf.cpp"
    "static constexpr int CODE_MESH_ARG_COUNT = 2;"
    "code-mesh argument ranges must fit the two dedicated shader constants")
require_source_contains(
    "gfx_d3d/r_scene.h"
    "volatile uint32_t drawSurfCount[34];"
    "draw-surface reservation counters must retain exact 32-bit layout")
require_source_not_contains(
    "gfx_d3d/r_scene.h"
    "volatile long drawSurfCount"
    "draw-surface counters must not widen under LP64")
foreach(_renderer_counter
    "codeMeshCount"
    "codeMeshArgsCount"
    "markMeshCount"
)
    require_source_contains(
        "gfx_d3d/r_rendercmds.h"
        "volatile uint32_t ${_renderer_counter};"
        "renderer back-end reservation counters must retain exact 32-bit layout")
    require_source_not_contains(
        "gfx_d3d/r_rendercmds.h"
        "volatile long ${_renderer_counter}"
        "renderer back-end reservation counters must not widen under LP64")
    require_source_contains(
        "gfx_d3d/r_rendercmds.cpp"
        "Sys_AtomicStore(&frontEndDataOut->${_renderer_counter}, 0u);"
        "renderer back-end reservation counters must reset through their atomic boundary")
endforeach()
require_source_contains(
    "gfx_d3d/r_scene.cpp"
    "Sys_AtomicStore(&scene.drawSurfCount[drawSurfType], 0u);"
    "draw-surface counters must reset through their atomic boundary")
require_source_contains(
    "gfx_d3d/r_pretess.cpp"
    "Sys_AtomicLoad(&scene.drawSurfCount[stageIndex]);"
    "draw-surface merging must acquire each worker-published FX extent")
require_source_contains(
    "gfx_d3d/r_pretess.cpp"
    "Sys_AtomicStore(
                    &scene.drawSurfCount[stageIndex],"
    "draw-surface clamping must remain inside the atomic ownership boundary")
require_source_contains(
    "gfx_d3d/rb_tess.cpp"
    "codeMeshArgsCount = Sys_AtomicLoad(&data->codeMeshArgsCount);"
    "the renderer back end must acquire the published code-mesh argument extent")
require_source_contains(
    "gfx_d3d/rb_tess.cpp"
    "codeMeshCount = Sys_AtomicLoad(&data->codeMeshCount);"
    "the renderer back end must acquire the published code-mesh record extent")
require_source_ordered(
    "gfx_d3d/rb_tess.cpp"
    "if (objectId >= codeMeshCount)"
    "codeMesh = &data->codeMeshes[objectId];"
    "code-mesh object IDs must be bounded before the backing array is indexed")
require_source_contains(
    "gfx_d3d/rb_tess.cpp"
    "codeMesh->triCount <= kCodeMeshTriangleLimit"
    "code-mesh triangle counts must be bounded before byte arithmetic")
require_source_contains(
    "gfx_d3d/rb_tess.cpp"
    "<= indexBufferAddress + kCodeMeshIndexBytes
                    - codeMeshIndexAddress;"
    "code-mesh index spans must use subtraction-based bounds validation")
require_source_contains(
    "gfx_d3d/rb_tess.cpp"
    "argCount > codeMeshArgsCount - argOffset"
    "the renderer back end must reject malformed argument ranges without overflow")
require_source_contains(
    "gfx_d3d/rb_tess.cpp"
    "if (!R_TessCodeMeshList_AddCodeMeshArgs("
    "malformed code-mesh arguments must fail the owning draw-surface record closed")
require_source_contains(
    "gfx_d3d/rb_tess.cpp"
    "argGlobalIndex);
            return false;"
    "an invalid code-mesh argument index must not reach the backing array")
require_source_contains(
    "gfx_d3d/r_pretess.cpp"
    "stageCapacity < freeDrawSurfCount"
    "draw-surface merging must clamp each extent to its configured backing array")

# Renderer commands use typed native-width payloads and a bounded MPMC ring.
# Short producer/consumer guards protect byte publication and dequeue copies;
# handlers execute outside the guards, and one outstandingCount owns queued and
# inline work through handler completion.
require_source_not_contains(
    "gfx_d3d/r_worker_queue_atomic.h"
    "Interlocked"
    "worker queue protocols must remain independent of native Windows atomics")
require_source_not_matches(
    "gfx_d3d/r_worker_queue_atomic.h"
    "(^|[^A-Za-z0-9_])LONG([^A-Za-z0-9_]|$)"
    "worker queue protocols must use exact-width state")
require_source_contains(
    "gfx_d3d/r_worker_queue_atomic.h"
    "observed > capacity || amount > capacity - observed"
    "worker queue capacity claims must not use overflow-prone addition")
require_source_contains(
    "gfx_d3d/r_worker_queue_atomic.h"
    "amount > observed"
    "worker queue completion must reject underflow")
require_source_contains(
    "gfx_d3d/r_worker_queue_atomic.h"
    "TryAcquireGuard"
    "worker queue payload transitions must have a tested short ownership guard")
require_source_not_contains(
    "gfx_d3d/r_workercmds.cpp"
    "Interlocked"
    "renderer worker queues must use the portable fixed-width protocol")
foreach(_worker_native_token
    "LONG"
    "_WORD"
    "NET_Sleep"
    "win32/"
    "g_workerCmdMinType"
    "g_workerCmdWaitCount"
    "syncedEndPos"
)
    require_source_not_contains(
        "gfx_d3d/r_workercmds.cpp"
        "${_worker_native_token}"
        "worker queue source must not retain ${_worker_native_token}")
endforeach()
require_source_not_matches(
    "gfx_d3d/r_workercmds.cpp"
    "\\.(dataSize|bufSize)[ 	]*=[ 	]*(0x[0-9A-Fa-f]+|[0-9]+)[uU]?;"
    "worker payload descriptors must derive native sizes from typed buffers")
require_source_match_count(
    "gfx_d3d/r_workercmds.cpp"
    "R_BindWorkerCmdBuffer<WRKCMD_"
    17
    "every worker command must bind one typed payload buffer")
require_source_contains(
    "gfx_d3d/r_workercmds.cpp"
    "alignas(std::max_align_t) uint8_t data["
    "dequeued command payloads must retain native alignment")
require_source_contains(
    "gfx_d3d/r_workercmds.cpp"
    "TryAcquireGuard(&cmd.consumerGuard)"
    "consumer claim/copy/cursor transitions must be serialized against ABA")
require_source_contains(
    "gfx_d3d/r_workercmds.cpp"
    "TryAcquireGuard(&cmd.producerGuard)"
    "producer reserve/copy/publication transitions must remain ordered")
require_source_ordered(
    "gfx_d3d/r_workercmds.cpp"
    "const uint32_t readyCount = Sys_AtomicLoad(&cmd.outSize);"
    "const WorkerBusyPredicate busyPredicate = g_cmdOutputBusy[type];"
    "idle queue scans must not execute dependency predicates")
require_source_not_contains(
    "gfx_d3d/r_workercmds.cpp"
    "Sys_AtomicStore(guard, 0u)"
    "failed guard ownership must quarantine rather than force-unlock a queue")
require_source_contains(
    "gfx_d3d/r_workercmds.cpp"
    "Com_Error(ERR_FATAL, \"Renderer worker queue invariant failed: %s\", detail);"
    "impossible queue transitions must fail closed in release builds")
require_source_ordered(
    "gfx_d3d/r_workercmds.cpp"
    "R_ProcessWorkerCmdInternal(type, const_cast<void *>(data));"
    "const bool releasedInlineProducer = worker_atomic::TrySubtractBounded("
    "inline fallback must retain producer ownership through handler completion")
require_source_ordered(
    "gfx_d3d/r_workercmds.cpp"
    "R_ReleaseWorkerGuard(&cmd.producerGuard)"
    "R_WarnOncePerFrame(R_WARN_WORKER_CMD_SIZE, type);"
    "queue-full diagnostics must execute outside the producer guard")
require_source_contains(
    "gfx_d3d/r_workercmds.cpp"
    "return Sys_AtomicLoad(&cmd.outstandingCount) != 0u;"
    "wait predicates must use one linearizable full-lifetime work count")
require_source_ordered(
    "gfx_d3d/r_workercmds.cpp"
    "R_ProcessWorkerCmdInternal("
    "TrySubtractBounded(
            &cmd.outstandingCount,
            count,"
    "queued work must remain outstanding through handler completion")
require_source_contains(
    "gfx_d3d/r_workercmds.h"
    "RUNTIME_SIZE(WorkerCmds, 0x80, 0x88);"
    "worker queue storage must retain its native-width layout contract")
require_source_match_count(
    "gfx_d3d/r_workercmds.h"
    "KISAK_WORKER_CMD_PAYLOAD\\(WRKCMD_"
    17
    "every public worker command must map to one compile-time payload type")
require_source_ordered(
    "gfx_d3d/r_init.cpp"
    "R_InitWorkerCmds()"
    "R_InitRenderThread();"
    "worker descriptors must publish before the backend thread can scan them")
require_source_contains(
    "gfx_d3d/r_scene.cpp"
    "ShadowCookieCmd shadowCookieCmd{};"
    "shadow-cookie commands must preserve native-width pointers")
require_source_not_contains(
    "gfx_d3d/r_scene.cpp"
    "data[0] = (uint32_t)viewParmsDpvs;"
    "shadow-cookie command pointers must not truncate to retail x86 words")
require_source_not_contains(
    "gfx_d3d/r_scene.cpp"
    "LongNoSwap((uint32_t)"
    "scene lighting handles must not truncate their owning pose pointers")
require_source_not_contains(
    "gfx_d3d/r_dpvs.cpp"
    "LongNoSwap((uint32_t)sceneEnt->info.cachedLightingHandle)"
    "DPVS lighting handles must retain native pointer width")
require_source_contains(
    "gfx_d3d/r_dpvs_entity.cpp"
    "R_AddEntitySurfacesInFrustumCmd(const DpvsEntityCmd *cmd)"
    "DPVS entity commands must decode through their typed native layout")
foreach(_dpvs_x86_decode
    "(uint32_t *)data"
    "data[4]"
    "data[5]"
)
    require_source_not_contains(
        "gfx_d3d/r_dpvs_entity.cpp"
        "${_dpvs_x86_decode}"
        "DPVS worker dispatch must not retain x86 word-offset decoding")
endforeach()
require_source_not_contains(
    "gfx_d3d/r_init.cpp"
    "(int(*)())R_GpuFenceTimeout"
    "GPU wait callbacks must retain an exact function type")

file(GLOB _worker_producer_sources "${SOURCE_ROOT}/src/gfx_d3d/*.cpp")
set(_typed_worker_producer_count 0)
foreach(_worker_source IN LISTS _worker_producer_sources)
    file(READ "${_worker_source}" _worker_source_text)
    string(REGEX MATCHALL "R_AddWorkerCmd<WRKCMD_" _typed_worker_calls "${_worker_source_text}")
    list(LENGTH _typed_worker_calls _typed_worker_file_count)
    math(EXPR _typed_worker_producer_count
        "${_typed_worker_producer_count} + ${_typed_worker_file_count}")
    string(REGEX MATCH "R_AddWorkerCmd[ 	]*\\(" _untyped_worker_call "${_worker_source_text}")
    if (NOT "${_untyped_worker_call}" STREQUAL "")
        message(FATAL_ERROR
            "Untyped worker command producer remains in ${_worker_source}")
    endif()
endforeach()
if (NOT _typed_worker_producer_count EQUAL 17)
    message(FATAL_ERROR
        "Expected 17 typed worker producers, found ${_typed_worker_producer_count}")
endif()

foreach(_worker_layout_contract
    "gfx_d3d/fxprimitives.h|RUNTIME_SIZE(FxCmd, 0xC, 0x18);"
    "gfx_d3d/r_dpvs.h|RUNTIME_SIZE(DpvsDynamicCellCmd, 0xC, 0x10);"
    "gfx_d3d/r_dpvs.h|RUNTIME_SIZE(DpvsStaticCellCmd, 0xC, 0x18);"
    "gfx_d3d/r_dpvs.h|RUNTIME_SIZE(DpvsEntityCmd, 0x10, 0x20);"
    "gfx_d3d/r_scene.h|RUNTIME_SIZE(SceneEntCmd, 0x4, 0x8);"
    "gfx_d3d/r_spotshadow.h|RUNTIME_SIZE(GfxSpotShadowEntCmd, 0x8, 0x10);"
    "gfx_d3d/r_rendercmds.h|RUNTIME_SIZE(ShadowCookieCmd, 0x10, 0x20);"
    "EffectsCore/fx_system.h|RUNTIME_SIZE(FxGenerateVertsCmd, 0x44, 0x58);"
    "gfx_d3d/r_staticmodelcache.h|RUNTIME_SIZE(SkinCachedStaticModelCmd, 0x4, 0x4);"
    "gfx_d3d/r_dobj_skin.h|RUNTIME_SIZE(SkinXModelCmd, 0x1C, 0x28);"
)
    string(REPLACE "|" ";" _worker_layout_parts "${_worker_layout_contract}")
    list(GET _worker_layout_parts 0 _worker_layout_file)
    list(GET _worker_layout_parts 1 _worker_layout_text)
    require_source_contains(
        "${_worker_layout_file}"
        "${_worker_layout_text}"
        "worker payloads must freeze their 32/64-bit native layouts")
endforeach()

# Model-surface streams are heterogeneous native records.  Every producer and
# walker must share one aligned framing contract so a hidden dword cannot
# misalign the next pointer-bearing record on 64-bit targets.
require_source_not_contains(
    "gfx_d3d/r_model_surface_stream.h"
    "Interlocked"
    "model-surface framing must remain independent of native Windows atomics")
require_source_contains(
    "gfx_d3d/r_model_surface_stream.h"
    "HiddenRecordSize"
    "hidden records must preserve native alignment for the following record")
require_source_contains(
    "gfx_d3d/r_model_surface_stream.h"
    "TryReserveAligned"
    "the shared scene arena must use a tested aligned bounded reservation")
require_source_contains(
    "gfx_d3d/r_model_skin.cpp"
    "streamBytes <= end - streamAddress"
    "model-surface span checks must use subtraction rather than wrapping addition")
foreach(_model_surface_layout
    "RUNTIME_SIZE(GfxModelHiddenSurface, 0x4, 0x8);"
    "RUNTIME_SIZE(GfxModelSurfaceInfo, 0xC, 0x10);"
    "RUNTIME_SIZE(GfxModelSkinnedSurface, 0x18, 0x28);"
    "RUNTIME_SIZE(GfxModelRigidSurface, 0x38, 0x48);"
    "RUNTIME_OFFSET(SkinXModelCmd, modelSurfWordCount, 0x1A, 0x22);"
)
    require_source_contains(
        "gfx_d3d/r_dobj_skin.h"
        "${_model_surface_layout}"
        "model-surface records must freeze their native-width layout")
endforeach()
foreach(_unsafe_dobj_skin_token
    "Interlocked"
    "surfsBuffer[150"
    "reinterpret_cast<uint>"
    "(int)&frontEndDataOut->tempSkinBuf"
)
    require_source_not_contains(
        "gfx_d3d/r_dobj_skin.cpp"
        "${_unsafe_dobj_skin_token}"
        "DObj skinning must not retain ${_unsafe_dobj_skin_token}")
endforeach()
require_source_ordered(
    "gfx_d3d/r_dobj_skin.cpp"
    "R_PlanDObjSkinStream(
            *sceneEnt,
            *obj,
            hideBits,
            fastFileLoad,
            &plan)"
    "TryReserveAligned(
            &frontEndDataOut->surfPos"
    "DObj skin records must be capacity-proven before the shared arena is reserved")
require_source_ordered(
    "gfx_d3d/r_dobj_skin.cpp"
    "sceneEnt->cull.skinnedSurfs.surfCount ="
    "sceneEnt->cull.skinnedSurfs.firstSurf = stream;"
    "a DObj stream pointer must publish only after its exact bounds")
require_source_contains(
    "gfx_d3d/r_model_skin.cpp"
    "R_ValidateSkinXModelStream(skinCmd, &streamOwner)"
    "the skin worker must validate the complete bounded stream before execution")
require_source_not_matches(
    "gfx_d3d/r_model_skin.cpp"
    "surfPos[^\r\n]*(\\+ 4|\\+ 56)"
    "the skin worker must not retain retail byte strides")
require_source_contains(
    "gfx_d3d/r_scene.cpp"
    "model_surface_stream::Cursor cursor("
    "frontend DObj walkers must share the bounded stream parser")
require_source_not_matches(
    "gfx_d3d/r_scene.cpp"
    "surfSize[ \t]*=[ \t]*(4|24|56)"
    "frontend DObj walkers must not retain retail byte strides")
require_source_not_contains(
    "gfx_d3d/r_model.cpp"
    "uint8_t surfBuf[3580]"
    "single-model skin records must not use the undersized retail stack buffer")
require_source_contains(
    "gfx_d3d/r_dpvs.cpp"
    "sizeof(BModelSurface)"
    "brush-model arena reservations must account for native pointer width")
require_source_contains(
    "gfx_d3d/r_rendercmds.h"
    "volatile uint32_t surfPos;"
    "the shared model-surface arena cursor must remain exactly 32 bits")
require_source_contains(
    "gfx_d3d/r_rendercmds.h"
    "volatile uint32_t tempSkinPos;"
    "the temporary skin arena cursor must remain exactly 32 bits")
require_source_contains(
    "xanim/xmodel_utils.cpp"
    "const XModelLodInfo &lodInfo = model->lodInfo[lod];"
    "surface lookup must validate the selected LOD rather than LOD zero")
require_source_contains(
    "database/db_load.cpp"
    "db::validation::XSurfaceBlendRecordsValid("
    "completed weighted surfaces must validate every bone record before publication")
require_source_contains(
    "database/db_load.cpp"
    "db::validation::XSurfaceRigidSkinningValid("
    "completed rigid surfaces must validate exact vertex and bone coverage")
require_source_ordered(
    "database/db_load.cpp"
    "const bool validModel = DB_ValidateLoadedXModel("
    "DB_PopStreamPos();
    return validModel;"
    "completed models must validate before leaving their materialization scope")
require_source_contains(
    "database/db_load.cpp"
    "Invalid fast-file model parent relationship"
    "model skeleton parents must remain inside the bounded bone matrix")
require_source_contains(
    "database/db_load.cpp"
    "XModelPartClassificationsValid("
    "model part classifications must remain inside the hit-location priority map")
require_source_ordered(
    "database/db_load.cpp"
    "CountInRange(
            varXModelCollSurf->numCollTris"
    "varXModelCollSurf->collTris =
        (XModelCollTri_s *)AllocLoad_FxElemVisStateSample();"
    "model collision triangle counts must validate before allocation and loading")
require_source_contains(
    "database/db_load.cpp"
    "collision.boneIdx >= model->numBones"
    "model collision bones must remain inside the owning model")
require_source_contains(
    "gfx_d3d/r_model_skin.cpp"
    "publishedOutputBytes - outputOffset"
    "skin worker writes must remain inside an actually published vertex reservation")
require_source_contains(
    "gfx_d3d/r_model_skin.cpp"
    "owner != frontEndDataOut"
    "skin workers must reject stale streams from a different backend frame")
require_source_contains(
    "gfx_d3d/r_scene.cpp"
    "publishedBytes - (streamAddress - arenaBegin)"
    "scene walkers must reject unreserved or stale model-stream bytes")
require_source_contains(
    "xanim/dobj_utils.cpp"
    "obj->numModels > DOBJ_MAX_SUBMODELS"
    "DObj LOD writes must validate their fixed model-count capacity")
require_source_contains(
    "xanim/dobj_utils.cpp"
    "obj->numBones > boneInfoCapacity"
    "DObj bone-info flattening must honor the caller's output capacity")
require_source_ordered(
    "gfx_d3d/r_model_pose.cpp"
    "if (!DObjGetBoneInfo("
    "R_SetNoDraw(sceneEnt);
                    return nullptr;"
    "failed DObj bounds construction must release the pending cull-state owner")
require_source_ordered(
    "gfx_d3d/r_dpvs.cpp"
    "::new (bmodelSurf) BModelSurface"
    "bmodelInfo->surfId = surfId >> 2;"
    "brush-model records must finish construction before their ID is published")

# EffectsCore runtime atomics must remain exact-width on LP64 platforms while
# preserving the x86 layout and native operation return semantics.
file(GLOB _effectscore_atomic_sources
    "${SOURCE_ROOT}/src/EffectsCore/*.cpp"
    "${SOURCE_ROOT}/src/EffectsCore/*.h")
list(APPEND _effectscore_atomic_sources
    "${SOURCE_ROOT}/src/gfx_d3d/fxprimitives.h")
foreach(_effectscore_source IN LISTS _effectscore_atomic_sources)
    file(READ "${_effectscore_source}" _effectscore_text)
    foreach(_retired_effectscore_token
        "Interlocked"
        "win32/win_local.h")
        string(FIND "${_effectscore_text}" "${_retired_effectscore_token}" _token_position)
        if (NOT _token_position EQUAL -1)
            message(FATAL_ERROR
                "EffectsCore retains native Windows atomic dependency in ${_effectscore_source}")
        endif()
    endforeach()
    string(REGEX MATCH
        "(^|[^A-Za-z0-9_])LONG([^A-Za-z0-9_]|$)|volatile[ \\t]+long"
        _native_effectscore_word
        "${_effectscore_text}")
    if (NOT "${_native_effectscore_word}" STREQUAL "")
        message(FATAL_ERROR
            "EffectsCore retains a native-width atomic word in ${_effectscore_source}")
    endif()
endforeach()
require_source_contains(
    "gfx_d3d/fxprimitives.h"
    "#include <EffectsCore/fx_runtime.h>"
    "renderer FX declarations must consume the portable runtime layout")
require_source_match_count(
    "EffectsCore/fx_runtime.h"
    "alignas\\(4\\)[ \\t]+volatile[ \\t]+std::int32_t"
    19
    "all EffectsCore runtime atomic words must remain explicit int32_t storage")
require_source_contains(
    "EffectsCore/fx_effect_def.h"
    "RUNTIME_SIZE(FxEffectDef, 0x20, 0x28);"
    "the canonical effect definition must freeze both native layouts")
foreach(_fx_runtime_layout
    "FxEffect, 0x80, 0x88"
    "FxCamera, 0xB0, 0xB0"
    "FxSpriteInfo, 0x10, 0x20"
    "FxVisState, 0x1010, 0x1010"
    "FxSystem, 0xA60, 0xA90"
    "FxImpactEntry, 0x84, 0x108"
    "FxImpactTable, 0x8, 0x10"
    "FxProfileEntry, 0x1C, 0x20"
    "FxSystemBuffers, 0x47480, 0x49480")
    require_source_contains(
        "EffectsCore/fx_runtime.h"
        "RUNTIME_SIZE(${_fx_runtime_layout});"
        "EffectsCore runtime structures must freeze both native layouts")
endforeach()
require_source_contains(
    "cgame/cg_effects_load_obj.cpp"
    "static_cast<uint32_t>(sizeof(FxImpactTable))"
    "load-object impact tables must allocate their native runtime size")
require_source_contains(
    "cgame/cg_effects_load_obj.cpp"
    "static_cast<uint32_t>(sizeof(FxImpactEntry) * 12)"
    "load-object impact entries must allocate native pointer-bearing records")
require_source_contains(
    "EffectsCore/fx_convert.cpp"
    "FxRuntimeBlobCursor sizePlanner;"
    "editor effect conversion must use the checked runtime-blob planner")
require_source_contains(
    "EffectsCore/fx_convert.cpp"
    "sizePlanner.ReserveArray<FxEffectDef>(1)"
    "editor effect conversion must include an aligned native runtime header")
require_source_contains(
    "EffectsCore/fx_convert.cpp"
    "sizePlanner.ReserveArray<FxElemDef>(static_cast<uint32_t>(elemCountTotal))"
    "editor effect conversion must include aligned native element definitions")
require_source_contains(
    "EffectsCore/fx_convert.cpp"
    "FX_PlanElemDefsOfType("
    "editor effect conversion must plan editor payloads before allocation")
require_source_contains(
    "EffectsCore/fx_convert.cpp"
    "FX_PlanEmittedElemDefPayloads(&sizePlanner, editorEffect)"
    "editor effect conversion must plan copied emission payloads")
require_source_contains(
    "EffectsCore/fx_convert.cpp"
    "FxRuntimeBlobCursor writer(reinterpret_cast<uint8_t *>(effect), totalBytesNeeded);"
    "editor effect conversion must write through the bounded runtime-blob cursor")
require_source_contains(
    "EffectsCore/fx_convert.cpp"
    "writer.Offset();"
    "editor effect conversion must verify the planned and written byte counts")
require_source_ordered(
    "EffectsCore/fx_convert.cpp"
    "elemCountTotal = 0;"
    "if ((editorEffect->elems[elemIndex].editorFlags & 0x80000000) == 0)\n            ++elemCountTotal;"
    "disabled editor elements must not inflate the runtime definition count")
require_source_contains(
    "EffectsCore/fx_runtime_blob.h"
    "alignmentMask > (std::numeric_limits<std::size_t>::max)() - offset"
    "runtime-blob alignment must reject addition overflow")
require_source_contains(
    "EffectsCore/fx_runtime_blob.h"
    "std::memset(buffer_ + offset_, 0, alignedOffset - offset_);"
    "runtime-blob writers must initialize alignment padding")
require_source_not_matches(
    "EffectsCore/fx_convert.cpp"
    "_QWORD[ \\t]*\\*[ \\t]*\\)?[ \\t]*curves"
    "velocity sampling must not pack native curve pointers as x86 qwords")
require_source_not_matches(
    "EffectsCore/fx_convert.cpp"
    "(_DWORD|uint32_t)[^\r\n]*(\\+[ \\t]*53|\\[53\\])"
    "effect conversion must use typed XModel physics-preset access")
require_source_contains(
    "EffectsCore/fx_convert.cpp"
    "edElemDef->trailDef.indCount & 1"
    "trail conversion must reject an unpaired final edge index")
require_source_contains(
    "EffectsCore/fx_convert.cpp"
    "ARRAY_COUNT(edElemDef->trailDef.inds)"
    "trail conversion must bound editor index storage")
require_source_contains(
    "EffectsCore/fx_convert.cpp"
    "ARRAY_COUNT(edElemDef->trailDef.verts)"
    "trail conversion must bound editor vertex storage")
require_source_contains(
    "EffectsCore/fx_convert.cpp"
    "elemDef->elemType == 9 || elemDef->visualCount > 1u"
    "emitted decal definitions must deep-copy their mark visual pair")
require_source_contains(
    "EffectsCore/fx_load_obj.cpp"
    "static_cast<int>(alignof(FxEffectDef))"
    "converted effect blobs must receive native pointer alignment")
require_source_contains(
    "EffectsCore/fx_load_obj.cpp"
    "FX_TryPlanMissingEffectAlias"
    "missing-effect aliases must use a bounded native layout plan")
require_source_contains(
    "EffectsCore/fx_load_obj.cpp"
    "FX_TryBuildMissingEffectAlias"
    "missing-effect aliases must use typed native pointer publication")
require_source_not_contains(
    "EffectsCore/fx_load_obj.cpp"
    "_DWORD"
    "missing-effect aliases must not write native pointers through x86 words")
require_source_not_contains(
    "EffectsCore/fx_load_obj.cpp"
    "relocationDistance"
    "missing-effect aliases must not rebase hard-coded x86 payload offsets")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "visuals.model->physPreset"
    "effect physics must use typed XModel physics-preset access")
require_source_matches(
    "EffectsCore/fx_profile.cpp"
    "qsort\\([ \t\r\n]*entryPool,[ \t\r\n]*entryCount,[ \t\r\n]*sizeof\\(FxProfileEntry\\)"
    "FX profiling must sort with the native pointer-bearing entry stride")
require_source_ordered(
    "EffectsCore/fx_update_util.cpp"
    "TryBeginBlockerAppend(&visState->blockerCount, &blockerIndex)"
    "localVisBlocker->visibility = packed.visibility;"
    "FX visibility payloads must be written before their count is published")
require_source_ordered(
    "EffectsCore/fx_update_util.cpp"
    "localVisBlocker->visibility = packed.visibility;"
    "PublishBlockerAppend(&visState->blockerCount, blockerIndex)"
    "FX visibility count publication must follow the complete payload write")
require_source_contains(
    "EffectsCore/fx_visibility_atomic.h"
    "Sys_AtomicStore(blockerCount, expected + 1);"
    "FX visibility count publication must provide a post-payload atomic barrier")
require_source_not_contains(
    "EffectsCore/fx_visibility_atomic.h"
    "Sys_AtomicCompareExchange"
    "the single FX visibility producer must not publish a reservation before its payload")
require_source_not_contains(
    "EffectsCore/fx_update_util.cpp"
    "Sys_AtomicIncrement(&visState->blockerCount)"
    "FX visibility count must not be incremented before its payload is complete")
require_source_contains(
    "EffectsCore/fx_update_util.cpp"
    "WRKCMD_GENERATE_FX_VERTS completion boundary"
    "the non-atomic visibility buffer handoff must retain its external ordering contract")
require_source_contains(
    "EffectsCore/fx_runtime.h"
    "RUNTIME_OFFSET(FxVisState, blockerCount, 0x1000, 0x1000);"
    "the FX visibility count offset must remain fixed across native layouts")
require_all_occurrences_wrapped(
    "EffectsCore/fx_draw.cpp"
    "&system->iteratorCount"
    "(FxIterator(BeginCooperative|TryBeginCooperative|EndCooperative|DowngradeExclusiveToCooperative)|Sys_Atomic(Load|CompareExchange))[ \t\r\n]*\\([ \t\r\n]*&system->iteratorCount"
    "FX cooperative iterator ownership")
require_all_occurrences_wrapped(
    "EffectsCore/fx_sort.cpp"
    "&system->iteratorCount"
    "FxIterator(WaitBeginExclusive|EndExclusive)[ \t\r\n]*\\([ \t\r\n]*&system->iteratorCount"
    "FX sort exclusive iterator ownership")
require_all_occurrences_wrapped(
    "EffectsCore/fx_system.cpp"
    "&(context\\.)?system->iteratorCount"
    "((Sys_AtomicLoad|Sys_AtomicStore|FxIteratorEndExclusive|FxIteratorTryBeginExclusive|FxIteratorWaitBeginExclusive)[ \t\r\n]*\\([ \t\r\n]*&(context\\.)?system->iteratorCount|const_cast[ \t\r\n]*<[ \t\r\n]*std::int32_t[ \t\r\n]*\\*[ \t\r\n]*>[ \t\r\n]*\\([ \t\r\n]*&system->iteratorCount)"
    "FX system and archive-adapter iterator initialization/exclusive ownership")
require_source_match_count(
    "EffectsCore/fx_draw.cpp"
    "FxIterator(BeginCooperative|EndCooperative)[ \t\r\n]*\\([ \t\r\n]*&system->iteratorCount"
    6
    "nested/cooperative admission, both rollbacks, release, and error unwind must use their shared helpers")
require_source_match_count(
    "EffectsCore/fx_sort.cpp"
    "FxIterator(WaitBeginExclusive|EndExclusive)[ \t\r\n]*\\([ \t\r\n]*&(state\\.)?system->iteratorCount"
    4
    "sort admission, archive-race rollback, normal release, and error unwind must use their shared helpers")
require_source_match_count(
    "EffectsCore/fx_draw.cpp"
    "system->iteratorCount"
    10
    "all cooperative iterator references must remain accounted for")
require_source_match_count(
    "EffectsCore/fx_sort.cpp"
    "system->iteratorCount"
    4
    "all sort iterator references must remain accounted for")

# Archive ownership uses an external gate without changing the frozen runtime
# image. Pending blocks iterator admission but deliberately lets allocator work
# drain; Exclusive blocks both. Unknown values fail closed. Every non-archive
# admission path rechecks after acquiring its own gate and rolls back on races.
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "alignas(4) volatile std::int32_t fx_archiveGate[1]{};"
    "archive admission state must remain an aligned external sidecar")
require_source_not_contains(
    "EffectsCore/fx_runtime.h"
    "archiveGate"
    "archive admission state must not alter persisted FX layouts")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "if (system == &fx_systemPool[index])\n            return &fx_archiveGate[index];"
    "archive gates must resolve only for owned FX systems")
require_source_contains(
    "EffectsCore/fx_archive_gate_control.h"
    "return value != ArchiveGateValue::Open;"
    "pending, exclusive, and corrupt archive values must block iterator admission")
require_source_contains(
    "EffectsCore/fx_archive_gate_control.h"
    "return value != ArchiveGateValue::Open\n        && value != ArchiveGateValue::Pending;"
    "allocator admission must allow only open and pending archive values")
require_source_contains(
    "EffectsCore/fx_archive_gate_control.h"
    "return value == ArchiveGateValue::Open\n        || value == ArchiveGateValue::Pending;"
    "a prechecked exclusive writer must accept only Open or Pending completion")
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "ArchiveGateBlocksIteratorAdmission[ \t\r\n]*\\("
    2
    "archive active and waiter paths must share fail-closed iterator admission")
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "ArchiveGateBlocksAllocatorAdmission[ \t\r\n]*\\("
    4
    "allocator entry, pool rebuild, and graph validation must use typed exclusive-state checks")
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "const[ \t]+bool[ \t]+ownsArchive[ \t]*=[ \t]*FX_ValidateArchiveExclusiveState\\(system\\)[ \t]*;"
    2
    "pool rebuild and graph validation must prove full archive ownership before bypassing admission")
require_source_ordered(
    "EffectsCore/fx_system.cpp"
    "while (fx::archive::ArchiveGateBlocksAllocatorAdmission("
    "Sys_EnterCriticalSection(CRITSECT_FX_ALLOC);"
    "pool mutation admission must wait for exclusive ownership before locking")
require_source_ordered(
    "EffectsCore/fx_system.cpp"
    "Sys_EnterCriticalSection(CRITSECT_FX_ALLOC);"
    "if (!fx::archive::ArchiveGateBlocksAllocatorAdmission("
    "pool mutation admission must recheck exclusive ownership after locking")
require_source_ordered(
    "EffectsCore/fx_system.cpp"
    "if (!fx::archive::ArchiveGateBlocksAllocatorAdmission("
    "Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);"
    "pool mutation admission must release the allocator lock after losing an archive race")
require_source_contains(
    "EffectsCore/fx_draw.cpp"
    "if (FX_CurrentThreadOwnsCooperativeIterator(system))\n    {\n        FxIteratorBeginCooperative(&system->iteratorCount);\n        ++fx_cooperativeIteratorThreadState.depth;\n        return;\n    }"
    "nested cooperative iterator admission must preserve per-thread depth")
require_source_contains(
    "EffectsCore/fx_draw.cpp"
    "const std::uint32_t currentGeneration =\n            FX_GetCooperativeIteratorGeneration(system);\n        const bool initialized = system->isInitialized != 0;\n        const bool archiveActive = FX_ArchiveGateIsActive(system);\n        const bool killActive = FX_EffectKillGateIsActive(system);\n        if (!archiveActive && !killActive && initialized\n            && currentGeneration == admissionGeneration)\n        {\n            fx_cooperativeIteratorThreadState.system = system;\n            fx_cooperativeIteratorThreadState.generation = currentGeneration;"
    "first cooperative iterator admission must recheck lifecycle state before publishing thread ownership")
require_source_contains(
    "EffectsCore/fx_draw.cpp"
    "fx_cooperativeIteratorThreadState.generation\n            != FX_GetCooperativeIteratorGeneration(system)"
    "cooperative iterator release must reject stale reset generations before decrementing shared ownership")
require_source_ordered(
    "EffectsCore/fx_draw.cpp"
    "&& currentGeneration == admissionGeneration)"
    "FxIteratorEndCooperative(&system->iteratorCount, &remaining)"
    "cooperative admission must roll back after losing a gate/lifecycle race")
require_source_ordered(
    "EffectsCore/fx_sort.cpp"
    "FX_WaitForArchiveGate(system);"
    "FxIteratorWaitBeginExclusive(&system->iteratorCount);"
    "blocking exclusive iterators must wait before acquiring ownership")
require_source_ordered(
    "EffectsCore/fx_sort.cpp"
    "FxIteratorWaitBeginExclusive(&system->iteratorCount);"
    "const bool archiveActive = FX_ArchiveGateIsActive(system);"
    "blocking exclusive iterators must recheck lifecycle state after acquisition")
require_source_contains(
    "EffectsCore/fx_sort.cpp"
    "fx_sortExclusiveIteratorThreadState.system = system;\n            fx_sortExclusiveIteratorThreadState.generation =\n                currentGeneration;"
    "blocking exclusive iterators must publish generation-tagged thread ownership")
require_source_ordered(
    "EffectsCore/fx_sort.cpp"
    "if (!archiveActive && initialized\n            && currentGeneration == admissionGeneration)"
    "if (!FxIteratorEndExclusive(&system->iteratorCount))"
    "blocking exclusive iterators must roll back ownership after a lifecycle race")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "if (!archiveActive && initialized && !archiving\n        && currentGeneration == admissionGeneration)\n    {\n        return true;\n    }\n    if (!FxIteratorEndExclusive(&system->iteratorCount))"
    "nonblocking exclusive iterators must recheck and release after a lifecycle race")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "const fx::archive::ArchiveGateValue archiveGateState =\n            archiveGate\n            ? static_cast<fx::archive::ArchiveGateValue>(\n                Sys_AtomicLoad(archiveGate))\n            : static_cast<fx::archive::ArchiveGateValue>(-1);"
    "kill-exclusive admission must map a missing archive gate to a fail-closed value")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "if (fx::archive::ArchiveGateAllowsPrecheckedExclusiveCompletion(\n                archiveGateState)\n            && system->isInitialized\n            && currentGeneration == admissionGeneration)"
    "kill-exclusive completion must allow the Open-to-Pending race but reject Exclusive and corrupt values")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "thread_local fx::archive::ArchiveGateOwnerState fx_archiveThreadState;"
    "normal archive ownership must retain a phase-aware retryable TLS state")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "|| FX_CurrentThreadOwnsSortExclusive(system)\n        || FX_CurrentThreadOwnsAnyEffectLock()\n        || fx_archiveThreadState.phase\n            != fx::archive::ArchiveGateOwnerPhase::Idle"
    "archive admission must reject same-thread sort/effect-lock ownership before controller acquisition")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "fx::archive::AcquireArchiveGate("
    "normal archive admission must delegate to the portable controller")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "if (acquireStatus\n        == fx::archive::ArchiveGateControlStatus::UnsafeFailure)"
    "indeterminate normal archive acquisition must fail-stop")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "return fx::archive::ReleaseArchiveGate("
    "normal archive release must delegate retryable cleanup to the portable controller")
require_source_not_contains(
    "EffectsCore/fx_system.cpp"
    "fx_archiveThreadState.system = system;"
    "normal archive ownership must not recreate the obsolete TLS record")
file(READ
    "${SOURCE_ROOT}/src/EffectsCore/fx_system.cpp"
    _fx_system_archive_adapter_file)
extract_security_slice(
    _fx_system_archive_adapter_file
    "fx::archive::ArchiveGateControlStatus\nFX_PerformArchiveGateControlOperation("
    "volatile std::int32_t *FX_GetCooperativeIteratorGenerationState("
    _archive_adapter_source
    "production archive gate adapter")
foreach(_archive_adapter_operation
    ClaimPending
    TryAcquireIterator
    WaitForIteratorProgress
    PromoteExclusive
    ValidateAdmission
    ValidateExclusive
    ReleaseIterator
    ClearArchivingForError
    ReopenPending
    ReopenExclusive)
    require_source_match_count(
        "EffectsCore/fx_system.cpp"
        "case[ \t]+Operation::${_archive_adapter_operation}[ \t]*:"
        1
        "the production archive adapter must map ${_archive_adapter_operation} exactly once")
endforeach()

extract_security_slice(
    _archive_adapter_source
    "case Operation::TryAcquireIterator:"
    "case Operation::WaitForIteratorProgress:"
    _archive_adapter_try_iterator_source
    "TryAcquireIterator adapter operation")
require_security_slice_contains(
    _archive_adapter_try_iterator_source
    "return FxIteratorTryBeginExclusive(\n                   &context.system->iteratorCount)\n            ? Status::Success\n            : Status::Retry;"
    "TryAcquireIterator must bind success/retry to exclusive iterator acquisition")

extract_security_slice(
    _archive_adapter_source
    "case Operation::WaitForIteratorProgress:"
    "case Operation::PromoteExclusive:"
    _archive_adapter_wait_source
    "WaitForIteratorProgress adapter operation")
require_security_slice_contains(
    _archive_adapter_wait_source
    "if (!context.system->isInitialized\n            || context.system->isArchiving\n            || FX_GetCooperativeIteratorGeneration(context.system)\n                != context.expectedGeneration)\n        {\n            return Status::Cancelled;\n        }"
    "iterator waiting must cancel on lifecycle or generation changes")
require_security_slice_ordered(
    _archive_adapter_wait_source
    "return Status::Cancelled;"
    "std::this_thread::yield();"
    "iterator waiting must check cancellation before yielding")
require_security_slice_ordered(
    _archive_adapter_wait_source
    "std::this_thread::yield();"
    "return Status::Retry;"
    "iterator waiting must yield before requesting a retry")

extract_security_slice(
    _archive_adapter_source
    "case Operation::ValidateAdmission:"
    "case Operation::ValidateExclusive:"
    _archive_adapter_validate_admission_source
    "ValidateAdmission adapter operation")
require_security_slice_contains(
    _archive_adapter_validate_admission_source
    "if (Sys_AtomicLoad(context.gate) != exclusive\n            || Sys_AtomicLoad(&context.system->iteratorCount) != -1)\n        {\n            return Status::UnsafeFailure;\n        }\n        return context.system->isInitialized\n                && !context.system->isArchiving\n                && !fx::archive::EffectTableRestoreLeaseIsActive()\n                && FX_GetCooperativeIteratorGeneration(context.system)\n                    == context.expectedGeneration\n            ? Status::Success\n            : Status::Cancelled;"
    "admission validation must require exclusive gate/iterator ownership, no effect-table lease, and an unchanged live lifecycle")

extract_security_slice(
    _archive_adapter_source
    "case Operation::ValidateExclusive:"
    "case Operation::ReleaseIterator:"
    _archive_adapter_validate_exclusive_source
    "ValidateExclusive adapter operation")
require_security_slice_contains(
    _archive_adapter_validate_exclusive_source
    "return Sys_AtomicLoad(context.gate) == exclusive\n                && Sys_AtomicLoad(&context.system->iteratorCount) == -1\n            ? Status::Success\n            : Status::UnsafeFailure;"
    "exclusive validation must require exact exclusive gate and iterator ownership")

extract_security_slice(
    _archive_adapter_source
    "case Operation::ReleaseIterator:"
    "case Operation::ClearArchivingForError:"
    _archive_adapter_release_iterator_source
    "ReleaseIterator adapter operation")
require_security_slice_contains(
    _archive_adapter_release_iterator_source
    "return FxIteratorEndExclusive(\n                   &context.system->iteratorCount)\n            ? Status::Success\n            : Status::UnsafeFailure;"
    "ReleaseIterator must bind success to checked exclusive iterator release")

extract_security_slice(
    _archive_adapter_source
    "case Operation::ClearArchivingForError:"
    "case Operation::ReopenPending:"
    _archive_adapter_clear_archiving_source
    "ClearArchivingForError adapter operation")
require_security_slice_contains(
    _archive_adapter_clear_archiving_source
    "context.system->isArchiving = false;\n        return Status::Success;"
    "error cleanup must clear the production archiving flag before succeeding")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "context.gate, pending, open) != open"
    "normal archive acquisition must claim Pending only from Open")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "context.gate, exclusive, pending)\n                == pending"
    "normal archive acquisition must promote Exclusive only from Pending")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "context.gate, open, pending)\n                == pending"
    "pending rollback must reopen only the gate this thread claimed")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "context.gate, open, exclusive)\n                == exclusive"
    "exclusive release must reopen only the gate this thread acquired")
require_source_contains(
    "EffectsCore/fx_archive_gate_control.cpp"
    "state->phase = ArchiveGateOwnerPhase::PendingExclusive;"
    "controller acquisition must record iterator ownership before promotion")
require_source_contains(
    "EffectsCore/fx_archive_gate_control.cpp"
    "state->phase = ArchiveGateOwnerPhase::Acquired;"
    "controller acquisition must record complete ownership before final validation")
require_source_contains(
    "EffectsCore/fx_archive_gate_control.cpp"
    "if (state->phase == ArchiveGateOwnerPhase::ExclusiveGateOnly)\n        return ReopenOwnedGate(state, callbacks);"
    "partial normal release must retry only the remaining exclusive gate")
require_source_contains(
    "EffectsCore/fx_archive_gate_control.cpp"
    "if (ReleaseIteratorForCleanup(state, callbacks)\n        != ArchiveGateControlStatus::Success)\n    {\n        return ArchiveGateControlStatus::UnsafeFailure;\n    }\n    return ReopenOwnedGate(state, callbacks);"
    "normal release must relinquish iterator ownership before reopening its gate")
require_source_matches(
    "EffectsCore/fx_iterator_atomic.h"
    "if[ \t\r\n]*\\([^{}]*observed[ \t\r\n]*<[ \t\r\n]*0[^{}]*\\)[ \t\r\n]*\\{[ \t\r\n]*std::this_thread::yield[ \t\r\n]*\\([ \t\r\n]*\\)[ \t\r\n]*;[ \t\r\n]*continue[ \t\r\n]*;[ \t\r\n]*\\}"
    "FX cooperative admission must yield while the gate is unavailable")
require_source_matches(
    "EffectsCore/fx_iterator_atomic.h"
    "if[ \t\r\n]*\\([^{};]*Sys_AtomicCompareExchange[^{};]*\\)[ \t\r\n]*return[ \t\r\n]*;[ \t\r\n]*std::this_thread::yield[ \t\r\n]*\\([ \t\r\n]*\\)[ \t\r\n]*;"
    "FX cooperative admission must yield after a lost CAS race")
require_source_matches(
    "EffectsCore/fx_iterator_atomic.h"
    "while[ \t\r\n]*\\([ \t\r\n]*!FxIteratorTryBeginExclusive[ \t\r\n]*\\([ \t\r\n]*state[ \t\r\n]*\\)[ \t\r\n]*\\)[ \t\r\n]*\\{[ \t\r\n]*while[ \t\r\n]*\\([ \t\r\n]*Sys_AtomicLoad[ \t\r\n]*\\([ \t\r\n]*state[ \t\r\n]*\\)[ \t\r\n]*!=[ \t\r\n]*0[ \t\r\n]*\\)[ \t\r\n]*\\{[ \t\r\n]*std::this_thread::yield[ \t\r\n]*\\([ \t\r\n]*\\)[ \t\r\n]*;[ \t\r\n]*\\}[ \t\r\n]*std::this_thread::yield[ \t\r\n]*\\([ \t\r\n]*\\)[ \t\r\n]*;[ \t\r\n]*\\}"
    "FX exclusive admission must yield in both wait and retry paths")
require_source_contains(
    "EffectsCore/fx_iterator_atomic.h"
    "std::atomic_ref<bool>(*requested).store(true, std::memory_order_seq_cst);"
    "FX garbage-collection requests must retain their byte layout and publish atomically")
foreach(_fx_iterator_consumer
    "EffectsCore/fx_draw.cpp"
    "EffectsCore/fx_system.cpp")
    require_all_occurrences_wrapped(
        "${_fx_iterator_consumer}"
        "system->needsGarbageCollection"
        "Fx(GarbageCollectionRequested|RequestGarbageCollection|ClearGarbageCollectionRequest)[ \t\r\n]*\\([ \t\r\n]*&(reservation\\.)?system->needsGarbageCollection[ \t\r\n]*\\)"
        "FX garbage-collection request storage")
endforeach()

# Effect teardown is a writer transaction layered over the iterator gate.
# The durable lifecycle bit and allocation sidecar must agree, zero-reference
# records cannot be resurrected, and rewind keeps writer admission closed while
# it downgrades to a cooperative reader and repopulates retained roots.
require_source_contains(
    "EffectsCore/fx_system.h"
    "FX_STATUS_OWNER_ADMISSION_BLOCKED = FX_STATUS_DEFER_UPDATE"
    "killed effects must retain a layout-neutral lifecycle marker")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "alignas(4) volatile std::int32_t fx_effectKillGate[1]{};"
    "effect teardown writer intent must use an aligned external sidecar")
require_source_not_contains(
    "EffectsCore/fx_runtime.h"
    "effectKillGate"
    "effect teardown writer intent must not alter persisted FX layouts")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "if (referenceCount == 0
            || (increment
                && referenceCount == FX_STATUS_REF_COUNT_MASK))"
    "effect references must reject resurrection and packed-field carry")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "if ((blocked != 0 && blocked != 1)
            || (blocked == 1) != markerBlocked)"
    "effect admission sidecars must agree with durable lifecycle markers")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "const bool targetReferenceAdded =
        FX_TryAdjustEffectReferenceCount(effect, true);"
    "effect kill admission must retain its target before publication can drain")
require_source_ordered(
    "EffectsCore/fx_system.cpp"
    "while (Sys_AtomicCompareExchange(killGate, 1, 0) != 0)"
    "FxIteratorWaitBeginExclusive(&system->iteratorCount);"
    "effect teardown must publish writer intent before draining readers")
require_source_ordered(
    "EffectsCore/fx_system.cpp"
    "FxIteratorWaitBeginExclusive(&system->iteratorCount);"
    "Sys_AtomicCompareExchange(killGate, 2, 1)"
    "effect teardown must publish exclusive ownership only after draining readers")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "const bool released =
        FxIteratorEndExclusive(&system->iteratorCount);
    if (!released)
        return false;
    fx_effectKillExclusiveThreadState.exclusiveAcquired = false;
    if (Sys_AtomicCompareExchange(killGate, 0, 2) != 2)"
    "ordinary effect teardown must release the iterator before reopening admission")
require_source_contains(
    "EffectsCore/fx_iterator_atomic.h"
    "return Sys_AtomicCompareExchange(state, 1, -1) == -1;"
    "rewind must downgrade exclusive iterator ownership atomically")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "fx_effectRestartRetainThreadState.ownsKillGate = true;
    fx_effectKillExclusiveThreadState = {};"
    "rewind downgrade must transfer writer-gate ownership to restart state")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "fx_effectRestartRetainThreadState.effectCount != 0
        || !FX_CurrentThreadOwnsCooperativeIterator(system)"
    "rewind must retain cooperative ownership until every restart retain is consumed")
require_source_ordered(
    "EffectsCore/fx_update.cpp"
    "FX_RunGarbageCollection(system);"
    "FX_DowngradeEffectKillExclusiveToCooperative(system)"
    "rewind must reclaim killed pool slots before restarting effects")
require_source_ordered(
    "EffectsCore/fx_update.cpp"
    "FX_DowngradeEffectKillExclusiveToCooperative(system)"
    "FX_EndEffectRestartGate(system)"
    "rewind must hold its writer gate across all restart mutations")
require_source_ordered(
    "EffectsCore/fx_draw.cpp"
    "FX_AbandonCurrentThreadEffectRestartRetainsForError();"
    "FX_AbandonCurrentThreadEffectRestartGateForError();"
    "error cleanup must release restart retains before reopening writer admission")
require_source_ordered(
    "EffectsCore/fx_draw.cpp"
    "FX_AbandonCurrentThreadEffectRestartGateForError();"
    "FX_AbandonCurrentThreadCooperativeIteratorsForError();"
    "error cleanup must reopen restart admission before releasing its reader")
require_source_ordered(
    "EffectsCore/fx_draw.cpp"
    "FX_AbandonCurrentThreadSortExclusiveForError();"
    "FX_AbandonCurrentThreadArchiveForError();"
    "error cleanup must release sort exclusivity before abandoning archive ownership")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "if (fx::archive::AbandonArchiveGateForError("
    "archive error cleanup must delegate phase-aware ownership release")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "Sys_Error(\"Unable to abandon FX archive ownership safely\");\n        std::abort();"
    "incomplete archive error cleanup must fail-stop with controller state retained")
require_source_contains(
    "EffectsCore/fx_archive_gate_control.cpp"
    "if (Invoke(callbacks,\n            ArchiveGateControlOperation::ClearArchivingForError)\n        != ArchiveGateControlStatus::Success)"
    "archive error cleanup must clear the archiving marker through its checked adapter")
require_source_contains(
    "EffectsCore/fx_archive_gate_control.cpp"
    "if (Invoke(callbacks, operation) != ArchiveGateControlStatus::Success)\n        return ArchiveGateControlStatus::UnsafeFailure;\n    ClearOwner(state);"
    "archive controller state must clear only after its owned gate reopens")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "const bool reuseKillExclusive =
        FX_ThreadOwnsEffectKillExclusive(system);"
    "rewind garbage collection must reuse its owned kill-exclusive transaction")

# FX effect and pool handles retain their compact wire representation, but all
# address/range calculations must use native-width integers so LP64 and ARM64
# builds cannot truncate a pointer or derive a handle with an x86-only stride.
require_source_match_count(
    "EffectsCore/fx_runtime.h"
    "static[ \t]+constexpr[ \t]+std::size_t[ \t]+HANDLE_SCALE[ \t]*=[ \t]*4[ \t]*[;]"
    4
    "every compact FX handle type must declare its explicit legacy scale")
foreach(_fx_pool_runtime_layout
    "FxElem, 0x28, 0x28"
    "FxTrail, 0x8, 0x8"
    "FxTrailElem, 0x20, 0x20")
    require_source_contains(
        "EffectsCore/fx_runtime.h"
        "RUNTIME_SIZE(${_fx_pool_runtime_layout});"
        "FX pool item strides must remain fixed on both native layouts")
endforeach()
require_source_contains(
    "EffectsCore/fx_pool.h"
    "sizeof(FxPool<ITEM_TYPE>) == sizeof(ITEM_TYPE)"
    "FX pool slots must preserve the runtime item stride")
require_source_contains(
    "EffectsCore/fx_pool.h"
    "const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(entries);"
    "FX handle encoding must calculate from a native-width base address")
require_source_contains(
    "EffectsCore/fx_pool.h"
    "const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(entryAddress);"
    "FX handle encoding must calculate from a native-width item address")
require_source_contains(
    "EffectsCore/fx_pool.h"
    "constexpr std::size_t span = LIMIT * sizeof(ENTRY_TYPE);"
    "FX handle encoding must bound the complete native entry span")
require_source_contains(
    "EffectsCore/fx_pool.h"
    "if (offset >= span || offset % sizeof(ENTRY_TYPE) != 0)"
    "FX handle encoding must reject out-of-range and interior addresses")
require_source_contains(
    "EffectsCore/fx_pool.h"
    "constexpr std::size_t handleLimit = LIMIT * stride;"
    "FX handle decoding must derive its limit from the native entry stride")
require_source_contains(
    "EffectsCore/fx_pool.h"
    "if (!entries || handle >= handleLimit || handle % stride != 0)"
    "FX handle decoding must reject null, out-of-range, and misaligned handles")
require_source_contains(
    "EffectsCore/fx_pool.h"
    "static_cast<std::size_t>(FX_INVALID_HANDLE - 1) / stride"
    "FX handle arrays must prove their capacity fits below the invalid sentinel")
require_source_contains(
    "gfx_d3d/fxprimitives.h"
    "FxEncodeHandle<FxPool<ITEM_TYPE>, LIMIT, ITEM_TYPE::HANDLE_SCALE>("
    "generic FX pool encoding must use the checked native-width codec")
require_source_contains(
    "gfx_d3d/fxprimitives.h"
    "FxDecodeHandle<FxPool<ITEM_TYPE>, LIMIT, ITEM_TYPE::HANDLE_SCALE>("
    "generic FX pool decoding must use the checked native-width codec")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "FxEncodeHandle<FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>("
    "effect encoding must use the checked native-width codec")
require_source_contains(
    "EffectsCore/fx_archive.cpp"
    "FxDecodeHandle<FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>("
    "effect decoding must use the checked native-width codec")
require_source_not_matches(
    "EffectsCore/fx_system.cpp"
    "freedIndex[ \t]*=[^\r\n]*([/][ \t]*40|>>[ \t]*(3|5))"
    "FX pool release must not recover indices with x86-only divisors or shifts")
require_source_not_matches(
    "EffectsCore/fx_system.cpp"
    "\\([ \t]*char[ \t]*\\*[ \t]*\\)[^\r\n]*(item|effect)[^\r\n]*-[^\r\n]*(pool|system->effects)"
    "FX pool/effect encoding must not subtract raw pointers")
require_source_not_matches(
    "gfx_d3d/fxprimitives.h"
    "\\([ \t]*char[ \t]*\\*[ \t]*\\)[^\r\n]*(item|poolArray)[^\r\n]*[-+]"
    "generic FX handle functions must not perform raw byte-pointer arithmetic")
require_source_not_matches(
    "EffectsCore/fx_archive.cpp"
    "system->effects[^\r\n]*([+][ \t]*4[ \t]*\\*[ \t]*handle|handle[ \t]*\\*[ \t]*4)"
    "archive effect decoding must not assume the x86 effect stride")
require_source_contains(
    "EffectsCore/fx_runtime.h"
    "std::int32_t nextFree;"
    "FX pool free-list links must remain explicit fixed-width indices")
require_source_contains(
    "EffectsCore/fx_pool.h"
    "[[noreturn]] void KISAK_CDECL FX_InvalidPoolHandle("
    "production pool decoding must expose one fail-closed invalid-handle hook")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "[[noreturn]] void KISAK_CDECL FX_InvalidPoolHandle("
    "the invalid-pool-handle hook must remain explicitly nonreturning")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "FX_InvalidPoolHandle[^{]*\\{[^}]*Invalid FX pool handle %u for pool %p[^}]*std::abort\\(\\);"
    "the invalid-pool-handle hook must retain an abort fallback after ERR_DROP")
require_source_matches(
    "gfx_d3d/fxprimitives.h"
    "FxDecodeHandle<FxPool<ITEM_TYPE>,[ \t]*LIMIT,[ \t]*ITEM_TYPE::HANDLE_SCALE>[ \t\r\n]*\\([^;]*\\)[ \t]*[;][ \t\r\n]*vassert[ \t\r\n]*\\([^;]*\\)[ \t]*[;][ \t\r\n]*if[ \t\r\n]*\\([ \t\r\n]*!item[ \t\r\n]*\\)[ \t\r\n]*FX_InvalidPoolHandle[ \t\r\n]*\\([ \t\r\n]*poolArray[ \t]*,[ \t]*handle[ \t\r\n]*\\)[ \t]*[;][ \t\r\n]*return[ \t]+item[ \t]*[;]"
    "production pool decoding must route every invalid codec result to the nonreturning hook")

# Pool mutation is centralized behind bounded, transactional helpers. Failed
# validation must leave the list head, item payload, and active count untouched.
foreach(_fx_pool_status
    InvalidArgument
    CorruptFreeList
    InvalidActiveCount
    DuplicateFreeHead
    AlreadyFree
    UninitializedAllocationState
    AllocationStateMismatch)
    require_source_contains(
        "EffectsCore/fx_pool.h"
        "${_fx_pool_status},"
        "FX pool mutation must retain its ${_fx_pool_status} failure state")
endforeach()
require_source_contains(
    "EffectsCore/fx_pool.h"
    "std::array<std::uint64_t, WORD_COUNT> allocatedWords{};"
    "FX pool allocation ownership must use a fixed-size per-slot bitset")
require_source_contains(
    "EffectsCore/fx_pool.h"
    "std::size_t allocatedCount = 0;"
    "FX pool allocation state must retain a native-width ownership count")
require_source_contains(
    "EffectsCore/fx_pool.h"
    "bool initialized = false;"
    "FX pool allocation state must distinguish an uninitialized sidecar")
require_source_matches(
    "EffectsCore/fx_pool.h"
    "void[ \t]+FxPoolResetAllocationState[^}]*allocatedWords.fill[ \t\r\n]*\\([ \t\r\n]*0[ \t\r\n]*\\)[ \t]*[;][^}]*allocatedCount[ \t]*=[ \t]*0[ \t]*[;][^}]*initialized[ \t]*=[ \t]*true[ \t]*[;]"
    "FX pool reset must initialize an empty allocation sidecar")
require_source_contains(
    "EffectsCore/fx_pool.h"
    "FxPoolAllocationState<LIMIT> rebuilt{};"
    "FX pool restoration must construct allocation state transactionally")
require_source_contains(
    "EffectsCore/fx_pool.h"
    "rebuilt.allocatedWords.fill((std::numeric_limits<std::uint64_t>::max)());"
    "FX pool restoration must begin with every valid slot marked allocated")
require_source_matches(
    "EffectsCore/fx_pool.h"
    "if[ \t]+constexpr[ \t\r\n]*\\([ \t\r\n]*LIMIT[ \t]*%[ \t]*FxPoolAllocationState<LIMIT>::WORD_BITS[ \t]*!=[ \t]*0[ \t\r\n]*\\)[^}]*rebuilt.allocatedWords.back[ \t\r\n]*\\([ \t\r\n]*\\)[ \t]*=[ \t\r\n]*\\(std::uint64_t\\{1\\}[ \t]*<<[ \t]*usedBits\\)[ \t]*-[ \t]*1[ \t]*[;]"
    "FX pool restoration must mask padding bits beyond the fixed pool capacity")
require_source_matches(
    "EffectsCore/fx_pool.h"
    "while[ \t\r\n]*\\([ \t\r\n]*freeIndex[ \t]*!=[ \t]*-1[ \t\r\n]*\\)[ \t\r\n]*\\{[ \t\r\n]*if[ \t\r\n]*\\([ \t\r\n]*!FxPoolFreeIndexIsValid<LIMIT>\\(freeIndex\\)[ \t\r\n]*\\|\\|[ \t]*freeCount[ \t]*==[ \t]*LIMIT[ \t\r\n]*\\)"
    "FX pool restoration must bound and validate every free-list link")
require_source_matches(
    "EffectsCore/fx_pool.h"
    "if[ \t\r\n]*\\([ \t\r\n]*!FxPoolAllocationStateIsAllocated\\(rebuilt,[ \t]*index\\)[ \t\r\n]*\\)[ \t\r\n]*return[ \t]+FxPoolMutationStatus::CorruptFreeList[ \t]*[;][ \t\r\n]*FxPoolSetAllocationStateBit\\(&rebuilt,[ \t]*index,[ \t]*false\\)[ \t]*[;]"
    "FX pool restoration must reject repeated/cyclic free slots before clearing them")
require_source_ordered(
    "EffectsCore/fx_pool.h"
    "freeIndex = pool[index].nextFree;"
    "*allocationState = rebuilt;"
    "FX pool restoration must finish traversal before committing sidecar state")
require_source_ordered(
    "EffectsCore/fx_pool.h"
    "*allocationState = rebuilt;"
    "Sys_AtomicStore(\n        activeCount,"
    "FX pool restoration must commit validated ownership before reconciling the active count")
require_source_contains(
    "EffectsCore/fx_pool.h"
    "if (!FxPoolFreeIndexIsValid<LIMIT>(nextFree) || nextFree == itemIndex)"
    "FX allocation must validate and reject a self-referential next-free index")
require_source_contains(
    "EffectsCore/fx_pool.h"
    "if (!FxPoolItemIndex<ITEM_TYPE, LIMIT>(pool, item, &freedIndex))"
    "FX release must validate item ownership and native slot alignment")
require_source_contains(
    "EffectsCore/fx_pool.h"
    "if (!allocationState->initialized)"
    "FX pool mutation must reject uninitialized allocation state")
require_source_match_count(
    "EffectsCore/fx_pool.h"
    "static_cast<std::size_t>\\(active\\)[ \t\r\n]*!=[ \t\r\n]*allocationState->allocatedCount"
    2
    "both allocation and release must reject active-count/sidecar disagreement")
require_source_matches(
    "EffectsCore/fx_pool.h"
    "FxPoolAllocationStateIsAllocated[ \t\r\n]*\\([ \t\r\n]*\\*allocationState[ \t]*,[ \t]*static_cast<std::size_t>\\(itemIndex\\)[ \t\r\n]*\\)[ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*CorruptFreeList"
    "FX allocation must reject a free-list head marked allocated")
require_source_matches(
    "EffectsCore/fx_pool.h"
    "nextFree[ \t]*!=[ \t]*-1[^}]*FxPoolAllocationStateIsAllocated[ \t\r\n]*\\([ \t\r\n]*\\*allocationState[ \t]*,[ \t]*static_cast<std::size_t>\\(nextFree\\)[ \t\r\n]*\\)[ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*CorruptFreeList"
    "FX allocation must reject a next-free link marked allocated")
require_source_contains(
    "EffectsCore/fx_pool.h"
    "if (firstFree == freedIndex)\n        return FxPoolMutationStatus::DuplicateFreeHead;"
    "FX release must reject a duplicate free-list head before mutation")
require_source_contains(
    "EffectsCore/fx_pool.h"
    "return FxPoolMutationStatus::AlreadyFree;"
    "FX release must reject non-head duplicate frees from explicit ownership state")
require_source_ordered(
    "EffectsCore/fx_pool.h"
    "if (!FxPoolFreeIndexIsValid<LIMIT>(nextFree) || nextFree == itemIndex)"
    "*firstFreeIndex = nextFree;"
    "FX allocation validation must complete before its free-list head changes")
require_source_ordered(
    "EffectsCore/fx_pool.h"
    "FxPoolSetAllocationStateBit(\n        allocationState, static_cast<std::size_t>(itemIndex), true);"
    "*firstFreeIndex = nextFree;"
    "FX allocation must claim the sidecar slot before publishing its removal")
require_source_ordered(
    "EffectsCore/fx_pool.h"
    "++allocationState->allocatedCount;"
    "*firstFreeIndex = nextFree;"
    "FX allocation must update sidecar accounting before publishing its removal")
require_source_ordered(
    "EffectsCore/fx_pool.h"
    "*firstFreeIndex = nextFree;"
    "Sys_AtomicIncrement(activeCount);"
    "FX allocation must remove the free slot before publishing its active count")
require_source_ordered(
    "EffectsCore/fx_pool.h"
    "FxPoolCanFreeLocked<ITEM_TYPE, LIMIT>("
    "std::forward<BEFORE_PUBLISH>(beforePublish)();"
    "FX release validation must complete before any unlink callback runs")
require_source_ordered(
    "EffectsCore/fx_pool.h"
    "std::forward<BEFORE_PUBLISH>(beforePublish)();"
    "pool[freedIndex].item = ITEM_TYPE{};"
    "FX release must unlink the live item before clearing its payload")
require_source_ordered(
    "EffectsCore/fx_pool.h"
    "pool[freedIndex].item = ITEM_TYPE{};"
    "pool[freedIndex].nextFree = firstFree;"
    "FX release must clear payload data before installing its private free link")
require_source_ordered(
    "EffectsCore/fx_pool.h"
    "pool[freedIndex].nextFree = firstFree;"
    "FxPoolSetAllocationStateBit(\n        allocationState, static_cast<std::size_t>(freedIndex), false);"
    "FX release must install its private free link before clearing sidecar ownership")
require_source_ordered(
    "EffectsCore/fx_pool.h"
    "--allocationState->allocatedCount;"
    "*firstFreeIndex = freedIndex;"
    "FX release must update sidecar accounting before publishing the free-list head")
require_source_ordered(
    "EffectsCore/fx_pool.h"
    "*firstFreeIndex = freedIndex;"
    "Sys_AtomicDecrement(activeCount);"
    "FX release must publish the free slot before lowering its active count")
require_source_match_count(
    "EffectsCore/fx_pool.h"
    "\\*firstFreeIndex[ \t]*="
    2
    "only the validated allocate and free commit points may mutate the pool head")
require_source_match_count(
    "EffectsCore/fx_pool.h"
    "Sys_AtomicLoad[ \t\r\n]*\\([ \t\r\n]*activeCount[ \t\r\n]*\\)"
    2
    "both allocation and release must validate an atomic active-count snapshot")
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "FxPoolAllocateLocked<Fx"
    3
    "all three live FX pool allocators must use the transactional helper")
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "FxPoolFreeLocked<Fx"
    4
    "all four live FX pool release wrappers must use the transactional helper")
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "activeCount[ \t]*,[ \t\r\n]*allocationState[ \t]*,[ \t\r\n]*&status"
    3
    "all allocation wrappers must pass their capacity-matched sidecar to the helper")
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "activeCount[ \t]*,[ \t\r\n]*allocationState[ \t]*,[ \t\r\n]*std::forward<BEFORE_PUBLISH>"
    4
    "all release wrappers must pass their capacity-matched sidecar to the helper")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "struct FxPoolAllocationStates"
    "production allocation ownership must remain in an external sidecar")
require_source_not_contains(
    "EffectsCore/fx_runtime.h"
    "FxPoolAllocationState"
    "allocation sidecars must not alter persisted FX runtime layouts")
foreach(_fx_pool_sidecar_member
    "MAX_ELEMS;elems"
    "MAX_TRAILS;trails"
    "MAX_TRAIL_ELEMS;trailElems")
    list(GET _fx_pool_sidecar_member 0 _fx_pool_sidecar_limit)
    list(GET _fx_pool_sidecar_member 1 _fx_pool_sidecar_name)
    require_source_contains(
        "EffectsCore/fx_system.cpp"
        "FxPoolAllocationState<${_fx_pool_sidecar_limit}> ${_fx_pool_sidecar_name};"
        "production FX pool ${_fx_pool_sidecar_name} must have a capacity-matched sidecar")
endforeach()
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "if (system == &fx_systemPool[index])"
    "allocation sidecars must be resolved only for owned FX systems")
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "FxPoolResetAllocationState[ \t\r\n]*\\([ \t\r\n]*&states->(elems|trails|trailElems)[ \t\r\n]*\\)"
    3
    "system reset must initialize all three production allocation sidecars")
require_source_ordered(
    "EffectsCore/fx_system.cpp"
    "system->trails[MAX_TRAILS - 1].nextFree = -1;"
    "FxPoolResetAllocationState(&states->elems);"
    "system reset must finish every free list before publishing empty sidecar state")
require_source_contains(
    "EffectsCore/fx_system.h"
    "bool __cdecl FX_RebuildPoolAllocationStates(FxSystem *system);"
    "archive restoration must expose checked pool-sidecar reconstruction")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "FxPoolAllocationStates rebuilt{};"
    "production sidecar reconstruction must stage all three pools locally")
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "FxPoolRebuildAllocationStateLocked<Fx"
    3
    "production sidecar reconstruction must derive all three pool states")
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "Sys_AtomicLoad[ \t\r\n]*\\([ \t\r\n]*&system->active(Elem|Trail|TrailElem)Count[ \t\r\n]*\\)[ \t\r\n]*==[ \t\r\n]*Sys_AtomicLoad[ \t\r\n]*\\([ \t\r\n]*&rebuilt(Elem|Trail|TrailElem)Count[ \t\r\n]*\\)"
    3
    "archive pool reconstruction must match every persisted active count to the derived free list")
require_source_ordered(
    "EffectsCore/fx_system.cpp"
    "if (valid)"
    "*states = rebuilt;"
    "Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);"
    "production sidecar reconstruction must publish all pools atomically under the allocator lock")

# The pure archive graph validator binds every allocated pool slot to exactly
# one owning effect/list, validates backlinks and trail endpoints, and rejects
# orphaned or aliased records before save or restore publication.
foreach(_fx_graph_consumer
    "EffectsCore/fx_system.cpp"
    "EffectsCore/fx_archive.cpp")
    require_source_contains(
        "${_fx_graph_consumer}"
        "#include \"fx_pool_graph.h\""
        "production FX graph consumers must include the shared validator")
endforeach()
require_source_not_contains(
    "EffectsCore/fx_pool_graph.h"
    "inline bool FxValidatePoolAllocationGraph("
    "graph validation must not restore an implicit large-stack wrapper")
require_source_contains(
    "EffectsCore/fx_pool_graph.h"
    "struct FxPoolAllocationGraphScratch"
    "graph validation must expose bounded caller-owned working storage")
require_source_contains(
    "EffectsCore/fx_pool_graph.h"
    "inline bool FxValidatePoolAllocationGraphWithScratch("
    "stack-sensitive graph validation must accept caller-owned scratch")
require_source_contains(
    "EffectsCore/fx_pool_graph.h"
    "if (!state.initialized || state.allocatedCount > LIMIT)"
    "graph validation must reject malformed allocation-sidecar metadata")
require_source_contains(
    "EffectsCore/fx_pool_graph.h"
    "if (allocatedCount != state.allocatedCount)"
    "graph validation must reconcile allocation bits with their recorded count")
require_source_contains(
    "EffectsCore/fx_pool_graph.h"
    "for (std::size_t index = LIMIT; index < bitCapacity; ++index)"
    "graph validation must reject set padding bits outside a pool")
require_source_contains(
    "EffectsCore/fx_pool_graph.h"
    "if (!FxPoolAllocationStateIsAllocated(allocationState, index))"
    "graph handle decoding must reject references to free slots")
foreach(_fx_graph_visit_set
    "MAX_EFFECTS;allocatedEffectSlots"
    "MAX_ELEMS;visitedElems"
    "MAX_TRAILS;visitedTrails"
    "MAX_TRAIL_ELEMS;visitedTrailElems")
    list(GET _fx_graph_visit_set 0 _fx_graph_visit_limit)
    list(GET _fx_graph_visit_set 1 _fx_graph_visit_name)
    require_source_contains(
        "EffectsCore/fx_pool_graph.h"
        "std::array<bool, ${_fx_graph_visit_limit}> ${_fx_graph_visit_name}{};"
        "graph validation must track ${_fx_graph_visit_name} against aliases")
endforeach()
require_source_contains(
    "EffectsCore/fx_pool_graph.h"
    "std::array<bool, MAX_EFFECTS> effectSlotsInPermutation{};"
    "the entire effect-handle inventory must be a unique pool permutation")
foreach(_fx_graph_bound
    MAX_ELEMS
    MAX_TRAILS
    MAX_TRAIL_ELEMS)
    require_source_contains(
        "EffectsCore/fx_pool_graph.h"
        "== ${_fx_graph_bound}"
        "graph traversal must enforce the ${_fx_graph_bound} capacity")
endforeach()
require_source_contains(
    "EffectsCore/fx_pool_graph.h"
    "elem->item.prevElemHandleInEffect\n                        != expectedPreviousHandle"
    "graph validation must verify element backlinks while traversing")
require_source_contains(
    "EffectsCore/fx_pool_graph.h"
    "if (hasFirstTrailElem != hasLastTrailElem)"
    "graph validation must reject inconsistent trail endpoint sentinels")
require_source_contains(
    "EffectsCore/fx_pool_graph.h"
    "if (terminalTrailElemHandle != trail->item.lastElemHandle)"
    "graph validation must bind each recorded trail tail to its terminal element")
require_source_match_count(
    "EffectsCore/fx_pool_graph.h"
    "EveryAllocatedSlotWasVisited[ \t\r\n]*\\("
    4
    "the graph helper plus all three pool completeness checks must remain accounted for")
require_source_ordered(
    "EffectsCore/fx_archive.cpp"
    "!FxValidatePoolAllocationGraphWithScratch("
    "*states = rebuilt;"
    "restore staging must validate the reconstructed graph before publishing its sidecar")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "const bool valid = FxValidatePoolAllocationGraphWithScratch("
    "production graph validation must inspect live sidecars while allocator state is stable")
require_source_ordered(
    "EffectsCore/fx_archive.cpp"
    "FX_LinkSystemBuffers(validationSystem, bufferSnapshot);"
    "if (!FX_RebuildArchivePoolAllocationStates("
    "save must relink and validate the complete copied graph")
require_source_matches(
    "EffectsCore/fx_archive.cpp"
    "case[ \t]+Operation::ValidateDesiredState:[ \t\r\n]*return[ \t]+FX_ArchiveRestoreControlStatus\\([ \t\r\n]*FX_ArchiveVisibilitySelectorsMatch\\([ \t\r\n]*context.system,[ \t\r\n]*context.systemBuffers,[ \t\r\n]*context.desiredVisibilitySelectors\\)[ \t\r\n]*&&[ \t]*FX_RebuildPoolAllocationStatesNoReport\\(context.system\\)[ \t\r\n]*&&[ \t]*FX_ValidatePoolAllocationGraphStateWithScratch\\([ \t\r\n]*context.system,[ \t\r\n]*&context.physicsScratch->poolGraph\\)[ \t\r\n]*&&[ \t]*FX_ValidateArchivePhysicsOwnershipLockedWithScratch\\("
    "restore publication must round-trip selectors, then rebuild and revalidate pool/physics sidecars before admission")
foreach(_fx_pool_sidecar_use
    "elems;7"
    "trails;4"
    "trailElems;4")
    list(GET _fx_pool_sidecar_use 0 _fx_pool_sidecar_name)
    list(GET _fx_pool_sidecar_use 1 _fx_pool_sidecar_count)
    require_source_match_count(
        "EffectsCore/fx_system.cpp"
        "&states->${_fx_pool_sidecar_name}"
        ${_fx_pool_sidecar_count}
        "every production ${_fx_pool_sidecar_name} reset/check/allocate/free must use its matching sidecar")
endforeach()
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "FX_EnterArchiveAwarePoolCriticalSection[ \t\r\n]*\\([ \t\r\n]*\\)[ \t]*[;]"
    28
    "every pool/reference/admission mutation and graph or sidecar check must use archive-aware admission")
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "Sys_LeaveCriticalSection[ \t\r\n]*\\([ \t\r\n]*CRITSECT_FX_ALLOC[ \t\r\n]*\\)[ \t]*[;][ \t\r\n]*if[ \t\r\n]*\\([ \t\r\n]*status[ \t]*!=[ \t]*FxPoolMutationStatus::Success[ \t\r\n]*&&[ \t]*status[ \t]*!=[ \t]*FxPoolMutationStatus::Empty[ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*Com_Error[ \t\r\n]*\\([ \t\r\n]*ERR_DROP"
    3
    "every non-empty allocation failure must drop only after releasing the allocator lock")
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "Sys_LeaveCriticalSection[ \t\r\n]*\\([ \t\r\n]*CRITSECT_FX_ALLOC[ \t\r\n]*\\)[ \t]*[;][ \t\r\n]*if[ \t\r\n]*\\([ \t\r\n]*status[ \t]*!=[ \t]*FxPoolMutationStatus::Success[ \t\r\n]*&&[ \t]*status[ \t]*!=[ \t]*FxPoolMutationStatus::BeforePublishRejected[ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*Com_Error[ \t\r\n]*\\([ \t\r\n]*ERR_DROP"
    2
    "every unexpected element pool release failure must drop only after releasing the allocator lock")
require_source_not_matches(
    "EffectsCore/fx_system.cpp"
    "\\*firstFreeIndex[ \t]*=|(\\+\\+|--)[ \t]*\\*activeCount"
    "FX pool wrappers must not bypass their transactional mutation helpers")

# Runtime graph mutations consult the same allocation sidecars before trusting
# an owner, target, or neighboring link. This closes the valid-handle-to-free-
# slot gap that the bounded handle codec alone cannot detect.
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "state[ \t]*&&[ \t]*state->initialized[^;]*FxPoolItemIndex<ITEM_TYPE,[ \t]*LIMIT>[^;]*FxPoolAllocationStateIsAllocated"
    "runtime sidecar checks must validate initialization, pool ownership, alignment, and allocation state")
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "FX_IsPoolItemAllocatedLocked<Fx"
    5
    "the shared helper plus element, trail, and trail-element sidecar wrappers must remain accounted for")
foreach(_fx_runtime_allocation_check
    "Elem;11"
    "Trail;8"
    "TrailElem;8")
    list(GET _fx_runtime_allocation_check 0 _fx_runtime_item)
    list(GET _fx_runtime_allocation_check 1 _fx_runtime_count)
    require_source_match_count(
        "EffectsCore/fx_system.cpp"
        "FX_Is${_fx_runtime_item}Allocated[ \t\r\n]*\\("
        ${_fx_runtime_count}
        "all runtime ${_fx_runtime_item} ownership checks must remain accounted for")
endforeach()
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "FxDecodeHandle<FxPool<FxTrail>,[ \t]*MAX_TRAILS[^;]*[;][ \t\r\n]*if[ \t\r\n]*\\([ \t\r\n]*!trail[ \t]*\\|\\|[ \t]*!FX_IsTrailAllocated\\(system,[ \t]*&trail->item\\)[ \t\r\n]*\\)[ \t\r\n]*return[ \t]+false[ \t]*[;]"
    "garbage collection must reject a valid handle that names a free trail slot")
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "if[ \t\r\n]*\\([ \t\r\n]*!FX_IsTrailAllocated\\(system,[ \t]*trail(Owner)?\\)[ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*Com_Error[ \t\r\n]*\\([ \t\r\n]*ERR_DROP"
    3
    "trail spawners and trail-element free must reject an unallocated pool owner")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "nextElemInEffect[ \t]*=[ \t\r\n]*FX_PoolFromHandle_Generic<FxElem,[ \t]*MAX_ELEMS>[^;]*[;][ \t\r\n]*if[ \t\r\n]*\\([ \t\r\n]*!FX_IsElemAllocated\\(system,[ \t]*&nextElemInEffect->item\\)[^}]*Com_Error[ \t\r\n]*\\([ \t\r\n]*ERR_DROP"
    "element spawning must reject a free slot at the current list head")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "if[ \t\r\n]*\\([ \t\r\n]*!elem[ \t]*\\|\\|[ \t]*!FX_IsElemAllocated\\(system,[ \t]*&elem->item\\)[ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*Com_Error[ \t\r\n]*\\([ \t\r\n]*ERR_DROP"
    "element free must reject an unallocated target before reading its links")
foreach(_fx_elem_neighbor next prev)
    require_source_matches(
        "EffectsCore/fx_system.cpp"
        "!${_fx_elem_neighbor}Elem[ \t]*\\|\\|[ \t]*${_fx_elem_neighbor}Elem[ \t]*==[ \t]*elem[ \t\r\n]*\\|\\|[ \t]*!FX_IsElemAllocated\\(system,[ \t]*&${_fx_elem_neighbor}Elem->item\\)[ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*Com_Error[ \t\r\n]*\\([ \t\r\n]*ERR_DROP"
        "element free must reject an unallocated ${_fx_elem_neighbor} neighbor")
endforeach()
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "!trailElem[ \t\r\n]*\\|\\|[ \t]*!FX_IsTrailElemAllocated\\(system,[ \t]*&trailElem->item\\)[ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*Com_Error[ \t\r\n]*\\([ \t\r\n]*ERR_DROP"
    "trail-element free must reject an unallocated target")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "!lastTrailElem[^;]*!FX_IsTrailElemAllocated\\(system,[ \t]*&lastTrailElem->item\\)[^;]*lastTrailElem->item.nextTrailElemHandle[ \t]*!=[ \t]*FX_INVALID_HANDLE"
    "trail-element free must validate tail ownership before trusting terminal state")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "!nextTrailElem[ \t\r\n]*\\|\\|[ \t]*!FX_IsTrailElemAllocated[ \t\r\n]*\\([ \t\r\n]*system[ \t]*,[ \t]*&nextTrailElem->item\\)"
    "trail-element free must reject an unallocated successor")
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "if[ \t\r\n]*\\([ \t\r\n]*!FX_FreePool_Generic_Fx"
    2
    "both bool-returning trail free paths must stop when transactional publication fails")
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "if[ \t\r\n]*\\([ \t\r\n]*poolStatus[ \t]*!=[ \t]*FxPoolMutationStatus::Success"
    2
    "both status-returning element free paths must stop when transactional publication fails")
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "FX_FreePool_Generic_Fx(Elem(_Status)?|Trail|TrailElem)_[^;]*\\[&\\][ \t\r\n]*\\([ \t\r\n]*\\)[ \t\r\n]*noexcept"
    5
    "every live or rollback FX free path must supply a nonthrowing pre-publication unlink")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "FX_FreePool_Generic_FxTrail_[ \t\r\n]*\\([^;]*\\[&\\][ \t\r\n]*\\([ \t\r\n]*\\)[ \t\r\n]*noexcept[ \t\r\n]*\\{[^;]*effect->firstTrailHandle[ \t]*=[ \t]*nextTrailHandle[ \t]*[;]"
    "trail ownership must be unlinked inside the pre-publication callback")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "if[ \t\r\n]*\\([ \t]*!elemClass[ \t]*&&[ \t]*effect->firstSortedElemHandle[ \t]*==[ \t]*elemHandle[ \t]*\\)[ \t\r\n]*effect->firstSortedElemHandle[ \t]*=[ \t\r\n]*releasedElem.nextElemHandleInEffect[ \t]*;"
    "main element graph unlink must update the sorted head from the released snapshot")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "FX_FreePool_Generic_FxTrailElem_[ \t\r\n]*\\([^;]*\\[&\\][ \t\r\n]*\\([ \t\r\n]*\\)[ \t\r\n]*noexcept[ \t\r\n]*\\{[^;]*trail->lastElemHandle[ \t]*=[ \t]*FX_INVALID_HANDLE[ \t]*[;]"
    "trail element links must be removed inside the pre-publication callback")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "system->activeSpotLightElemHandle[ \t]*=[ \t]*FX_INVALID_HANDLE[ \t]*;[ \t\r\n]*system->activeSpotLightBoltDobj[ \t]*=[ \t]*-1[ \t]*;[ \t\r\n]*Sys_AtomicStore[ \t\r\n]*\\([ \t\r\n]*&system->activeSpotLightElemCount[ \t]*,[ \t]*0[ \t\r\n]*\\)"
    "spot-light graph ownership must be cleared as one pre-publication operation")

# Pool active counts are shared between the allocator and profiling paths.
# Resets and reads therefore must use the same exact-width atomic boundary.
require_all_occurrences_wrapped(
    "EffectsCore/fx_system.cpp"
    "system->active(Elem|Trail|TrailElem)Count"
    "(Sys_Atomic(Store|Load)[ \t\r\n]*\\([^)]*|FX_(Alloc|Free)Pool_Generic_Fx[^;{}]*)&system->active(Elem|Trail|TrailElem)Count"
    "FX pool active-count reset and mutation ownership")
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "Sys_AtomicStore[ \t\r\n]*\\([ \t\r\n]*&system->active(Elem|Trail|TrailElem)Count[ \t\r\n]*,[ \t\r\n]*0[ \t\r\n]*\\)"
    3
    "all three FX pool active counts must reset atomically")
require_all_occurrences_wrapped(
    "EffectsCore/fx_profile.cpp"
    "system->active(Elem|Trail|TrailElem)Count"
    "Sys_AtomicLoad[ \t\r\n]*\\([ \t\r\n]*&system->active(Elem|Trail|TrailElem)Count[ \t\r\n]*\\)"
    "FX profiling active-count snapshots")
require_source_match_count(
    "EffectsCore/fx_profile.cpp"
    "Sys_AtomicLoad[ \t\r\n]*\\([ \t\r\n]*&system->active(Elem|Trail|TrailElem)Count[ \t\r\n]*\\)"
    3
    "FX profiling must snapshot all three shared pool counts atomically")

# Archive traversal consumes externally persisted handles. The production
# collector is only an output adapter over the shared semantic oracle: it must
# not grow a second graph traversal, must not activate staged payloads, and
# must publish scalar outputs only after all optional state capture succeeds.
extract_security_slice(
    _fx_archive_save_source
    "struct FxArchivePhysicsEntrySink"
    "bool FX_ValidateArchiveEffectDefinitionReferences("
    _fx_archive_semantic_adapter
    "FX shared semantic collector adapter")
extract_security_slice(
    _fx_archive_save_source
    "bool FX_CollectArchivePhysicsEntries("
    "bool FX_ValidateArchiveEffectDefinitionReferences("
    _fx_archive_semantic_collector
    "FX shared semantic collector wrapper")
require_security_slice_ordered(
    _fx_archive_semantic_adapter
    "if (!context.entries)"
    "if (physicsIndex >= context.entryCapacity)"
    "count-only collection must bypass storage before the bounded sink check")
require_security_slice_ordered(
    _fx_archive_semantic_adapter
    "if (physicsIndex >= context.entryCapacity)"
    "context.entries[physicsIndex]"
    "the semantic sink must prove capacity before indexing output storage")
require_security_slice_contains(
    _fx_archive_semantic_collector
    "&sink, nullptr, FX_AppendArchivePhysicsEntry"
    "production collection must disable staged-payload preparation")
require_security_slice_contains(
    _fx_archive_semantic_collector
    "fx::archive::TryValidateFxArchiveSemanticsNoReport("
    "production collection must delegate to the shared semantic oracle")
require_security_slice_ordered(
    _fx_archive_semantic_collector
    "fx::archive::TryValidateFxArchiveSemanticsNoReport("
    "FX_CaptureArchivePhysicsStates("
    "semantic validation and bounded collection must precede native state capture")
require_security_slice_ordered(
    _fx_archive_semantic_collector
    "FX_CaptureArchivePhysicsStates("
    "*outSpotLightBoltDobj = result.spotLightBoltDobj;"
    "optional native state capture must succeed before spotlight publication")
require_security_slice_ordered(
    _fx_archive_semantic_collector
    "*outSpotLightBoltDobj = result.spotLightBoltDobj;"
    "*outEntryCount = entryCount;"
    "entry count must be the collector's final scalar publication")
foreach(_duplicate_semantic_traversal_marker
    "firstActiveEffect"
    "firstElemHandle"
    "firstTrailHandle"
    "activeSpotLightEffectHandle"
    "ValidateArchiveEffectRuntime("
    "ValidateArchiveElemRuntime(")
    forbid_security_slice_contains(
        _fx_archive_semantic_collector
        "${_duplicate_semantic_traversal_marker}"
        "the production collector must not duplicate shared semantic traversal")
endforeach()
foreach(_removed_archive_semantic_helper
    FX_GetArchiveEffectDefCount
    FX_ValidateArchiveEffectDefTiming
    FX_ArchiveElemTypeMatchesClass
    FX_ValidateArchiveElemRuntime
    FX_ValidateArchiveEffectRuntime
    FX_ValidateArchiveSampledLifespan)
    require_source_not_contains(
        "EffectsCore/fx_archive.cpp"
        "${_removed_archive_semantic_helper}"
        "removed semantic helper ${_removed_archive_semantic_helper} must remain owned by the shared oracle")
endforeach()

# Visibility roles cross the legacy archive boundary only as a validated,
# distinct selector pair. Shared helpers must use exact pointer equality, and
# restore publication must resolve fresh pointers in the destination buffers
# rather than copying or reinterpreting staged pointer values.
require_source_contains(
    "EffectsCore/fx_snapshot_publication.h"
    "std::uint8_t read = 0;\n    std::uint8_t write = 0;"
    "the zero-initialized visibility selector pair must remain invalid")
require_source_contains(
    "EffectsCore/fx_snapshot_publication.h"
    "selectors.read >= 2 || selectors.write >= 2\n        || selectors.read == selectors.write"
    "visibility selector resolution must reject out-of-range or aliased roles")
require_source_contains(
    "EffectsCore/fx_snapshot_publication.h"
    "readState == slot1"
    "visibility selector derivation must use exact owned-slot equality")
foreach(_forbidden_visibility_helper_codec
    "reinterpret_cast"
    "uintptr_t"
    "readState -"
    "writeState -"
    "slot1 - slot0")
    require_source_not_contains(
        "EffectsCore/fx_snapshot_publication.h"
        "${_forbidden_visibility_helper_codec}"
        "visibility selector helpers must not use integer or pointer-arithmetic codecs")
endforeach()
require_source_matches(
    "EffectsCore/fx_archive.cpp"
    "FX_ArchiveVisibilitySelectorsMatch\\([^}]*system->visState[ \t]*==[ \t]*buffers->visState[^}]*FX_VisibilitySelectorsRoundTrip\\([^}]*&buffers->visState\\[0\\][^}]*&buffers->visState\\[1\\][^}]*system->visStateBufferRead[^}]*system->visStateBufferWrite"
    "archive graph admission must round-trip selector roles against its exact buffers")
foreach(_restore_selector_context_member
    "FxVisibilityBufferSelectors desiredVisibilitySelectors{};"
    "FxVisibilityBufferSelectors originalVisibilitySelectors{};")
    require_source_contains(
        "EffectsCore/fx_archive.cpp"
        "${_restore_selector_context_member}"
        "restore control must own both selector values with its staged graphs")
endforeach()
file(READ
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive.cpp"
    _fx_archive_visibility_source)
extract_security_slice(
    _fx_archive_visibility_source
    "[[nodiscard]] bool FX_PrepareArchiveDisk32PhysicsEntries("
    "[[noreturn]] void FX_ReportArchiveDisk32RestoreFailure("
    _fx_candidate_visibility_staging
    "candidate visibility staging")
require_security_slice_ordered(
    _fx_candidate_visibility_staging
    "FX_TryDeriveVisibilitySelectorPair("
    "FX_ArchiveVisibilitySelectorsMatch("
    "restore must derive and round-trip candidate-owned visibility roles")
extract_security_slice(
    _fx_archive_visibility_source
    "void __cdecl FX_Restore(int32_t clientIndex, MemoryFile *memFile)"
    "FxEffect *__cdecl FX_EffectFromHandle("
    _fx_portable_restore_source
    "portable FX restore")
require_security_slice_ordered(
    _fx_portable_restore_source
    "TryGetFxArchiveRestoreCandidateDisk32ReadyView("
    "FX_PrepareArchiveDisk32PhysicsEntries("
    "desired roles must derive only after the candidate Ready view commits")
require_source_ordered(
    "EffectsCore/fx_archive.cpp"
    "restoreContext.originalVisibilitySelectors ="
    "fx::archive::RunRestoreControl(restoreCallbacks)"
    "both selector pairs must enter controller ownership before dispatch")
foreach(_forbidden_restore_selector_publication
    "reinterpret_cast<const FxVisState *>"
    "reinterpret_cast<FxVisState *>"
    "context.system->visStateBufferRead = context.desiredSystem"
    "context.system->visStateBufferWrite = context.desiredSystem"
    "context.system->visStateBufferRead = context.originalSystem"
    "context.system->visStateBufferWrite = context.originalSystem")
    require_source_not_contains(
        "EffectsCore/fx_archive.cpp"
        "${_forbidden_restore_selector_publication}"
        "restore must not publish staged or reinterpreted visibility pointers")
endforeach()
require_source_matches(
    "EffectsCore/fx_archive.cpp"
    "case[ \t]+Operation::ValidateOriginalGraph:[^;]*FX_ArchiveVisibilitySelectorsMatch\\([ \t\r\n]*context.system,[ \t\r\n]*context.systemBuffers,[ \t\r\n]*context.originalVisibilitySelectors\\)[ \t\r\n]*&&[ \t]*\\(!context.originalGraphPublished"
    "rollback graph admission must lead with an exact selector round-trip")
require_source_ordered(
    "EffectsCore/fx_system.cpp"
    "system->visStateBufferRead = system->visState;"
    "system->visStateBufferWrite = system->visState + 1;"
    "safe-empty recovery must canonicalize visibility roles to read-zero/write-one")

# Restore uses nullable codecs and completes all raw validation before it can
# publish live state. The legacy public wrappers for partial physics
# restore/save have been removed so callers cannot bypass the transaction.
require_source_matches(
    "EffectsCore/fx_archive.cpp"
    "\\[\\[noreturn\\]\\][ \t]+void[ \t]+FX_DropInvalidEffectHandle[^}]*Com_Error[ \t\r\n]*\\([ \t\r\n]*ERR_DROP[^;]*\\)[ \t]*[;][ \t\r\n]*std::abort[ \t\r\n]*\\([ \t\r\n]*\\)[ \t]*[;]"
    "invalid effect handles must use ERR_DROP with a nonreturning abort fallback")
require_source_matches(
    "EffectsCore/fx_archive.cpp"
    "FxDecodeHandle<FxEffect,[ \t]*FX_EFFECT_LIMIT,[ \t]*FxEffect::HANDLE_SCALE>[ \t\r\n]*\\([^;]*\\)[ \t]*[;][ \t\r\n]*if[ \t\r\n]*\\([ \t\r\n]*!effect[ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*FX_DropInvalidEffectHandle[ \t\r\n]*\\([ \t\r\n]*handle[ \t\r\n]*\\)[ \t]*[;][ \t\r\n]*\\}[ \t\r\n]*return[ \t]+effect[ \t]*[;]"
    "effect decoding must route invalid codec results through its central drop hook")
require_source_matches(
    "EffectsCore/fx_archive_semantics.cpp"
    "FxDecodeHandle<[ \t\r\n]*FxEffect,[ \t]*MAX_EFFECTS,[ \t]*FxEffect::HANDLE_SCALE>[ \t\r\n]*\\([^;]*\\)[ \t]*[;][ \t\r\n]*std::size_t[ \t]+elemDefCount[ \t]*=[ \t]*0[ \t]*[;][ \t\r\n]*if[ \t\r\n]*\\([ \t\r\n]*!effect[ \t]*\\|\\|[ \t]*!ValidateArchiveEffectRuntime"
    "archive traversal must reject an invalid effect handle before using runtime fields")
require_source_matches(
    "EffectsCore/fx_archive_semantics.cpp"
    "remoteElem[ \t]*=[ \t]*FxDecodeHandle<[^;]+[;][ \t\r\n]*if[ \t\r\n]*\\([ \t\r\n]*!remoteElem[ \t\r\n]*\\|\\|[ \t]*remoteElem->item.defIndex[ \t]*>=[ \t]*elemDefCount"
    "archive element traversal must reject invalid handles and definition indices")
require_source_contains(
    "EffectsCore/fx_archive_semantics.cpp"
    "if (chainLength++ == MAX_ELEMS)"
    "archive element chains must remain bounded by the shared pool")
require_source_contains(
    "EffectsCore/fx_archive_semantics.cpp"
    "if (trailCount++ == MAX_TRAILS)"
    "archive trail chains must remain bounded by the shared pool")
require_source_contains(
    "EffectsCore/fx_archive_semantics.cpp"
    "if (trailElemCount++ == MAX_TRAIL_ELEMS)"
    "archive trail-element chains must remain bounded by the shared pool")
require_source_matches(
    "EffectsCore/fx_archive_semantics.cpp"
    "firstNewEffect[ \t]*==[ \t]*firstFreeEffect[^;]*allocatedEffectCount[ \t]*>=[ \t]*0[^;]*allocatedEffectCount[^;]*<=[^;]*MAX_EFFECTS"
    "archive effect rings must be quiescent, forward, and pool-bounded")
# Only the complete archive transaction is public. Definition resolution and
# graph normalization now remain inside the lease-bound portable reader and
# independently owned candidate; obsolete raw fixup and partial APIs must not
# reappear.
require_source_contains(
    "EffectsCore/fx_archive.cpp"
    "#include \"fx_archive_reader_disk32.h\""
    "production restore must compile the unified portable reader")
require_source_contains(
    "EffectsCore/fx_archive.cpp"
    "#include \"fx_archive_restore_candidate_disk32.h\""
    "production restore must compile the mutable candidate boundary")
require_source_ordered(
    "EffectsCore/fx_archive.cpp"
    "TryReadFxArchiveDisk32NoReport("
    "TryBuildFxArchiveRestoreCandidateDisk32("
    "the complete reader image must precede candidate copying")
require_source_matches(
    "EffectsCore/fx_archive.cpp"
    "TryBuildFxArchiveRestoreCandidateDisk32\\([ \t\r\n]*staging.reader,[ \t\r\n]*tableResult.lease,[ \t\r\n]*staging.candidate\\)"
    "candidate construction must bind exact reader and lease provenance")
require_source_matches(
    "EffectsCore/fx_archive.cpp"
    "TryGetFxArchiveRestoreCandidateDisk32ReadyView\\([ \t\r\n]*staging.candidate,[ \t\r\n]*tableResult.lease,[ \t\r\n]*&candidateView\\)"
    "candidate Ready lookup must retain the exact build lease")
foreach(_fx_removed_effect_table_api
    FxEffectDefTable
    FX_RestoreEffectDefTable
    FX_AddEffectDefTableEntry
    FX_FixupEffectDefHandles)
    require_source_not_contains(
        "EffectsCore/fx_system.h"
        "${_fx_removed_effect_table_api}"
        "obsolete public ${_fx_removed_effect_table_api} contract must remain absent")
endforeach()
foreach(_fx_removed_effect_table_impl
    FxEffectDefTable
    FX_RestoreEffectDefTable
    FX_AddEffectDefTableEntry
    "bool FX_FixupEffectDefHandlesNoDrop("
    "bool __cdecl FX_FixupEffectDefHandles(")
    require_source_not_contains(
        "EffectsCore/fx_archive.cpp"
        "${_fx_removed_effect_table_impl}"
        "obsolete ${_fx_removed_effect_table_impl} implementation must remain absent")
endforeach()
foreach(_fx_removed_public_api
    FX_RestorePhysicsData
    FX_SavePhysicsData
    FX_RemoveAllEffectElems
    FX_EffectNoLongerReferenced)
    require_source_not_matches(
        "EffectsCore/fx_system.h"
        "(^|[;{}\r\n])[ \t]*(bool|void)[ \t]+(__cdecl[ \t]+)?${_fx_removed_public_api}[ \t\r\n]*\\("
        "obsolete public ${_fx_removed_public_api} API must remain absent")
endforeach()
foreach(_fx_removed_archive_api
    FX_RestorePhysicsData
    FX_SavePhysicsData)
    require_source_not_matches(
        "EffectsCore/fx_archive.cpp"
        "(^|[;{}\r\n])[ \t]*(bool|void)[ \t]+(__cdecl[ \t]+)?${_fx_removed_archive_api}[ \t\r\n]*\\("
        "obsolete partial ${_fx_removed_archive_api} implementation must remain absent")
endforeach()

# Archive parsing and serialization stage complete native snapshots. Restore
# validates a private reader image, copies it into an independently owned
# mutable candidate, and completes every fallible allocation before acquiring
# live ownership; save releases exclusion before fallible serialization.
require_source_contains(
    "EffectsCore/fx_archive.cpp"
    "FxArchiveDisk32RestoreStaging staging{};"
    "restore must centralize ownership for every staged allocation")
require_source_contains(
    "EffectsCore/fx_archive.cpp"
    "FxSystem *const restoredSystem = candidateView.system;"
    "restore must publish only candidate-owned system staging")
require_source_contains(
    "EffectsCore/fx_archive.cpp"
    "FxSystemBuffers *const restoredBuffers = candidateView.buffers;"
    "restore must publish only candidate-owned buffer staging")
require_security_slice_ordered(
    _fx_portable_restore_source
    "TryReadFxArchiveDisk32NoReport("
    "TryGetFxArchiveRestoreCandidateDisk32ReadyView("
    "the complete portable graph must validate before candidate publication")
require_security_slice_ordered(
    _fx_portable_restore_source
    "TryGetFxArchiveRestoreCandidateDisk32ReadyView("
    "FX_PrepareArchiveDisk32PhysicsEntries("
    "candidate graph publication must precede physics-entry preparation")
require_security_slice_ordered(
    _fx_portable_restore_source
    "FX_PrepareArchiveDisk32PhysicsEntries("
    "FX_AllocateArchiveRestoreTransactionWorkspace()"
    "candidate physics must validate before final transaction allocation")
require_security_slice_ordered(
    _fx_portable_restore_source
    "FX_DestroyArchiveDisk32ReaderWorkspace(staging.reader)"
    "staging.reader = nullptr;"
    "the reader must be destroyed before centralized ownership is cleared")
extract_security_slice(
    _fx_portable_restore_source
    "staging.reader = nullptr;"
    "if (tableReleaseStatus"
    _fx_post_reader_lease_handshake
    "post-reader exact lease handshake")
require_security_slice_contains(
    _fx_post_reader_lease_handshake
    "ValidateEffectTableRestoreLease(tableResult.lease)"
    "the exact lease must be revalidated after reader destruction")
require_security_slice_ordered(
    _fx_post_reader_lease_handshake
    "ValidateEffectTableRestoreLease(tableResult.lease)"
    "ReleaseEffectTableRestore(tableResult.lease)"
    "the final exact lease handshake must precede release")
require_security_slice_ordered(
    _fx_portable_restore_source
    "ReleaseEffectTableRestore(tableResult.lease)"
    "if (!FX_BeginArchive(system, restoreGeneration))"
    "archive admission must immediately follow successful definition-lease release")
file(READ
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive.cpp"
    _fx_archive_production_source)
extract_security_slice(
    _fx_archive_production_source
    "const fx::archive::EffectTableRestoreStatus tableReleaseStatus ="
    "if (!FX_BeginArchive(system, restoreGeneration))"
    _fx_restore_release_to_admission
    "definition release to archive admission")
foreach(_forbidden_release_gap_work
    "Z_Malloc("
    "MemFile_"
    "candidateView"
    "restoredSystem->"
    "restoredBuffers->"
    "TryReadFxArchiveDisk32NoReport("
    "TryBuildFxArchiveRestoreCandidateDisk32("
    "TryGetFxArchiveRestoreCandidateDisk32ReadyView("
    "FX_PrepareArchiveDisk32PhysicsEntries("
    "FX_CollectArchivePhysicsEntries(")
    forbid_security_slice_contains(
        _fx_restore_release_to_admission
        "${_forbidden_release_gap_work}"
        "successful definition release must flow directly into generation-checked admission")
endforeach()
foreach(_large_restore_local
    "FxArchiveDisk32ReaderWorkspace"
    "FxArchiveRestoreCandidateDisk32Workspace")
    require_source_not_matches(
        "EffectsCore/fx_archive.cpp"
        "(^|[^A-Za-z0-9_])${_large_restore_local}[ \t]+[A-Za-z_][A-Za-z0-9_]*"
        "large reader/candidate workspaces must remain heap-only")
endforeach()
foreach(_forbidden_candidate_interior_free
    "Z_Free(staging->candidate"
    "Z_Free(candidateView"
    "Z_Free(restoredSystem"
    "Z_Free(restoredBuffers")
    require_source_not_contains(
        "EffectsCore/fx_archive.cpp"
        "${_forbidden_candidate_interior_free}"
        "candidate interiors remain owned solely by the candidate workspace")
endforeach()
foreach(_obsolete_raw_restore
    "FX_ReadArchiveDataNoDrop("
    "FX_FixupEffectDefHandlesNoDrop("
    "FxSystem restoredSystem{};"
    "relocationBits"
    "relocatedReadAddress"
    "relocatedWriteAddress")
    require_source_not_contains(
        "EffectsCore/fx_archive.cpp"
        "${_obsolete_raw_restore}"
        "portable production restore must not revive raw native-pointer parsing")
endforeach()
# Restore publication must preserve the archive owner's existing iterator -1
# across both desired and rollback graph copies. Reacquiring after overwriting
# the iterator would expose live sidecars behind a nonexclusive graph window.
require_source_not_contains(
    "EffectsCore/fx_archive.cpp"
    "FX_RestoreArchiveExclusiveState("
    "archive graph publication must never reacquire overwritten iterator state")
require_source_contains(
    "EffectsCore/fx_archive.cpp"
    "Sys_AtomicStore(&context.desiredSystem->iteratorCount, -1);"
    "the desired graph image must preserve archive-exclusive iterator state")
require_source_matches(
    "EffectsCore/fx_archive.cpp"
    "Sys_AtomicLoad\\(&rollbackSystem.iteratorCount\\)[ \t]*==[ \t]*-1[ \t\r\n]*&&[ \t]*FX_ValidateArchiveExclusiveState\\(system\\);"
    "the rollback graph image must preserve archive-exclusive iterator state")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "FX_ValidateArchiveExclusiveState[^}]*ArchiveGateOwnerPhase::Acquired[^}]*ArchiveGateValue::Exclusive[^}]*FX_CurrentThreadOwnsArchive\\(system\\)[^}]*Sys_AtomicLoad\\(&system->iteratorCount\\)[ \t]*==[ \t]*-1"
    "archive publication validation must require acquired controller ownership, exclusive gate, and iterator -1")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "FX_CanPublishArchiveSafeEmptyStateLocked[^}]*FX_ValidateArchiveExclusiveState\\(system\\)[^}]*FX_CanResetSystemGraphUnderExclusiveClaim\\(system\\)"
    "safe-empty publication must preflight graph reset viability under archive exclusivity")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "system->effects == buffers->effects"
    "safe-empty graph reset must require canonical owned storage")
require_source_contains(
    "EffectsCore/fx_archive.cpp"
    "FX_CanPublishArchiveSafeEmptyStateLocked(system)"
    "safe-empty recovery must preflight graph reset prerequisites")
require_source_contains(
    "EffectsCore/fx_archive.cpp"
    "Sys_Error(\"FX archive restore could not recover a safe runtime state\")"
    "unrecoverable archive restore must fail-stop before releasing ownership")
require_source_matches(
    "EffectsCore/fx_archive.cpp"
    "if[ \t]*\\(restoreOutcome[ \t\r\n]*==[ \t]*fx::archive::RestoreControlOutcome::UnsafeFailure\\)[ \t\r\n]*\\{[^}]*Sys_Error\\(\"FX archive restore could not recover a safe runtime state\"\\)[ \t]*[;][^}]*std::abort\\(\\)[ \t]*[;][^}]*\\}[ \t\r\n]*Sys_LeaveCriticalSection\\(CRITSECT_PHYSICS\\)[ \t]*[;]"
    "unsafe controller outcome must abort before PHYSICS exclusion is released")
require_source_not_contains(
    "EffectsCore/fx_archive.cpp"
    "FX_FailArchivePhysicsCleanupLocked"
    "native cleanup failure must propagate through controller status")
require_source_not_contains(
    "EffectsCore/fx_archive.cpp"
    "FX archive native-body cleanup failed after ownership transfer"
    "native cleanup failure must use the centralized controller fail-stop")
require_source_not_contains(
    "EffectsCore/fx_archive.cpp"
    "(void)Phys_TryDestroyBodyLockedNoReport("
    "archive restore must never discard a fallible native cleanup result")
require_source_contains(
    "EffectsCore/fx_archive.cpp"
    "fx::archive::RunRestoreControl(restoreCallbacks)"
    "restore must delegate terminal-state sequencing to the portable controller")
require_source_contains(
    "EffectsCore/fx_archive.cpp"
    "!= fx::archive::RestoreControlOutcome::DesiredPublished"
    "only desired publication may report restore success")
foreach(_obsolete_restore_control
    livePhysicsCaptured
    retirementPlanned
    retirementComplete
    replacementPrepared
    replacementCreated
    stagedPhysicsValid
    replacementPublished
    restoreSucceeded
    validCommittedState
    oldStateRestored
    safeEmptyPublished
    safeTerminalState)
    require_source_not_contains(
        "EffectsCore/fx_archive.cpp"
        "${_obsolete_restore_control}"
        "the obsolete inline restore controller must remain absent")
endforeach()
require_source_contains(
    "EffectsCore/fx_archive.cpp"
    "struct FxArchiveSaveSnapshotWorkspace"
    "save must own one checked heap workspace for its copied image")
require_source_contains(
    "EffectsCore/fx_archive.cpp"
    "FxSystem serializedSystem{};"
    "save must preserve a distinct raw serialized system image")
require_source_contains(
    "EffectsCore/fx_archive.cpp"
    "FxSystem validationSystem{};"
    "save must validate through a separately relinked system image")
require_source_contains(
    "EffectsCore/fx_archive.cpp"
    "FxSystemBuffers *const bufferSnapshot = &snapshotWorkspace->buffers;"
    "save must stage pool buffers outside live state")
require_source_match_count(
    "EffectsCore/fx_archive.cpp"
    "FxArchiveSaveSnapshotWorkspace[ \t]*\\*[ \t]*const[ \t]+snapshotWorkspace[ \t]*=[ \t\r\n]*FX_AllocateArchiveSaveSnapshotWorkspace\\(\\)"
    1
    "save must allocate its bounded snapshot workspace outside the worker stack")
require_source_contains(
    "EffectsCore/fx_archive.cpp"
    "RUNTIME_SIZE(FxArchivePhysicsOwnershipScratch, 0x5808, 0x7010);"
    "save physics scratch must retain exact checked native layouts")
require_source_contains(
    "EffectsCore/fx_archive.cpp"
    "FxArchivePhysicsOwnershipScratch physics{};"
    "the checked save workspace must retain nested physics ownership scratch")
require_source_matches(
    "EffectsCore/fx_archive.cpp"
    "AllocateArchiveRestoreWorkspace<[ \t\r\n]*FxArchiveSaveSnapshotWorkspace>[ \t]*\\([ \t\r\n]*FX_ARCHIVE_RESTORE_WORKSPACE_MEMORY\\)"
    "save workspace must use checked construction and allocation")
require_source_matches(
    "EffectsCore/fx_archive.cpp"
    "DestroyArchiveRestoreWorkspace\\([ \t\r\n]*workspace,[ \t\r\n]*FX_ARCHIVE_RESTORE_WORKSPACE_MEMORY\\)"
    "save workspace must be destroyed before its storage is freed")
require_source_ordered(
    "EffectsCore/fx_archive.cpp"
    "FX_AllocateArchiveSaveSnapshotWorkspace();"
    "if (!FX_BeginArchive(system))"
    "save must allocate checked snapshot storage before archive exclusion")
require_source_contains(
    "EffectsCore/fx_archive.cpp"
    "&snapshotWorkspace->physics;"
    "save physics cross-check must use nested workspace scratch")
require_source_matches(
    "EffectsCore/fx_archive.cpp"
    "FX_CaptureArchivePhysicsStates\\([ \t\r\n]*system,[ \t\r\n]*systemSnapshot.msecNow,[ \t\r\n]*physicsEntries,[ \t\r\n]*physicsEntryCount,[ \t\r\n]*physicsOwnershipScratch\\)"
    "save physics capture must cross-check live ownership with workspace scratch")
require_source_matches(
    "EffectsCore/fx_archive.cpp"
    "if[ \t]*\\([ \t]*!liveOwner[ \t]*\\|\\|[ \t]*\\(entryCount[ \t]*!=[ \t]*0[ \t]*&&[ \t]*!entries\\)[ \t]*\\|\\|[ \t]*!ownershipScratch\\)"
    "state capture must reject missing caller-owned validation scratch")
require_source_not_contains(
    "EffectsCore/fx_archive.cpp"
    "bool FX_ValidateArchivePhysicsOwnershipLocked("
    "archive validation must not restore the large stack-owning wrapper")
require_source_match_count(
    "EffectsCore/fx_archive.cpp"
    "FX_DestroyArchiveSaveSnapshotWorkspace\\([ \t\r\n]*snapshotWorkspace\\)"
    7
    "save must destroy its complete snapshot workspace on every owned exit")
require_source_match_count(
    "EffectsCore/fx_archive.cpp"
    "FX_DestroyArchiveSaveSnapshotWorkspace\\([ \t\r\n]*snapshotWorkspace\\)[ \t]*[;][^}]*return[ \t]*[;]"
    7
    "every returning save path with workspace ownership must destroy it first")
foreach(_obsolete_save_scratch_wrapper
    FX_AllocateArchiveSavePhysicsOwnershipScratch
    FX_DestroyArchiveSavePhysicsOwnershipScratch)
    require_source_not_contains(
        "EffectsCore/fx_archive.cpp"
        "${_obsolete_save_scratch_wrapper}"
        "save scratch ownership must remain inside the complete workspace")
endforeach()
require_source_matches(
    "EffectsCore/fx_archive.cpp"
    "void[ \t]+FX_DestroyArchiveSaveSnapshotWorkspace\\([^}]*DestroyArchiveRestoreWorkspace\\([^}]*std::abort\\(\\)"
    "save workspace destruction must fail-stop if nested cleanup is refused")
require_source_matches(
    "EffectsCore/fx_archive.cpp"
    "Sys_EnterCriticalSection\\(CRITSECT_FX_ALLOC\\)[ \t]*[;][ \t\r\n]*snapshotCapturedExclusive[ \t]*=[ \t]*FX_ValidateArchiveExclusiveState\\(system\\)[ \t]*[;][ \t\r\n]*if[ \t]*\\(snapshotCapturedExclusive\\)[ \t\r\n]*\\{[ \t\r\n]*memcpy\\(&systemSnapshot,[ \t]*system,[ \t]*sizeof\\(systemSnapshot\\)\\)[ \t]*[;][ \t\r\n]*memcpy\\(bufferSnapshot,[ \t]*systemBuffers,[ \t]*sizeof\\(\\*bufferSnapshot\\)\\)[ \t]*[;][ \t\r\n]*\\}[ \t\r\n]*Sys_LeaveCriticalSection\\(CRITSECT_FX_ALLOC\\)[ \t]*[;]"
    "save must copy both raw images inside one proven-exclusive allocator interval")
require_source_ordered(
    "EffectsCore/fx_archive.cpp"
    "memcpy(bufferSnapshot, systemBuffers, sizeof(*bufferSnapshot));"
    "&& FX_ValidateArchiveCopiedSnapshot("
    "save must stage both raw images before validating the copied graph")
require_source_ordered(
    "EffectsCore/fx_archive.cpp"
    "FxSystem *const validationSystem = &workspace->validationSystem;"
    "FX_LinkSystemBuffers(validationSystem, bufferSnapshot);"
    "save must relink only the separate validation system")
require_source_not_contains(
    "EffectsCore/fx_archive.cpp"
    "FX_LinkSystemBuffers(&systemSnapshot"
    "save must never relink the raw serialized pointer image")
require_source_matches(
    "EffectsCore/fx_archive.cpp"
    "FX_CollectArchivePhysicsEntries\\([ \t\r\n]*validationSystem,[^;]*[ \t\r\n]*false,[ \t\r\n]*nullptr,[ \t\r\n]*&spotLightBoltDobj\\)"
    "save must collect copied graph semantics without capturing live physics")
require_source_ordered(
    "EffectsCore/fx_archive.cpp"
    "&& FX_ValidateArchiveCopiedSnapshot("
    "&& FX_CaptureArchivePhysicsStates("
    "save must validate copied semantics before live physics cross-check")
require_source_ordered(
    "EffectsCore/fx_archive.cpp"
    "systemSnapshot.activeSpotLightBoltDobj ="
    "if (!releasedArchive)"
    "save must finish staging before releasing archive exclusion")
require_source_ordered(
    "EffectsCore/fx_archive.cpp"
    "if (!releasedArchive)"
    "FX_WriteStagedEffectTableNoDrop(&effectTableStaging, memFile)"
    "save must release live ownership before fallible archive writes")
require_source_ordered(
    "EffectsCore/fx_archive.cpp"
    "FX_StageEffectTableNoDrop(&effectTableStaging)"
    "if (!FX_BeginArchive(system))"
    "save must stage and validate the effect table before graph capture")
require_source_ordered(
    "EffectsCore/fx_archive.cpp"
    "FX_ValidateArchiveEffectDefinitionReferences("
    "if (!FX_RebuildArchivePoolAllocationStates("
    "copied definition pointers must be admitted before graph traversal")
require_source_ordered(
    "EffectsCore/fx_archive.cpp"
    "FX_WriteStagedEffectTableNoDrop(&effectTableStaging, memFile)"
    "memFile, FX_ARCHIVE_SYSTEM_SIZE, &systemSnapshot"
    "the wire-compatible effect table must precede the staged system image")
require_source_matches(
    "EffectsCore/fx_archive.cpp"
    "FX_WriteArchiveDataNoDrop[ \t\r\n]*\\([ \t\r\n]*memFile,[ \t]*FX_ARCHIVE_BUFFER_SIZE,[ \t]*bufferSnapshot\\)"
    "save must serialize staged pool buffers rather than mutable live storage")

# The legacy archive is Disk32. Restore now decodes fixed-width addresses inside
# the portable reader, so only the not-yet-ported save path may inspect and
# narrow a native system pointer. Physics ownership remains a stable per-owner
# marker and never native pointer bits.
require_source_match_count(
    "EffectsCore/fx_archive.cpp"
    "reinterpret_cast<std::uintptr_t>\\(system\\)"
    1
    "only save may inspect the native system address")
require_source_matches(
    "EffectsCore/fx_archive.cpp"
    "systemAddress[ \t]*>[ \t\r\n]*\\(std::numeric_limits<std::uint32_t>::max\\)\\(\\)"
    "save must reject native system addresses outside Disk32")
require_source_not_contains(
    "EffectsCore/fx_archive.cpp"
    "FX_EncodeArchivedPhysicsBody"
    "archive physics pointer encoders must not return")
require_source_not_contains(
    "EffectsCore/fx_archive.cpp"
    "FX_DecodeArchivedPhysicsBody"
    "archive physics pointer decoders must not return")
require_source_matches(
    "EffectsCore/fx_archive.cpp"
    "const[ \t]+fx::physics::BodyToken[ \t]+marker[ \t]*=[ \t\r\n]*static_cast<fx::physics::BodyToken>\\(ownerIndex[ \t]*\\+[ \t]*1u\\)[ \t]*;[ \t\r\n]*bufferSnapshot->elems\\[ownerIndex\\].item.physObjId[ \t]*=[ \t\r\n]*fx::physics::TokenToLegacyField\\(marker\\)"
    "archive physics ownership must use canonical per-owner markers")
require_source_match_count(
    "EffectsCore/fx_archive.cpp"
    "if[ \t\r\n]*\\([ \t\r\n]*sizeof\\(void[ \t]*\\*\\)[ \t]*!=[ \t]*4[ \t\r\n]*\\)"
    1
    "only save remains guarded until a portable Disk32 writer exists")

# Portable BodyState conversion must not revive the legacy three-byte stack
# disclosure, and the non-publishing reader must keep every partial archive
# image hidden behind an exact lease and final Ready transition.
require_source_contains(
    "physics/phys_body_state.h"
    "RUNTIME_SIZE(BodyState, 0x70, 0x70);"
    "native BodyState must retain its exact pointer-free ABI")
require_source_contains(
    "physics/phys_ode.cpp"
    "BodyState state{};"
    "generic physics serialization must initialize the complete wire record")
require_source_contains(
    "physics/phys_ode.cpp"
    "state->underwater = dBodyIsEnabled(body) ? 1 : 0;"
    "generic physics capture must initialize the complete underwater word")
require_source_not_contains(
    "physics/phys_ode.cpp"
    "LOBYTE(state->underwater)"
    "physics serialization must not disclose uninitialized underwater bytes")
require_source_ordered(
    "physics/phys_ode.cpp"
    "BodyState state{};"
    "MemFile_WriteData(memFile, 112, &state);"
    "deterministic BodyState initialization must precede raw serialization")

require_source_contains(
    "EffectsCore/fx_archive_reader_disk32.h"
    "RUNTIME_SIZE(FxArchiveDisk32ReaderWorkspace, 0xA3D00, 0xA9D58);"
    "the heap-only reader workspace must retain exact x86/native64 bounds")
require_source_ordered(
    "EffectsCore/fx_archive_reader_disk32.cpp"
    "memFile, sizeof(workspace->sourceSystem_)"
    "memFile, sizeof(workspace->sourceBuffers_)"
    "the reader must consume the fixed system before its buffer image")
require_source_ordered(
    "EffectsCore/fx_archive_reader_disk32.cpp"
    "memFile, sizeof(workspace->sourceBuffers_)"
    "memFile, sizeof(workspace->archivedSystemAddress_)"
    "the reader must consume the legacy address after the fixed graph")
require_source_ordered(
    "EffectsCore/fx_archive_reader_disk32.cpp"
    "memFile, sizeof(workspace->archivedSystemAddress_)"
    "memFile, sizeof(workspace->bodyScratch_)"
    "the reader must consume semantic-counted bodies after the address")
require_source_ordered(
    "EffectsCore/fx_archive_reader_disk32.cpp"
    "ValidateEffectTableRestoreLease(leaseSnapshot)"
    "ReadExact(\n        memFile, sizeof(workspace->sourceSystem_)"
    "the exact definition lease must validate before fixed-tail consumption")
require_source_ordered(
    "EffectsCore/fx_archive_reader_disk32.cpp"
    "workspace->lease_ = SnapshotLease(leaseSnapshot);"
    "workspace->phase_ = FxArchiveDisk32ReaderPhase::Ready;"
    "reader Ready publication must follow its complete validated lease snapshot")
foreach(_forbidden_reader_operation
    "MemFile_ReadData("
    "ReleaseEffectTableRestore("
    "AbandonCurrentThreadEffectTableRestoreForError("
    "reinterpret_cast"
    "Z_Malloc"
    "operator new"
    "Com_Error"
    "Com_Print"
    "Sys_Error"
    "Sys_EnterCriticalSection")
    require_source_not_contains(
        "EffectsCore/fx_archive_reader_disk32.cpp"
        "${_forbidden_reader_operation}"
        "portable reader must remain bounded, borrowed, report-free, and non-publishing")
endforeach()

require_source_contains(
    "EffectsCore/fx_archive_restore_candidate_disk32.h"
    "FxArchiveDisk32ReaderLeaseIdentity lease_{};"
    "the mutable candidate must snapshot exact build-lease identity")
require_source_contains(
    "EffectsCore/fx_archive_restore_candidate_disk32.h"
    "RUNTIME_SIZE(FxArchiveRestoreCandidateDisk32Workspace, 0x5BDB0, 0x61E08);"
    "lease-bound candidate workspace must retain measured x86/native64 bounds")
require_source_matches(
    "EffectsCore/fx_archive_restore_candidate_disk32.h"
    "TryGetFxArchiveRestoreCandidateDisk32ReadyView\\([^;]*const EffectTableRestoreLease[ \t]*&lease,[^;]*FxArchiveRestoreCandidateDisk32ReadyView[ \t]*\\*outView\\)[ \t]*noexcept"
    "candidate Ready API must require the exact retained lease")
require_source_ordered(
    "EffectsCore/fx_archive_restore_candidate_disk32.cpp"
    "candidateWorkspace->lease_ = FxArchiveDisk32ReaderLeaseIdentity{};"
    "candidateWorkspace->operating_ = true;"
    "every idle build must clear old lease identity before callback-capable work")
require_source_ordered(
    "EffectsCore/fx_archive_restore_candidate_disk32.cpp"
    "candidateWorkspace->lease_ = SnapshotLease(lease);"
    "candidateWorkspace->phase_ =\n        FxArchiveRestoreCandidateDisk32Phase::Ready;"
    "candidate success must snapshot its exact lease before Ready publication")
require_source_match_count(
    "EffectsCore/fx_archive_restore_candidate_disk32.cpp"
    "!LeaseMatches\\(workspace->lease_,[ \t]*lease\\)"
    2
    "candidate getter must check exact identity before and after lifecycle validation")
file(READ
    "${SOURCE_ROOT}/src/EffectsCore/fx_archive_restore_candidate_disk32.cpp"
    _fx_candidate_source)
extract_security_slice(
    _fx_candidate_source
    "bool TryGetFxArchiveRestoreCandidateDisk32ReadyView("
    "} // namespace fx::archive"
    _fx_candidate_getter
    "lease-bound candidate Ready getter")
extract_security_slice(
    _fx_candidate_getter
    "workspace->operating_ = true;"
    "const FxArchiveRestoreCandidateDisk32ReadyView view{"
    _fx_candidate_getter_callback
    "candidate Ready lifecycle callback interval")
require_security_slice_ordered(
    _fx_candidate_getter_callback
    "workspace->operating_ = true;"
    "ValidateEffectTableRestoreLease(lease)"
    "candidate operation gate must close before lifecycle validation")
require_security_slice_ordered(
    _fx_candidate_getter_callback
    "ValidateEffectTableRestoreLease(lease)"
    "!LeaseMatches(workspace->lease_, lease)"
    "candidate identity must be rechecked after callback-capable validation")
extract_security_slice(
    _fx_candidate_getter
    "const FxArchiveRestoreCandidateDisk32ReadyView view{"
    "return true;"
    _fx_candidate_getter_success
    "candidate Ready output commit")
require_security_slice_ordered(
    _fx_candidate_getter_success
    "const FxArchiveRestoreCandidateDisk32ReadyView view{"
    "workspace->operating_ = false;"
    "a complete candidate view must form before reopening the gate")
require_security_slice_ordered(
    _fx_candidate_getter_success
    "workspace->operating_ = false;"
    "*outView = view;"
    "candidate output commits only after callback-capable work completes")
extract_security_slice(
    _fx_candidate_getter
    "bool TryGetFxArchiveRestoreCandidateDisk32ReadyView("
    "*outView = view;"
    _fx_candidate_getter_precommit
    "candidate Ready rejection paths")
forbid_security_slice_contains(
    _fx_candidate_getter_precommit
    "*outView ="
    "failed candidate Ready lookup must preserve caller output")

require_source_ordered(
    "EffectsCore/fx_archive.cpp"
    "FX archive save requires Disk32 conversion on this target"
    "FX_StageEffectTableNoDrop(&effectTableStaging)"
    "unsupported native save addresses must fail before staging archive payload")
foreach(_obsolete_restore_width_guard
    "FX archive restore requires Disk32 conversion on 64-bit targets"
    "FX archive restore requires Disk32 conversion on this target"
    "currentSystemAddress"
    "relocationBits"
    "relocatedReadAddress"
    "relocatedWriteAddress")
    require_source_not_contains(
        "EffectsCore/fx_archive.cpp"
        "${_obsolete_restore_width_guard}"
        "portable restore must not depend on native pointer width or raw relocation")
endforeach()
require_source_ordered(
    "EffectsCore/fx_effect_table_restore.cpp"
    "status = ParseEffectTable(memFile, lease, &entryCount);"
    "stableCallbacks.registerEffect("
    "archive effect definitions must all parse before the first registration")
require_source_ordered(
    "EffectsCore/fx_effect_table_restore.cpp"
    "if (!definition)"
    "g_restoreDefinitions[index] = definition;"
    "archive effect-definition registration must reject null before staging")
require_source_ordered(
    "EffectsCore/fx_effect_table_restore.cpp"
    "g_restoreDefinitions[index] = definition;"
    "g_restoreEntryCount = entryCount;"
    "archive effect-definition lookup must publish only after every registration")
require_source_contains(
    "EffectsCore/fx_archive_reader_disk32.cpp"
    "EffectTableRestoreFind(*context->lease, key)"
    "archive effect-definition lookup must require the exact active lease")
require_source_contains(
    "EffectsCore/fx_archive_semantics.cpp"
    "if (!effect || !effect->def || !outCount"
    "archive definition-count validation must reject missing owning pointers")
foreach(_fx_archive_elem_count
    elemDefCountLooping
    elemDefCountOneShot
    elemDefCountEmission)
    require_source_contains(
        "EffectsCore/fx_archive_semantics.cpp"
        "effect->def->${_fx_archive_elem_count} < 0"
        "archive element-definition lookup must reject a negative ${_fx_archive_elem_count}")
endforeach()
require_source_contains(
    "EffectsCore/fx_archive_semantics.cpp"
    "|| (count != 0 && !effect->def->elemDefs))"
    "archive definition-count validation must require storage for nonempty definitions")
require_source_matches(
    "EffectsCore/fx_archive_semantics.cpp"
    "if[ \t\r\n]*\\([ \t\r\n]*!effect[ \t]*\\|\\|[ \t]*!ValidateArchiveEffectRuntime\\(system,[ \t]*effect\\)[^;]*!TryGetArchiveEffectDefCount[^;]*!ValidateArchiveEffectDefTiming"
    "unified archive traversal must validate effect runtime and definition timing before pool links")
require_source_matches(
    "EffectsCore/fx_archive_semantics.cpp"
    "if[ \t\r\n]*\\([^;]*!ValidateArchiveElemRuntime[^;]*!ArchiveElemTypeMatchesClass[^;]*!AcceptPhysicsElem"
    "unified archive traversal must validate element runtime and class before physics collection")
require_source_contains(
    "EffectsCore/fx_archive.cpp"
    "if (!FX_ValidateArchiveBodyState(entries[index].state))"
    "captured physics states must be validated before serialization")

# Free paths validate each handle before reading or rewriting linked records;
# malformed neighbors must abort before the transactional pool publication.
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "else[ \t]+if[ \t\r\n]*\\([ \t\r\n]*!FX_RunGarbageCollection_FreeTrails[ \t\r\n]*\\([ \t\r\n]*system[ \t]*,[ \t]*effect[ \t\r\n]*\\)[ \t\r\n]*\\|\\|[ \t]*effect->firstTrailHandle[ \t]*!=[ \t]*FX_INVALID_HANDLE[ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*invalidTrailGraph[ \t]*=[ \t]*true[ \t]*[;]"
    "garbage collection must defer a corrupt trail graph until iterator ownership is released")
require_source_ordered(
    "EffectsCore/fx_system.cpp"
    "const bool releasedExclusive ="
    "else if (invalidEffectHandle)"
    "garbage collection must report corrupt effects only after ending exclusive iteration")
require_source_ordered(
    "EffectsCore/fx_system.cpp"
    "const bool releasedExclusive ="
    "else if (invalidTrailGraph)"
    "garbage collection must report corrupt effects/trails only after ending exclusive iteration")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "FxDecodeHandle<[ \t\r\n]*FxEffect,[ \t]*FX_EFFECT_LIMIT,[ \t]*FxEffect::HANDLE_SCALE>[ \t\r\n]*\\([^;]*effectHandle[^;]*[;][ \t\r\n]*if[ \t\r\n]*\\([ \t\r\n]*!effect[ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*FxRequestGarbageCollection[^}]*invalidEffectHandle[ \t]*=[ \t]*true[ \t]*[;]"
    "garbage collection must retain and defer an invalid active effect handle")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "FxDecodeHandle<[ \t\r\n]*FxEffect,[ \t]*FX_EFFECT_LIMIT,[ \t]*FxEffect::HANDLE_SCALE>[ \t\r\n]*\\([^;]*freedHandle[^;]*[;][ \t\r\n]*if[ \t\r\n]*\\([ \t\r\n]*effect[ \t\r\n]*\\)[^;]*memset[^;]*[;][ \t\r\n]*else[ \t\r\n]*invalidEffectHandle[ \t]*=[ \t]*true[ \t]*[;]"
    "garbage collection must clear only a validated freed effect record")
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "FxPool<FxElem>[ \t]*\\*[ \t]*const[ \t]+elem[ \t]*=[ \t\r\n]*FX_PoolFromHandle_Generic<FxElem,[ \t]*2048>[ \t\r\n]*\\([^;]*\\)[ \t]*[;]"
    2
    "element and spot-light free paths must use the central fatal decoder")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "FxDecodeHandle<FxPool<FxTrail>,[ \t]*MAX_TRAILS,[ \t]*FxTrail::HANDLE_SCALE>[ \t\r\n]*\\([^;]*trailHandle[^;]*\\)[ \t]*[;][ \t\r\n]*if[ \t\r\n]*\\([ \t\r\n]*!trail[ \t]*\\|\\|[ \t]*!FX_IsTrailAllocated\\(system,[ \t]*&trail->item\\)[ \t\r\n]*\\)[ \t\r\n]*return[ \t]+false[ \t]*[;]"
    "trail garbage collection must use a recoverable checked decoder during prevalidation")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "FxPool<FxTrailElem>[ \t]*\\*[ \t]*const[ \t]+trailElem[ \t]*=[ \t\r\n]*FX_PoolFromHandle_Generic<FxTrailElem,[ \t]*2048>[ \t\r\n]*\\([^;]*trailElemHandle[^;]*\\)[ \t]*[;]"
    "trail-element free must use the central fatal decoder")
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "nextTrailElemHandle[ \t]*==[ \t]*trailElemHandle"
    1
    "trail-element free must reject a self-referential successor")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "if[ \t\r\n]*\\([ \t\r\n]*trail->lastElemHandle[ \t]*==[ \t]*FX_INVALID_HANDLE[ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*return[ \t]*[;][ \t\r\n]*\\}[ \t\r\n]*FxPool<FxTrailElem>[ \t]*\\*[ \t]*const[ \t]+lastTrailElem[ \t]*="
    "trail-element free must reject a missing tail before decoding it")
require_source_ordered(
    "EffectsCore/fx_system.cpp"
    "lastTrailElem->item.nextTrailElemHandle != FX_INVALID_HANDLE"
    "if (!FX_FreePool_Generic_FxTrailElem_("
    "trail-element tail validation must complete before pool publication")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "static[ \t]+void[ \t]+FX_RemoveAllEffectElems_Internal[ \t\r\n]*\\([^)]*\\)[ \t\r\n]*\\{[ \t\r\n]*if[ \t\r\n]*\\([ \t\r\n]*!system[ \t\r\n]*\\|\\|[ \t\r\n]*!effect[ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*return[ \t]*[;][ \t\r\n]*\\}"
    "internal bulk effect cleanup must reject null state before mutation")
foreach(_fx_cleanup_visited_set
    "MAX_ELEMS;releasedElemIndices"
    "MAX_TRAIL_ELEMS;releasedTrailElemIndices"
    "MAX_TRAILS;traversedTrailIndices")
    list(GET _fx_cleanup_visited_set 0 _fx_cleanup_limit)
    list(GET _fx_cleanup_visited_set 1 _fx_cleanup_set)
    require_source_contains(
        "EffectsCore/fx_system.cpp"
        "std::array<bool, ${_fx_cleanup_limit}> ${_fx_cleanup_set}{};"
        "bulk effect cleanup must track every visited ${_fx_cleanup_limit} pool slot")
endforeach()
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "const[ \t]+std::uint16_t[ \t]+previousHandle"
    2
    "both bulk element cleanup loops must retain a no-progress snapshot")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "FX_PoolFromHandle_Generic<FxElem,[ \t]*MAX_ELEMS>[ \t\r\n]*\\([ \t\r\n]*system->elems[ \t]*,[ \t]*previousHandle[ \t\r\n]*\\)"
    "bulk element cleanup must decode each target through the central fatal boundary")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "static_cast<std::size_t>(elem - system->elems);"
    "bulk element cleanup must derive its visited index from the decoded pool slot")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "if[ \t\r\n]*\\([ \t\r\n]*releasedElemIndices\\[elemIndex\\][ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*FxRequestGarbageCollection[^}]*break[ \t]*[;][ \t\r\n]*\\}[ \t\r\n]*releasedElemIndices\\[elemIndex\\][ \t]*=[ \t]*true[ \t]*[;][ \t\r\n]*FX_FreeElem"
    "bulk element cleanup must reject duplicate/cyclic handles before freeing")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "FX_FreeElem[ \t\r\n]*\\([ \t\r\n]*system[ \t]*,[ \t]*previousHandle[ \t]*,[ \t]*effect[ \t]*,[ \t]*elemClass[ \t\r\n]*\\)[ \t]*[;][ \t\r\n]*if[ \t\r\n]*\\([ \t\r\n]*effect->firstElemHandle\\[elemClass\\][ \t]*==[ \t]*previousHandle[ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*FxRequestGarbageCollection[^}]*break[ \t]*[;][ \t\r\n]*\\}"
    "bulk effect cleanup must stop and defer collection when an element free makes no progress")
require_source_contains(
    "EffectsCore/fx_runtime.h"
    "constexpr std::size_t MAX_TRAILS = 128;"
    "bulk trail cleanup must share the runtime trail-pool capacity")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "if[ \t\r\n]*\\([ \t\r\n]*traversedTrailCount\\+\\+[ \t]*>=[ \t]*MAX_TRAILS[ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*FxRequestGarbageCollection[^}]*break[ \t]*[;][ \t\r\n]*\\}"
    "bulk trail cleanup must bound corrupt multi-record cycles and defer collection")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "if[ \t\r\n]*\\([ \t\r\n]*traversedTrailCount\\+\\+[ \t]*>=[ \t]*MAX_TRAILS[ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*FxRequestGarbageCollection[^}]*break[ \t]*[;][ \t\r\n]*\\}[ \t\r\n]*FxPool<FxTrail>[ \t]*\\*[ \t]*const[ \t]+trail[ \t]*=[ \t\r\n]*FX_PoolFromHandle_Generic<FxTrail,[ \t]*MAX_TRAILS>"
    "bulk trail cleanup must enforce its traversal bound before decoding a record")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "FX_PoolFromHandle_Generic<FxTrail,[ \t]*MAX_TRAILS>[ \t\r\n]*\\([ \t\r\n]*system->trails[ \t]*,[ \t]*trailHandle[ \t\r\n]*\\)"
    "bulk trail cleanup must decode each target through the central fatal boundary")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "static_cast<std::size_t>(trail - system->trails);"
    "bulk trail cleanup must derive its visited index from the decoded pool slot")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "if[ \t\r\n]*\\([ \t\r\n]*traversedTrailIndices\\[trailIndex\\][ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*FxRequestGarbageCollection[^}]*break[ \t]*[;][ \t\r\n]*\\}[ \t\r\n]*traversedTrailIndices\\[trailIndex\\][ \t]*=[ \t]*true[ \t]*[;][ \t\r\n]*const[ \t]+std::uint16_t[ \t]+nextTrailHandle[ \t]*=[ \t]*trail->item.nextTrailHandle[ \t]*[;]"
    "bulk trail cleanup must reject duplicate/cyclic handles before reading links")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "FX_PoolFromHandle_Generic<FxTrailElem,[ \t]*MAX_TRAIL_ELEMS>[ \t\r\n]*\\([ \t\r\n]*system->trailElems[ \t]*,[ \t]*previousHandle[ \t\r\n]*\\)"
    "bulk trail-element cleanup must decode each target through the central fatal boundary")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "static_cast<std::size_t>(trailElem - system->trailElems);"
    "bulk trail-element cleanup must derive its visited index from the decoded pool slot")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "if[ \t\r\n]*\\([ \t\r\n]*releasedTrailElemIndices\\[trailElemIndex\\][ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*FxRequestGarbageCollection[^}]*trailComplete[ \t]*=[ \t]*false[ \t]*[;][^}]*break[ \t]*[;][ \t\r\n]*\\}[ \t\r\n]*releasedTrailElemIndices\\[trailElemIndex\\][ \t]*=[ \t]*true[ \t]*[;][ \t\r\n]*FX_FreeTrailElem"
    "bulk trail-element cleanup must reject duplicate/cyclic handles before freeing")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "FX_FreeTrailElem[ \t\r\n]*\\([ \t\r\n]*system[ \t]*,[ \t\r\n]*previousHandle[ \t]*,[ \t\r\n]*effect[ \t]*,[ \t\r\n]*&trail->item[ \t]*,[ \t\r\n]*&trail->item[ \t\r\n]*\\)[ \t]*[;][ \t\r\n]*if[ \t\r\n]*\\([ \t\r\n]*trail->item.firstElemHandle[ \t]*==[ \t]*previousHandle[ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*FxRequestGarbageCollection[^}]*trailComplete[ \t]*=[ \t]*false[ \t]*[;][^}]*break[ \t]*[;][ \t\r\n]*\\}"
    "bulk trail-element cleanup must stop and defer collection when a free makes no progress")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "if[ \t\r\n]*\\([ \t\r\n]*!trailComplete[ \t\r\n]*\\|\\|[ \t\r\n]*nextTrailHandle[ \t]*==[ \t]*trailHandle[ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*FxRequestGarbageCollection[^}]*break[ \t]*[;][ \t\r\n]*\\}[ \t\r\n]*trailHandle[ \t]*=[ \t]*nextTrailHandle[ \t]*[;]"
    "bulk trail cleanup must reject incomplete/self-linked records before advancing")
require_source_not_matches(
    "EffectsCore/fx_system.cpp"
    "for[ \t\r\n]*\\([^;]*trailHandle[^;]*[;][^;]*trailHandle[^;]*[;][^)]*trail->item.nextTrailHandle[^)]*\\)[ \t\r\n]*\\{[^}]*FX_FreeTrailElem"
    "bulk trail cleanup must not dereference an unchecked decoder in a for-loop increment")

# Partial updates intentionally stage each trail record on the stack. Element
# retirement validates the real pool owner and updates its endpoints inside the
# same pool transaction, so the staged copy cannot bypass ownership checks or
# leave the live trail pointing at a newly freed element.
require_source_contains(
    "EffectsCore/fx_update.cpp"
    "FX_UpdateEffectPartialTrail(
            system,
            effect,
            &trail,
            remoteTrail,"
    "partial trail updates must retain their allocated pool owner")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "if (!FX_IsTrailAllocated(system, trailOwner))"
    "trail-element free must validate the real allocated trail owner")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "trail->firstElemHandle != trailOwner->firstElemHandle
            || trail->lastElemHandle != trailOwner->lastElemHandle"
    "staged trail mutation must begin from the live owner's endpoints")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "if (trailOwner != trail)
                {
                    if (releasesTail)
                        trailOwner->lastElemHandle = FX_INVALID_HANDLE;
                    trailOwner->firstElemHandle = nextTrailElemHandle;"
    "trail-element free must publish staged endpoint mutation to the live owner")

# Garbage collection validates the complete trail graph without mutation,
# then frees exactly that snapshot. Fatal reporting happens only after the
# exclusive iterator state has been released.
require_source_contains(
    "EffectsCore/fx_system.h"
    "bool __cdecl FX_RunGarbageCollection_FreeTrails(FxSystem *system, FxEffect *effect);"
    "trail garbage collection must expose validation failure to its owner")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "std::array<FxPool<FxTrail> *, MAX_TRAILS> trailsToFree{};"
    "trail garbage collection must snapshot validated records before mutation")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "std::array<bool, MAX_TRAILS> visitedTrailIndices{};"
    "trail garbage collection must reject cyclic or aliased pool slots")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "if (trailCount == MAX_TRAILS)"
    "trail garbage collection must enforce the exact pool capacity")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "FxPool<FxTrail> *const trail =
            FxDecodeHandle<FxPool<FxTrail>, MAX_TRAILS, FxTrail::HANDLE_SCALE>(
                system->trails, trailHandle);
        if (!trail || !FX_IsTrailAllocated(system, &trail->item))
            return false;"
    "trail garbage-collection prevalidation must reject invalid or free handles before indexing")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "if[ \t\r\n]*\\([ \t\r\n]*visitedTrailIndices\\[trailIndex\\][ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*return[ \t]+false[ \t]*[;][ \t\r\n]*\\}[ \t\r\n]*visitedTrailIndices\\[trailIndex\\][ \t]*=[ \t]*true[ \t]*[;][ \t\r\n]*trailsToFree\\[trailCount\\+\\+\\][ \t]*=[ \t]*trail[ \t]*[;]"
    "trail garbage-collection prevalidation must reject duplicates before snapshotting")
require_source_ordered(
    "EffectsCore/fx_system.cpp"
    "trailsToFree[trailCount++] = trail;"
    "for (std::size_t index = 0; index < trailCount; ++index)"
    "trail garbage collection must finish its validation pass before freeing")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "const[ \t]+std::uint16_t[ \t]+expectedTrailHandle[ \t]*=[ \t\r\n]*FX_PoolToHandle_Generic<FxTrail,[ \t]*MAX_TRAILS>[^;]*[;][ \t\r\n]*if[ \t\r\n]*\\([ \t\r\n]*firstTrailHandle[ \t]*!=[ \t]*expectedTrailHandle[ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*return[ \t]+false[ \t]*[;][ \t\r\n]*\\}[^}]*if[ \t\r\n]*\\([ \t\r\n]*!FX_FreePool_Generic_FxTrail_"
    "trail garbage collection must verify snapshot order before each free publication")
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "FxDecodeHandle<FxPool<FxTrail>,[ \t]*MAX_TRAILS,[ \t]*FxTrail::HANDLE_SCALE>"
    2
    "only cycle-recoverable trail snapshots must use the nullable low-level decoder")

# Retrigger snapshots are capacity-aware and cycle-safe, and the caller drops
# its temporary ownership before escalating a malformed trail chain.
require_source_contains(
    "EffectsCore/fx_system.h"
    "bool __cdecl FX_GetTrailHandleList_Last("
    "trail snapshot construction must return validation status")
require_source_contains(
    "EffectsCore/fx_system.h"
    "std::size_t outHandleCapacity);"
    "trail snapshot construction must receive an explicit native-width capacity")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "uint16_t lastOldTrailElemHandle[MAX_TRAILS];"
    "retrigger snapshots must use the shared trail-pool capacity")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "outHandleCapacity == 0 || outHandleCapacity > MAX_TRAILS"
    "trail snapshot construction must reject invalid caller capacities")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "FxDecodeHandle<FxPool<FxTrail>,[ \t]*MAX_TRAILS,[ \t]*FxTrail::HANDLE_SCALE>[ \t\r\n]*\\([^;]*\\)[ \t]*[;][ \t\r\n]*if[ \t\r\n]*\\([ \t\r\n]*!trail[ \t]*\\|\\|[ \t]*!FX_IsTrailAllocated\\(system,[ \t]*&trail->item\\)[ \t\r\n]*\\)[ \t\r\n]*return[ \t]+false[ \t]*[;][ \t\r\n]*const[ \t]+std::size_t[ \t]+poolIndex"
    "retrigger trail snapshots must reject invalid handles before indexing")
require_source_ordered(
    "EffectsCore/fx_system.cpp"
    "if (visitedTrailIndices[poolIndex] || trailIndex == outHandleCapacity)"
    "outHandleList[trailIndex++] = terminalTrailElemHandle;"
    "retrigger trail snapshots must reject duplicates/capacity exhaustion before writing")
require_source_ordered(
    "EffectsCore/fx_system.cpp"
    "if (visitedTrailElemIndices[trailElemIndex])"
    "if (terminalTrailElemHandle != trail->item.lastElemHandle)"
    "retrigger trail snapshots must reject cyclic trail elements before accepting the stored tail")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "if[ \t\r\n]*\\([ \t\r\n]*!FX_GetTrailHandleList_Last[^}]*\\{[ \t\r\n]*FX_DelRefToEffect[ \t\r\n]*\\([ \t\r\n]*system[ \t]*,[ \t]*effect[ \t\r\n]*\\)[ \t]*[;][ \t\r\n]*FX_UnlockEffect[ \t\r\n]*\\([ \t\r\n]*system[ \t]*,[ \t]*effect[ \t\r\n]*\\)[ \t]*[;][ \t\r\n]*Com_Error[ \t\r\n]*\\([ \t\r\n]*ERR_DROP"
    "retrigger must release its reference and status lock before reporting an invalid snapshot")

# Spawn paths validate the existing tail/head before allocating a new record,
# fully initialize the new payload, then publish backlinks and owning heads.
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "void[ \t]+__cdecl[ \t]+FX_SpawnEffect_AllocTrails[^}]*if[ \t\r\n]*\\([ \t\r\n]*!system[ \t]*\\|\\|[ \t]*!effect[ \t]*\\|\\|[ \t]*!effect->def[ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*return[ \t]*[;][ \t\r\n]*\\}[ \t\r\n]*def[ \t]*=[ \t]*effect->def[ \t]*[;]"
    "effect trail allocation must validate its owners before reading the definition")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "const std::int64_t validatedElemDefCount ="
    "effect trail allocation must sum definition counts without signed overflow")
foreach(_fx_trail_elem_count
    elemDefCountOneShot
    elemDefCountLooping
    elemDefCountEmission)
    require_source_contains(
        "EffectsCore/fx_system.cpp"
        "def->${_fx_trail_elem_count} < 0"
        "effect trail allocation must reject a negative ${_fx_trail_elem_count}")
endforeach()
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "validatedElemDefCount[ \t]*>[ \t]*256[^}]*validatedElemDefCount[ \t]*&&[ \t]*!def->elemDefs[^}]*Com_Error[ \t\r\n]*\\([ \t\r\n]*ERR_DROP[^}]*return[ \t]*[;]"
    "effect trail allocation must enforce uint8 index capacity and definition storage")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "localTrail.defIndex = static_cast<std::uint8_t>(elemDefIter);"
    "validated trail definition indices must narrow explicitly")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "FX_PoolToHandle_Generic<FxTrail, MAX_TRAILS>("
    "new trail handles must use the shared pool capacity")
require_source_ordered(
    "EffectsCore/fx_system.cpp"
    "remoteTrail->item = localTrail;"
    "effect->firstTrailHandle = trailHandle;"
    "effect trail allocation must initialize the payload before publishing its handle")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "(trail->firstElemHandle == FX_INVALID_HANDLE)\n            != (trail->lastElemHandle == FX_INVALID_HANDLE)"
    "trail spawning must reject inconsistent endpoint sentinels")
require_source_ordered(
    "EffectsCore/fx_system.cpp"
    "lastTrailElemInEffect = &lastTrailElem->item;"
    "remoteTrailElem = FX_AllocTrailElem(system);"
    "trail spawning must validate the existing tail before allocating")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "if[ \t\r\n]*\\([ \t\r\n]*!FX_IsTrailElemAllocated\\(system,[ \t]*&lastTrailElem->item\\)[ \t\r\n]*\\|\\|[ \t\r\n]*lastTrailElem->item.nextTrailElemHandle[ \t]*!=[ \t]*FX_INVALID_HANDLE[ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*Com_Error[ \t\r\n]*\\([ \t\r\n]*ERR_DROP[^}]*return[ \t]*[;][ \t\r\n]*\\}[ \t\r\n]*lastTrailElemInEffect[ \t]*=[ \t]*&lastTrailElem->item[ \t]*[;]"
    "trail spawning must reject a decoded tail that is free or nonterminal")
require_source_ordered(
    "EffectsCore/fx_system.cpp"
    "FX_TrailElem_CompressBasis(basis, remoteTrailElem->item.basis);"
    "if (lastTrailElemInEffect)"
    "trail spawning must initialize the complete payload before linking it")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "if[ \t\r\n]*\\([ \t\r\n]*lastTrailElemInEffect[ \t\r\n]*\\)[ \t\r\n]*lastTrailElemInEffect->nextTrailElemHandle[ \t]*=[ \t]*trailElemHandle[ \t]*[;][ \t\r\n]*else[ \t\r\n]*trail->firstElemHandle[ \t]*=[ \t]*trailElemHandle[ \t]*[;][ \t\r\n]*trail->lastElemHandle[ \t]*=[ \t]*trailElemHandle[ \t]*[;]"
    "trail spawning must publish its tail/head links before the final endpoint")
require_source_ordered(
    "EffectsCore/fx_system.cpp"
    "nextElemInEffect =\n                            FX_PoolFromHandle_Generic<FxElem, MAX_ELEMS>("
    "elem = FX_AllocElem(system);"
    "element spawning must validate the current head before allocating")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "if[ \t\r\n]*\\([ \t\r\n]*!FX_IsElemAllocated\\(system,[ \t]*&nextElemInEffect->item\\)[ \t\r\n]*\\|\\|[ \t\r\n]*nextElemInEffect->item.prevElemHandleInEffect[ \t\r\n]*!=[ \t\r\n]*FX_INVALID_HANDLE[ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*Com_Error[ \t\r\n]*\\([ \t\r\n]*ERR_DROP[^}]*return[ \t]*[;][ \t\r\n]*\\}"
    "element spawning must reject a free head or one that already has a predecessor")
require_source_ordered(
    "EffectsCore/fx_system.cpp"
    "elem->item.nextElemHandleInEffect =\n                            nextElemHandleInEffect;"
    "if (nextElemInEffect)"
    "element spawning must initialize its forward link before publishing backlinks")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "nextElemInEffect->item.prevElemHandleInEffect[ \t]*=[ \t\r\n]*elemHandle[ \t]*;[ \t\r\n]*\\}[ \t\r\n]*effect->firstElemHandle\\[elemClass\\][ \t]*=[ \t]*elemHandle[ \t]*;"
    "element spawning must publish the validated backlink before the owning head")

# Every runtime element-definition lookup validates the owning pointers,
# signed component counts, byte-sized persisted index capacity, and requested
# index before returning an array element. All callers stop on rejection.
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "const FxElemDef *FX_GetValidatedRuntimeElemDef("
    "runtime element-definition access must use one checked boundary")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "!effect || !effect->def || !effect->def->elemDefs"
    "runtime element-definition lookup must validate every owning pointer")
foreach(_fx_runtime_elem_count
    elemDefCountLooping
    elemDefCountOneShot
    elemDefCountEmission)
    require_source_contains(
        "EffectsCore/fx_system.cpp"
        "effect->def->${_fx_runtime_elem_count} < 0"
        "runtime element-definition lookup must reject a negative ${_fx_runtime_elem_count}")
endforeach()
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "const std::int64_t elemDefCount ="
    "runtime element-definition lookup must sum counts without signed overflow")
require_source_matches(
    "EffectsCore/fx_system.cpp"
    "if[ \t\r\n]*\\([ \t\r\n]*elemDefCount[ \t]*>[ \t]*256[ \t]*\\|\\|[ \t]*elemDefIndex[ \t]*>=[ \t]*elemDefCount[ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*Com_Error[ \t\r\n]*\\([ \t\r\n]*ERR_DROP[^}]*return[ \t]+nullptr[ \t]*[;][ \t\r\n]*\\}[ \t\r\n]*return[ \t]+&effect->def->elemDefs\\[elemDefIndex\\][ \t]*[;]"
    "runtime element-definition lookup must validate byte capacity and index before dereference")
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "FX_GetValidatedRuntimeElemDef[ \t\r\n]*\\("
    6
    "the shared helper plus all five runtime element-definition consumers must remain accounted for")
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "elemDef[ \t]*=[ \t]*FX_GetValidatedRuntimeElemDef[^;]*[;][ \t\r\n]*if[ \t\r\n]*\\([ \t\r\n]*!elemDef[ \t\r\n]*\\)[ \t\r\n]*return[ \t]*[;]"
    3
    "every runtime element-definition consumer must stop before dereferencing a rejected definition")
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "if[ \t\r\n]*\\([ \t\r\n]*!system[ \t]*\\|\\|[ \t]*!effect[ \t]*\\|\\|[ \t]*!effect->def[ \t]*\\|\\|[ \t]*!trail[ \t\r\n]*\\|\\|[ \t]*!effectFrameWhenPlayed[ \t\r\n]*\\)"
    2
    "both trail-spawn paths must validate the trail before reading its definition index")
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "if[ \t\r\n]*\\([ \t\r\n]*elemDef->elemType[ \t]*!=[ \t]*3[ \t\r\n]*\\)[ \t\r\n]*\\{[^}]*MyAssertHandler[^}]*return[ \t]*[;][ \t\r\n]*\\}"
    2
    "both trail-spawn paths must stop after rejecting a non-trail definition")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "|| elemDefIndex < 0)"
    "element spawning must reject a negative definition index before unsigned conversion")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "elem->item.defIndex = static_cast<std::uint8_t>(elemDefIndex);"
    "element spawning must narrow its validated definition index explicitly")
require_source_contains(
    "EffectsCore/fx_system.cpp"
    "elem->item.sequence = static_cast<std::uint8_t>(sequence);"
    "element spawning must preserve explicit byte-width sequence semantics")

# Sort paths validate both the old/new chains before any list-head mutation.
# Every fallback traversal remains bounded even if a future edit bypasses a
# prevalidation call.
require_source_contains(
    "EffectsCore/fx_sort.cpp"
    "[[noreturn]] void FX_DropCorruptSortList(const char *const reason)"
    "sort corruption must use a central nonreturning drop hook")
require_source_ordered(
    "EffectsCore/fx_sort.cpp"
    "Corrupt FX element sort list: %s"
    "std::abort();"
    "sort corruption must retain an abort fallback after ERR_DROP")
require_source_match_count(
    "EffectsCore/fx_sort.cpp"
    "std::array<bool,[ \t]*MAX_ELEMS>[ \t]+visited\\{\\}[ \t]*[;]"
    2
    "both sort prevalidation passes must track visited element slots")
require_source_contains(
    "EffectsCore/fx_sort.cpp"
    "if (handle == insertedElemHandle)"
    "insertion prevalidation must reject an already-linked new element")
require_source_match_count(
    "EffectsCore/fx_sort.cpp"
    "if[ \t\r\n]*\\([ \t\r\n]*visited\\[elemIndex\\][ \t\r\n]*\\)"
    2
    "both sort prevalidation passes must reject cycles and duplicates")
require_source_match_count(
    "EffectsCore/fx_sort.cpp"
    ">=[ \t]*MAX_ELEMS"
    5
    "all sort validation and mutation traversals must remain pool-bounded")
require_source_ordered(
    "EffectsCore/fx_sort.cpp"
    "FX_ValidateSortChains(
        system, effect, elemHandle, stopElemHandle);"
    "effect->firstElemHandle[0] = stopElemHandle;"
    "sort-chain prevalidation must complete before list-head mutation")
require_source_ordered(
    "EffectsCore/fx_sort.cpp"
    "FX_ValidateInsertionList(
        system, effect, effect->firstElemHandle[0], elemHandle);"
    "elem->nextElemHandleInEffect = *prevNextElemHandle;"
    "insertion-chain prevalidation must complete before link mutation")
require_source_match_count(
    "EffectsCore/fx_sort.cpp"
    "FX_PoolFromHandle_Generic<FxElem,[ \t]*MAX_ELEMS>"
    6
    "all sort-chain decodes must use the shared element capacity and fatal boundary")

# Runtime byte fields must not depend on implementation-defined plain-char
# signedness on ARM targets.
require_source_contains(
    "EffectsCore/fx_runtime.h"
    "std::uint8_t defIndex;"
    "trail definition indices must remain explicitly unsigned bytes")
require_source_match_count(
    "EffectsCore/fx_runtime.h"
    "std::uint8_t[ \t]+sequence[ \t]*[;]"
    3
    "element, trail, and trail-element sequences must remain explicitly unsigned bytes")
require_source_contains(
    "EffectsCore/fx_runtime.h"
    "std::int8_t basis[2][3];"
    "compressed trail bases must retain signed-byte semantics")
require_source_match_count(
    "EffectsCore/fx_system.h"
    "std::int8_t[ \t]*\\([ \t]*\\*outBasis\\)[ \t]*\\[3\\]"
    2
    "both public compressed-basis declarations must use signed bytes")
require_source_contains(
    "EffectsCore/fx_system.h"
    "const std::int8_t (*inBasis)[3]"
    "public trail-basis decompression must consume signed bytes")
require_source_contains(
    "EffectsCore/fx_update.cpp"
    "std::int8_t (*outBasis)[3]"
    "trail-basis compression must write signed bytes")
require_source_contains(
    "EffectsCore/fx_update.cpp"
    "outBasis[basisVecIter][dimIter] = static_cast<std::int8_t>(
                static_cast<std::int32_t>(clamped));"
    "trail-basis compression must narrow its clamped value explicitly")
require_source_contains(
    "EffectsCore/fx_draw.cpp"
    "const std::int8_t (*inBasis)[3]"
    "trail-basis decompression must read signed bytes")

# Static-XModel and BModel draw IDs must be resolved against the published
# surface arena before any record or embedded pointer is dereferenced.
require_source_contains(
    "gfx_d3d/r_model_surface_stream.h"
    "TryResolveWordOffsetRange"
    "model-surface IDs must have one bounded word-offset resolver")
require_source_contains(
    "gfx_d3d/r_scene.cpp"
    "StaticXModelSurfaceCursor"
    "static-XModel traversal must use its checked heterogeneous stream cursor")
require_source_not_contains(
    "gfx_d3d/r_scene.cpp"
    "modelSurf = (GfxModelRigidSurface *)((char *)frontEndDataOut + 4 * surfId);"
    "static-XModel scene submission must not decode surfId with a raw cast")
require_source_match_count(
    "gfx_d3d/r_draw_xmodel.cpp"
    "R_ResolveModelSurface<"
    5
    "all XModel draw-list consumers must use the bounded typed resolver")
require_source_match_count(
    "gfx_d3d/rb_tess.cpp"
    "RB_ResolveModelSurface<"
    7
    "all scoped XModel tessellation consumers must use the bounded typed resolver")
require_source_match_count(
    "gfx_d3d/r_draw_xmodel.cpp"
    "gfxEntIndex >= ARRAY_COUNT\\(data->gfxEnts\\)"
    2
    "XModel draw consumers must bound every changed gfxEnt index")
require_source_match_count(
    "gfx_d3d/rb_tess.cpp"
    "gfxEntIndex[a-z]* >= ARRAY_COUNT\\(data->gfxEnts\\)"
    5
    "XModel tessellation consumers must bound every changed gfxEnt index")
require_source_contains(
    "gfx_d3d/r_bmodel_surface_stream.h"
    "TryResolveSequence"
    "frontend BModel traversal must validate the complete published sequence")
require_source_contains(
    "gfx_d3d/r_bmodel_surface_stream.h"
    "InvalidRecordProgress"
    "invalid backend BModel records must still advance the draw loop")
require_source_contains(
    "gfx_d3d/rb_tess.cpp"
    "RB_ResolveBModelSurface(data, drawSurf)"
    "backend BModel records must use the bounded tagged resolver")
require_source_contains(
    "gfx_d3d/r_scene.cpp"
    "R_TryResolveBModelSurfaceSequence("
    "frontend BModel records must use the bounded sequence resolver")
foreach(_bmodel_consumer
    "gfx_d3d/rb_tess.cpp"
    "gfx_d3d/r_scene.cpp")
    require_source_not_matches(
        "${_bmodel_consumer}"
        "\\(char \\*\\)(data|frontEndDataOut) \\+ 4 \\* (drawSurf.fields.objectId|surfId)"
        "BModel object IDs must not be decoded with raw base-plus-offset casts")
endforeach()

# The renderer no longer depends on the Win32 Interlocked namespace. Every
# executable reservation must use the fixed-width bounded Sys_Atomic boundary.
file(GLOB_RECURSE _renderer_atomic_sources
    "${SOURCE_ROOT}/src/gfx_d3d/*.cpp"
    "${SOURCE_ROOT}/src/gfx_d3d/*.h")
foreach(_renderer_atomic_source IN LISTS _renderer_atomic_sources)
    file(READ "${_renderer_atomic_source}" _renderer_atomic_text)
    string(REGEX MATCH
        "(^|[^A-Za-z0-9_])_?Interlocked[A-Za-z0-9_]*[ \t\r\n]*\\("
        _renderer_interlocked_call
        "${_renderer_atomic_text}")
    if (NOT "${_renderer_interlocked_call}" STREQUAL "")
        message(FATAL_ERROR
            "Renderer retains a direct Interlocked call in ${_renderer_atomic_source}")
    endif()
endforeach()
require_source_not_contains(
    "universal/sys_atomic.h"
    "#define Interlocked"
    "the portable atomic boundary must not mask incomplete migrations with Win32 aliases")
foreach(_backend_counter
    "modelLightingPatchCount"
    "gfxEntCount"
    "cloudCount")
    require_source_contains(
        "gfx_d3d/r_rendercmds.h"
        "volatile uint32_t ${_backend_counter};"
        "renderer backend counters must remain exact-width atomic storage")
endforeach()
require_source_matches(
    "gfx_d3d/r_scene.cpp"
    "gfx::reservation_atomic::TryReserveIndex[ \t\r\n]*\\([ \t\r\n]*&frontEndDataOut->gfxEntCount[ \t\r\n]*,"
    "scene entities must reserve their exact backing array through the bounded CAS helper")
require_source_matches(
    "gfx_d3d/r_dpvs.cpp"
    "gfx::reservation_atomic::TryReserveIndex[ \t\r\n]*\\([ \t\r\n]*&frontEndDataOut->gfxEntCount[ \t\r\n]*,"
    "DPVS entities must reserve their exact backing array through the bounded CAS helper")
require_source_matches(
    "gfx_d3d/r_scene.cpp"
    "gfx::reservation_atomic::TryReserveIndex[ \t\r\n]*\\([ \t\r\n]*&frontEndDataOut->cloudCount[ \t\r\n]*,"
    "particle clouds must reserve their exact backing array through the bounded CAS helper")
require_source_matches(
    "gfx_d3d/rb_light.cpp"
    "gfx::reservation_atomic::TryReserveIndex[ \t\r\n]*\\([ \t\r\n]*&frontEndDataOut->modelLightingPatchCount[ \t\r\n]*,"
    "model-lighting patches must reserve their exact backing array through the bounded CAS helper")
foreach(_backend_counter
    "modelLightingPatchCount"
    "gfxEntCount"
    "cloudCount")
    require_source_matches(
        "gfx_d3d/r_rendercmds.cpp"
        "Sys_AtomicStore[ \t\r\n]*\\([ \t\r\n]*&frontEndDataOut->${_backend_counter}[ \t\r\n]*,"
        "renderer backend ${_backend_counter} must reset through its atomic boundary")
endforeach()
foreach(_cloud_count_consumer
    "gfx_d3d/r_drawsurf.cpp;frontEndDataOut"
    "gfx_d3d/rb_tess.cpp;data"
    "gfx_d3d/r_rendercmds.cpp;frontEndDataOut")
    list(GET _cloud_count_consumer 0 _cloud_count_source)
    list(GET _cloud_count_consumer 1 _cloud_count_owner)
    require_source_matches(
        "${_cloud_count_source}"
        "Sys_AtomicLoad[ \t\r\n]*\\([ \t\r\n]*&${_cloud_count_owner}->cloudCount[ \t\r\n]*\\)"
        "renderer cloud extents must be acquired before their backing array is consumed")
endforeach()
require_source_matches(
    "gfx_d3d/rb_backend.cpp"
    "Sys_AtomicLoad[ \t\r\n]*\\([ \t\r\n]*&backEndData->modelLightingPatchCount[ \t\r\n]*\\)"
    "renderer model-lighting extents must be acquired before their backing array is consumed")
foreach(_renderer_member_census
    "gfx_d3d/r_scene.cpp;frontEndDataOut;gfxEntCount;1"
    "gfx_d3d/r_scene.cpp;frontEndDataOut;cloudCount;1"
    "gfx_d3d/r_dpvs.cpp;frontEndDataOut;gfxEntCount;1"
    "gfx_d3d/rb_light.cpp;frontEndDataOut;modelLightingPatchCount;1"
    "gfx_d3d/r_rendercmds.cpp;frontEndDataOut;modelLightingPatchCount;1"
    "gfx_d3d/r_rendercmds.cpp;frontEndDataOut;gfxEntCount;1"
    "gfx_d3d/r_rendercmds.cpp;frontEndDataOut;cloudCount;3"
    "gfx_d3d/r_drawsurf.cpp;frontEndDataOut;cloudCount;2"
    "gfx_d3d/rb_tess.cpp;data;cloudCount;2"
    "gfx_d3d/rb_backend.cpp;backEndData;modelLightingPatchCount;1")
    list(GET _renderer_member_census 0 _renderer_member_source)
    list(GET _renderer_member_census 1 _renderer_member_owner)
    list(GET _renderer_member_census 2 _renderer_member_name)
    list(GET _renderer_member_census 3 _renderer_member_count)
    require_source_match_count(
        "${_renderer_member_source}"
        "${_renderer_member_owner}->${_renderer_member_name}"
        "${_renderer_member_count}"
        "all ${_renderer_member_owner}->${_renderer_member_name} accesses must remain accounted for")
endforeach()
require_source_matches(
    "gfx_d3d/r_add_cmdbuf.cpp"
    "R_EndCmdBuf[ \t\r\n]*\\([ \t\r\n]*delayedCmdBuf[ \t\r\n]*\\)[ \t\r\n]*;[ \t\r\n]*delayedCmdBuf->primDrawSurfSize[ \t\r\n]*=[ \t\r\n]*0[ \t\r\n]*;"
    "oversized primitive commands must close active delayed-buffer state before rejection")
require_source_contains(
    "gfx_d3d/r_dpvs.cpp"
    "gfxEntIndex = 0u;"
    "default renderer entities must initialize the complete native index word")
require_source_contains(
    "gfx_d3d/rb_light.cpp"
    "ARRAY_COUNT(frontEndDataOut->modelLightingPatchList)"
    "model-lighting reservations must derive their exact backing capacity")

# Virtual-memory consumers use one native-page, size_t service boundary and no
# longer import the Win32 allocator directly.
foreach(_portable_memory_consumer
    "universal/com_memory.cpp"
    "universal/physicalmemory.cpp")
    require_source_contains(
        "${_portable_memory_consumer}"
        "#include <qcommon/sys_memory.h>"
        "runtime memory consumers must use the portable virtual-memory service")
    require_source_not_contains(
        "${_portable_memory_consumer}"
        "#include <win32/win_local.h>"
        "runtime memory consumers must not import Win32 platform state")
    require_source_not_matches(
        "${_portable_memory_consumer}"
        "(^|[^A-Za-z0-9_])Virtual(Alloc|Free)[ \\t\r\n]*\\("
        "runtime memory consumers must not call Win32 virtual memory directly")
endforeach()
require_source_contains(
    "qcommon/sys_memory.h"
    "std::size_t KISAK_CDECL Sys_VirtualMemoryPageSize();"
    "virtual-memory page size must use native size width")
require_source_contains(
    "qcommon/sys_memory.h"
    "void *KISAK_CDECL Sys_VirtualMemoryReserve(std::size_t size);"
    "virtual-memory reservation sizes must use native size width")
require_source_contains(
    "_platform/posix/sys_memory.cpp"
    "MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED"
    "POSIX decommit must replace pages with a guaranteed zero-filled reservation")
require_source_contains(
    "_platform/win32/sys_memory.cpp"
    "MEM_RESERVE"
    "Win32 virtual memory must preserve reserve/commit separation")
require_source_ordered(
    "universal/com_memory.cpp"
    "uint8_t *const commitBegin = PageFloor(ptr);"
    "uint8_t *const commitEnd = PageCeil("
    "zone commits must cover an unaligned caller's first and last native pages")
require_source_ordered(
    "universal/com_memory.cpp"
    "uint8_t *const commitEnd = PageCeil("
    "Sys_VirtualMemoryCommit(\n        commitBegin,"
    "zone commits must pass the aligned covering range to the strict platform API")

# Common path queries and one-directory creation use UTF-8 platform services.
# Recursive deletion remains deliberately outside this boundary until it can
# receive handle-relative enumeration and deletion on every target.
require_source_contains(
    "universal/win_common.cpp"
    "#include <qcommon/sys_filesystem.h>"
    "shared filesystem wrappers must consume the portable path service")
require_source_contains(
    "universal/win_common.cpp"
    "Sys_FileSystemCreateDirectory(path)"
    "Sys_Mkdir must delegate to the portable directory service")
require_source_contains(
    "universal/win_common.cpp"
    "thread_local std::vector<char> cwd"
    "current-directory results must not retain a fixed 256-byte global buffer")
require_source_contains(
    "universal/win_common.cpp"
    "ReadDynamicPath(Sys_FileSystemGetExecutablePath, &path)"
    "executable-path lookup must retry without silent truncation")
require_source_contains(
    "universal/win_common.cpp"
    "Sys_FileSystemParentPathLength(fullPath.c_str())"
    "install-path extraction must preserve POSIX, drive, and UNC roots")
require_source_not_contains(
    "universal/win_common.cpp"
    "_getcwd("
    "shared current-directory lookup must not call the Win32 CRT directly")
require_source_not_contains(
    "universal/win_common.cpp"
    "GetModuleFileNameA("
    "shared executable-path lookup must not use a truncating ANSI API")
require_source_contains(
    "universal/win_common.cpp"
    "if (!path || !path[0])"
    "legacy recursive deletion must reject null and empty roots before length arithmetic")
require_source_not_contains(
    "qcommon/sys_filesystem.h"
    "RemoveDirectoryTree"
    "recursive deletion must not enter the portable contract without race-safe backends")
require_source_contains(
    "_platform/posix/sys_filesystem.cpp"
    "O_NOFOLLOW"
    "POSIX directory creation must not follow ancestor symbolic links")
require_source_contains(
    "_platform/posix/sys_filesystem.cpp"
    "mkdirat(parentFd, leaf, 0777)"
    "POSIX directory creation must remain relative to a held parent descriptor")
require_source_contains(
    "_platform/posix/sys_filesystem.cpp"
    "fstatat(parentFd, name, &status, AT_SYMLINK_NOFOLLOW)"
    "POSIX existing-leaf classification must not follow a symbolic link")
require_source_contains(
    "_platform/win32/sys_filesystem.cpp"
    "FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT"
    "Win32 ancestor handles must classify reparse points themselves")
require_source_not_contains(
    "_platform/win32/sys_filesystem.cpp"
    "FILE_SHARE_DELETE"
    "held Win32 ancestor directories must not be replaceable during validation")
require_source_contains(
    "_platform/win32/sys_filesystem.cpp"
    "reservedName"
    "extended Win32 paths must still reject DOS device names")
require_source_contains(
    "_platform/win32/sys_filesystem.cpp"
    "CreateDirectoryW(extendedPath.c_str(), nullptr)"
    "Win32 directory creation must use the Unicode long-path API")

# Upstream SP restorations must retain fixed-width save records and release-safe
# bounds checks. These guards cover defects found during PR review that portable
# utility targets cannot compile directly.
require_source_contains(
    "game/g_scr_vehicle.cpp"
    "SaveMemory_SaveWrite(&sndIndices[i], static_cast<int>(sizeof(sndIndices[i])), save);"
    "vehicle sound-index saves must always write one fixed-width element")
require_source_contains(
    "game/g_scr_vehicle.cpp"
    "SaveMemory_LoadRead(&sndIndices[i], static_cast<int>(sizeof(sndIndices[i])), save);"
    "vehicle sound-index loads must always read one fixed-width element")
require_source_not_matches(
    "game/g_scr_vehicle.cpp"
    "if[ \t]*\\([ \t]*sndIndices\\[i\\][ \t]*\\)[\r\n \t]*SaveMemory_(SaveWrite|LoadRead)"
    "vehicle save records must not depend on runtime sound-index occupancy")
require_source_match_count(
    "game/g_scr_vehicle.cpp"
    "obj[ \t]*=[ \t]*Com_GetServerDObj\\(target->s.number\\);[\r\n \t]*if[ \t]*\\(!obj\\)[\r\n \t]*continue;[\r\n \t]*DObjPhysicsGetBounds\\(obj,"
    2
    "both vehicle-touch paths must null-check the server DObj before dereference")
require_source_ordered(
    "cgame/cg_weapons.cpp"
    "weapDef = BG_GetWeaponDef(BG_GetViewmodelWeaponIndex(ps));"
    "if (!weapDef)\n        return false;"
    "night-vision attachment logic must reject a missing weapon definition")
require_source_contains(
    "game/g_scr_main.cpp"
    "!level.openScriptIOFileHandles[v2] && !level.openScriptIOFileBuffers[v2]"
    "single-player script-file slots must remain occupied by either storage kind")
require_source_match_count(
    "game/g_scr_main.cpp"
    "ARRAY_COUNT\\(level.openScriptIOFile(Handles|Buffers)\\)"
    6
    "all single-player script-file slot loops and lookups must use their real bounds")
require_source_not_matches(
    "game/g_scr_main.cpp"
    "void[ \\t]*\\*[ \\t]*v23"
    "filesystem handles must not be stored in pointer-width scratch slots")
require_source_ordered(
    "game/g_scr_main.cpp"
    "int fileHandle;"
    "FS_FOpenFileByMode((char *)v9, &fileHandle, FS_READ);"
    "single-player script reads must use a typed filesystem handle")
require_source_ordered(
    "game/g_scr_main.cpp"
    "FS_FOpenFileByMode((char *)v9, &fileHandle, FS_READ);"
    "Remote <= 0x7FFFFFFEu && fileHandle"
    "single-player script reads must reject failed and oversized files")
require_source_contains(
    "game/g_scr_main.cpp"
    "(Remote & 0x80000000u) == 0 && *v5"
    "append success must test the unsigned filesystem status explicitly")
require_source_contains(
    "game/g_scr_main.cpp"
    "Com_Printf(23, \"FPrintln failed, invalid file number %i\\n\", Int);"
    "single-player invalid write-slot diagnostics must supply their format argument")
require_source_contains(
    "game/g_scr_main.cpp"
    "Com_Printf(23, \"FPrintln failed, file number %i was not open for writing\\n\", Int);"
    "single-player unopened write-slot diagnostics must supply their format argument")
require_source_contains(
    "game/g_scr_main.cpp"
    "Com_Printf(23, \"freadln failed, invalid file number %i\\n\", Int);"
    "single-player invalid read-slot diagnostics must supply their format argument")
require_source_contains(
    "game/g_scr_main.cpp"
    "Com_Printf(23, \"freadln failed, file number %i was not open for reading\\n\", Int);"
    "single-player unopened read-slot diagnostics must supply their format argument")
require_source_contains(
    "game_mp/g_scr_main_mp.cpp"
    "!level.openScriptIOFileHandles[filenum] && !level.openScriptIOFileBuffers[filenum]"
    "script-file slots must remain occupied by either a write handle or a read buffer")
require_source_match_count(
    "game_mp/g_scr_main_mp.cpp"
    "ARRAY_COUNT\\(level.openScriptIOFile(Handles|Buffers)\\)"
    5
    "all multiplayer script-file slot loops and lookups must use their real one-slot bounds")
require_source_not_matches(
    "game_mp/g_scr_main_mp.cpp"
    "filenum[ \\t]*(<|>=)[ \\t]*2"
    "multiplayer script-file APIs must not accept slot one for one-element arrays")
require_source_ordered(
    "game_mp/g_scr_main_mp.cpp"
    "filesize = FS_FOpenFileByMode(fullpathname, &tempFile, FS_READ);"
    "filesize <= 0x7FFFFFFEu && tempFile"
    "multiplayer script reads must reject failed and oversized files")
require_source_ordered(
    "game_mp/g_scr_main_mp.cpp"
    "filesize <= 0x7FFFFFFEu && tempFile"
    "const int32_t fileLength = static_cast<int32_t>(filesize);"
    "multiplayer script reads must validate size before signed allocation math")

# FX physics ownership lives outside the frozen 32-bit element record. Keep
# the live generation-token API and transactional pool hook in place across
# spawn, draw, free, lifecycle, and archive paths.
require_source_contains(
    "EffectsCore/fx_runtime.h"
    "RUNTIME_OFFSET(FxElem, physObjId, 0x18, 0x18);"
    "the legacy FX physics token field must retain its frozen ABI offset")
require_source_contains(
    "EffectsCore/fx_runtime.h"
    "std::int32_t physObjId;"
    "the frozen FX physics token field must remain exactly 32 bits wide")
require_source_contains(
    "EffectsCore/fx_physics_sidecar.h"
    "dxBody *body = nullptr;"
    "FX physics bodies must live in a native-pointer sidecar")
require_source_contains(
    "EffectsCore/fx_physics_sidecar.h"
    "BodyToken generation = INVALID_BODY_TOKEN;"
    "FX physics sidecar slots must carry generation ownership")
require_source_contains(
    "EffectsCore/fx_physics_sidecar.h"
    "constexpr std::size_t BODY_LIMIT = 512;"
    "FX physics sidecar ownership must respect the ODE body capacity")
require_source_contains(
    "EffectsCore/fx_physics_sidecar.h"
    "std::memcpy(&token, &legacyField, sizeof(token));"
    "legacy signed FX fields must decode token bits without signed conversion")
require_source_contains(
    "EffectsCore/fx_physics_sidecar.h"
    "std::memcpy(&legacyField, &token, sizeof(legacyField));"
    "FX physics tokens must encode legacy bits without signed conversion")
require_source_contains(
    "EffectsCore/fx_physics_sidecar.h"
    "#ifdef KISAK_FX_PHYSICS_SIDECAR_TESTING\nstruct SidecarTestAccess;\n#endif"
    "production FX headers must not declare the test corruption helper")
require_source_contains(
    "EffectsCore/fx_physics_sidecar.h"
    "#ifdef KISAK_FX_PHYSICS_SIDECAR_TESTING\n    friend struct SidecarTestAccess;\n#endif"
    "production FX registries must not friend a recreatable test-helper name")
require_source_match_count(
    "EffectsCore/fx_physics_sidecar.h"
    "#[ \t]*ifdef[ \t]+KISAK_FX_PHYSICS_SIDECAR_TESTING"
    3
    "every SidecarTestAccess declaration, friendship, and definition must be test gated")

set(_fx_sidecar_production_seal_test_path
    "${SOURCE_ROOT}/tests/fx_physics_sidecar_production_seal_tests.cpp")
if(NOT EXISTS "${_fx_sidecar_production_seal_test_path}")
    message(FATAL_ERROR
        "Missing FX physics production authority-seal compile test")
endif()
file(READ "${_fx_sidecar_production_seal_test_path}"
    _fx_sidecar_production_seal_test)
forbid_security_slice_contains(
    _fx_sidecar_production_seal_test
    "#define KISAK_FX_PHYSICS_SIDECAR_TESTING"
    "the production authority-seal test must compile with test access disabled")
require_security_slice_contains(
    _fx_sidecar_production_seal_test
    "struct SidecarTestAccess"
    "the production authority-seal test must recreate the public helper name")
require_security_slice_contains(
    _fx_sidecar_production_seal_test
    "sidecar->activeCount_ = 1u;"
    "the production authority-seal test must query private ownership mutation access")
require_security_slice_contains(
    _fx_sidecar_production_seal_test
    "sidecar->initialized_ = true;"
    "the production authority-seal test must query private lifecycle mutation access")
require_security_slice_contains(
    _fx_sidecar_production_seal_test
    "!SidecarTestAccess::CanMutateActiveCount<BodySidecar>"
    "the production authority seal must reject private ownership mutation access")
require_security_slice_contains(
    _fx_sidecar_production_seal_test
    "!SidecarTestAccess::CanMutateInitialized<BodySidecar>"
    "the production authority seal must reject private lifecycle mutation access")

file(READ "${SOURCE_ROOT}/tests/CMakeLists.txt"
    _fx_sidecar_production_seal_tests_cmake)
extract_security_slice(
    _fx_sidecar_production_seal_tests_cmake
    "# Keep the test-only corruption helper out of production's namespace"
    "add_executable(kisakcod-fx-archive-capacity-tests"
    _fx_sidecar_production_seal_registration
    "FX physics production authority-seal CMake registration")
require_security_slice_contains(
    _fx_sidecar_production_seal_registration
    "add_executable("
    "the production seal must be a normal positive-build target")
require_security_slice_contains(
    _fx_sidecar_production_seal_registration
    "fx_physics_sidecar_production_seal_tests.cpp"
    "the CMake seal must compile the dependent access checks")
require_security_slice_contains(
    _fx_sidecar_production_seal_registration
    "effectscore-physics-sidecar-production-test-access-sealed"
    "the positive production seal must be registered with CTest")
forbid_security_slice_contains(
    _fx_sidecar_production_seal_registration
    "WILL_FAIL"
    "the positive seal must not accept arbitrary compiler failures")
forbid_security_slice_contains(
    _fx_sidecar_production_seal_registration
    "EXCLUDE_FROM_ALL"
    "portable builds must compile the production authority seal normally")
forbid_security_slice_contains(
    _fx_sidecar_production_seal_registration
    "KISAK_FX_PHYSICS_SIDECAR_TESTING"
    "the production seal target must not opt into test access")

file(READ "${SOURCE_ROOT}/.github/workflows/ci.yml"
    _fx_sidecar_production_seal_ci)
extract_security_slice(
    _fx_sidecar_production_seal_ci
    "portable-tests:"
    "windows-x86:"
    _fx_sidecar_production_seal_portable_ci
    "portable production authority-seal CI")
require_security_slice_contains(
    _fx_sidecar_production_seal_portable_ci
    "-DBUILD_TESTING=ON"
    "portable CI must configure the production authority seal")
require_security_slice_contains(
    _fx_sidecar_production_seal_portable_ci
    "ctest --test-dir build-tests -C Release --output-on-failure"
    "portable CI must execute the registered production authority seal")
extract_security_slice(
    _fx_sidecar_production_seal_ci
    "windows-x86:"
    "windows-x86-nosteam:"
    _fx_sidecar_production_seal_measured_ci
    "measured Windows x86 production authority-seal CI")
require_security_slice_contains(
    _fx_sidecar_production_seal_measured_ci
    "kisakcod-fx-physics-sidecar-production-seal-tests"
    "measured Windows x86 CI must build the production authority seal")
require_security_slice_contains(
    _fx_sidecar_production_seal_measured_ci
    "effectscore-physics-sidecar-production-test-access-sealed"
    "measured Windows x86 CI must select the production authority seal")
foreach(_fx_physics_sidecar_operation
    ValidateVacantOwner
    Bind
    Resolve
    Take
    TakeFirst
    PrepareReplacement
    PublishReplacement
    RollbackReplacement)
    require_source_matches(
        "EffectsCore/fx_physics_sidecar.h"
        "(SidecarStatus|TokenResult|BodyResult|IndexedBodyResult)[ \t\r\n]+${_fx_physics_sidecar_operation}[ \t\r\n]*\\("
        "FX physics sidecar must retain ${_fx_physics_sidecar_operation}")
endforeach()
require_source_contains(
    "EffectsCore/fx_physics_sidecar.h"
    "BodySidecar(const BodySidecar &) = delete;"
    "FX native-body ownership registries must not be copy constructed")
require_source_contains(
    "EffectsCore/fx_physics_sidecar.h"
    "BodySidecar &operator=(BodySidecar &&) = delete;"
    "FX native-body ownership registries must not be moved by assignment")
require_source_not_matches(
    "EffectsCore/fx_physics_sidecar.h"
    "(Bind|Resolve|Take)[^;{]*(BodyToken[ \t]*[&*]|dxBody[ \t]*\\*[ \t]*[&*])"
    "FX physics ownership APIs must return values rather than aliasable output pointers")
require_source_matches(
    "EffectsCore/fx_physics_sidecar.h"
    "slot[.]body[ \t]*=[ \t]*nullptr[ \t]*;[ \t\r\n]*slot[.]generation[ \t]*=[ \t]*NextGeneration\\(slot[.]generation\\)[ \t]*;"
    "taking an FX physics body must invalidate the slot generation")
require_source_matches(
    "EffectsCore/fx_physics_sidecar.h"
    "ValidateReplacementRelationWithScratch[ \t\r\n]*\\([ \t\r\n]*live,[ \t\r\n]*staged,[ \t\r\n]*scratch[ \t\r\n]*\\)"
    "FX archive publication must validate the staged generation relation")
require_source_matches(
    "EffectsCore/fx_physics_sidecar.h"
    "ValidateReplacementRelationWithScratch[ \t\r\n]*\\([ \t\r\n]*rollback,[ \t\r\n]*live,[ \t\r\n]*scratch[ \t\r\n]*\\)"
    "FX archive rollback must validate the published generation relation")
require_source_contains(
    "EffectsCore/fx_physics_sidecar.h"
    "staged->transactionPeer_ != live"
    "FX archive publication must bind staging to the exact live registry")
require_source_contains(
    "EffectsCore/fx_physics_sidecar.h"
    "staged->transactionRevision_ != live->revision_"
    "FX archive publication must reject a changed live registry")
require_source_contains(
    "EffectsCore/fx_physics_sidecar.h"
    "rollback->transactionPeer_ != live"
    "FX archive rollback must bind its snapshot to the published registry")
require_source_contains(
    "EffectsCore/fx_physics_sidecar.h"
    "rollback->transactionRevision_ != live->revision_"
    "FX archive rollback must reject post-publication mutation")
require_source_contains(
    "EffectsCore/fx_physics_sidecar.h"
    "staged->transactionPeerLifetimeNonce_ != live->lifetimeNonce_"
    "FX archive publication must reject a reconstructed source registry")
require_source_contains(
    "EffectsCore/fx_physics_sidecar.h"
    "rollback->transactionPeerLifetimeNonce_ != live->lifetimeNonce_"
    "FX archive rollback must reject a reconstructed published registry")
require_source_not_matches(
    "EffectsCore/fx_physics_sidecar.h"
    "reinterpret_cast[ \t\r\n]*<[^>]*dxBody"
    "FX physics sidecar tokens must never be decoded as native pointers")
require_source_not_matches(
    "EffectsCore/fx_physics_sidecar.h"
    "\\([ \t]*(const[ \t]+)?dxBody[ \t]*\\*[ \t]*\\)"
    "FX physics sidecar tokens must never use C-style native-body pointer casts")
require_source_contains(
    "EffectsCore/fx_pool.h"
    "BeforePublishRejected,"
    "FX pool frees must expose a distinct rejected-publication status")
require_source_contains(
    "EffectsCore/fx_pool.h"
    "std::is_same_v<BeforePublishResult, bool>"
    "fallible FX pool callbacks must return bool by value")
require_source_ordered(
    "EffectsCore/fx_pool.h"
    "return FxPoolMutationStatus::BeforePublishRejected;"
    "pool[freedIndex].item = ITEM_TYPE{};"
    "fallible FX pool callbacks must reject before publishing a free slot")
require_source_match_count(
    "EffectsCore/fx_system.cpp"
    "status[ \t]*!=[ \t]*FxPoolMutationStatus::Success[ \t\r\n]*&&[ \t]*status[ \t]*!=[ \t]*FxPoolMutationStatus::BeforePublishRejected"
    3
    "all fallible FX free wrappers must return callback vetoes without a fatal error")

# Production runtime-table translation units must not acquire the corruption
# helper's raw mutable authority by recreating its stable public name.
require_source_contains(
    "database/db_zone_runtime_table.h"
    "#ifdef KISAK_DB_ZONE_RUNTIME_TABLE_TESTING\nstruct ZoneRuntimeTableTestAccess;\n#endif"
    "production runtime-table headers must not declare the test helper")
require_source_contains(
    "database/db_zone_runtime_table.h"
    "#ifdef KISAK_DB_ZONE_RUNTIME_TABLE_TESTING\n    friend struct ZoneRuntimeTableTestAccess;\n#endif"
    "both runtime-table ownership classes must gate test friendship")
require_source_match_count(
    "database/db_zone_runtime_table.h"
    "#[ \t]*ifdef[ \t]+KISAK_DB_ZONE_RUNTIME_TABLE_TESTING[\r\n]+[ \t]*friend[ \t]+struct[ \t]+ZoneRuntimeTableTestAccess[ \t]*;[\r\n]+#[ \t]*endif"
    2
    "both private runtime-table owners must gate test friendship independently")
require_source_match_count(
    "database/db_zone_runtime_table.h"
    "#[ \t]*ifdef[ \t]+KISAK_DB_ZONE_RUNTIME_TABLE_TESTING"
    4
    "every runtime-table helper declaration, friendship, and definition must be test gated")

set(_zone_runtime_production_seal_test_path
    "${SOURCE_ROOT}/tests/db_zone_runtime_table_production_seal_tests.cpp")
if(NOT EXISTS "${_zone_runtime_production_seal_test_path}")
    message(FATAL_ERROR
        "Missing zone runtime table production authority-seal test")
endif()
file(READ "${_zone_runtime_production_seal_test_path}"
    _zone_runtime_production_seal_test)
forbid_security_slice_contains(
    _zone_runtime_production_seal_test
    "#define KISAK_DB_ZONE_RUNTIME_TABLE_TESTING"
    "the production runtime-table seal must compile with test access disabled")
require_security_slice_contains(
    _zone_runtime_production_seal_test
    "struct ZoneRuntimeTableTestAccess"
    "the production runtime-table seal must recreate the helper's public name")
foreach(_zone_runtime_probe_marker IN ITEMS
    "&table->entries_;"
    "table->reserved_ = 1u;"
    "&entry->lifecycle_;"
    "&entry->scriptStringOwnership_;"
    "entry->key_ = zone_load::ZoneLoadContextKey{};")
    require_security_slice_contains(
        _zone_runtime_production_seal_test
        "${_zone_runtime_probe_marker}"
        "the runtime-table seal must probe each private mutable capability")
endforeach()
foreach(_zone_runtime_seal_marker IN ITEMS
    "!ZoneRuntimeTableTestAccess::CanReachEntries<ZoneRuntimeTable>"
    "!ZoneRuntimeTableTestAccess::CanMutateReserved<ZoneRuntimeTable>"
    "!ZoneRuntimeTableTestAccess::CanReachMutableLifecycle<ZoneRuntimeEntry>"
    "!ZoneRuntimeTableTestAccess::CanReachMutableOwnership<ZoneRuntimeEntry>"
    "!ZoneRuntimeTableTestAccess::CanMutateKey<ZoneRuntimeEntry>")
    require_security_slice_contains(
        _zone_runtime_production_seal_test
        "${_zone_runtime_seal_marker}"
        "every raw runtime-table capability must be sealed independently")
endforeach()

file(READ "${SOURCE_ROOT}/tests/CMakeLists.txt"
    _zone_runtime_production_seal_tests_cmake)
extract_security_slice(
    _zone_runtime_production_seal_tests_cmake
    "# Compile production's runtime-table header without its test-access opt-in."
    "add_executable(kisakcod-fx-archive-disk32-tests"
    _zone_runtime_production_seal_registration
    "runtime-table production authority-seal CMake registration")
require_security_slice_contains(
    _zone_runtime_production_seal_registration
    "add_executable("
    "the runtime-table production seal must be a normal positive target")
require_security_slice_contains(
    _zone_runtime_production_seal_registration
    "db_zone_runtime_table_production_seal_tests.cpp"
    "the runtime-table production seal must compile dependent access checks")
require_security_slice_contains(
    _zone_runtime_production_seal_registration
    "database-zone-runtime-table-production-test-access-sealed"
    "the runtime-table production seal must be registered with CTest")
forbid_security_slice_contains(
    _zone_runtime_production_seal_registration
    "WILL_FAIL"
    "the positive runtime-table seal cannot accept arbitrary compiler failures")
forbid_security_slice_contains(
    _zone_runtime_production_seal_registration
    "EXCLUDE_FROM_ALL"
    "portable builds must compile the runtime-table production seal normally")
forbid_security_slice_contains(
    _zone_runtime_production_seal_registration
    "KISAK_DB_ZONE_RUNTIME_TABLE_TESTING"
    "the production seal target cannot opt into runtime-table test access")

file(READ "${SOURCE_ROOT}/.github/workflows/ci.yml"
    _zone_runtime_production_seal_ci)
extract_security_slice(
    _zone_runtime_production_seal_ci
    "portable-tests:"
    "windows-x86:"
    _zone_runtime_production_seal_portable_ci
    "portable runtime-table production authority-seal CI")
require_security_slice_contains(
    _zone_runtime_production_seal_portable_ci
    "-DBUILD_TESTING=ON"
    "portable CI must configure the runtime-table production seal")
require_security_slice_contains(
    _zone_runtime_production_seal_portable_ci
    "ctest --test-dir build-tests -C Release --output-on-failure"
    "portable CI must execute the runtime-table production seal")
extract_security_slice(
    _zone_runtime_production_seal_ci
    "windows-x86:"
    "windows-x86-nosteam:"
    _zone_runtime_production_seal_measured_ci
    "measured Windows x86 runtime-table production authority-seal CI")
require_security_slice_contains(
    _zone_runtime_production_seal_measured_ci
    "kisakcod-db-zone-runtime-table-production-seal-tests"
    "measured Windows x86 CI must build the runtime-table production seal")
require_security_slice_contains(
    _zone_runtime_production_seal_measured_ci
    "production-test-access-sealed"
    "measured Windows x86 CI must select the runtime-table production seal")

# Checked physical-memory scopes form a report-free authority boundary, but
# remain deliberately unenrolled until the exact-key resource coordinator owns
# receipt lifetime and prevents legacy lifecycle bypass/reinitialization.
foreach(_checked_pmem_marker IN ITEMS
    "class AllocationReceipt final"
    "AllocationReceipt(const AllocationReceipt &) = delete;"
    "~AllocationReceipt() noexcept;"
    "std::uint8_t phaseWitness_;"
    "[[nodiscard]] bool isCanonical() const noexcept;"
    "lifecycle/init helpers or directly replace"
    "PhysicalMemory control storage and AllocationReceipt storage must be"
    "mutually disjoint. Both objects must remain wholly outside the entire"
    "High-prim topology can suggest a historical upper bound"
    "does not retain an independently authenticated"
    "cannot authenticate or validate"
    "owns the authoritative initialization"
    "reclaimable backing range unless the caller independently guarantees"
    "cannot be overwritten or reused")
    require_source_contains(
        "universal/physicalmemory_checked.h"
        "${_checked_pmem_marker}"
        "sealed checked physical-memory receipt authority")
endforeach()
foreach(_checked_pmem_marker IN ITEMS
    "memory->prim[0].pos <= memory->prim[1].pos"
    "ValidatePrimTopology(memory->prim[0], 0)"
    "ValidatePrimTopology(memory->prim[1], 1)"
    "allocType == 0 && prim.allocList[0].pos != 0"
    "if (!receipt->isCanonical())"
    "if (!receipt->matchesEntry(prim))"
    "while (remaining != 0"
    "prim.allocList[remaining - 1].name == nullptr")
    require_source_contains(
        "universal/physicalmemory_checked.cpp"
        "${_checked_pmem_marker}"
        "bounded typed checked physical-memory topology")
endforeach()
foreach(_checked_pmem_forbidden IN ITEMS
    "PMem_BeginAlloc("
    "PMem_EndAlloc("
    "PMem_Free("
    "MyAssertHandler("
    "Com_Error("
    "Sys_OutOfMem")
    require_source_not_contains(
        "universal/physicalmemory_checked.cpp"
        "${_checked_pmem_forbidden}"
        "checked physical-memory operations cannot report or call legacy")
endforeach()
foreach(_checked_pmem_registry_forbidden IN ITEMS
    "physicalmemory_checked.h"
    "physical_memory::TryBegin"
    "physical_memory::TryEnd"
    "physical_memory::TryFree")
    require_source_not_contains(
        "database/db_registry.cpp"
        "${_checked_pmem_registry_forbidden}"
        "checked PMem must remain unenrolled before coordinator ownership")
endforeach()
file(GLOB_RECURSE _checked_pmem_production_sources
    "${SOURCE_ROOT}/src/*.cpp" "${SOURCE_ROOT}/src/*.h")
foreach(_checked_pmem_production_path IN LISTS
    _checked_pmem_production_sources)
    if(_checked_pmem_production_path MATCHES
        "physicalmemory_checked\\.(cpp|h)$")
        continue()
    endif()
    file(READ "${_checked_pmem_production_path}"
        _checked_pmem_production_text)
    string(FIND "${_checked_pmem_production_text}"
        "physicalmemory_checked.h" _checked_pmem_header_reference)
    if(NOT _checked_pmem_header_reference EQUAL -1)
        message(FATAL_ERROR
            "Premature checked PMem header enrollment in "
            "${_checked_pmem_production_path}")
    endif()
    if(_checked_pmem_production_text MATCHES
        "physical_memory[ \t\r\n]*::[ \t\r\n]*Try(Begin|End|Free)")
        message(FATAL_ERROR
            "Premature checked PMem operation enrollment in "
            "${_checked_pmem_production_path}")
    endif()
    if(_checked_pmem_production_text MATCHES
        "namespace[ \t\r\n]+physical_memory([ \t\r\n]*\\{|[ \t\r\n]*::)")
        message(FATAL_ERROR
            "Premature checked PMem namespace declaration in "
            "${_checked_pmem_production_path}")
    endif()
endforeach()

# The pending-copy ledger is deliberately shipped as a production-neutral
# primitive. Its receipt self-authentication, callback reentry guard, bounded
# serial/capacity checks, and unknown-result poison path must remain present,
# while the legacy registry remains untouched until an atomic caller cutover.
foreach(_pending_copy_marker IN ITEMS
    "if (self_ != this"
    "receipt->self_ = receipt;"
    "if (ledger->nextGenerationSerial_ == UINT64_MAX)"
    "if (ledger->callbackActive_ != 0)"
    "receipt.setPhase(PendingCopyAdmissionPhase::Admitting);"
    "completion(completionContext);"
    "case PendingCopyDrainCallbackStatus::UnsafeFailure:"
    "ledger->poison();")
    require_source_contains(
        "database/db_zone_pending_copy_ledger.cpp"
        "${_pending_copy_marker}"
        "bounded fail-closed pending-copy authority")
endforeach()
foreach(_pending_copy_forbidden IN ITEMS
    "database/database.h"
    "g_copyInfo"
    "XAsset"
    "PMem_"
    "Com_Error("
    "Sys_Error("
    "malloc("
    "operator new")
    require_source_not_contains(
        "database/db_zone_pending_copy_ledger.cpp"
        "${_pending_copy_forbidden}"
        "pending-copy primitive cannot enroll production or allocation")
endforeach()
require_source_not_contains(
    "database/db_registry.cpp"
    "db_zone_pending_copy_ledger"
    "pending-copy caller cutover must remain atomic")
require_source_not_contains(
    "database/db_registry.cpp"
    "zone_pending_copy"
    "pending-copy namespace cannot be partially enrolled")
require_source_contains(
    "database/database.h"
    "extern XAssetEntry *g_copyInfo[0x800];"
    "legacy pending-copy declaration remains before atomic cutover")
require_source_contains(
    "database/db_registry.cpp"
    "XAssetEntry *g_copyInfo[0x800];"
    "legacy pending-copy storage remains before atomic cutover")

set(_format_sensitive_sources
    "cgame/cg_hudelem.cpp"
    "cgame/cg_info.cpp"
    "client_mp/cl_cgame_mp.cpp"
    "client_mp/cl_main_mp.cpp"
    "client_mp/cl_parse_mp.cpp"
    "game/g_main.cpp"
    "game_mp/g_main_mp.cpp"
    "game_mp/g_scr_main_mp.cpp"
    "qcommon/com_bsp_load_obj.cpp"
    "server_mp/sv_client_mp.cpp"
    "win32/win_steam.cpp"
)

# These files contain network/content-facing diagnostics. Reject the dangerous
# two-pass pattern where preformatted or external text is supplied as the format
# argument with no variadic values. This source tripwire complements compiler
# format warnings, which cannot see through va() or decompiled temporary variables.
set(_bare_format_regex
    "Com_(Error|Printf|PrintError|PrintWarning|DPrintf)[ \t]*\\([^,\r\n]+,[ \t]*(va[ \t]*\\([^\r\n]*\\)|[A-Za-z_][A-Za-z0-9_]*)[ \t]*\\)[ \t]*;")

foreach(_relative_path IN LISTS _format_sensitive_sources)
    set(_path "${SOURCE_ROOT}/src/${_relative_path}")
    file(STRINGS "${_path}" _matches REGEX "${_bare_format_regex}")
    foreach(_line IN LISTS _matches)
        string(STRIP "${_line}" _stripped)
        if (NOT _stripped MATCHES "^//")
            message(FATAL_ERROR
                "Dynamic diagnostic format in src/${_relative_path}: ${_stripped}\n"
                "Pass a literal format and external text as a %s argument.")
        endif()
    endforeach()
endforeach()

message(STATUS "Security source invariants verified")
