// BatteryManager.h
#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "PaperGui.h"
#include "BLEManager.h"
#include "Globals.h"

class BatteryManager
{
public:
    BatteryManager(PaperGui &gui, BLEManager &bleManager);

    void doPolling();
    void finishedPolling();
    void init();

    // Inline getters
    int getSOC() const { return m_soc; }
    float getVoltage() const { return m_voltage; }
    float getPowerFlow() const { return m_powerFlow; }
    bool hasPolled() const { return m_hasPolled; }
    bool isPolling() const { return m_isPolling; }

private:
    PaperGui &m_gui;
    BLEManager &m_bleManager;
    bool m_isPolling = false;
    bool m_hasPolled = false;
    int m_soc = -1;          // State of Charge (%)
    float m_voltage = 0.0f;  // Battery voltage
    float m_powerFlow = 0.0f; // Current power flow (positive = charging, negative = discharging)

    void processBleResult(const TaskResult &result);
    void processBleTDTResult(const TaskResult &result);
    int calculateLiFePO4SOC(float voltage);
};