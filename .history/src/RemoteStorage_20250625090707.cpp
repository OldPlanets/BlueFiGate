#include "RemoteStorage.h"
#include "Log.h"
#include <Arduino.h>
#include <AsyncHTTPRequest_Generic.h>
#include <WiFi.h>
#include "OtherFunctions.h"

constexpr const char* storageUrl = "http://vancontrol.server3.org/vancontrol.php";
const uint32_t STORE_INTERVAL_SEC = 2 * 60;
const uint32_t HTTP_TIMEOUT_MS = 10 * 1000;

const uint8_t MAGIC_STO_VALUE = 0x9D;
static uint8_t stoBeaconKey[] = {0x00, 0x00, 0x19, 0x23, 0x42, 0x67, 0x11, 0xA3, 0xF4, 0x2E};
constexpr const uint16_t BLE_SERVICE_BASE = 0x489C;

const char* NVS_RS_NAMESPACE = "ReStore";

RemoteStorage::RemoteStorage(WIFIMANAGER &wifiManager)
    :  m_wifiManager(wifiManager)
{
}

void RemoteStorage::init(/*Preferences& preferences*/)
{
    m_storageEnabled = true;// preferences.getBool("enableStore", false) && preferences.getBool("enableWifi", false); // fixme - works only after reset when changing settings, but, eh..
    m_storageUrl = storageUrl; //preferences.getString("storeUrl", "");
    //LOG_INFO_F("RemoteStorage: Init. DeepSleep: %s Enabled: %s, URL: %s, LastRTC: %llu\n", (m_deepSleepWakeup ? "Yes" : "No"), (m_storageEnabled ? "Yes" : "No"), m_storageUrl.c_str(), m_lastStoreRtcTime);
}

RemoteStorage::~RemoteStorage()
{
    if (m_request)
    {
        delete m_request;
        m_request = NULL;
    }
}

uint32_t RemoteStorage::getUniqueServiceId()
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    // Use last 2 bytes of MAC as the unique part
    uint16_t uniquePart = (mac[4] << 8) | mac[5];
    return ((uint32_t)uniquePart << 16) | BLE_SERVICE_BASE;
}

void RemoteStorage::setupRequest()
{
    m_shutDownGracePeriod = 0;
    if (m_request == NULL)
    {
        m_request = new AsyncHTTPRequest();
    }
    m_request->setTimeout(8);
    m_request->onReadyStateChange([this](void *, AsyncHTTPRequest* request, int readyState)
                                 {
        if (readyState == 4) {
            if (m_state == State::WAITING_FOR_RESPONSE) {
                int statusCode = request->responseHTTPcode();
                String response = request->responseText();
                this->onRequestComplete(request, statusCode, response);
            }
        } });
}

void RemoteStorage::onRequestComplete(AsyncHTTPRequest *request, int statusCode, String response)
{
    if (m_state == State::COMPLETED_FAILURE || m_state == State::COMPLETED_SUCCESS)
    {
        return;
    }

    if (statusCode == 200 && response.indexOf("OK") >= 0)
    {
        m_state = State::COMPLETED_SUCCESS;
        Log.debug("RemoteStorage: Request completed successfully with status %d", statusCode);
    }
    else
    {
        m_state = State::COMPLETED_FAILURE;
        Log.debug("RemoteStorage: Request failed with status %d, response: %s", statusCode, response.c_str());
    }

    endRequest();
}

bool RemoteStorage::isTimeToStore()
{
    return true;
}

void RemoteStorage::checkWifiStatus()
{
    if (WiFi.isConnected())
    {
        m_state = State::SENDING_REQUEST;
        Log.debug("RemoteStorage: WiFi connected successfully");
    }
}

void RemoteStorage::sendHttpRequest()
{
    String url = m_storageUrl;
    if (url.indexOf("?") >= 0)
    {
        url += "&";
    }
    else
    {
        url += "?";
    }
    url += "s=" + String(getUniqueServiceId()) + "&v=" + createPayload();

    if (m_request && (m_request->readyState() == 0 || m_request->readyState() == 4))
    {
        if (m_request->open("GET", url.c_str()))
        {
            m_request->send();
            Log.debug("RemoteStorage: Sending HTTP request to %s, ServiceID: %u", url.c_str(), getUniqueServiceId());
        }
        else
        {
            m_state = State::COMPLETED_FAILURE;
            Log.error("RemoteStorage: Failed to open HTTP request");
            endRequest();
        }
        m_state = State::WAITING_FOR_RESPONSE;
    }
    else
    {
        m_state = State::COMPLETED_FAILURE;
        Log.error("RemoteStorage: Request object busy or NULL");
        endRequest();
    }
}

String RemoteStorage::createPayload()
{
    uint8_t serviceData[8];
    uint32_t random = esp_random();
    serviceData[2] = MAGIC_STO_VALUE;
    serviceData[3] = (uint8_t)m_payloadBattery;
    serviceData[4] = m_payloadTemp;
    serviceData[5] = (m_payloadTemp >> 8) & 0xFF;
    serviceData[6] = m_payloadAltitude;
    serviceData[7] = (m_payloadAltitude >> 8) & 0xFF;    
    stoBeaconKey[0] = random;
    stoBeaconKey[1] = (random >> 8) & 0xFF;
    mini_rc4_crypt(serviceData, sizeof(serviceData), stoBeaconKey, sizeof(stoBeaconKey));
    serviceData[0] = random;
    serviceData[1] = (random >> 8) & 0xFF;
    
    String result = "";
    const char hexChars[] = "0123456789ABCDEF";
    
    for (size_t i = 0; i < sizeof(serviceData); i++) {
        uint8_t value = serviceData[i];
        result += hexChars[value >> 4];    // High nibble
        result += hexChars[value & 0x0F];  // Low nibble
    }
    
    return result;
}

void RemoteStorage::saveLastStoreTime()
{
    /*
    if (m_deepSleepWakeup)
    {
        m_lastStoreRtcTime = rtctime();
        Preferences remoteStoragePrefs;
        remoteStoragePrefs.begin(NVS_RS_NAMESPACE, false);
        remoteStoragePrefs.putULong64(PREFERENCES_KEY_RTC, m_lastStoreRtcTime);
        remoteStoragePrefs.end();
        LOG_INFO_F("RemoteStorage: Saved last store RTC time: %llu\n", m_lastStoreRtcTime);
    }
    else
    {
        m_lastStoreMillis = millis();
        //LOG_INFO_LN(F("RemoteStorage: Not storing last query in NVS"));
    }*/
}

void RemoteStorage::handleTimeout()
{
    if (m_state == State::WAITING_FOR_WIFI ||
        m_state == State::SENDING_REQUEST ||
        m_state == State::WAITING_FOR_RESPONSE)
    {
        /*if (m_state == State::WAITING_FOR_RESPONSE && m_request)
        {
            m_request->abort();
        }*/
        m_state = State::COMPLETED_FAILURE;
        Log.warn("RemoteStorage: Operation timed out");
        endRequest();
    }
}

void RemoteStorage::updateValues(uint16_t payloadAltitude, int16_t payloadTemp, uint8_t payloadBattery)
{
    if (isBusy())
    {
        return;
    }

    if (!m_storageEnabled || m_storageUrl.isEmpty())
    {
        return;
    }

    if (!isTimeToStore())
    {
        return;
    }

    setupRequest();

    m_payloadTemp = payloadTemp;
    m_payloadAltitude = payloadAltitude;
    m_payloadBattery = payloadBattery;
    m_operationStartTime = millis();

    Log.debug("RemoteStorage: Starting update with altitude=%i, temp=%i, bat:=%i", payloadAltitude, payloadTemp, payloadBattery);
    
    // requestWifi is blocking, so we can skip the WAITING_FOR_WIFI state and handle the result immediately
    if (m_wifiManager.requestWifi(this, true))
    {
        m_state = State::SENDING_REQUEST;
    }
    else
    {
        m_state = State::COMPLETED_FAILURE;
        Log.warn("RemoteStorage: No Wifi");
        endRequest();       
    }
}

void RemoteStorage::endRequest()
{
    m_lastStorageSucceeded = m_state == State::COMPLETED_SUCCESS;
    m_shutDownGracePeriod = millis() + 500;
    if (m_request)
    {
        m_request->abort();
    }
    saveLastStoreTime();
}

void RemoteStorage::process()
{
    if ((m_state == State::WAITING_FOR_WIFI ||
         m_state == State::SENDING_REQUEST ||
         m_state == State::WAITING_FOR_RESPONSE) &&
        (millis() - m_operationStartTime > HTTP_TIMEOUT_MS))
    {
        handleTimeout();
        return;
    }

    switch (m_state)
    {
    case State::IDLE:
        break;

    case State::WAITING_FOR_WIFI:
        checkWifiStatus();
        break;

    case State::SENDING_REQUEST:
        sendHttpRequest();
        break;

    case State::WAITING_FOR_RESPONSE:
        break;

    case State::COMPLETED_SUCCESS:
    case State::COMPLETED_FAILURE:
        if (millis() > m_shutDownGracePeriod)
        {
            if (m_request)
            {
                delete m_request;
                m_request = NULL;
            }
            m_wifiManager.finishedWifi(this);
            m_state = State::IDLE;
        }
        break;
    }
}

bool RemoteStorage::isBusy() const
{ 
    return m_state != State::IDLE;
}