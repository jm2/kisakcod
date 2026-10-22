# Architecture truth gate for Windows targets: parses the PE machine type
# directly so the check needs no VS dev environment (no dumpbin on PATH) and
# cannot be fooled by a runner silently falling back to emulation. Fails
# closed unless EVERY named binary reports the expected machine type.
#
# Usage: verify-artifact-arch.ps1 -Expected <x86|x64|ARM64> <file> [<file>...]
#
# Runs in ConstrainedLanguage mode: the header is read with Get-Content and
# decoded with integer arithmetic, since that mode blocks the .NET file,
# BitConverter, Encoding and Console calls.
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$Expected,
  [Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)][string[]]$Binary
)

$ErrorActionPreference = "Stop"

$machineByName = @{
  x86   = 0x014C  # IMAGE_FILE_MACHINE_I386
  x64   = 0x8664  # IMAGE_FILE_MACHINE_AMD64
  ARM64 = 0xAA64  # IMAGE_FILE_MACHINE_ARM64
}
if (-not $machineByName.ContainsKey($Expected)) {
  Write-Error -ErrorAction Continue "unknown expected architecture '$Expected' (use x86, x64, or ARM64)"
  exit 2
}
$want = $machineByName[$Expected]

# The first $Count bytes of $Path (fewer if the file is shorter).
function Read-Head {
  param([string]$Path, [int]$Count)
  if ($PSVersionTable.PSVersion.Major -ge 6) {
    $head = Get-Content -LiteralPath $Path -AsByteStream -TotalCount $Count -ReadCount 0
  }
  else {
    $head = Get-Content -LiteralPath $Path -Encoding Byte -TotalCount $Count -ReadCount 0
  }
  return , [byte[]]@($head)
}

function Get-UInt16LE { param([byte[]]$Bytes, [int]$At) $Bytes[$At] + 256 * $Bytes[$At + 1] }
function Get-Int32LE {
  param([byte[]]$Bytes, [int]$At)
  $value = [long](Get-UInt16LE $Bytes $At) + 65536L * (Get-UInt16LE $Bytes ($At + 2))
  if ($value -ge 2147483648L) { $value -= 4294967296L }
  $value
}

$failed = $false
foreach ($path in $Binary) {
  try {
    if (-not (Test-Path $path)) { throw "does not exist" }
    $bytes = Read-Head $path 4096
    if ($bytes.Length -lt 0x40) { throw "too small to hold a DOS header" }
    $peOffset = Get-Int32LE $bytes 0x3C
    if ($peOffset -ge 0 -and ($peOffset + 6) -gt $bytes.Length -and $bytes.Length -eq 4096) {
      # The PE header sits past the first page; read up to it.
      $bytes = Read-Head $path ($peOffset + 6)
    }
    if ($peOffset -lt 0 -or ($peOffset + 6) -gt $bytes.Length) { throw "bad PE offset" }
    if ($bytes[$peOffset] -ne 0x50 -or $bytes[$peOffset + 1] -ne 0x45 -or
        $bytes[$peOffset + 2] -ne 0 -or $bytes[$peOffset + 3] -ne 0) { throw "missing PE signature" }
    $machine = Get-UInt16LE $bytes ($peOffset + 4)
    if ($machine -ne $want) {
      throw ("machine 0x{0:X4} != expected 0x{1:X4}" -f $machine, $want)
    }
    # Write-Output, not Write-Host: Codacy ACTION_REQUIRED (PSAvoidUsingWriteHost);
    # the success stream reaches the CI log identically for direct script invocation.
    Write-Output "ok: $path reports $Expected"
  }
  catch {
    # A plain Write-Error would throw under $ErrorActionPreference = "Stop";
    # record the failure without stopping and let the exit code carry the verdict.
    Write-Error -ErrorAction Continue "FAIL: $path $($_.Exception.Message)"
    $failed = $true
  }
}

if ($failed) { exit 1 } else { exit 0 }
