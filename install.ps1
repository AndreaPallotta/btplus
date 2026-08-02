# install.ps1 — Register btplus as a Windows startup task
# Run this script once (as administrator) after building btplus.exe.
# It creates a Task Scheduler task that launches btplus at every login.

#Requires -RunAsAdministrator

$taskName   = "btplus"
$exePath    = Join-Path $PSScriptRoot "btplus.exe"
$iniPath    = Join-Path $PSScriptRoot "btplus.ini"

# Verify the executable exists
if (-not (Test-Path $exePath)) {
    Write-Error "btplus.exe not found at: $exePath`nBuild the project first (Release|x64)."
    exit 1
}

# Verify the config file exists
if (-not (Test-Path $iniPath)) {
    Write-Error "btplus.ini not found at: $iniPath`nCreate it before installing."
    exit 1
}

# Remove any existing task with this name
$existing = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
if ($existing) {
    Write-Host "Removing existing task '$taskName'..." -ForegroundColor Yellow
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
}

# Build task components
$action   = New-ScheduledTaskAction -Execute $exePath
$trigger  = New-ScheduledTaskTrigger -AtLogOn
$settings = New-ScheduledTaskSettingsSet `
    -ExecutionTimeLimit (New-TimeSpan -Hours 0) `   # No time limit (runs indefinitely)
    -MultipleInstances IgnoreNew `                  # Don't start a second instance
    -StartWhenAvailable                             # Run ASAP if missed

$principal = New-ScheduledTaskPrincipal `
    -UserId ([System.Security.Principal.WindowsIdentity]::GetCurrent().Name) `
    -LogonType Interactive `
    -RunLevel Highest   # Elevated — required to call BluetoothSetServiceState

# Register the task
Register-ScheduledTask `
    -TaskName $taskName `
    -Action   $action   `
    -Trigger  $trigger  `
    -Settings $settings `
    -Principal $principal `
    -Description "btplus — automatically connects Bluetooth audio devices at login." `
    -Force | Out-Null

Write-Host ""
Write-Host "btplus installed successfully." -ForegroundColor Green
Write-Host ""
Write-Host "  Task name : $taskName"
Write-Host "  Executable: $exePath"
Write-Host "  Trigger   : At every login"
Write-Host ""
Write-Host "btplus will start automatically on next login."
Write-Host "To start it now, run: Start-ScheduledTask -TaskName '$taskName'"
