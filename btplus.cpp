#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <bluetoothapis.h>
#include <bthdef.h>
#include <shellapi.h>
#include <dbt.h>
#include <string>
#include <algorithm>
#include <cctype>
#include <functional>
#include <thread>
#include <chrono>
#include <atomic>

#pragma comment(lib, "Bthprops.lib")
#pragma comment(lib, "Shell32.lib")

// ─── Bluetooth profile UUIDs ─────────────────────────────────────────────────
// These are standard Bluetooth SIG UUIDs. AudioSinkServiceClass_UUID is usually
// defined in bthdef.h, but HandsFreeServiceClass_UUID is absent from many
// Windows SDK versions — define both defensively with #ifndef guards.

#ifndef AudioSinkServiceClass_UUID
// A2DP Audio Sink — 0x110B
static const GUID AudioSinkServiceClass_UUID = {
    0x110B, 0x0000, 0x1000,
    { 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB }
};
#endif

#ifndef HandsFreeServiceClass_UUID
// HFP Hands-Free — 0x111E
static const GUID HandsFreeServiceClass_UUID = {
    0x111E, 0x0000, 0x1000,
    { 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB }
};
#endif

// ─── Constants ───────────────────────────────────────────────────────────────

// Custom window messages
#define WM_TRAY_ICON    (WM_APP + 1)
#define WM_TRY_CONNECT  (WM_APP + 2)

// Timer ID for fallback poll
#define IDT_POLL_TIMER  1

static const int DEFAULT_POLL_SECONDS = 30;
static const int MAX_RETRIES          = 3;
static const int RETRY_DELAY_SECONDS  = 10;

// Bluetooth port device interface GUID for RegisterDeviceNotification.
// {0850302A-9527-4572-8003-1AA9C4C7C64D}
static const GUID GUID_BTHPORT_DEVICE_INTERFACE = {
    0x0850302A, 0x9527, 0x4572,
    { 0x80, 0x03, 0x1A, 0xA9, 0xC4, 0xC7, 0xC6, 0x4D }
};

// ─── Globals ─────────────────────────────────────────────────────────────────

static std::string       g_DeviceName;
static int               g_PollIntervalMs = DEFAULT_POLL_SECONDS * 1000;
static HWND              g_hWnd           = NULL;
static HDEVNOTIFY        g_hDevNotify     = NULL;
static std::atomic<bool> g_ConnectBusy    { false };

// ─── Utility ─────────────────────────────────────────────────────────────────

std::string WStrToStr(const WCHAR* wstr)
{
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &s[0], len, NULL, NULL);
    return s;
}

std::wstring StrToWStr(const std::string& str)
{
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    std::wstring ws(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &ws[0], len);
    return ws;
}

std::string ToLower(const std::string& str)
{
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return lower;
}

// ─── Config ──────────────────────────────────────────────────────────────────

struct Config
{
    std::string deviceName;
    int         pollIntervalSeconds = DEFAULT_POLL_SECONDS;
};

Config ReadConfig()
{
    Config cfg;

    // Resolve btplus.ini path relative to the executable
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(path, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';
    wcscat_s(path, L"btplus.ini");

    wchar_t buf[256] = {};
    GetPrivateProfileStringW(L"btplus", L"device", L"", buf, 256, path);
    cfg.deviceName = WStrToStr(buf);

    int secs = (int)GetPrivateProfileIntW(
        L"btplus", L"poll_interval_seconds", DEFAULT_POLL_SECONDS, path);
    cfg.pollIntervalSeconds = max(secs, 5); // enforce a minimum of 5 seconds

    return cfg;
}

// ─── Notifications ───────────────────────────────────────────────────────────

// Show a Windows tray balloon notification (surfaces as a modern Action Center
// toast on Windows 10/11). flags: NIIF_INFO, NIIF_WARNING, or NIIF_ERROR.
void ShowNotification(const std::wstring& title, const std::wstring& body, DWORD flags)
{
    if (!g_hWnd) return;

    NOTIFYICONDATAW nid = {};
    nid.cbSize      = sizeof(nid);
    nid.hWnd        = g_hWnd;
    nid.uID         = 1;
    nid.uFlags      = NIF_INFO;
    nid.dwInfoFlags = flags | NIIF_NOSOUND;
    nid.uTimeout    = 6000;
    wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo,      body.c_str(),  _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

// ─── Bluetooth ───────────────────────────────────────────────────────────────

void EnumerateDevices(std::function<void(BLUETOOTH_DEVICE_INFO&)> callback,
                      BOOL returnAuthenticated, BOOL returnUnknown)
{
    BLUETOOTH_DEVICE_SEARCH_PARAMS params = {};
    params.dwSize              = sizeof(params);
    params.fReturnRemembered   = TRUE;
    params.fReturnAuthenticated = returnAuthenticated;
    params.fReturnUnknown      = returnUnknown;
    params.fIssueInquiry       = FALSE;
    params.cTimeoutMultiplier  = 1;
    params.hRadio              = NULL;

    BLUETOOTH_DEVICE_INFO info = {};
    info.dwSize = sizeof(info);

    HBLUETOOTH_DEVICE_FIND hFind = BluetoothFindFirstDevice(&params, &info);
    if (!hFind) return;

    do { callback(info); } while (BluetoothFindNextDevice(hFind, &info));
    BluetoothFindDeviceClose(hFind);
}

// Activate the A2DP (stereo audio) and HFP (hands-free/mic) profiles.
// Returns true if at least one profile was enabled successfully.
bool EnableAudioProfiles(BLUETOOTH_DEVICE_INFO& info)
{
    bool ok = false;
    if (BluetoothSetServiceState(NULL, &info, &AudioSinkServiceClass_UUID,
        BLUETOOTH_SERVICE_ENABLE) == ERROR_SUCCESS) ok = true;
    if (BluetoothSetServiceState(NULL, &info, &HandsFreeServiceClass_UUID,
        BLUETOOTH_SERVICE_ENABLE) == ERROR_SUCCESS) ok = true;
    return ok;
}

// Background thread: locate the target device and connect it with retries.
// Posts a success or failure toast when done.
void ConnectThread(std::string device_name)
{
    for (int attempt = 1; attempt <= MAX_RETRIES; ++attempt)
    {
        bool found       = false;
        bool alreadyConn = false;
        BLUETOOTH_DEVICE_INFO target = {};
        target.dwSize = sizeof(target);

        EnumerateDevices([&](BLUETOOTH_DEVICE_INFO& info) {
            if (ToLower(WStrToStr(info.szName)) == ToLower(device_name)) {
                found = true;
                if (info.fConnected) alreadyConn = true;
                else                 target = info;
            }
        }, TRUE, TRUE);

        // Already connected, or device isn't in the paired list yet — nothing to do.
        if (!found || alreadyConn) {
            g_ConnectBusy = false;
            return;
        }

        if (EnableAudioProfiles(target)) {
            ShowNotification(
                L"Bluetooth Connected",
                StrToWStr(device_name) + L" connected successfully.",
                NIIF_INFO);
            g_ConnectBusy = false;
            return;
        }

        // Connection failed — wait before the next attempt, unless this was the last one.
        if (attempt < MAX_RETRIES) {
            std::this_thread::sleep_for(std::chrono::seconds(RETRY_DELAY_SECONDS));
        }
    }

    // All retries exhausted
    ShowNotification(
        L"Bluetooth \u2014 Connection Failed",
        L"Could not connect " + StrToWStr(device_name) +
        L" after " + std::to_wstring(MAX_RETRIES) + L" attempts.",
        NIIF_ERROR);

    g_ConnectBusy = false;
}

// Spawns a ConnectThread if one isn't already running (atomic guard).
void TriggerConnect()
{
    bool expected = false;
    if (!g_ConnectBusy.compare_exchange_strong(expected, true)) return;
    std::thread(ConnectThread, g_DeviceName).detach();
}

// ─── Window / Message Loop ───────────────────────────────────────────────────

void AddTrayIcon(HWND hWnd)
{
    NOTIFYICONDATAW nid  = {};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = hWnd;
    nid.uID              = 1;
    nid.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_TRAY_ICON;
    nid.hIcon            = LoadIconW(NULL, IDI_APPLICATION);
    wcsncpy_s(nid.szTip, L"btplus \u2014 Bluetooth Auto-Connect", _TRUNCATE);
    Shell_NotifyIconW(NIM_ADD, &nid);

    // Enable NOTIFYICON_VERSION_4 for modern balloon-to-toast behavior
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void RemoveTrayIcon(HWND hWnd)
{
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = hWnd;
    nid.uID    = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_hWnd = hWnd;
        AddTrayIcon(hWnd);

        // Subscribe to Bluetooth device-arrival events so we can react
        // the instant the headphones are detected, rather than waiting for
        // the next poll cycle.
        DEV_BROADCAST_DEVICEINTERFACE_W dbi = {};
        dbi.dbcc_size       = sizeof(dbi);
        dbi.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        dbi.dbcc_classguid  = GUID_BTHPORT_DEVICE_INTERFACE;
        g_hDevNotify = RegisterDeviceNotificationW(
            hWnd, &dbi, DEVICE_NOTIFY_WINDOW_HANDLE);

        // Fallback poll timer in case device-change events are missed
        SetTimer(hWnd, IDT_POLL_TIMER, g_PollIntervalMs, NULL);

        // Run an initial check at startup in case the headphones are
        // already on but not connected (e.g. PC rebooted)
        PostMessageW(hWnd, WM_TRY_CONNECT, 0, 0);
        return 0;
    }

    case WM_TRY_CONNECT:
        TriggerConnect();
        return 0;

    case WM_TIMER:
        if (wParam == IDT_POLL_TIMER)
            TriggerConnect();
        return 0;

    case WM_DEVICECHANGE:
        // DBT_DEVICEARRIVAL fires when the OS detects a new device on
        // a registered interface. Trigger an immediate connection check
        // so the headphones connect without waiting for the poll timer.
        if (wParam == DBT_DEVICEARRIVAL)
            PostMessageW(hWnd, WM_TRY_CONNECT, 0, 0);
        return TRUE;

    case WM_DESTROY:
        KillTimer(hWnd, IDT_POLL_TIMER);
        if (g_hDevNotify) {
            UnregisterDeviceNotification(g_hDevNotify);
            g_hDevNotify = NULL;
        }
        RemoveTrayIcon(hWnd);
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

// ─── Entry Point ─────────────────────────────────────────────────────────────

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    // Prevent multiple instances from running simultaneously
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"btplus_single_instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 0;
    }

    // Read btplus.ini from the executable's directory
    Config cfg = ReadConfig();
    if (cfg.deviceName.empty()) {
        MessageBoxW(NULL,
            L"btplus.ini is missing or has no 'device' key.\n\n"
            L"Place btplus.ini next to btplus.exe with the following content:\n\n"
            L"    [btplus]\n"
            L"    device=Your Headphone Name\n"
            L"    poll_interval_seconds=30",
            L"btplus \u2014 Configuration Error",
            MB_ICONERROR | MB_OK);
        CloseHandle(hMutex);
        return 1;
    }
    g_DeviceName     = cfg.deviceName;
    g_PollIntervalMs = cfg.pollIntervalSeconds * 1000;

    // Register a minimal window class for our hidden background window
    WNDCLASSEXW wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = L"btplus_window";
    if (!RegisterClassExW(&wc)) {
        CloseHandle(hMutex);
        return GetLastError();
    }

    // Create a hidden (never shown) window. A real HWND is required by both
    // Shell_NotifyIcon and RegisterDeviceNotification.
    HWND hWnd = CreateWindowExW(
        0, L"btplus_window", L"btplus",
        WS_OVERLAPPED, 0, 0, 0, 0,
        NULL, NULL, hInstance, NULL);

    if (!hWnd) {
        CloseHandle(hMutex);
        return GetLastError();
    }

    // Standard message pump
    MSG msg = {};
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CloseHandle(hMutex);
    return (int)msg.wParam;
}