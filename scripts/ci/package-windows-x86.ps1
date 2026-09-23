# Packages a Windows x86 bin directory for release.yml: everything except
# Miles (mss32.dll and the miles/ plugin directory), plus a short README.
# Miles and Bink are not redistributed; steam_api.dll stays until the owner's
# legal decision (docs/NOW.md, Blocked on owner).
#
# Usage: package-windows-x86.ps1 -BinDir bin/Release -Zip dist/<name>.zip
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $BinDir,
    [Parameter(Mandatory = $true)] [string] $Zip
)
$ErrorActionPreference = 'Stop'

$excluded = @('mss32.dll', 'miles')
Set-Content -LiteralPath (Join-Path $BinDir 'README.txt') -Encoding utf8 -Value @(
    'KisakCOD for Windows x86: multiplayer client and dedicated server.',
    'Copy these files into a licensed Call of Duty 4 installation.',
    'Miles and Bink come from the user''s own retail install.'
)
$items = Get-ChildItem -LiteralPath $BinDir |
    Where-Object { $excluded -notcontains $_.Name }
Compress-Archive -LiteralPath $items.FullName -DestinationPath $Zip
Write-Host "Packaged $($items.Count) entries into $Zip (Miles excluded)"
