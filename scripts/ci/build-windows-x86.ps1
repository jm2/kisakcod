# Canonical Windows x86 production configure+build, shared by ci.yml and
# release.yml (CI_RELEASE_WORKFLOW_AUDIT gap 11: CI must validate exactly what
# release ships, so both workflows call this one script with the same flags).
#
# Production profile: MP client + dedicated server, Win32, CICD, DirectX SDK,
# FX archive stack measurement. Callers may append extra CMake cache args via
# -CmakeExtraArgs (e.g. -DKISAK_REPRODUCIBLE_BUILD=ON for the parity gate).
#
# Requires: cmake, dotnet (actions/setup-dotnet runs first in the workflows).
# Fails closed on the first non-zero tool exit.
[CmdletBinding()]
param(
  [string]$BuildDir = "build",
  [string]$Config = "Release",
  [string[]]$Targets = @("KisakCOD-mp", "KisakCOD-dedi"),
  [string[]]$CmakeExtraArgs = @()
)

$ErrorActionPreference = "Stop"

$DXSDK_VERSION = "9.29.952.8"
$env:NUGET_PACKAGES = Join-Path (Get-Location) ".nuget\packages"

# Restore the DirectX SDK the same way the previous inline steps did: pull the
# nuget package through a throwaway classlib, then point DXSDK_DIR at the
# native props directory inside the global packages folder.
dotnet new classlib -o dxsdk-temp
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
dotnet add dxsdk-temp package Microsoft.DXSDK.D3DX --version $DXSDK_VERSION
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$dxsdkNative = Join-Path $env:NUGET_PACKAGES "microsoft.dxsdk.d3dx\$DXSDK_VERSION\build\native"

cmake -S . -B $BuildDir `
  -G "Visual Studio 17 2022" -A Win32 `
  -DCICD=ON "-DDXSDK_DIR=$dxsdkNative" `
  -DKISAK_BUILD_MP=ON `
  -DKISAK_BUILD_DEDICATED=ON `
  -DKISAK_BUILD_SP=OFF `
  -DKISAK_MEASURE_FX_ARCHIVE_STACK=ON `
  @CmakeExtraArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build $BuildDir --config $Config --target $Targets --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Keep the workspace tidy: the throwaway classlib is never part of an artifact.
Remove-Item -Recurse -Force dxsdk-temp -ErrorAction SilentlyContinue
