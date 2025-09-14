#pragma once

struct WifiCredential {
    const char* apName;
    const char* apPass;
};

constexpr WifiCredential wifiCredentials[] = {
    {"HighwayToHell", "x"},
    {"ByteMe", "x"}
};

constexpr const char *TDT_DEVICE = "x";