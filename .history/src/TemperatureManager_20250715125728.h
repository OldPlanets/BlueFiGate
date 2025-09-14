#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "PaperGui.h"
#include "WebRequestManager.h"
#include "Globals.h"

class TemperatureManager
{
public:
    TemperatureManager(PaperGui &gui, WebRequestManager &webRequestManager, Preferences &prefs);

    void doPolling();
    void finishedPolling();
    void init();

    // Inline getters
    float getOutsideTemp() const { return m_outsideTemp; }
    float getInsideTemp() const { return m_insideTemp; }
    float getHumidity() const { return m_humidity; }
    float getAltitude() const { return m_altitude; }
    int getBatteryOutside() const { return m_batteryOutside; }
    int getBatteryInside() const { return m_batteryInside; }
    bool hasPolled() const { return m_hasPolled; }
    bool isPolling() const { return m_isPolling; }

private:
    PaperGui &m_gui;
    WebRequestManager &m_webRequestManager;
    Preferences &m_prefs;
    bool m_isPolling = false;
    bool m_hasPolled = false;

    // Sensor data
    float m_outsideTemp = 0.0f;
    float m_insideTemp = 0.0f;
    float m_humidity = 0.0f;
    uint16_t m_altitude = -1;
    uint8_t m_batteryOutside = -1;
    uint8_t m_batteryInside = -1;

    void loadFromPreferences();
    void saveToPreferences();
    void processWebResult(const WebTaskResult &result);
};