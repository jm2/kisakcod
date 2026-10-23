# Packages a Windows x86 bin directory: the executables, the OpenAL Soft and
# FFmpeg DLLs vcpkg built (LGPL; their licenses go in licenses\), and a short
# README. steam_api.dll stays until the owner's legal decision (docs/NOW.md,
# Blocked on owner).
#
# Usage: package-windows-x86.ps1 -BinDir bin/Release -Zip dist/<name>.zip
#            -VcpkgInstalled build/vcpkg_installed/x86-windows
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $BinDir,
    [Parameter(Mandatory = $true)] [string] $Zip,
    [Parameter(Mandatory = $true)] [string] $VcpkgInstalled
)
$ErrorActionPreference = 'Stop'

$licenses = Join-Path $BinDir 'licenses'
New-Item -ItemType Directory -Force -Path $licenses | Out-Null
foreach ($port in @('openal-soft', 'ffmpeg')) {
    $copyright = Join-Path $VcpkgInstalled "share\$port\copyright"
    if (-not (Test-Path -LiteralPath $copyright)) {
        throw "No license for $port at $copyright."
    }
    Copy-Item -LiteralPath $copyright -Destination (Join-Path $licenses "$port.txt")
}
Set-Content -LiteralPath (Join-Path $BinDir 'README.txt') -Encoding utf8 -Value @(
    'KisakCOD for Windows x86: multiplayer client and dedicated server.',
    'Copy these files into a licensed Call of Duty 4 installation.',
    'Sound plays through OpenAL Soft and cinematics through FFmpeg, both LGPL',
    'DLLs included here; their licenses are in licenses\.'
)
$items = Get-ChildItem -LiteralPath $BinDir
Compress-Archive -LiteralPath $items.FullName -DestinationPath $Zip
Write-Output "Packaged $($items.Count) entries into $Zip"
