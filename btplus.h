#pragma once
#ifndef BTPLUS_H
#define BTPLUS_H

BLUETOOTH_DEVICE_SEARCH_PARAMS CreateSearchParams(BOOL returnAuthenticated, BOOL returnUnknown);
BLUETOOTH_DEVICE_INFO CreateDeviceInfo();
void EnumerateDevices(std::function<void(BLUETOOTH_DEVICE_INFO&)> callback, BOOL returnAuthenticated, BOOL returnUnknown);
void GetDevices(std::string device_name);
void ConnectDeviceByName(std::string device_name);
void DisconnectDeviceByName(std::string device_name);
void DisplayDevices(const std::unordered_map<std::wstring, BLUETOOTH_DEVICE_INFO>& devices_map, const std::string& device_name);
void ShowHelp();
std::string WStrToStr(const WCHAR* wstr);
std::string ToLower(const std::string& str);
#endif // BTPLUS_H
