#pragma once

#include <Arduino.h>
#include <map>
#include <memory>

class AsyncWebServerRequest;
class AsyncWebServer;
class BatteryManager;

class VanControlWebServer {
private:
    std::unique_ptr<AsyncWebServer> server;
    bool isRunning;
    BatteryManager* batteryManager;
    
    // Helper functions
    unsigned long getCurrentTime() const;
    bool isDigitsOnly(const String& str) const;
    bool isHexOnly(const String& str) const;
    void sendError(AsyncWebServerRequest* request, int code, const String& message) const;
    
    // Request handlers
    void handleBatteryJson(AsyncWebServerRequest* request);
    void handleBatteryHtml(AsyncWebServerRequest* request);

    String generateBatteryHtml() const;
    
public:
    explicit VanControlWebServer(BatteryManager* batteryManager, int port = 80);
    ~VanControlWebServer();
    
    
    // Public interface
    bool start();
    void stop();
    [[nodiscard]] bool running() const;
    void clearData();
    [[nodiscard]] size_t getDataCount() const;
    bool getData(int sensorId, unsigned long& timestamp, String& value) const;
};