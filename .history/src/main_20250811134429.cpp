#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Ticker.h>
#include "esp_sleep.h"
#include <Adafruit_NeoPixel.h>
#include "esp_task_wdt.h"

#include "Log.h"
#include "wifimanager.h"
#include "HardCodedWifi.h"
#include "OtherFunctions.h"
#include "BLEManager.h"
#include "BatteryManager.h"
#include "VanControlWebServer.h"
#include "TimeSync.h"

#define ONBOARD_LED GPIO_NUM_8

const char *NVS_NAMESPACE = "vanbatsec";

Preferences prefs;
WIFIMANAGER wifiManager;
BLEManager bleManager;
BatteryManager batteryManager(bleManager);
Ticker ledTimer;
Adafruit_NeoPixel pixel(1, 8, NEO_GRB + NEO_KHZ800);
VanControlWebServer webserver(&batteryManager);
TimeSync timeSync;
bool bCrashedBefore;

bool bIsOtaRunning = false;

void ledColor(uint8_t red, uint8_t green, uint8_t blue)
{
    
}

void setup()
{
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    esp_reset_reason_t reason = esp_reset_reason();
    switch (reason)
    {
        case ESP_RST_PANIC:      //!< Software reset due to exception/panic
        case ESP_RST_INT_WDT:    //!< Reset (software or hardware) due to interrupt watchdog
        case ESP_RST_TASK_WDT:   //!< Reset due to task watchdog
        case ESP_RST_WDT:        //!< Reset due to other watchdogs
        case ESP_RST_BROWNOUT:   //!< Brownout reset (software or hardware)
            bCrashedBefore = true;
            break;
        default:
            bCrashedBefore = false;
    }
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    const char *strReason = getResetReasonString(reason);
    bool isWakeUpRestart = cause == ESP_SLEEP_WAKEUP_UNDEFINED;
    bool isWakeUpByTimer = cause == ESP_SLEEP_WAKEUP_TIMER;
    bool isWakeUpByUSB = cause == ESP_SLEEP_WAKEUP_GPIO;

    // pinMode(ONBOARD_LED, OUTPUT);
    // digitalWrite(ONBOARD_LED, LOW);
    pixel.begin();
    pixel.setPixelColor(0, 0, 0, 0); // RGB all to 0
    pixel.show();

    Serial.begin(115200);
    delay(200); // Give serial monitor time to open
    Serial.setDebugOutput(true);

    Log.debug("****************** Setup()  cause: %i, Reset Reason: %s", (int)cause, strReason);

    // prefs.begin(NVS_NAMESPACE, false);

    // init the Wifi with hardcoded creds
    for (const auto &credential : wifiCredentials)
    {
        wifiManager.addWifi(credential.apName, credential.apPass, false);
    }
    wifiManager.fallbackToSoftAp(false, "", "");


    while (!wifiManager.requestWifi(&wifiManager, true))
    {
        Log.debug("Retrying Wifi in 30 seconds");
        esp_sleep_enable_timer_wakeup(30 * 1000000);
        esp_light_sleep_start();
    }

    ArduinoOTA.setHostname(getUniqueHostname().c_str()).setPassword(OTAPASSWORD).setMdnsEnabled(true).onStart([]()
                                                                                                              {
                                String type;
                                if (ArduinoOTA.getCommand() == U_FLASH) type = "sketch";
                                else {
                                    type = "filesystem";
                                    //LittleFS.end();
                                }
                                bIsOtaRunning = true;
                                Log.info("Start updating - %s", type.c_str()); })
        .onEnd([]()
               { bIsOtaRunning = false; })
        .onProgress([](unsigned int progress, unsigned int total)
                    {
                        // Log.info("Progress: %u%%\r", (progress / (total / 100)));
                    })
        .onError([](ota_error_t error)
                 {
                                bIsOtaRunning = false;;
                                Log.error("OTA Error[%u]: ", error);
                                if (error == OTA_AUTH_ERROR) Log.error("Auth Failed");
                                else if (error == OTA_BEGIN_ERROR) Log.error("Begin Failed");
                                else if (error == OTA_CONNECT_ERROR) Log.error("Connect Failed");
                                else if (error == OTA_RECEIVE_ERROR) Log.error("Receive Failed");
                                else if (error == OTA_END_ERROR) Log.error("End Failed"); });

    ArduinoOTA.begin();
    WiFi.setSleep(true);
    setCpuFrequencyMhz(80);

    bleManager.init(true);
    batteryManager.init();
    batteryManager.doPolling();
    webserver.start();
    timeSync.begin();
}

void loop()
{
    ArduinoOTA.handle();
    if (bIsOtaRunning)
    {
        // Do not continue regular operation as long as a OTA is running
        // Reason: Background workload can cause upgrade issues that we want to avoid!
        delay(50);
        return;
    }
    yield();
    esp_task_wdt_reset();  
    bleManager.process();
    yield();
    timeSync.loop();

    delay(20);
}