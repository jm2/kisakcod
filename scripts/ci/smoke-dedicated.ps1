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
$homePath = Join-Path ([IO.Path]::GetTempPath()) "kisakcod-smoke-$PID"
$stdout = Join-Path $homePath 'server.stdout.log'
$stderr = Join-Path $homePath 'server.stderr.log'
New-Item -ItemType Directory -Force -Path $homePath | Out-Null

$arguments = @(
    '+set', 'dedicated', '2',
    '+set', 'net_ip', '127.0.0.1',
    '+set', 'net_port', "$Port",
    '+set', 'fs_basepath', "`"$GameDirectory`"",
    '+set', 'fs_homepath', "`"$homePath`"",
    '+map', $Map
)

$process = Start-Process -FilePath $Executable -ArgumentList $arguments `
    -WorkingDirectory $GameDirectory -PassThru `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr

try {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $request = [byte[]](0xff, 0xff, 0xff, 0xff) +
        [Text.Encoding]::ASCII.GetBytes("getstatus`n")

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
            if ($text.Contains('statusResponse')) {
                Write-Host "Dedicated server smoke test passed on UDP port $Port."
                exit 0
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

    throw "Dedicated server did not answer getstatus within $TimeoutSeconds seconds."
}
finally {
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit()
    }
    if (Test-Path -LiteralPath $stdout) {
        Get-Content -LiteralPath $stdout -Tail 200
    }
    if (Test-Path -LiteralPath $stderr) {
        Get-Content -LiteralPath $stderr -Tail 200
    }
}
