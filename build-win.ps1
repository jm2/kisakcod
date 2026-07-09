<#
.SYNOPSIS
    Configures and builds KisakCOD on Windows. PowerShell reimplementation of build-win.bat.

.DESCRIPTION
    Faithful, parameterized replacement for build-win.bat. By default it reproduces
    the batch file's behavior (configure a Visual Studio Win32 solution in .\build\
    and build the KisakCOD-mp and KisakCOD-dedi targets in Debug).

    Unlike the batch file, this script:
      * Fails fast on any configure/build error (checks the exit code of every step).
      * Lets you pick the configuration, architecture, generator, and target list.
      * Can configure only (like scripts\mksln.bat) or skip configure and just build.

    Run it from the repository ROOT (the directory containing CMakeLists.txt).

.PARAMETER Config
    CMake build configuration: Debug (default) or Release.

.PARAMETER Arch
    Visual Studio platform passed to cmake -A. Defaults to Win32, matching the
    current build. x64 and ARM64 are exposed for port development and require
    -Experimental64Bit.

.PARAMETER Targets
    One or more CMake targets to build. Defaults to KisakCOD-mp and KisakCOD-dedi,
    exactly as build-win.bat. Other available targets include KisakCOD-sp.

.PARAMETER Generator
    CMake generator. Defaults to "Visual Studio 17 2022".

.PARAMETER BuildDir
    Out-of-source build directory. Defaults to ".\build".

.PARAMETER Clean
    Delete the build directory before configuring (fresh configure).

.PARAMETER ConfigureOnly
    Only run the CMake configure step (equivalent to scripts\mksln.bat), then stop.

.PARAMETER NoConfigure
    Skip the configure step and build the existing solution in -BuildDir.

.PARAMETER Jobs
    Parallel build jobs passed to cmake --build (defaults to the number of logical CPUs).

.PARAMETER Experimental64Bit
    Allows x64 or ARM64 configuration while the runtime ABI conversion is incomplete.
    These targets are expected to expose compile/link work and are not release builds.

.EXAMPLE
    .\build-win.ps1
    Reproduces build-win.bat: Debug, Win32, builds KisakCOD-mp and KisakCOD-dedi.

.EXAMPLE
    .\build-win.ps1 -Config Release -Targets KisakCOD-mp,KisakCOD-dedi,KisakCOD-sp -Clean
    Clean Release build of the MP client, dedicated server, and single-player client.

.EXAMPLE
    .\build-win.ps1 -ConfigureOnly
    Only (re)generates the Visual Studio solution in .\build (like scripts\mksln.bat).
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug',

    [ValidateSet('Win32', 'x64', 'ARM64')]
    [string]$Arch = 'Win32',

    [string[]]$Targets = @('KisakCOD-mp', 'KisakCOD-dedi'),

    [string]$Generator = 'Visual Studio 17 2022',

    [string]$BuildDir = 'build',

    [switch]$Clean,

    [switch]$ConfigureOnly,

    [switch]$NoConfigure,

    [switch]$Experimental64Bit,

    [ValidateRange(1, 1024)]
    [int]$Jobs = [Environment]::ProcessorCount
)

# Stop on the first uncaught error; treat native-command failures explicitly via $LASTEXITCODE.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Invoke-Checked {
    # Run a native command and abort the script if it returns a non-zero exit code.
    param(
        [Parameter(Mandatory)][string]$File,
        [Parameter(Mandatory)][string[]]$Arguments
    )
    Write-Host "    $File $($Arguments -join ' ')" -ForegroundColor DarkGray
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed (exit $LASTEXITCODE): $File $($Arguments -join ' ')"
    }
}

# --- Pre-flight checks ---------------------------------------------------------

# Resolve the repo root as the directory this script lives in, then work from there.
$RepoRoot = $PSScriptRoot
if ([string]::IsNullOrEmpty($RepoRoot)) { $RepoRoot = (Get-Location).Path }
Set-Location -LiteralPath $RepoRoot

if (-not (Test-Path -LiteralPath (Join-Path $RepoRoot 'CMakeLists.txt'))) {
    throw "CMakeLists.txt not found in '$RepoRoot'. Run this script from the repository ROOT directory."
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "cmake was not found on PATH. Install CMake and reopen your shell."
}

if ([string]::IsNullOrEmpty($env:DXSDK_DIR)) {
    Write-Warning "DXSDK_DIR is not set. A local (non-CI) build needs the DirectX SDK (June 2010); the CMake configure will fail without it."
}

if ($Arch -ne 'Win32' -and -not $Experimental64Bit) {
    throw "$Arch is an in-progress port target. Pass -Experimental64Bit to configure it intentionally."
}
if ($ConfigureOnly -and $NoConfigure) {
    throw '-ConfigureOnly and -NoConfigure cannot be used together.'
}
if ($Targets.Count -eq 0) {
    throw 'At least one build target is required.'
}

$BuildPath = Join-Path $RepoRoot $BuildDir

# --- Clean ---------------------------------------------------------------------

if ($Clean -and (Test-Path -LiteralPath $BuildPath)) {
    Write-Step "Cleaning build directory: $BuildPath"
    Remove-Item -LiteralPath $BuildPath -Recurse -Force
}

# --- Configure (equivalent to scripts\mksln.bat) -------------------------------

if (-not $NoConfigure) {
    Write-Step "Configuring ($Generator, -A $Arch, $Config) in $BuildPath"
    if (-not (Test-Path -LiteralPath $BuildPath)) {
        New-Item -ItemType Directory -Path $BuildPath | Out-Null
    }
    $configureArguments = @(
        '-S', $RepoRoot,
        '-B', $BuildPath,
        '-G', $Generator,
        '-A', $Arch,
        "-DCMAKE_BUILD_TYPE=$Config",
        "-DKISAK_BUILD_MP=$([int]($Targets -contains 'KisakCOD-mp'))",
        "-DKISAK_BUILD_DEDICATED=$([int]($Targets -contains 'KisakCOD-dedi'))",
        "-DKISAK_BUILD_SP=$([int]($Targets -contains 'KisakCOD-sp'))"
    )
    if ($Experimental64Bit) {
        $configureArguments += '-DKISAK_ALLOW_UNSUPPORTED_64BIT=ON'
    }
    Invoke-Checked -File 'cmake' -Arguments $configureArguments
}
elseif (-not (Test-Path -LiteralPath $BuildPath)) {
    throw "-NoConfigure was specified but the build directory '$BuildPath' does not exist. Run a configure first."
}

if ($ConfigureOnly) {
    Write-Step "Configure complete (ConfigureOnly). Solution is in $BuildPath"
    Write-Host $RepoRoot
    return
}

# --- Build ---------------------------------------------------------------------

foreach ($target in $Targets) {
    Write-Step "Building target '$target' ($Config)"
    Invoke-Checked -File 'cmake' -Arguments @(
        '--build', $BuildPath,
        '--config', $Config,
        '--target', $target,
        '--parallel', "$Jobs"
    )
}

Write-Step "Build complete: $($Targets -join ', ') [$Config/$Arch]"
Write-Host "Output: $(Join-Path $RepoRoot (Join-Path 'bin' $Config))"
Write-Host $RepoRoot
