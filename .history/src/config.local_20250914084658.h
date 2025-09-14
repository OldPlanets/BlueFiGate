#pragma once

struct WifiCredential {
    const char* apName;
    const char* apPass;
};

constexpr WifiCredential wifiCredentials[] = {
    {"HighwayToHell", "625ytgkaFAVSGmd2"},
    {"ByteMe", "pr78TAdef3PhanUT"}
};

constexpr const char *TDT_DEVICE = "C0:D6:3C:54:36:53";