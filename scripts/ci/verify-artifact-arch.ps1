# Architecture truth gate for Windows targets (CI_RELEASE_WORKFLOW_AUDIT
# gap 16): parses the PE machine type directly so the check needs no VS dev
# environment (no dumpbin on PATH) and cannot be fooled by a runner silently
# falling back to emulation. Fails closed unless EVERY named binary reports
# the expected machine type.
#
# Usage: verify-artifact-arch.ps1 -Expected <x86|x64|ARM64> <file> [<file>...]
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
  [Console]::Error.WriteLine("unknown expected architecture '$Expected' (use x86, x64, or ARM64)")
  exit 2
}
$want = $machineByName[$Expected]

$failed = $false
foreach ($path in $Binary) {
  try {
    if (-not (Test-Path $path)) { throw "does not exist" }
    $bytes = [System.IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 0x40) { throw "too small to hold a DOS header" }
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($peOffset -lt 0 -or ($peOffset + 6) -gt $bytes.Length) { throw "bad PE offset" }
    $sig = [System.Text.Encoding]::ASCII.GetString($bytes, $peOffset, 4)
    if ($sig -ne "PE`0`0") { throw "missing PE signature" }
    $machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
    if ($machine -ne $want) {
      throw ("machine 0x{0:X4} != expected 0x{1:X4}" -f $machine, $want)
    }
    # Write-Output, not Write-Host: Codacy ACTION_REQUIRED (PSAvoidUsingWriteHost);
    # the success stream reaches the CI log identically for direct script invocation.
    Write-Output "ok: $path reports $Expected"
  }
  catch {
    # Write-Error would throw under $ErrorActionPreference = "Stop"; record
    # the failure on stderr and let the final exit code carry the verdict.
    [Console]::Error.WriteLine("FAIL: $path $($_.Exception.Message)")
    $failed = $true
  }
}

if ($failed) { exit 1 } else { exit 0 }
