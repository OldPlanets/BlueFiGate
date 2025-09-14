// BatteryManager.cpp
#include "BatteryManager.h"
#include "Log.h"
#include "OtherFunctions.h"
#include "TDTPollCharacteristicTask.h"

constexpr const char *TDT_DEVICE = "C0:D6:3C:54:36:53";

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
        int correctedSoc = min(calculateLiFePO4SOC(minVoltage / 1000.0f) + (bms.current / 10.0f <= -2 ? 20 : 10), (int)bms.batteryLevel);
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

int BatteryManager::calculateLiFePO4SOC(float voltage)
{
    // Voltage thresholds for each SOC percentage (0-100%)
    static const float socVoltageMap[101] = {
        /* 0% */ 2.500f, /* 1% */ 2.569f, /* 2% */ 2.638f, /* 3% */ 2.707f, /* 4% */ 2.776f,
        /* 5% */ 2.800f, /* 6% */ 2.844f, /* 7% */ 2.889f, /* 8% */ 2.933f, /* 9% */ 2.978f,
        /* 10% */ 3.017f, /* 11% */ 3.050f, /* 12% */ 3.083f, /* 13% */ 3.117f, /* 14% */ 3.150f,
        /* 15% */ 3.159f, /* 16% */ 3.168f, /* 17% */ 3.178f, /* 18% */ 3.187f, /* 19% */ 3.196f,
        /* 20% */ 3.205f, /* 21% */ 3.208f, /* 22% */ 3.210f, /* 23% */ 3.213f, /* 24% */ 3.215f,
        /* 25% */ 3.218f, /* 26% */ 3.220f, /* 27% */ 3.223f, /* 28% */ 3.225f, /* 29% */ 3.228f,
        /* 30% */ 3.230f, /* 31% */ 3.232f, /* 32% */ 3.234f, /* 33% */ 3.236f, /* 34% */ 3.238f,
        /* 35% */ 3.240f, /* 36% */ 3.242f, /* 37% */ 3.244f, /* 38% */ 3.246f, /* 39% */ 3.248f,
        /* 40% */ 3.250f, /* 41% */ 3.251f, /* 42% */ 3.252f, /* 43% */ 3.253f, /* 44% */ 3.254f,
        /* 45% */ 3.255f, /* 46% */ 3.256f, /* 47% */ 3.257f, /* 48% */ 3.258f, /* 49% */ 3.259f,
        /* 50% */ 3.260f, /* 51% */ 3.262f, /* 52% */ 3.264f, /* 53% */ 3.266f, /* 54% */ 3.268f,
        /* 55% */ 3.270f, /* 56% */ 3.272f, /* 57% */ 3.274f, /* 58% */ 3.276f, /* 59% */ 3.278f,
        /* 60% */ 3.280f, /* 61% */ 3.282f, /* 62% */ 3.284f, /* 63% */ 3.286f, /* 64% */ 3.288f,
        /* 65% */ 3.290f, /* 66% */ 3.292f, /* 67% */ 3.294f, /* 68% */ 3.296f, /* 69% */ 3.298f,
        /* 70% */ 3.300f, /* 71% */ 3.303f, /* 72% */ 3.306f, /* 73% */ 3.309f, /* 74% */ 3.312f,
        /* 75% */ 3.315f, /* 76% */ 3.318f, /* 77% */ 3.321f, /* 78% */ 3.324f, /* 79% */ 3.327f,
        /* 80% */ 3.330f, /* 81% */ 3.332f, /* 82% */ 3.334f, /* 83% */ 3.336f, /* 84% */ 3.338f,
        /* 85% */ 3.340f, /* 86% */ 3.342f, /* 87% */ 3.344f, /* 88% */ 3.346f, /* 89% */ 3.348f,
        /* 90% */ 3.350f, /* 91% */ 3.353f, /* 92% */ 3.357f, /* 93% */ 3.360f, /* 94% */ 3.363f,
        /* 95% */ 3.367f, /* 96% */ 3.370f, /* 97% */ 3.373f, /* 98% */ 3.377f, /* 99% */ 3.380f,
        /* 100% */ 3.650f};

    // Handle boundary conditions
    if (voltage >= socVoltageMap[100])
        return 100;
    if (voltage <= socVoltageMap[0])
        return 0;

    // Find the SOC percentage based on voltage
    for (int i = 1; i <= 100; i++)
    {
        if (voltage < socVoltageMap[i])
        {
            return i - 1;
        }
    }

    return 0; // Fallback (should never reach here)
}