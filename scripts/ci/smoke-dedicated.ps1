[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$Executable,

    [string]$GameDirectory = $env:KISAKCOD_GAME_DIR,

    [string]$Map = 'mp_backlot',

    [ValidateRange(1024, 65535)]
    [int]$Port = 28961,

    [ValidateRange(5, 120)]
    [int]$TimeoutSeconds = 45
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($GameDirectory) -or -not (Test-Path -LiteralPath $GameDirectory -PathType Container)) {
    throw 'GameDirectory must point to a licensed COD4 data installation.'
}

$Executable = (Resolve-Path -LiteralPath $Executable).Path
$GameDirectory = (Resolve-Path -LiteralPath $GameDirectory).Path
$serverIdentity = "kisakcod-smoke-$([Guid]::NewGuid().ToString('N'))"
$homePath = Join-Path ([IO.Path]::GetTempPath()) $serverIdentity
$stdout = Join-Path $homePath 'server.stdout.log'
$stderr = Join-Path $homePath 'server.stderr.log'
New-Item -ItemType Directory -Force -Path $homePath | Out-Null

$arguments = @(
    '+set', 'dedicated', '2',
    '+set', 'net_ip', '127.0.0.1',
    '+set', 'net_port', "$Port",
    '+set', 'fs_basepath', "`"$GameDirectory`"",
    '+set', 'fs_homepath', "`"$homePath`"",
    '+set', 'sv_hostname', $serverIdentity,
    '+set', 'logfile', '2',
    '+map', $Map
)

$process = $null
try {
    $reservation = [Net.Sockets.UdpClient]::new()
    try {
        $reservation.Client.ExclusiveAddressUse = $true
        $reservation.Client.Bind(
            [Net.IPEndPoint]::new([Net.IPAddress]::Loopback, $Port))
    }
    catch [Net.Sockets.SocketException] {
        throw "UDP port $Port is not available for an isolated smoke test."
    }
    finally {
        $reservation.Dispose()
    }

    $process = Start-Process -FilePath $Executable -ArgumentList $arguments `
        -WorkingDirectory $GameDirectory -PassThru `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $request = [byte[]](0xff, 0xff, 0xff, 0xff) +
        [Text.Encoding]::ASCII.GetBytes("getstatus`n")
    $expectedMap = "\mapname\$Map"
    $expectedIdentity = "\sv_hostname\$serverIdentity"
    $passed = $false

    while ([DateTime]::UtcNow -lt $deadline) {
        if ($process.HasExited) {
            throw "Dedicated server exited early with code $($process.ExitCode)."
        }

        $udp = [Net.Sockets.UdpClient]::new()
        try {
            $udp.Client.ReceiveTimeout = 1000
            $udp.Connect('127.0.0.1', $Port)
            [void]$udp.Send($request, $request.Length)
            $remote = [Net.IPEndPoint]::new([Net.IPAddress]::Any, 0)
            $response = $udp.Receive([ref]$remote)
            $text = [Text.Encoding]::ASCII.GetString($response)
            if ($text.Contains('statusResponse') -and
                $text.Contains($expectedMap) -and
                $text.Contains($expectedIdentity)) {
                $passed = $true
                break
            }
        }
        catch [Net.Sockets.SocketException] {
            # The server is still starting.
        }
        finally {
            $udp.Dispose()
        }
        Start-Sleep -Milliseconds 500
    }

    if (-not $passed) {
        throw "Dedicated server did not return this run's map and identity within $TimeoutSeconds seconds."
    }
    Write-Host "Dedicated server smoke test passed for map '$Map' on UDP port $Port."
}
finally {
    if ($null -ne $process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit()
    }
    if (Test-Path -LiteralPath $stdout) {
        Get-Content -LiteralPath $stdout -Tail 200
    }
    if (Test-Path -LiteralPath $stderr) {
        Get-Content -LiteralPath $stderr -Tail 200
    }
    if (Test-Path -LiteralPath $homePath) {
        Get-ChildItem -LiteralPath $homePath -Filter console_mp.log -File -Recurse `
            -ErrorAction SilentlyContinue | ForEach-Object {
                Write-Host "Tail of $($_.FullName):"
                Get-Content -LiteralPath $_.FullName -Tail 200
            }
        Remove-Item -LiteralPath $homePath -Recurse -Force -ErrorAction SilentlyContinue
    }
}
