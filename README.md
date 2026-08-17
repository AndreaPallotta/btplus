# btplus

Auto-connect a Bluetooth audio device whenever it turns on, without touching Bluetooth Settings.

## How it works

btplus runs silently in the background as a login startup task. It:

1. Watches for Bluetooth device-arrival events via `WM_DEVICECHANGE`
2. As a fallback, polls on a configurable interval (default: 30 s)
3. When your headphones are found but not connected, calls `BluetoothSetServiceState` to enable A2DP (stereo audio) and HFP (microphone/hands-free)
4. Shows a Windows notification on success or failure
5. Retries up to 3 times (10 s apart) if the first attempt fails

## Install (recommended: MSI wizard)

Download `btplus-vX.X.X.msi` from the [Releases](../../releases) page and run it.

The wizard will ask you to configure:

| Setting | Description | Default |
|---|---|---|
| **Bluetooth Device Name** | Exact name shown in Bluetooth Settings (case-insensitive) | `Arctis Nova 7` |
| **Fallback Poll Interval** | Seconds between background scans | `30` |

The installer will:
- Copy `btplus.exe` to `Program Files\btplus\`
- Write `btplus.ini` with your configured values
- Add the install folder to the system `PATH`
- Register a **Task Scheduler** startup task that runs btplus at every login (elevated)

To start it immediately without rebooting:

```powershell
Start-ScheduledTask -TaskName "btplus"
```

### Uninstall

Use **Add or Remove Programs** - the uninstaller automatically stops btplus and removes the scheduled task.

---

## Install (portable / manual)

Download `btplus-vX.X.X-portable.zip`, extract it anywhere, then:

### 1. Configure

Edit `btplus.ini`:

```ini
[btplus]
device=Arctis Nova 7
poll_interval_seconds=30
```

### 2. Register the startup task

Run as **Administrator**:

```powershell
.\install.ps1
```

### Start immediately

```powershell
Start-ScheduledTask -TaskName "btplus"
```

### Uninstall (portable)

```powershell
Stop-ScheduledTask  -TaskName "btplus"
Unregister-ScheduledTask -TaskName "btplus" -Confirm:$false
```

---

## Building from source

Requires: Visual Studio 2022, .NET SDK, WiX v4

```powershell
# Build the executable
msbuild btplus.sln /p:Configuration=Release /p:Platform=x64

# Install WiX (once)
dotnet tool install --global wix
wix extension add WixToolset.UI.wixext
wix extension add WixToolset.Util.wixext

# Build the MSI
wix build installer/btplus.wxs installer/BtConfigDlg.wxs `
  -ext WixToolset.UI.wixext `
  -ext WixToolset.Util.wixext `
  -d Version=1.0.0 `
  -o btplus.msi
```

## Files

| File | Description |
|---|---|
| `btplus.cpp` | Main source - message loop, BT detection, notifications |
| `btplus.ini` | Runtime configuration (device name, poll interval) |
| `installer/btplus.wxs` | WiX package: components, custom actions, UI wiring |
| `installer/BtConfigDlg.wxs` | Custom "Configure btplus" wizard dialog |
| `install.ps1` | Manual Task Scheduler registration (portable install) |
| `.github/workflows/release.yml` | CI/CD pipeline: builds exe + MSI, creates GitHub Release |
