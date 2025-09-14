#include "TemperatureManager.h"
#include "Log.h"
#include "OtherFunctions.h"

// Placeholder BLE service UUID and key
constexpr const uint32_t BLE_SERVICE_TEMPERATURE = 0x8b09489c;
static uint8_t bleBeaconKey[] = {0x00, 0x00, 0x19, 0x23, 0x42, 0x67, 0x11, 0xA3, 0xF4, 0x2E};
constexpr uint8_t MAGIC_BLE_VALUE = 0x9D;
constexpr const char* storageTempUrl = "http://vancontrol.server3.org/vancontrol.php";

TemperatureManager::TemperatureManager(WebRequestManager &webRequestManager)
    : m_webRequestManager(webRequestManager)
{
}

void TemperatureManager::init()
{
    Log.debug("[TEMPERATURE] Init");
}

void TemperatureManager::doPolling()
{
    if (m_isPolling)
        return;

    m_isPolling = true;
    m_hasPolled = false;

    m_webRequestManager.queueFetchDataTask(1, 11000, [this](const WebTaskResult &result)
                                     { processWebResult(result); }, storageTempUrl, BLE_SERVICE_TEMPERATURE);
}

void TemperatureManager::finishedPolling()
{
    m_isPolling = false;
    m_hasPolled = true;
}

void TemperatureManager::processWebResult(const WebTaskResult &result)
{
    if (result.status == WebTaskStatus::SUCCESS)
    {
        if (result.dataLength != 8)
        {
            Log.error("TemperatureManager: Web: Unexpected service data size: %i", result.dataLength);
        }
        else
        {
            bleBeaconKey[0] = result.data[0];
            bleBeaconKey[1] = result.data[1];
            mini_rc4_crypt(result.data.get(), result.dataLength, bleBeaconKey, sizeof(bleBeaconKey));
            if (result.data[2] != MAGIC_BLE_VALUE)
            {
                Log.error("TemperatureManager: Web: Unexpected data, magic value mismatch. BLE key error or invalid data.", result.dataLength);
            }
            else
            {
                m_batteryOutside = min((uint8_t)100, result.data[3]);
                m_outsideTemp = (((uint16_t)result.data[5] << 8) | result.data[4]) / 10.0f;
                m_altitude = ((uint16_t)result.data[7] << 8) | result.data[6];
                Log.info("TemperatureManager: Web:  Received poll answer for outside temp. Temperature level: %.1f, Altitude: %u Thermometer battery: %d", m_outsideTemp, m_altitude, m_batteryOutside);
            }
        }
    }
    else
    {
        Log.info("TemperatureManager: Web: Trying to poll temperature data failed, reason: %s", WebTask::getResultLabel(result.status));
    }

    finishedPolling();
}