# Collect the ACTUAL resolved build toolchain from a completed CMake build
# tree and write it as a JSON object for embedding in a provenance manifest
# (PR #107 review P2: provenance manifests must record the compiler, linker,
# CMake, and SDK versions that really built the artifacts - never assumed
# constants).
#
# Everything is read from the build tree's own CMake toolchain-detection
# output (CMakeFiles/<version>/CMake{C,CXX}Compiler.cmake and
# CMakeCache.txt), so the recorded values are what CMake resolved and used
# for THIS build. The compiler binary is never executed, which keeps this
# working for MSVC without a Visual Studio dev environment.
#
# Usage:
#   collect-toolchain.ps1 -BuildDir <dir> -OutFile <path.json>
#       [-SdkLabel <name> -SdkVersion <version>]
#
# Failure policy: a missing build tree or missing compiler-detection files
# fails hard (a provenance manifest must not silently omit the toolchain of
# a build that ran). Individual optional probes (linker metadata, SDKs)
# degrade honestly to null/omitted fields instead of inventing values.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $BuildDir,
    [Parameter(Mandatory = $true)] [string] $OutFile,
    # Optional explicit SDK (e.g. the DirectX SDK a job restored by hand).
    [string] $SdkLabel,
    [string] $SdkVersion
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $BuildDir -PathType Container)) {
    Write-Error "collect-toolchain: build directory not found: $BuildDir"
    exit 1
}
$buildRoot = (Resolve-Path -LiteralPath $BuildDir).Path
$cache = Join-Path $buildRoot 'CMakeCache.txt'

function Read-CmakeSetVar {
    # CMake compiler-detection files store lines like: set(NAME "value")
    # (quoted strings) or set(NAME value) (unquoted scalars/numbers, e.g.
    # linker versions and IDs). Capture both; a quoted empty value is
    # treated as absent.
    param([string] $File, [string] $Name)
    foreach ($line in Get-Content -LiteralPath $File) {
        if ($line -match ('^set\(\s*' + [regex]::Escape($Name) + '\s+(?:"(?<q>.*)"|(?<u>[^)\s]*))\s*\)\s*$')) {
            if ($Matches.ContainsKey('q') -and $Matches['q']) { return $Matches['q'] }
            if ($Matches.ContainsKey('u')) { return $Matches['u'] }
            return $null
        }
    }
    return $null
}

function Read-CmakeCacheVar {
    # CMakeCache.txt stores lines like: NAME:TYPE=value
    param([string] $File, [string] $Name)
    foreach ($line in Get-Content -LiteralPath $File) {
        if ($line -match ('^' + [regex]::Escape($Name) + ':[^=]+=(.*)$')) {
            return $Matches[1]
        }
    }
    return $null
}

# --- locate CMake's own toolchain-detection files --------------------------
# The CMakeFiles/<x.y.z>/ directory name IS the CMake version that
# configured this tree (authoritative even when the cmake on PATH changed).
$cmakeFilesDir = Join-Path $buildRoot 'CMakeFiles'
if (-not (Test-Path -LiteralPath $cmakeFilesDir -PathType Container)) {
    Write-Error "collect-toolchain: no CMakeFiles directory under $buildRoot (was it configured by CMake?)"
    exit 1
}
$versionDir = Get-ChildItem -LiteralPath $cmakeFilesDir -Directory |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'CMakeCXXCompiler.cmake') } |
    Select-Object -First 1
if (-not $versionDir) {
    Write-Error "collect-toolchain: no CMakeFiles/*/CMakeCXXCompiler.cmake under $buildRoot (nothing was configured?)"
    exit 1
}
$cmakeVersion = $versionDir.Name
$cxxFile = Join-Path $versionDir.FullName 'CMakeCXXCompiler.cmake'
$cFile = Join-Path $versionDir.FullName 'CMakeCCompiler.cmake'

# --- compilers, exactly as CMake resolved them -----------------------------
function Get-CompilerInfo {
    param([string] $File, [string] $Stem)
    if (-not (Test-Path -LiteralPath $File)) { return $null }
    $path = Read-CmakeSetVar $File "CMAKE_${Stem}_COMPILER"
    if (-not $path) { return $null }
    [ordered]@{
        id      = Read-CmakeSetVar $File "CMAKE_${Stem}_COMPILER_ID"
        version = Read-CmakeSetVar $File "CMAKE_${Stem}_COMPILER_VERSION"
        path    = $path
    }
}
$cCompiler = Get-CompilerInfo $cFile 'C'
$cxxCompiler = Get-CompilerInfo $cxxFile 'CXX'

# --- linker ----------------------------------------------------------------
# CMake >= 3.29 identifies the linker during compiler detection and records
# it alongside the compiler. For older CMake on an MSVC layout, link.exe
# sits next to cl.exe; its version is read from file version metadata (no
# execution, no dev environment). On Unix, fall back to asking ld directly.
# All of $linker/[$linkerId|$linkerVersion|$linkerPath] are assigned up
# front so Set-StrictMode can never observe them unset.
$linker = $null
$linkerId = Read-CmakeSetVar $cxxFile 'CMAKE_CXX_COMPILER_LINKER_ID'
$linkerVersion = Read-CmakeSetVar $cxxFile 'CMAKE_CXX_COMPILER_LINKER_VERSION'
$linkerPath = Read-CmakeSetVar $cxxFile 'CMAKE_CXX_COMPILER_LINKER'
if (-not $linkerPath -and $cxxCompiler -and $cxxCompiler['path']) {
    $clPath = [string]$cxxCompiler['path']
    if ($clPath -match '[\\/](cl|clang-cl|icc)\.exe$') {
        # .NET path APIs, not Join-Path/Split-Path: those resolve drive
        # letters and fail on non-Windows hosts parsing a Windows path
        # (CMake always writes forward slashes, even in VS layouts).
        $parent = [System.IO.Path]::GetDirectoryName($clPath)
        $sibling = [System.IO.Path]::Combine($parent, 'link.exe')
        if (Test-Path -LiteralPath $sibling) {
            $linkerPath = $sibling
            if (-not $linkerId) { $linkerId = 'MSVC' }
            if (-not $linkerVersion) {
                $vi = (Get-Item -LiteralPath $sibling).VersionInfo
                if ($vi -and $vi.ProductVersion) { $linkerVersion = $vi.ProductVersion }
            }
        }
    }
}
# Unix ld fallback: only for build trees whose compiler is not MSVC —
# a cross/foreign build tree (e.g. a VS cache inspected on Linux) must not
# end up labeled with the host linker.
if (-not $linkerPath -and $IsLinux -and $cxxCompiler -and $cxxCompiler['id'] -ne 'MSVC') {
    $ld = Get-Command ld -ErrorAction SilentlyContinue
    if ($ld) {
        $linkerPath = $ld.Source
        if (-not $linkerVersion) { $linkerVersion = ((ld --version) | Select-Object -First 1) }
    }
}
if (-not $linkerPath -and $IsMacOS -and $cxxCompiler -and $cxxCompiler['id'] -ne 'MSVC') {
    $ld = Get-Command ld -ErrorAction SilentlyContinue
    if ($ld) {
        $linkerPath = $ld.Source
        if (-not $linkerVersion) { $linkerVersion = ((ld -v 2>&1) | Select-Object -First 1) }
    }
}
if ($linkerPath -or $linkerId -or $linkerVersion) {
    $linker = [ordered]@{
        id      = $linkerId
        version = $linkerVersion
        path    = $linkerPath
    }
}

# --- SDKs -------------------------------------------------------------------
$sdks = @()
# Fail closed on a half-specified SDK: a label without a version (or the
# reverse) would otherwise be silently dropped here, hiding a real dependency
# from the published manifest.
if ([string]::IsNullOrEmpty($SdkLabel) -ne [string]::IsNullOrEmpty($SdkVersion)) {
    Write-Error "collect-toolchain: -SdkLabel and -SdkVersion must be passed together (label='$SdkLabel' version='$SdkVersion')"
    exit 1
}
if ($SdkLabel -and $SdkVersion) {
    $sdks += [ordered]@{ name = $SdkLabel; version = $SdkVersion }
}
$windowsSdk = $null
if (Test-Path -LiteralPath $cache) {
    $windowsSdk = Read-CmakeCacheVar $cache 'CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION'
}
if (-not $windowsSdk) {
    # VS generators record the resolved SDK in the generated project files.
    $vcxproj = Get-ChildItem -LiteralPath $buildRoot -Recurse -Filter '*.vcxproj' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($vcxproj) {
        foreach ($line in (Get-Content -LiteralPath $vcxproj.FullName)) {
            if ($line -match '<WindowsTargetPlatformVersion>([^<]+)</WindowsTargetPlatformVersion>') {
                $windowsSdk = $Matches[1]
                break
            }
        }
    }
}
if ($windowsSdk) {
    $sdks += [ordered]@{ name = 'Windows SDK'; version = $windowsSdk }
}
if ($IsMacOS) {
    $xcrun = Get-Command xcrun -ErrorAction SilentlyContinue
    if ($xcrun) {
        $macSdk = (& xcrun --show-sdk-version 2>$null)
        if ($macSdk) { $sdks += [ordered]@{ name = 'macOS SDK'; version = $macSdk } }
    }
}

# --- assemble ---------------------------------------------------------------
$result = [ordered]@{ cmake = $cmakeVersion }
$generator = $null
$generatorPlatform = $null
if (Test-Path -LiteralPath $cache) {
    $generator = Read-CmakeCacheVar $cache 'CMAKE_GENERATOR'
    $generatorPlatform = Read-CmakeCacheVar $cache 'CMAKE_GENERATOR_PLATFORM'
}
if ($generator) { $result.generator = $generator }
if ($generatorPlatform) { $result.generator_platform = $generatorPlatform }
if ($cCompiler) { $result.c_compiler = $cCompiler }
if ($cxxCompiler) { $result.cxx_compiler = $cxxCompiler }
if ($linker) { $result.linker = $linker }
$result.sdks = @($sdks)

$outDir = Split-Path -Parent $OutFile
if ($outDir -and -not (Test-Path -LiteralPath $outDir)) {
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}
$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $OutFile -Encoding utf8
$cxxSummary = if ($cxxCompiler) { "$($cxxCompiler['id']) $($cxxCompiler['version'])" } else { 'unknown' }
Write-Output "collect-toolchain: wrote $OutFile (cmake $cmakeVersion, cxx compiler $cxxSummary)"
