#pragma once
#include <Arduino.h>

const char *getResetReasonString(esp_reset_reason_t reason);
String getUniqueHostname();