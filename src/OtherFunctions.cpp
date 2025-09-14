#include "OtherFunctions.h"

const char *getResetReasonString(esp_reset_reason_t reason)
{
    switch (reason)
    {
    case ESP_RST_UNKNOWN:
        return "Unknown";
    case ESP_RST_POWERON:
        return "Power-on reset (cold boot)";
    case ESP_RST_EXT:
        return "External reset (reset button/pin)";
    case ESP_RST_SW:
        return "Software reset";
    case ESP_RST_PANIC:
        return "Exception/panic reset";
    case ESP_RST_INT_WDT:
        return "Interrupt watchdog reset";
    case ESP_RST_TASK_WDT:
        return "Task watchdog reset";
    case ESP_RST_WDT:
        return "Other watchdog reset";
    case ESP_RST_DEEPSLEEP:
        return "Reset after exiting deep sleep";
    case ESP_RST_BROWNOUT:
        return "Brownout reset (voltage dip)";
    case ESP_RST_SDIO:
        return "SDIO reset";
    default:
        return "Unknown reset reason";
    }
}

String getUniqueHostname()
{
#ifdef UNIQUEHOSTNAME
    return String(UNIQUEHOSTNAME);
#else
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char suffix[6];
    snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
    return String(OTAHOSTNAME) + "-" + suffix;
#endif
}