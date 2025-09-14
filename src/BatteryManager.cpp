// BatteryManager.cpp
#include "BatteryManager.h"
#include "Log.h"
#include "OtherFunctions.h"
#include "TDTPollCharacteristicTask.h"
#include "config.h"

BatteryManager::BatteryManager(BLEManager &bleManager)
    : m_bleManager(bleManager)
{
}

void BatteryManager::init()
{
    Log.debug("[BATTERY] Init");
    // No preferences to load
}

void BatteryManager::doPolling()
{
    if (m_isPolling)
        return;

    m_isPolling = true;
    m_bleManager.queueTDTPollCharacteristicTask(1, 20000, [this](const TaskResult &result)
                                                { processBleTDTResult(result); }, NimBLEAddress(TDT_DEVICE, 1), true);
}

void BatteryManager::finishedPolling()
{
    m_isPolling = false;
    m_hasPolled = true;
}

void BatteryManager::processBleTDTResult(const TaskResult &result)
{
    if (result.status == TaskStatus::SUCCESS)
    {
        TDTBMSData bms = TDTPollCharacteristicTask::getBMSDataFromResultTaskResult(result);
        uint16_t minVoltage = UINT16_MAX;
        for (int i = 0; i < bms.cellCount; ++i)
        {
            if (bms.cellVoltages[i] < minVoltage)
            {
                minVoltage = bms.cellVoltages[i];
            }
        }
        lastTdtUpdateMs = millis();
        tdtBmsData = bms;
        Log.info("[Battery]: BLE: TDT Poll succeeded, min voltage: %f, Status: %s", minVoltage / 1000.0f, result.errorMessage.c_str());
    }
    else
    {
        Log.info("[Battery]: BLE: Trying to poll TDT battery status failed, reason: %s",
                 BLETask::getResultLabel(result.status));
    }

    finishedPolling();
}