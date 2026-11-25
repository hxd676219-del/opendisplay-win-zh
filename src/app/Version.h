#pragma once

// Single source of truth for the application version, shared by
// resources.rc (VERSIONINFO) and any code that prints a version string.
//
// v1.0.0  First Chinese-localized release.
// v1.0.1  Fix MttVddSettings.cpp (an early encoding round-trip had replaced
//         every 'N' with 'o', breaking the build) and ship the TaskbarCreated
//         tray-icon re-register fix.
#define OD_VERSION_MAJOR 1
#define OD_VERSION_MINOR 0
#define OD_VERSION_PATCH 1
#define OD_VERSION_BUILD 0

#define OD_VERSION_STRING "1.0.1"
