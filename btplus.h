#pragma once
#ifndef BTPLUS_H
#define BTPLUS_H

BLUETOOTH_DEVICE_SEARCH_PARAMS CreateSearchParams(BOOL returnAuthenticated, BOOL returnUnknown);
BLUETOOTH_DEVICE_INFO CreateDeviceInfo();
void EnumerateDevices(std::function<void(BLUETOOTH_DEVICE_INFO&)> callback, BOOL returnAuthenticated, BOOL returnUnknown);
void ConnectDeviceByAddr(ULONGLONG btAddr, const std::string device_name);
void MonitorDevice(std::string device_name);
void ServiceWorkerThread(const std::string device_name);
std::string WStrToStr(const WCHAR* wstr);
LPWSTR charArrToLpwstr(const char charArr[]);
std::string ToLower(const std::string& str);
#endif // BTPLUS_H
