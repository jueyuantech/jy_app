param(
    [string]$Device = "",
    [int]$Port = 24680,
    [string]$Adb = "adb",
    [string]$Connect = ""
)

$ErrorActionPreference = "Stop"

function Invoke-Adb {
    param([string[]]$Arguments)

    $output = & $Adb @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "adb $($Arguments -join ' ') failed with exit code ${exitCode}: $output"
    }

    return $output
}

function Get-AdbDevices {
    $lines = Invoke-Adb @("devices")
    $devices = @()

    foreach ($line in $lines) {
        if ($line -match "^(\S+)\s+device$") {
            $devices += $Matches[1]
        }
    }

    return $devices
}

function Select-AdbDevice {
    param([string[]]$Devices)

    Write-Host "ADB devices:"
    for ($i = 0; $i -lt $Devices.Count; $i++) {
        Write-Host ("  [{0}] {1}" -f ($i + 1), $Devices[$i])
    }

    while ($true) {
        $raw = Read-Host "Select device number"
        $index = 0

        if ([int]::TryParse($raw, [ref]$index) -and $index -ge 1 -and $index -le $Devices.Count) {
            return $Devices[$index - 1]
        }

        Write-Host "Invalid selection. Enter a number from 1 to $($Devices.Count)."
    }
}

try {
    if ($Connect -ne "") {
        Write-Host "Connecting adb device: $Connect"
        Invoke-Adb @("connect", $Connect) | Write-Host
    }

    $devices = Get-AdbDevices
    if ($devices.Count -eq 0) {
        throw "No adb device found. Start LDPlayer, then run: adb devices"
    }

    if ($Device -eq "") {
        $Device = Select-AdbDevice $devices
    }

    Write-Host "Using adb device: $Device"
    Write-Host "Forwarding local tcp:$Port -> device tcp:$Port"

    try {
        Invoke-Adb @("-s", $Device, "forward", "--remove", "tcp:$Port") | Out-Null
    } catch {
        # It is fine when no previous mapping exists for this device.
    }

    Invoke-Adb @("-s", $Device, "forward", "tcp:$Port", "tcp:$Port") | Write-Host

    Write-Host ""
    Write-Host "Current forward list:"
    Invoke-Adb @("-s", $Device, "forward", "--list") | Write-Host
} catch {
    Write-Host "Error: $_" -ForegroundColor Red
} finally {
    Write-Host ""
    Read-Host "Press Enter to exit"
}
