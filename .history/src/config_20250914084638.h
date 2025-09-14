#pragma once

struct WifiCredential {
    const char* apName;
    const char* apPass;
};

constexpr WifiCredential wifiCredentials[] = {
    {"SSID", "Password"},
};

// your battery 
constexpr const char *TDT_DEVICE = "00:00:00:00:00:00";
