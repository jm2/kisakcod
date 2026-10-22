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
    CMake generator. Defaults to "Visual Studio 18 2026".

.PARAMETER BuildDir
    Out-of-source build directory. Defaults to ".\build" for Win32 and
    ".\build-x64" or ".\build-ARM64" otherwise, since CMake can't change the
    platform of an existing build directory.

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

.PARAMETER HeadlessDedi
    Configures the experimental dedicated-server source profile that excludes
    client, renderer, audio, UI, Bink, and Miles source groups. This is a port
    burn-down target and is not the default dedicated server build yet.

.PARAMETER InstallDeps
    Installs what a build needs and the machine lacks: CMake through winget, and
    the DirectX SDK headers and libraries from the pinned Microsoft.DXSDK.D3DX
    NuGet package (the one CI uses), unpacked into .deps\. Visual Studio 2026
    with the C++ workload must be installed by hand. Once .deps\ holds the
    package, later builds use it whenever DXSDK_DIR is unset, with or without
    -InstallDeps.

.PARAMETER Run
    After building, launches the first built game executable (KisakCOD-mp,
    KisakCOD-dedi or KisakCOD-sp) from bin\<Config>. 64-bit builds add
    +set db_unverified64 1, the loader opt-in the 64-bit port still needs.

.PARAMETER RunArgs
    Arguments passed to the game with -Run, e.g. '+set','fs_basepath','D:\cod4copy'.

.EXAMPLE
    .\build-win.ps1 -InstallDeps
    Installs missing dependencies, then builds as build-win.bat does.

.EXAMPLE
    .\build-win.ps1 -Arch x64 -Experimental64Bit -Targets KisakCOD-mp -Run -RunArgs '+devmap','mp_crash'
    Builds the Win64 client and launches it on mp_crash.

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

    [string]$Generator = 'Visual Studio 18 2026',

    [string]$BuildDir = '',

    [switch]$Clean,

    [switch]$ConfigureOnly,

    [switch]$NoConfigure,

    [switch]$Experimental64Bit,

    [switch]$HeadlessDedi,

    [ValidateRange(1, 1024)]
    [int]$Jobs = [Environment]::ProcessorCount,

    [switch]$InstallDeps,

    [switch]$Run,

    [string[]]$RunArgs = @()
)

# The DirectX SDK as CI gets it (docs/design/CLIENT.md). Bump deliberately.
$DxsdkPackage = 'Microsoft.DXSDK.D3DX'
$DxsdkVersion = '9.29.952.8'
$DxsdkSha256 = 'ead0906ae8a26c18a7525da7490127a2110f7c58f18293738283e30e97c6ea4b'

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
    if ($InstallDeps -and (Get-Command winget -ErrorAction SilentlyContinue)) {
        Write-Step 'Installing CMake through winget'
        Invoke-Checked -File 'winget' -Arguments @('install', '--id', 'Kitware.CMake', '-e', '--accept-source-agreements', '--accept-package-agreements')
        # winget updates the stored PATH; pick it up without a new shell. Read it
        # from the registry: ConstrainedLanguage mode blocks [Environment] methods.
        $machinePath = (Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment' -Name Path -ErrorAction SilentlyContinue).Path
        $userPath = (Get-ItemProperty -LiteralPath 'HKCU:\Environment' -Name Path -ErrorAction SilentlyContinue).Path
        $env:Path = (@($machinePath, $userPath, $env:Path) | Where-Object { $_ }) -join ';'
    }
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        throw "cmake was not found on PATH. Run with -InstallDeps, or install CMake (winget install Kitware.CMake) and reopen your shell."
    }
}

$vswhere = if (${env:ProgramFiles(x86)}) { Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe' } else { '' }
if ($Generator -like 'Visual Studio 18*' -and $vswhere -and (Test-Path -LiteralPath $vswhere)) {
    $vs = & $vswhere -version '[18.0,19.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ([string]::IsNullOrEmpty($vs)) {
        Write-Warning 'Visual Studio 2026 with the "Desktop development with C++" workload was not found; the configure will fail without it.'
    }
}

$needsClientMedia =
    ($Targets -contains 'KisakCOD-mp') -or
    ($Targets -contains 'KisakCOD-sp') -or
    (($Targets -contains 'KisakCOD-dedi') -and -not $HeadlessDedi)

# Without a June 2010 SDK (DXSDK_DIR), use the NuGet package in .deps\, as CI does.
$NugetDxsdk = Join-Path $RepoRoot ".deps\$DxsdkPackage.$DxsdkVersion\build\native"
if ($needsClientMedia -and [string]::IsNullOrEmpty($env:DXSDK_DIR) -and $InstallDeps -and -not (Test-Path -LiteralPath $NugetDxsdk)) {
    Write-Step "Fetching $DxsdkPackage $DxsdkVersion into .deps"
    $depsDir = Join-Path $RepoRoot '.deps'
    $nupkg = Join-Path $depsDir "$DxsdkPackage.$DxsdkVersion.nupkg"
    New-Item -ItemType Directory -Force -Path $depsDir | Out-Null
    Invoke-WebRequest -UseBasicParsing -Uri "https://www.nuget.org/api/v2/package/$DxsdkPackage/$DxsdkVersion" -OutFile $nupkg
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $nupkg).Hash.ToLowerInvariant()
    if ($actual -ne $DxsdkSha256) {
        Remove-Item -LiteralPath $nupkg -Force
        throw "$DxsdkPackage $DxsdkVersion has SHA-256 $actual, expected $DxsdkSha256."
    }
    $unpackDir = Join-Path $depsDir "$DxsdkPackage.$DxsdkVersion"
    New-Item -ItemType Directory -Force -Path $unpackDir | Out-Null
    if (Get-Command tar -ErrorAction SilentlyContinue) {
        # Windows' tar.exe (bsdtar) reads the zip. Expand-Archive calls .NET
        # methods that ConstrainedLanguage mode blocks.
        Invoke-Checked -File 'tar' -Arguments @('-xf', $nupkg, '-C', $unpackDir)
    }
    else {
        Expand-Archive -LiteralPath $nupkg -DestinationPath $unpackDir -Force
    }
    Remove-Item -LiteralPath $nupkg -Force
}
$UseNugetDxsdk = $needsClientMedia -and [string]::IsNullOrEmpty($env:DXSDK_DIR) -and (Test-Path -LiteralPath $NugetDxsdk)
if ($needsClientMedia -and [string]::IsNullOrEmpty($env:DXSDK_DIR) -and -not $UseNugetDxsdk) {
    Write-Warning "DXSDK_DIR is not set. Run with -InstallDeps to fetch the DirectX SDK NuGet package, or install the June 2010 DirectX SDK; the CMake configure will fail without one."
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
if ($Run -and $ConfigureOnly) {
    throw '-Run needs a build; drop -ConfigureOnly.'
}
if ($RunArgs.Count -gt 0 -and -not $Run) {
    throw '-RunArgs is for the game; add -Run.'
}

if ([string]::IsNullOrEmpty($BuildDir)) {
    $BuildDir = if ($Arch -eq 'Win32') { 'build' } else { "build-$Arch" }
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
        "-DKISAK_BUILD_SP=$([int]($Targets -contains 'KisakCOD-sp'))",
        "-DKISAK_DEDI_HEADLESS=$([int][bool]$HeadlessDedi)"
    )
    if ($Experimental64Bit) {
        # As CI's Win64 and ARM64 legs: the 64-bit targets link no Steamworks
        # and no x86 MMX skinning.
        $configureArguments += @('-DKISAK_ALLOW_UNSUPPORTED_64BIT=ON', '-DKISAK_ENABLE_STEAM=OFF', '-DKISAK_ENABLE_X86_MMX_SKINNING=OFF')
    }
    if ($UseNugetDxsdk) {
        # CICD selects the NuGet package layout (build\native\release\lib).
        $configureArguments += @('-DCICD=ON', "-DDXSDK_DIR=$NugetDxsdk")
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

$OutputDir = Join-Path $RepoRoot (Join-Path 'bin' $Config)

# The Win32 client links D3DX, whose runtime DLL isn't on a stock Windows; the
# NuGet package carries it.
if ($UseNugetDxsdk -and $Arch -eq 'Win32' -and (Test-Path -LiteralPath $OutputDir)) {
    $d3dx = Join-Path $NugetDxsdk 'release\bin\x86\D3DX9_43.dll'
    if (Test-Path -LiteralPath $d3dx) {
        Copy-Item -LiteralPath $d3dx -Destination $OutputDir -Force
    }
}

Write-Step "Build complete: $($Targets -join ', ') [$Config/$Arch]"
Write-Host "Output: $(Join-Path $RepoRoot (Join-Path 'bin' $Config))"
Write-Host $RepoRoot

if ($Run) {
    $game = @('KisakCOD-mp', 'KisakCOD-dedi', 'KisakCOD-sp') | Where-Object { $Targets -contains $_ } | Select-Object -First 1
    if (-not $game) {
        throw "-Run needs KisakCOD-mp, KisakCOD-dedi or KisakCOD-sp in -Targets."
    }
    $exe = Join-Path $OutputDir "$game.exe"
    if (-not (Test-Path -LiteralPath $exe)) {
        throw "$exe was not built."
    }
    $gameArgs = @()
    if ($Arch -ne 'Win32') {
        # The 64-bit loader still needs its opt-in until every asset type is verified (NOW K4).
        $gameArgs += @('+set', 'db_unverified64', '1')
    }
    $gameArgs += $RunArgs
    Write-Step "Launching $game $($gameArgs -join ' ')"
    Push-Location -LiteralPath $OutputDir
    try {
        & $exe @gameArgs
    }
    finally {
        Pop-Location
    }
}
