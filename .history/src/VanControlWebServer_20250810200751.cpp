#include "VanControlWebServer.h"
#include <cctype>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include "BLEManager.h"
#include <ArduinoJson.h>
#include "BatteryManager.h"

VanControlWebServer::DataEntry::DataEntry(unsigned long ts, const String &val)
    : timestamp(ts), value(val)
{
}

VanControlWebServer::VanControlWebServer(BatteryManager *batteryManager, int port)
    : server(std::make_unique<AsyncWebServer>(port)), isRunning(false)
{
    this->batteryManager = batteryManager;
}

VanControlWebServer::~VanControlWebServer()
{
    stop();
}

unsigned long VanControlWebServer::getCurrentTime() const
{
    return millis() / 1000; // Convert milliseconds to seconds
    // Note: For production, you might want to use NTP time instead
}

bool VanControlWebServer::isDigitsOnly(const String &str) const
{
    if (str.length() == 0)
        return false;

    for (size_t i = 0; i < str.length(); ++i)
    {
        if (!std::isdigit(str.charAt(i)))
        {
            return false;
        }
    }
    return true;
}

bool VanControlWebServer::isHexOnly(const String &str) const
{
    if (str.length() == 0)
        return false;

    for (size_t i = 0; i < str.length(); ++i)
    {
        if (!std::isxdigit(str.charAt(i)))
        {
            return false;
        }
    }
    return true;
}

void VanControlWebServer::sendError(AsyncWebServerRequest *request, int code, const String &message) const
{
    request->send(code, "text/plain", "Error: " + message + "\n");
}

void VanControlWebServer::handleStoreData(AsyncWebServerRequest *request)
{
    if (!request->hasParam("s") || !request->hasParam("v"))
    {
        sendError(request, 400, "Missing parameters");
        return;
    }

    const String sensorParam = request->getParam("s")->value();
    const String valueParam = request->getParam("v")->value();

    // Validate input
    if (!isDigitsOnly(sensorParam) || !isHexOnly(valueParam))
    {
        sendError(request, 400, "Invalid input");
        return;
    }

    const int sensorId = sensorParam.toInt();
    const unsigned long timestamp = getCurrentTime();

    // Store data in RAM (overwrites existing data with same ID)
    dataStore[sensorId] = DataEntry(timestamp, valueParam);

    request->send(200, "text/plain", "OK\n");
}

void VanControlWebServer::handleFetchData(AsyncWebServerRequest *request)
{
    if (!request->hasParam("f"))
    {
        sendError(request, 400, "Missing parameters");
        return;
    }

    const String fetchParam = request->getParam("f")->value();

    // Validate input
    if (!isDigitsOnly(fetchParam))
    {
        sendError(request, 400, "Invalid input");
        return;
    }

    const int sensorId = fetchParam.toInt();

    // Check if data exists
    const auto it = dataStore.find(sensorId);
    if (it == dataStore.end())
    {
        sendError(request, 460, "Data not found");
        return;
    }

    const DataEntry &entry = it->second;
    const unsigned long currentTime = getCurrentTime();

    // Check if data is too old
    if ((currentTime - entry.timestamp) > DATA_MAX_AGE_SECONDS)
    {
        sendError(request, 461, "Data too old");
        return;
    }

    // Return current time, stored timestamp, and stored value
    const String response = String(currentTime) + "|" + String(entry.timestamp) + "|" + entry.value;
    request->send(200, "text/plain", response);
}

void VanControlWebServer::handleRequest(AsyncWebServerRequest *request)
{
    // Check if this is a store request
    if (request->hasParam("s") && request->hasParam("v"))
    {
        handleStoreData(request);
        return;
    }

    // Check if this is a fetch request
    if (request->hasParam("f"))
    {
        handleFetchData(request);
        return;
    }

    // No valid parameters
    sendError(request, 400, "Missing parameters");
}

bool VanControlWebServer::start()
{
    if (isRunning)
    {
        return true;
    }

    // Set up the main route handler
    server->on("/", HTTP_GET, [this](AsyncWebServerRequest *request)
               { handleRequest(request); });

    // You can also set up a specific route if needed
    server->on("/vancontrol.php", HTTP_GET, [this](AsyncWebServerRequest *request)
               { handleRequest(request); });

    // Handle 404 errors
    server->onNotFound([](AsyncWebServerRequest *request)
                       { request->send(404, "text/plain", "Not Found"); });

    server->begin();
    isRunning = true;

    return true;
}

void VanControlWebServer::stop()
{
    if (isRunning)
    {
        server->end();
        isRunning = false;
    }
}

bool VanControlWebServer::running() const
{
    return isRunning;
}

void VanControlWebServer::clearData()
{
    dataStore.clear();
}

size_t VanControlWebServer::getDataCount() const
{
    return dataStore.size();
}

bool VanControlWebServer::getData(int sensorId, unsigned long &timestamp, String &value) const
{
    const auto it = dataStore.find(sensorId);
    if (it == dataStore.end())
    {
        return false;
    }

    timestamp = it->second.timestamp;
    value = it->second.value;
    return true;
}

void VanControlWebServer::handleBatteryJson(AsyncWebServerRequest* request) {
    if (!batteryManager) {
        sendError(request, 500, "Battery manager not available");
        return;
    }
    
    const TDTBMSData data = batteryManager->getTdtBms();
    const time_t timestamp = batteryManager->getLastTdtUpdateTime();
    const uint32_t timestampMs = batteryManager->getLastTdtUpdateMs();
    
    if (timestampMs == 0) {
        sendError(request, 500, "No status available");
        return;
    }

    // Create JSON document (adjust size if needed)
    DynamicJsonDocument doc(1024);
    
    doc["timestamp"] = timestamp;
    doc["timestampMs"] = timestampMs;
    doc["cellCount"] = data.cellCount;
    doc["tempSensorCount"] = data.tempSensorCount;
    doc["voltage"] = data.voltage / 100.0f; // Convert to actual volts
    doc["current"] = data.current / 10.0f;  // Convert to actual amps
    doc["batteryLevel"] = data.batteryLevel;
    doc["cycleCharge"] = data.cycleCharge / 10.0f; // Convert to actual Ah
    doc["cycles"] = data.cycles;
    doc["problemCode"] = data.problemCode;
    
    // Cell voltages array
    JsonArray cellVoltages = doc.createNestedArray("cellVoltages");
    for (int i = 0; i < data.cellCount && i < 4; ++i) {
        cellVoltages.add(data.cellVoltages[i] / 1000.0f); // Convert to actual volts
    }
    
    // Temperatures array
    JsonArray temperatures = doc.createNestedArray("temperatures");
    for (int i = 0; i < data.tempSensorCount && i < 4; ++i) {
        temperatures.add(data.temperatures[i] / 10.0f); // Convert to actual °C
    }
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    request->send(200, "application/json", jsonString);
}