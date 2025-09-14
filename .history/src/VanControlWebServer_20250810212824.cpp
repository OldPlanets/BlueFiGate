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
               { handleBatteryHtml(request); });

    // You can also set up a specific route if needed
    server->on("/vancontrol.php", HTTP_GET, [this](AsyncWebServerRequest *request)
               { handleRequest(request); });

    server->on("/battery.json", HTTP_GET, [this](AsyncWebServerRequest *request)
               { handleBatteryJson(request); });

    server->on("/battery", HTTP_GET, [this](AsyncWebServerRequest *request)
               { handleBatteryHtml(request); });

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

void VanControlWebServer::handleBatteryJson(AsyncWebServerRequest *request)
{
    if (!batteryManager)
    {
        sendError(request, 500, "Battery manager not available");
        return;
    }

    const TDTBMSData data = batteryManager->getTdtBms();
    const time_t timestamp = batteryManager->getLastTdtUpdateTime();
    const uint32_t timestampMs = batteryManager->getLastTdtUpdateMs();

    if (timestampMs == 0)
    {
        sendError(request, 500, "No status available");
        return;
    }

    DynamicJsonDocument doc(1024);

    doc["timestamp"] = timestamp;
    doc["timestampMs"] = timestampMs;
    doc["cellCount"] = data.cellCount;
    doc["tempSensorCount"] = data.tempSensorCount;
    doc["voltage"] = data.voltage;
    doc["current"] = data.current;
    doc["batteryLevel"] = data.batteryLevel;
    doc["cycleCharge"] = data.cycleCharge;
    doc["cycles"] = data.cycles;
    doc["problemCode"] = data.problemCode;

    // Cell voltages array
    JsonArray cellVoltages = doc.createNestedArray("cellVoltages");
    for (int i = 0; i < data.cellCount && i < 4; ++i)
    {
        cellVoltages.add(data.cellVoltages[i]);
    }

    // Temperatures array
    JsonArray temperatures = doc.createNestedArray("temperatures");
    for (int i = 0; i < data.tempSensorCount && i < 4; ++i)
    {
        temperatures.add(data.temperatures[i]);
    }

    String jsonString;
    serializeJson(doc, jsonString);

    request->send(200, "application/json", jsonString);
}

void VanControlWebServer::handleBatteryHtml(AsyncWebServerRequest *request)
{
    if (!batteryManager)
    {
        sendError(request, 500, "Battery manager not available");
        return;
    }

    const String html = generateBatteryHtml();
    request->send(200, "text/html", html);
}

String VanControlWebServer::generateBatteryHtml() const
{
    const TDTBMSData data = batteryManager->getTdtBms();
    const time_t timestamp = batteryManager->getLastTdtUpdateTime();
    String html = R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Van Battery Monitor</title>
    <style>
        body { 
            font-family: Arial, sans-serif; 
            margin: 20px; 
            background-color: #f0f0f0; 
        }
        .container { 
            max-width: 800px; 
            margin: 0 auto; 
            background: white; 
            padding: 20px; 
            border-radius: 10px; 
            box-shadow: 0 2px 10px rgba(0,0,0,0.1); 
        }
        h1 { 
            color: #333; 
            text-align: center; 
            margin-bottom: 30px; 
        }
        .main-stats { 
            display: grid; 
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); 
            gap: 20px; 
            margin-bottom: 30px; 
        }
        .stat-card { 
            background: #f8f9fa; 
            padding: 20px; 
            border-radius: 8px; 
            text-align: center; 
            border-left: 4px solid #007bff; 
        }
        .stat-card.critical { border-left-color: #dc3545; }
        .stat-card.warning { border-left-color: #ffc107; }
        .stat-card.good { border-left-color: #28a745; }
        .stat-value { 
            font-size: 2em; 
            font-weight: bold; 
            margin: 10px 0; 
        }
        .stat-label { 
            color: #666; 
            font-size: 0.9em; 
        }
        .details { 
            display: grid; 
            grid-template-columns: 1fr 1fr; 
            gap: 20px; 
            margin-top: 20px; 
        }
        .detail-section { 
            background: #f8f9fa; 
            padding: 15px; 
            border-radius: 8px; 
        }
        .detail-section h3 { 
            margin-top: 0; 
            color: #495057; 
        }
        .detail-row { 
            display: flex; 
            justify-content: space-between; 
            padding: 5px 0; 
            border-bottom: 1px solid #dee2e6; 
        }
        .detail-row:last-child { 
            border-bottom: none; 
        }
        .timestamp { 
            text-align: center; 
            color: #666; 
            font-size: 0.9em; 
            margin-top: 20px; 
        }
        .problem-alert { 
            background: #f8d7da; 
            color: #721c24; 
            padding: 15px; 
            border-radius: 8px; 
            margin-bottom: 20px; 
            border: 1px solid #f5c6cb; 
        }
        @media (max-width: 600px) { 
            .details { grid-template-columns: 1fr; }
        }
    </style>
    <script>
        setTimeout(function(){ location.reload(); }, 30000); // Auto-refresh every 30 seconds
    </script>
</head>
<body>
    <div class="container">
        <h1>🔋 Van Battery Monitor</h1>
)";

    // Problem code alert
    if (data.problemCode != 0)
    {
        html += "<div class=\"problem-alert\">⚠️ <strong>Problem Code: " + String(data.problemCode) + "</strong></div>";
    }

    // Main statistics
    html += "<div class=\"main-stats\">";

    // Battery Level (SOC)
    String socClass = "good";
    if (data.batteryLevel < 20)
        socClass = "critical";
    else if (data.batteryLevel < 50)
        socClass = "warning";

    html += "<div class=\"stat-card " + socClass + "\">";
    html += "<div class=\"stat-label\">State of Charge</div>";
    html += "<div class=\"stat-value\">" + String(data.batteryLevel) + "%</div>";
    html += "</div>";

    // Voltage
    float voltage = data.voltage / 100.0f;
    String voltageClass = "good";
    if (voltage < 12.0f)
        voltageClass = "critical";
    else if (voltage < 12.5f)
        voltageClass = "warning";

    html += "<div class=\"stat-card " + voltageClass + "\">";
    html += "<div class=\"stat-label\">Voltage</div>";
    html += "<div class=\"stat-value\">" + String(voltage, 2) + "V</div>";
    html += "</div>";

    // Current
    float current = data.current / 10.0f;
    String currentClass = current < 0 ? "warning" : "good";
    String currentSymbol = current < 0 ? "⬇️" : "⬆️";

    html += "<div class=\"stat-card " + currentClass + "\">";
    html += "<div class=\"stat-label\">Current " + currentSymbol + "</div>";
    html += "<div class=\"stat-value\">" + String(abs(current), 1) + "A</div>";
    html += "</div>";

    html += "</div>"; // End main-stats

    // Details sections
    html += "<div class=\"details\">";

    // Cell Information
    html += "<div class=\"detail-section\">";
    html += "<h3>📱 Cell Information</h3>";
    for (int i = 0; i < data.cellCount && i < 4; ++i)
    {
        float cellVoltage = data.cellVoltages[i] / 1000.0f;
        html += "<div class=\"detail-row\">";
        html += "<span>Cell " + String(i + 1) + ":</span>";
        html += "<span>" + String(cellVoltage, 3) + "V</span>";
        html += "</div>";
    }
    html += "</div>";

    // Temperature Information
    html += "<div class=\"detail-section\">";
    html += "<h3>🌡️ Temperature Sensors</h3>";
    for (int i = 0; i < data.tempSensorCount && i < 4; ++i)
    {
        float temp = data.temperatures[i] / 10.0f;
        html += "<div class=\"detail-row\">";
        html += "<span>Sensor " + String(i + 1) + ":</span>";
        html += "<span>" + String(temp, 1) + "°C</span>";
        html += "</div>";
    }
    html += "</div>";

    html += "</div>"; // End details

    // Additional info
    html += "<div class=\"details\">";
    html += "<div class=\"detail-section\">";
    html += "<h3>📊 Battery Statistics</h3>";
    html += "<div class=\"detail-row\"><span>Cycle Charge:</span><span>" + String(data.cycleCharge / 10.0f, 1) + " Ah</span></div>";
    html += "<div class=\"detail-row\"><span>Cycles:</span><span>" + String(data.cycles) + "</span></div>";
    html += "<div class=\"detail-row\"><span>Problem Code:</span><span>" + String(data.problemCode) + "</span></div>";
    html += "</div>";
    html += "</div>";

    // Timestamp
    html += "<div class=\"timestamp\">";
    html += "Last Update: " + String(ctime(&timestamp));
    html += "<br>Auto-refresh in 30 seconds";
    html += "</div>";

    html += R"(
    </div>
</body>
</html>
)";

    return html;
}
