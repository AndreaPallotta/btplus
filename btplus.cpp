#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <ws2bth.h>
#include <Windows.h>
#include <bluetoothapis.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <thread>
#include <chrono>
#include <functional>
#include "btplus.h"

#pragma comment(lib, "Bthprops.lib")
#pragma comment(lib, "Ws2_32.lib")
#pragma warning(disable : 4996)

SERVICE_STATUS g_ServiceStatus = { 0 };
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE g_ServiceStopEvent = NULL;

BLUETOOTH_DEVICE_SEARCH_PARAMS CreateSearchParams(BOOL returnAuthenticated, BOOL returnUnknown)
{
    BLUETOOTH_DEVICE_SEARCH_PARAMS search_params;
    search_params.dwSize = sizeof(BLUETOOTH_DEVICE_SEARCH_PARAMS);
    search_params.fReturnRemembered = TRUE;
    search_params.fReturnAuthenticated = returnAuthenticated;
    search_params.fReturnUnknown = returnUnknown;
    search_params.fIssueInquiry = FALSE;
    search_params.cTimeoutMultiplier = 1;
    search_params.hRadio = NULL;

    return search_params;
}

BLUETOOTH_DEVICE_INFO CreateDeviceInfo()
{
    BLUETOOTH_DEVICE_INFO device_info;
    device_info.dwSize = sizeof(BLUETOOTH_DEVICE_INFO);

    return device_info;
}

std::string WStrToStr(const WCHAR* wstr)
{
    int length = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    std::string str(length, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &str[0], length, NULL, NULL);
    return str;
}

LPWSTR charArrToLpwstr(const char charArr[]) {
    wchar_t w_str[20];
    mbstowcs(w_str, charArr, strlen(charArr) + 1);
    LPWSTR lpw_str = w_str;

    return lpw_str;
}

std::string ToLower(const std::string& str)
{
    std::string lowerStr = str;
    std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(), [](unsigned char c) { return std::tolower(c); });
    return lowerStr;
}

void LogEvent(WORD type, const std::string& message)
{
    HANDLE hEventSource = RegisterEventSource(NULL, charArrToLpwstr("btplus"));
    if (hEventSource != NULL) {
        std::wstring wmessage(message.begin(), message.end());
        const wchar_t* messages[1] = { wmessage.c_str() };
        ReportEventW(
            hEventSource,
            type,
            0,
            0,
            NULL,
            1,
            0,
            messages,
            NULL
        );
        DeregisterEventSource(hEventSource);
    }
}

void EnumerateDevices(std::function<void(BLUETOOTH_DEVICE_INFO&)> callback, BOOL returnAuthenticated, BOOL returnUnknown)
{
    BLUETOOTH_DEVICE_SEARCH_PARAMS search_params = CreateSearchParams(returnAuthenticated, returnUnknown);
    BLUETOOTH_DEVICE_INFO device_info = CreateDeviceInfo();

    HBLUETOOTH_DEVICE_FIND hFind = BluetoothFindFirstDevice(&search_params, &device_info);
    if (hFind == NULL) {
        DWORD error = GetLastError();
        std::cerr << "Error finding devices: " << error << std::endl;
        return;
    }

    do {
        callback(device_info);
    } while (BluetoothFindNextDevice(hFind, &device_info));

    BluetoothFindDeviceClose(hFind);
}

void MonitorDevice(const std::string device_name)
{
    while (WaitForSingleObject(g_ServiceStopEvent, 0) != WAIT_OBJECT_0) {
        bool isConnected = false;
        bool found = false;
        ULONGLONG btAddr = 0;

        EnumerateDevices([&](BLUETOOTH_DEVICE_INFO& device_info) {
            std::string current_device_name = WStrToStr(device_info.szName);
            if (ToLower(current_device_name) == ToLower(device_name)) {
                found = true;
                if (device_info.fConnected) {
                    LogEvent(EVENTLOG_INFORMATION_TYPE, "Device " + device_name + " is already connected. Skipping...");
                    isConnected = true;
                }
                else {
                    btAddr = device_info.Address.ullLong;
                    LogEvent(EVENTLOG_INFORMATION_TYPE, "Device " + device_name + " is not connected. Attempting to connect...");
                }
            }
        }, TRUE, TRUE);

        if (found && !isConnected) {
            LogEvent(EVENTLOG_INFORMATION_TYPE, "Device " + device_name + " is not connected. Attempting to connect...");
            ConnectDeviceByAddr(btAddr, device_name);
        }

        std::this_thread::sleep_for(std::chrono::minutes(2));
    }
}

void ConnectDeviceByAddr(ULONGLONG btAddr, const std::string device_name)
{
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << std::endl;
        return;
    }

    SOCKET bt_socket = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
    if (bt_socket == INVALID_SOCKET) {
        std::cerr << "Error creating socket: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return;
    }

    SOCKADDR_BTH sock_addr = { 0 };
    sock_addr.addressFamily = AF_BTH;
    sock_addr.btAddr = btAddr;
    sock_addr.serviceClassId = RFCOMM_PROTOCOL_UUID;
    sock_addr.port = BT_PORT_ANY;

    result = connect(bt_socket, (SOCKADDR*)&sock_addr, sizeof(sock_addr));
    if (result == SOCKET_ERROR) {
        std::cerr << "Error connecting to device: " << WSAGetLastError() << std::endl;
        closesocket(bt_socket);
        WSACleanup();
        return;
    }

    LogEvent(EVENTLOG_INFORMATION_TYPE, "Successfully connected to " + device_name);

    closesocket(bt_socket);
    WSACleanup();
}

void ServiceWorkerThread(const std::string device_name)
{
    MonitorDevice(device_name);
}

void WINAPI ServiceCtrlHandler(DWORD CtrlCode)
{
    switch (CtrlCode) {
    case SERVICE_CONTROL_STOP:
        if (g_ServiceStatus.dwCurrentState != SERVICE_RUNNING) {
            break;
        }
        g_ServiceStatus.dwControlsAccepted = 0;
        g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

        SetEvent(g_ServiceStopEvent);
        break;
    default:
        break;
    }
}

void WINAPI ServiceMain(DWORD argc, LPSTR* argv)
{
    g_StatusHandle = RegisterServiceCtrlHandlerW(charArrToLpwstr("btplus"), ServiceCtrlHandler);
    if (g_StatusHandle == NULL) {
        return;
    }

    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwServiceSpecificExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 0;

    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (g_ServiceStopEvent == NULL) {
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        g_ServiceStatus.dwWin32ExitCode = GetLastError();
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        LogEvent(EVENTLOG_ERROR_TYPE, "Failed to start 'btplus'. Exiting...");
        return;
    }

    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    const std::string target_device = "Arctis Nova 7";
    std::thread worker_thread(ServiceWorkerThread, target_device);

    WaitForSingleObject(g_ServiceStopEvent, INFINITE);

    worker_thread.join();

    CloseHandle(g_ServiceStopEvent);

    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 3;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    LogEvent(EVENTLOG_INFORMATION_TYPE, "'btplus' started successfully.");
}

int main()
{
    LogEvent(EVENTLOG_INFORMATION_TYPE, "Starting 'btplus'...");
    SERVICE_TABLE_ENTRY ServiceTable[] = {
        {charArrToLpwstr("btplus"), (LPSERVICE_MAIN_FUNCTION)ServiceMain},
        {NULL, NULL}
    };

    if (StartServiceCtrlDispatcher(ServiceTable) == FALSE) {
        return GetLastError();
    }

    return 0;
}