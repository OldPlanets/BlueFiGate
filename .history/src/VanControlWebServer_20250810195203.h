#pragma once

#include <Arduino.h>
#include <map>
#include <memory>

class AsyncWebServerRequest;
class AsyncWebServer;
c

class VanControlWebServer {
private:
    static constexpr int DATA_MAX_AGE_HOURS = 6;
    static constexpr int DATA_MAX_AGE_SECONDS = DATA_MAX_AGE_HOURS * 60 * 60;
    
    struct DataEntry {
        unsigned long timestamp;
        String value;
        
        DataEntry() = default;
        DataEntry(unsigned long ts, const String& val);
    };
    
    std::unique_ptr<AsyncWebServer> server;
    std::map<int, DataEntry> dataStore;
    bool isRunning;
    BatteryManager* batteryManager;
    
    // Helper functions
    unsigned long getCurrentTime() const;
    bool isDigitsOnly(const String& str) const;
    bool isHexOnly(const String& str) const;
    void sendError(AsyncWebServerRequest* request, int code, const String& message) const;
    
    // Request handlers
    void handleStoreData(AsyncWebServerRequest* request);
    void handleFetchData(AsyncWebServerRequest* request);
    void handleRequest(AsyncWebServerRequest* request);
    
public:
    explicit VanControlWebServer(int port = 80);
    ~VanControlWebServer();
    
    
    // Public interface
    bool start();
    void stop();
    [[nodiscard]] bool running() const;
    void clearData();
    [[nodiscard]] size_t getDataCount() const;
    bool getData(int sensorId, unsigned long& timestamp, String& value) const;
};