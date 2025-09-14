#pragma once
#include <Arduino.h>

void mini_rc4_crypt(uint8_t* data, size_t length, const uint8_t* key, size_t keylen);
bool isNightInBerlin();
uint16_t read2BytesToInt(const uint8_t* data, size_t offset);
const char *getResetReasonString(esp_reset_reason_t reason);
uint8_t calculateLiFePO4SOC(float voltage);
String getUniqueHostname();