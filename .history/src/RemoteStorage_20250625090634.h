#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "wifimanager.h"

class AsyncHTTPRequest;
class RemoteStorage
{
public:
    enum class State
    {
        IDLE,
        WAITING_FOR_WIFI,
        SENDING_REQUEST,
        WAITING_FOR_RESPONSE,
        COMPLETED_SUCCESS,
        COMPLETED_FAILURE
    };

    RemoteStorage(WIFIMANAGER& wifiManager);
    ~RemoteStorage();

    void init(/*Preferences& preferences*/);

    void updateValues(uint16_t payloadAltitude, int16_t payloadTemp, uint8_t payloadBattery);
    void process();
    bool isBusy() const;
    State getState() const { return m_state; }

private:
    WIFIMANAGER &m_wifiManager;
    bool m_storageEnabled = false;
    bool m_lastStorageSucceeded = false;
    String m_storageUrl = "";

    // Time tracking
    uint64_t m_lastStoreRtcTime = 0; // RTC time for deep sleep tracking
    uint32_t m_lastStoreMillis = 0;  // millis() for normal operation
;
    AsyncHTTPRequest* m_request = NULL;
    State m_state = State::IDLE;
    int m_payloadAltitude = -1;
    int m_payloadTemp = -1;
    int m_payloadBattery = -1;
    uint32_t m_operationStartTime = 0;
    uint32_t m_shutDownGracePeriod = 0;

    void setupRequest();
    void onRequestComplete(AsyncHTTPRequest *request, int statusCode, String response);
    bool isTimeToStore();
    void checkWifiStatus();
    void sendHttpRequest();
    void saveLastStoreTime();
    void handleTimeout();
    void endRequest();
    uint32_t getUniqueServiceId();
    String createPayload();
};