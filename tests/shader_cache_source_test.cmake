cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

function(read_source PATH OUT_VARIABLE DESCRIPTION)
    if(NOT EXISTS "${PATH}")
        message(FATAL_ERROR
            "Missing derived-shader-cache source (${DESCRIPTION}): ${PATH}")
    endif()
    file(READ "${PATH}" _source)
    set(${OUT_VARIABLE} "${_source}" PARENT_SCOPE)
endfunction()

function(require_contains TEXT NEEDLE DESCRIPTION)
    string(FIND "${TEXT}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing derived-shader-cache invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(forbid_contains TEXT NEEDLE DESCRIPTION)
    string(FIND "${TEXT}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden derived-shader-cache regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

set(HEADER "${SOURCE_ROOT}/src/database/shader_cache.h")
set(IMPLEMENTATION "${SOURCE_ROOT}/src/database/shader_cache.cpp")

read_source("${HEADER}" HEADER_TEXT "header")
read_source("${IMPLEMENTATION}" IMPLEMENTATION_TEXT "implementation")

# The portable core must keep its dependencies platform-neutral: identity and
# validation come from the portable database headers, not from D3D9.
require_contains("${HEADER_TEXT}" "#include \"database/db_graph_hash.h\""
    "content-hash dependency")
require_contains("${HEADER_TEXT}" "#include \"database/db_validation.h\""
    "bytecode-validation dependency")

# Versioned identity: the domain, format, and converter versions are the
# invalidation contract. Removing one would silently reuse stale artifacts.
require_contains("${HEADER_TEXT}" "kHashDomain" "domain-separation tag")
require_contains("${HEADER_TEXT}" "kFormatVersion" "sidecar format version")
require_contains("${HEADER_TEXT}" "kConverterVersion" "translator version")
require_contains("${HEADER_TEXT}" "kSidecarHeaderBytes = 100"
    "pinned sidecar framing size")
require_contains("${HEADER_TEXT}" "NeedsRegeneration" "regeneration outcome")
require_contains("${HEADER_TEXT}" "UnsupportedSource" "unsupported-source outcome")

# The implementation must validate original bytecode and classify each
# outcome, so unsupported programs are rejected and stale evidence is rebuilt.
require_contains("${IMPLEMENTATION_TEXT}" "#include \"database/shader_cache.h\""
    "implementation include")
require_contains("${IMPLEMENTATION_TEXT}" "db::validation::MaterialShaderLoadDefValid"
    "load-def validation")
require_contains("${IMPLEMENTATION_TEXT}" "db::validation::D3D9ShaderBytecodeValid"
    "bytecode validation")
require_contains("${IMPLEMENTATION_TEXT}" "LookupResult::Missing"
    "missing outcome")
require_contains("${IMPLEMENTATION_TEXT}" "LookupResult::NeedsRegeneration"
    "regeneration outcome")
require_contains("${IMPLEMENTATION_TEXT}" "LookupResult::UnsupportedSource"
    "unsupported-source outcome")
require_contains("${IMPLEMENTATION_TEXT}" "HashArtifact"
    "artifact verification")

# The core must never grow a D3D9/Win32 surface or do its own file I/O; it
# identifies and verifies bytecode only. Storage and translation stay at the
# platform boundary where the retail archives are read, never rewritten.
string(TOLOWER "${HEADER_TEXT}" HEADER_LOWER)
string(TOLOWER "${IMPLEMENTATION_TEXT}" IMPLEMENTATION_LOWER)
string(CONCAT COMBINED_LOWER "${HEADER_LOWER}" "${IMPLEMENTATION_LOWER}")

forbid_contains("${COMBINED_LOWER}" "d3d9.h" "D3D9 umbrella header")
forbid_contains("${COMBINED_LOWER}" "d3dx9" "D3DX helper library")
forbid_contains("${COMBINED_LOWER}" "windows.h" "Win32 umbrella header")
forbid_contains("${COMBINED_LOWER}" "idirect3d" "D3D9 device interface")
forbid_contains("${COMBINED_LOWER}" "findfirstfile" "Win32 directory enumeration")
forbid_contains("${COMBINED_LOWER}" "deletefilea" "Win32 file deletion")
forbid_contains("${COMBINED_LOWER}" "createfilea" "Win32 file creation")
forbid_contains("${COMBINED_LOWER}" "sys_mkdir" "engine filesystem write")
forbid_contains("${COMBINED_LOWER}" "fopen" "C stdio file access")
forbid_contains("${COMBINED_LOWER}" "std::filesystem" "C++ filesystem access")
# The derived cache must not reuse or overwrite the legacy shadercache2 path.
forbid_contains("${COMBINED_LOWER}" "shadercache" "legacy shader cache path")
