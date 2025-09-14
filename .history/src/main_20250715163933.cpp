#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Ticker.h>
#include "esp_sleep.h"
#include "esp_adc_cal.h"

#include <Adafruit_Sensor.h>
#ifdef USE_BME280
#include <Adafruit_BME280.h>
#else
#include <Adafruit_BMP280.h>
#endif
#ifdef USE_EINKDISPLAY
#include "EPaperGui.h"
#include "WebRequestManager.h"
#include "TemperatureManager.h"
#endif

#include "Log.h"
#include "wifimanager.h"
#include "HardCodedWifi.h"
#include "OtherFunctions.h"
#ifdef STORE_SENSORDATA
#include "RemoteStorage.h"
#endif

#define USB_POWER GPIO_NUM_3
#define ONBOARD_LED GPIO_NUM_8
#define BMP280_POWER GPIO_NUM_10
#define MEASURE_POWER GPIO_NUM_2
#define MEASURE_ADC GPIO_NUM_1
#define uS_TO_S_FACTOR 1000000
#define SEALEVELPRESSURE_HPA (1013.25)

const char *NVS_NAMESPACE = "vantemp";
const int USER_INTERACTION_STAYAWAKE_SEC = 5 * 120;
const int DEFAULT_UPDATE_INTERVAL_SEC = 6 * 60;
// const int NIGHTTIME_UPDATE_INTERVAL_SEC = 5;   // 10 * 60;

Preferences prefs;
WIFIMANAGER wifiManager;
#ifdef STORE_SENSORDATA
RemoteStorage remoteStorage(wifiManager);
#endif
#ifdef USE_EINKDISPLAY
WebRequestManager webRequestManager(wifiManager);
TemperatureManager temperatureManager(webRequestManager);
EPaperGui gui;
#endif
Ticker wifiTimer;
Ticker ledTimer;
Ticker debugTimer;
int stayAwakeUntil = 0;
bool bIsOtaRunning = false;
bool isUserInteraction = false;
bool isExternPower = false;

void ledFlashAsync(int duration)
{
    digitalWrite(ONBOARD_LED, LOW);
    ledTimer.once_ms(duration, []()
                     { digitalWrite(ONBOARD_LED, HIGH); });
}

void ledFlashSync(int duration, int count)
{
    for (int i = 0; i <= count; i++)
    {
        if (i > 0)
        {
            delay(duration);
        }
        digitalWrite(ONBOARD_LED, LOW);
        delay(duration);
        digitalWrite(ONBOARD_LED, HIGH);
    }
}

float readBatteryVoltage()
{

    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);

    pinMode(MEASURE_POWER, OUTPUT);
    digitalWrite(MEASURE_POWER, HIGH); // Turn on the divider
    delay(20);                         // Brief stabilization time
    int rawValue = analogRead(MEASURE_ADC);
    digitalWrite(MEASURE_POWER, LOW);
    pinMode(MEASURE_POWER, INPUT);

    uint32_t voltage_mv = esp_adc_cal_raw_to_voltage(rawValue, &adc_chars);
    float voltage = voltage_mv * 0.001 * 2.0; // Convert mV to V and apply divider ratio

    // float voltage = rawValue * (3.3 / 4095.0) * 2.0;  // Convert to voltage
    return voltage;
}

void setup()
{
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    esp_reset_reason_t reason = esp_reset_reason();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    const char *strReason = getResetReasonString(reason);
    bool isWakeUpRestart = cause == ESP_SLEEP_WAKEUP_UNDEFINED;
    bool isWakeUpByTimer = cause == ESP_SLEEP_WAKEUP_TIMER;
    bool isWakeUpByUSB = cause == ESP_SLEEP_WAKEUP_GPIO;

    pinMode(ONBOARD_LED, OUTPUT);
    digitalWrite(ONBOARD_LED, HIGH);
    pinMode(USB_POWER, INPUT);
    isExternPower = digitalRead(USB_POWER);

    isUserInteraction = true; // isWakeUpByUSB || (isExternPower && !isWakeUpByTimer);

    if (isExternPower)
    {
        ledFlashAsync(100);
    }
    Serial.begin(115200);
    delay(200); // Give serial monitor time to open
    Serial.setDebugOutput(true);

    if (isWakeUpByUSB)
    {
        ledFlashSync(100, 2);
        delay(100);
        ledFlashSync(500, 1);
    }
    else if (isUserInteraction)
    {
        ledFlashSync(100, 3);
    }
    Log.debug("****************** Setup()  cause: %i, Reset Reason: %s", (int)cause, strReason);

    if (isUserInteraction)
    {
        stayAwakeUntil = millis() + (USER_INTERACTION_STAYAWAKE_SEC * 1000) + 1000;
    }

    // prefs.begin(NVS_NAMESPACE, false);

    // init the Wifi with hardcoded creds
    for (const auto &credential : wifiCredentials)
    {
        wifiManager.addWifi(credential.apName, credential.apPass, false);
    }
    wifiManager.fallbackToSoftAp(false, "", "");

    if (isUserInteraction)
    {
        // if we wake up by reset or power, let Wifi run for 2 minutes to allow OTA updates
        if (wifiManager.requestWifi(&wifiManager, true))
        {
            if (isUserInteraction)
            {
                ledFlashAsync(1000);
            }
            wifiTimer.once(USER_INTERACTION_STAYAWAKE_SEC, []()
                           {
                    if (bIsOtaRunning)
                    {
                        return;
                    }
                    Log.info("[WIFI] Shutting down, initial user interaction uptime has passed");
                    ArduinoOTA.end();
                    wifiManager.finishedWifi(&wifiManager); });
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
        }
    }

    float batteryVoltage = readBatteryVoltage();
    Log.info("Read Battery Voltage: %0.2f, External Power: %s", batteryVoltage, (isExternPower ? "Yes" : "nNo"));

    if (batteryVoltage < 3.0f && !isUserInteraction && !isExternPower)
    {
        Log.critical("Battery voltage too low, going into forever sleep");
        delay(100);
        esp_deep_sleep_enable_gpio_wakeup(1ULL << USB_POWER, ESP_GPIO_WAKEUP_GPIO_HIGH);
        esp_deep_sleep_start();
    }

    pinMode(BMP280_POWER, OUTPUT);
    digitalWrite(BMP280_POWER, HIGH);
    delay(50);
    Wire.begin(GPIO_NUM_21, GPIO_NUM_20);

#ifdef USE_BME280
    Adafruit_BME280 bmx280;
    bool bSensorReady = bmx280.begin(0x76, &Wire);
    if (!bSensorReady)
    {
        Log.critical("BME280 error initalizing");
        Serial.print("SensorID was: 0x");
        Serial.println(bmx280.sensorID(), 16);
    }
    else
    {
        Serial.print("Temperature = ");
        Serial.print(bmx280.readTemperature());
        Serial.println(" °C");
        Serial.print("Humidty = ");
        Serial.print(bmx280.readHumidity());
        Serial.println(" %");
    }
#else
    Adafruit_BMP280 bmx280(&Wire);
    bool bSensorReady = bmx280.begin(118);
    if (!bSensorReady)
    {
        Log.critical("BMP280 error initalizing");
        Serial.print("SensorID was: 0x");
        Serial.println(bmx280.sensorID(), 16);
    }
    else
    {
        Serial.print("Temperature = ");
        Serial.print(bmx280.readTemperature());
        Serial.println(" °C");
    }
#endif

    if (bSensorReady)
    {
        float readTemp;
        bool valid = false;

        for (int i = 0; i < 3 && !valid; ++i)
        {
            readTemp = bmx280.readTemperature();
            valid = (readTemp >= -40.0f && readTemp <= 85.0f);
            if (!valid)
            {
                delay(50);
            }
        }

        if (valid)
        {
            const int batteryPercent = calculateLiFePO4SOC(batteryVoltage);
#ifdef STORE_SENSORDATA            
            remoteStorage.init();
            remoteStorage.updateValues(
#ifdef USE_BME280
                (uint16_t)round(bmx280.readHumidity()),
#else
                (uint16_t)bmx280.readAltitude(SEALEVELPRESSURE_HPA),
#endif
                (uint16_t)(readTemp * 10),
                batteryPercent);
#endif
#if USE_EINKDISPLAY
            gui.setTemperatureInside(readTemp);
#ifdef USE_BME280            
            gui.setHumidityInside(bmx280.readHumidity());
#endif            
            gui.setBatteryInside(batteryPercent);
#endif
        }
        else
        {
            Log.error("BME280 values invalid");
        }
    }
    digitalWrite(BMP280_POWER, LOW);
    pinMode(BMP280_POWER, INPUT);
}

void deepSleepOrDelay()
{
    bool canSleep = millis() > stayAwakeUntil && wifiManager.getWifiUserCount() == 0 && !bIsOtaRunning;
    #ifdef STORE_SENSORDATA
    canSleep = canSleep && !remoteStorage.isBusy();
    #endif
    if (!canSleep)
    {
        yield();
        delay(50);
        return;
    }
    else
    {
        const int sleepSeconds = DEFAULT_UPDATE_INTERVAL_SEC;
        esp_sleep_enable_timer_wakeup(sleepSeconds * uS_TO_S_FACTOR);
        if (!isExternPower) // we'd wake up right away again if we are already on power
        {
            esp_deep_sleep_enable_gpio_wakeup(1ULL << USB_POWER, ESP_GPIO_WAKEUP_GPIO_HIGH);
        }

        // prefs.end();
        Log.debug("Deep Sleep");
        delay(100);
        esp_deep_sleep_start();
    }
}

void loop()
{
    if (isUserInteraction)
    {
        ArduinoOTA.handle();
    }
    if (bIsOtaRunning)
    {
        // Do not continue regular operation as long as a OTA is running
        // Reason: Background workload can cause upgrade issues that we want to avoid!
        delay(50);
        return;
    }
    bool bReadyForPolling = true;
#ifdef STORE_SENSORDATA
    remoteStorage.process();
#endif    
    return deepSleepOrDelay();
}