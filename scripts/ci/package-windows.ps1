# Packages a Windows amd64 or arm64 release (ADR-0012): the MP client, the
# headless dedicated server, the OpenAL Soft and FFmpeg DLLs they import with
# the Visual C++ runtime DLLs those need, KisakCOD-README.txt,
# KisakCOD-LICENSE.txt and KisakCOD-THIRD-PARTY-NOTICES.txt. The text files
# carry a prefix because the installer can put them in the player's Call of
# Duty 4 folder. No Steam, Miles, Bink or game files. Writes
# <OutDir>\kisakcod-<version>-windows-<arch>.zip and, with -Installer, the Inno
# Setup installer built from the same folder (scripts/platform/windows/kisakcod.iss).
#
# -Notices is the THIRD-PARTY-NOTICES to ship, from
# scripts/packaging/third_party_notices.py --client with a --component for each
# bundled library (its license is <VcpkgInstalled>\share\<port>\copyright) and
# for the Visual C++ runtime (scripts/platform/windows/vcruntime-notice.txt).
# Packaging fails if a bundled DLL's component isn't named in it.
#
# Usage: package-windows.ps1 -Version 0.1.0 -Arch amd64 -ClientExe <bin>\KisakCOD-mp.exe
#            -DediExe <bin>\KisakCOD-dedi.exe -VcpkgInstalled <build>\vcpkg_installed\x64-windows
#            -Notices <file> -OutDir dist [-Installer [-Iscc <path>\ISCC.exe]]
#            [-CrtDir <redist>\Microsoft.VC145.CRT]
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $Version,
    [Parameter(Mandatory = $true)] [ValidateSet('amd64', 'arm64')] [string] $Arch,
    [Parameter(Mandatory = $true)] [string] $ClientExe,
    [Parameter(Mandatory = $true)] [string] $DediExe,
    [Parameter(Mandatory = $true)] [string] $VcpkgInstalled,
    [Parameter(Mandatory = $true)] [string] $Notices,
    [Parameter(Mandatory = $true)] [string] $OutDir,
    [switch] $Installer,
    [string] $Iscc = $env:KISAK_ISCC,
    [string] $CrtDir
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

if ($Version -notmatch '^(\d+)\.(\d+)\.(\d+)(-[0-9A-Za-z.-]+)?$') { throw "Version $Version is not X.Y.Z[-suffix]." }
$numericVersion = "$($Matches[1]).$($Matches[2]).$($Matches[3]).0"
$peArch = @{ amd64 = 'x64'; arm64 = 'ARM64' }[$Arch]
$stem = "kisakcod-$Version-windows-$Arch"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$stage = Join-Path $OutDir $stem
if (Test-Path -LiteralPath $stage) { throw "$stage already exists." }
New-Item -ItemType Directory -Path $stage | Out-Null

# The DLL names a PE file imports, delay-loaded ones included.
function Get-PeImports([string] $Path) {
    $b = [IO.File]::ReadAllBytes($Path)
    $pe = [BitConverter]::ToInt32($b, 0x3C)
    $sections = [BitConverter]::ToUInt16($b, $pe + 6)
    $opt = $pe + 24
    $dirs = $opt + $(if ([BitConverter]::ToUInt16($b, $opt) -eq 0x20B) { 112 } else { 96 })
    $table = $opt + [BitConverter]::ToUInt16($b, $pe + 20)
    $offset = {
        param([uint32] $rva)
        for ($i = 0; $i -lt $sections; ++$i) {
            $s = $table + 40 * $i
            $va = [BitConverter]::ToUInt32($b, $s + 12)
            if ($rva -ge $va -and $rva -lt $va + [BitConverter]::ToUInt32($b, $s + 16)) {
                return [int]($rva - $va + [BitConverter]::ToUInt32($b, $s + 20))
            }
        }
        throw "$Path`: RVA 0x$('{0:X}' -f $rva) is in no section."
    }
    $name = {
        param([uint32] $rva)
        $at = & $offset $rva
        $end = [Array]::IndexOf($b, [byte]0, $at)
        [Text.Encoding]::ASCII.GetString($b, $at, $end - $at)
    }
    # Directory 1 holds 20-byte import descriptors (name at +12); directory 13
    # holds 32-byte delay-load descriptors (name at +4).
    foreach ($d in @(@{ Index = 1; Size = 20; Name = 12 }, @{ Index = 13; Size = 32; Name = 4 })) {
        $rva = [BitConverter]::ToUInt32($b, $dirs + 8 * $d.Index)
        if ($rva -eq 0) { continue }
        for ($at = & $offset $rva; ; $at += $d.Size) {
            $nameRva = [BitConverter]::ToUInt32($b, $at + $d.Name)
            if ($nameRva -eq 0) { break }
            & $name $nameRva
        }
    }
}

# The component each bundled DLL belongs to, as the notices name it.
function Get-Component([string] $Dll) {
    switch -Regex ($Dll) {
        '^OpenAL32\.dll$' { return 'OpenAL Soft' }
        '^(avcodec|avformat|avutil|swresample|swscale)-\d+\.dll$' { return 'FFmpeg' }
        '^(vcruntime|msvcp|concrt)\d' { return 'Visual C++' }
    }
    throw "$Dll has no license mapping in package-windows.ps1."
}

$exes = @(Get-Item -LiteralPath $ClientExe, $DediExe)
& (Join-Path $PSScriptRoot 'verify-artifact-arch.ps1') -Expected $peArch $exes.FullName
if ($LASTEXITCODE) { throw "The executables are not $peArch." }
$exes | Copy-Item -Destination $stage

# The executables link the C runtime statically, but vcpkg's DLLs import the
# Visual C++ runtime DLLs, which Windows doesn't ship. Those come app-local
# from the newest Visual Studio redist folder for the target.
if (-not $CrtDir) {
    $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
    $CrtDir = Get-ChildItem "$vs\VC\Redist\MSVC\*\$($Arch -replace 'amd64', 'x64')\Microsoft.VC*.CRT" -Directory |
        Where-Object { $_.Parent.Parent.Name -match '^\d+(\.\d+)+$' } |
        Sort-Object { [version]$_.Parent.Parent.Name } | Select-Object -Last 1 -ExpandProperty FullName
    if (-not $CrtDir) { throw "No Visual C++ redist folder for $Arch under $vs; pass -CrtDir." }
}

# Bundle every DLL the executables reach that vcpkg built, and the runtime
# DLLs those need; everything else must be a system DLL.
$vcpkgBin = Join-Path $VcpkgInstalled 'bin'
$bundled = [Collections.Generic.SortedDictionary[string, string]]::new([StringComparer]::OrdinalIgnoreCase)
$queue = [Collections.Generic.Queue[string]]::new([string[]]$exes.FullName)
while ($queue.Count) {
    $from = $queue.Dequeue()
    foreach ($dll in Get-PeImports $from) {
        if ($dll -match '^(steam_api|mss|binkw|d3dx9|ucrtbased)') { throw "$from imports $dll." }
        $isCrt = $dll -match '^(vcruntime|msvcp|concrt)\d'
        if ($isCrt -and $from -in $exes.FullName) { throw "$from imports $dll; link the C runtime statically." }
        # -Filter gives the name's on-disk case.
        $file = Get-ChildItem -LiteralPath $(if ($isCrt) { $CrtDir } else { $vcpkgBin }) -Filter $dll -File
        if ($file) {
            if (-not $bundled.ContainsKey($file.Name)) { $bundled[$file.Name] = $file.FullName; $queue.Enqueue($file.FullName) }
        }
        elseif ($isCrt -or ($dll -notmatch '^(api|ext)-ms-' -and -not (Test-Path (Join-Path "$env:SystemRoot\System32" $dll)))) {
            throw "$from imports $dll, which is in neither $vcpkgBin, $CrtDir nor System32."
        }
    }
}
$bundled.Values | Copy-Item -Destination $stage

$noticesText = Get-Content -Raw -LiteralPath $Notices
foreach ($group in $bundled.Keys | Group-Object { Get-Component $_ }) {
    if ($noticesText -notmatch [regex]::Escape($group.Name)) {
        throw "$Notices has no license for $($group.Name) ($($group.Group -join ', '))."
    }
}
# The license texts mix LF and CRLF; ship CRLF throughout for Notepad.
[IO.File]::WriteAllText((Join-Path $stage 'KisakCOD-THIRD-PARTY-NOTICES.txt'), ($noticesText.TrimEnd() -replace "`r?`n", "`r`n") + "`r`n")
Copy-Item -LiteralPath (Join-Path $repo 'LICENSE') -Destination (Join-Path $stage 'KisakCOD-LICENSE.txt')
Set-Content -LiteralPath (Join-Path $stage 'KisakCOD-README.txt') -Encoding utf8NoBOM -Value @(
    "KisakCOD $Version for Windows $Arch",
    '',
    'KisakCOD is an open-source engine for Call of Duty 4: Modern Warfare',
    'multiplayer, compatible with Steam version 1.8. It needs your own copy of the',
    'game data. https://github.com/jm2/kisakcod',
    '',
    '  KisakCOD-mp.exe    the multiplayer client',
    '  KisakCOD-dedi.exe  the dedicated server, run from a command prompt',
    '',
    'Install KisakCOD into your Call of Duty 4 folder, beside iw3mp.exe, or',
    'copy the game''s files into the KisakCOD folder or into %APPDATA%\KisakCOD.',
    'Settings, profiles and logs go in %APPDATA%\KisakCOD.',
    '',
    'Sound uses OpenAL Soft and cinematics use FFmpeg, both included here as',
    'LGPL DLLs, with the Visual C++ runtime DLLs they need.',
    'KisakCOD-THIRD-PARTY-NOTICES.txt holds their licenses and those of the code',
    'built into the executables. KisakCOD itself is GPL-3.0 (KisakCOD-LICENSE.txt).'
)

# A fixed timestamp keeps the zip reproducible, as release.yml's headless legs are.
$epoch = if ($env:SOURCE_DATE_EPOCH) { $env:SOURCE_DATE_EPOCH } else { git -C $repo log -1 --format=%ct }
$zip = Join-Path $OutDir "$stem.zip"
python (Join-Path $PSScriptRoot 'release_meta.py') zip --root $OutDir --stem $stem --epoch $epoch --out $zip
if ($LASTEXITCODE) { throw 'Writing the zip failed.' }
$outputs = @($zip)

if ($Installer) {
    if (-not $Iscc) { $Iscc = (Get-Command ISCC.exe -ErrorAction Stop).Source }
    & $Iscc /Q "/DAppVersion=$Version" "/DAppNumericVersion=$numericVersion" "/DSourceDir=$stage" `
        "/DOutputDir=$OutDir" "/DOutputBaseFilename=$stem-setup" "/DTargetArch=$(@{ amd64 = 'x64'; arm64 = 'arm64' }[$Arch])" `
        (Join-Path $repo 'scripts\platform\windows\kisakcod.iss')
    if ($LASTEXITCODE) { throw 'The Inno Setup compiler failed.' }
    $outputs += Join-Path $OutDir "$stem-setup.exe"
}

Get-ChildItem -LiteralPath $stage | ForEach-Object { '{0,-36} {1,10}' -f $_.Name, $_.Length }
foreach ($file in $outputs) { '{0}  {1}' -f (Get-FileHash -LiteralPath $file).Hash.ToLowerInvariant(), (Split-Path -Leaf $file) }
