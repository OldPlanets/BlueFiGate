#pragma once

#if __has_include("config.local.h")
#  include "config.local.h"
#else
struct WifiCredential {
    const char* apName;
    const char* apPass;
};

// your wifi credentials
constexpr WifiCredential wifiCredentials[] = {
    {"SSID", "Password"},
};

// your battery MAC address, can be seen in the app
constexpr const char *TDT_DEVICE = "00:00:00:00:00:00";
#endif
