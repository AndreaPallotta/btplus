# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-08-02

### Added
- Automatic Bluetooth audio connection daemon for Windows running as a login background task.
- Dual detection engine combining `WM_DEVICECHANGE` message hook with fallback interval polling.
- Windows native toast notifications for connection success and retry events.
- WiX v4 interactive MSI installer wizard with custom Bluetooth device selection and polling interval configuration dialog (`BtConfigDlg.wxs`).
- Automated elevated Task Scheduler registration on install.
- Portable standalone installation script (`install.ps1`).
