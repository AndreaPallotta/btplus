#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <ws2bth.h>
#include <Windows.h>
#include <bluetoothapis.h>
#include <iostream>
#include <iomanip>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <functional>
#include "btplus.h"

#pragma comment(lib, "Bthprops.lib")
#pragma comment(lib, "Ws2_32.lib")

int main(int argc, char* argv[])
{
    if (argc < 2) {
        ShowHelp();
        return 1;
    }

    std::string command = argv[1];

    if ((command == "-show" || command == "-connect" || command == "-disconnect") && argc != 3) {
        ShowHelp();
        return 1;
    }

    if (command == "-showall") {
        GetDevices("");
    }
    else if (command == "-show") {
        GetDevices(ToLower(argv[2]));
    }
    else if (command == "-connect") {
        ConnectDeviceByName(ToLower(argv[2]));
    }
    else if (command == "-disconnect") {
        DisconnectDeviceByName(ToLower(argv[2]));
    }
    else if (command == "-help") {
        ShowHelp();
    }
    else {
        std::cerr << "Invalid command" << std::endl;
        ShowHelp();
        return 0;
    }

    return 0;
}

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

std::string ToLower(const std::string& str)
{
    std::string lowerStr = str;
    std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(), [](unsigned char c) { return std::tolower(c); });
    return lowerStr;
}

void ShowHelp()
{
    std::cout << "btplus Usage: 'btplus [-showall] [-show <device_name>] [-connect <device_name>] [-disconnect <device_name]'" << std::endl;
    std::cout << "\t[-showall]                 : Display all remembered or connected devices" << std::endl;
    std::cout << "\t[-show <device_name>]      : Display all remembered or connected devices that contain/match the <device_name>" << std::endl;
    std::cout << "\t[-connect <device_name>]   : Connect device that contains/matches the <device_name>" << std::endl;
    std::cout << "\t[-disconnect <device_name>]: Disconnect device that contains/matches the <device_name>" << std::endl;
}

void DisplayDevices(const std::unordered_map<std::wstring, BLUETOOTH_DEVICE_INFO>& devices_map, const std::string& device_name)
{
    bool is_device_printed = false;

    std::cout << std::left << std::setw(30) << "Device Name"
        << std::setw(20) << "Device Address"
        << std::setw(15) << "Connected"
        << std::setw(15) << "Remembered"
        << std::setw(15) << "Authenticated"
        << std::endl;

    std::cout << std::string(100, '-') << std::endl;

    for (const auto& pair : devices_map) {
        if (pair.first.empty()) {
            continue;
        }

        if (!device_name.empty() && ToLower(WStrToStr(pair.first.c_str())).find(device_name) == std::string::npos) {
            continue;
        }

        const auto& device = pair.second;
        std::wcout << std::left << std::setw(30) << pair.first
            << std::setw(20) << device.Address.ullLong
            << std::setw(15) << (device.fConnected ? L"Yes" : L"No")
            << std::setw(15) << (device.fRemembered ? L"Yes" : L"No")
            << std::setw(15) << (device.fAuthenticated ? L"Yes" : L"No")
            << std::endl;

        is_device_printed = true;
    }

    if (!is_device_printed) {
        std::cout << "NO DEVICES FOUND" << (!device_name.empty() ? " THAT MATCH " + device_name : "") << std::endl;
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

void GetDevices(std::string device_name)
{
    std::unordered_map<std::wstring, BLUETOOTH_DEVICE_INFO> devices_map;
    EnumerateDevices([&](BLUETOOTH_DEVICE_INFO& device_info) {
        devices_map[device_info.szName] = device_info;
        }, TRUE, TRUE);

    DisplayDevices(devices_map, device_name);
}

void ConnectDeviceByName(std::string device_name)
{
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << std::endl;
        return;
    }

    ULONGLONG btAddr = 0;
    bool device_found = false;

    EnumerateDevices([&](BLUETOOTH_DEVICE_INFO& device_info) {
        std::string current_device_name = WStrToStr(device_info.szName);
        if (ToLower(current_device_name).find(ToLower(device_name)) != std::string::npos) {
            device_found = true;
            btAddr = device_info.Address.ullLong;
            std::cout << "Matching device found: " << current_device_name << std::endl;
        }
        }, TRUE, TRUE);

    if (!device_found) {
        std::cerr << "Device " << device_name << " not found" << std::endl;
        WSACleanup();
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

    std::cout << "Successfully connected to " << device_name << std::endl;

    closesocket(bt_socket);
    WSACleanup();
}

void DisconnectDeviceByName(std::string device_name)
{
    bool device_found = false;

    EnumerateDevices([&](BLUETOOTH_DEVICE_INFO& device_info) {
        std::string current_device_name = WStrToStr(device_info.szName);
        if (ToLower(current_device_name).find(device_name) != std::string::npos) {
            if (!device_info.fConnected) {
                std::cout << "Device " << current_device_name << " is already disconnected" << std::endl;
                return;
            }

            if (BluetoothRemoveDevice(&device_info.Address) == ERROR_SUCCESS) {
                std::cout << "Successfully disconnected " << current_device_name << std::endl;
            }
            else {
                std::cerr << "Error disconnecting from " << current_device_name << std::endl;
            }

            device_found = true;
        }
        }, TRUE, TRUE);

    if (!device_found) {
        std::cerr << "Device " << device_name << " not found" << std::endl;
    }
}