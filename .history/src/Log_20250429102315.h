// Log.h
#pragma once

#include <Arduino.h>
#include <vector>
#include <string>
#include <cstdarg>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3,
    CRITICAL = 4,
    NONE = 5
};

#ifndef LOG_LEVEL_SERIAL
#define LOG_LEVEL_SERIAL LogLevel::DEBUG
#endif

#ifndef LOG_LEVEL_GUI
#define LOG_LEVEL_GUI LogLevel::INFO
#endif

struct LogEntry {
    unsigned long timestamp;
    LogLevel level;
    std::string message;
};

class LogClass {
public:
    LogClass() {
        /*logMutex = xSemaphoreCreateMutex();
        if (logMutex == nullptr) {
            Serial.println("Failed to create log mutex!");
        }*/
    }

    ~LogClass() {
        /*
        if (logMutex != nullptr) {
            vSemaphoreDelete(logMutex);
        }*/
    }

    void debug(const char* format, ...) {
        va_list args;
        va_start(args, format);
        log(LogLevel::DEBUG, format, args);
        va_end(args);
    }

    void info(const char* format, ...) {
        va_list args;
        va_start(args, format);
        log(LogLevel::INFO, format, args);
        va_end(args);
    }

    void warn(const char* format, ...) {
        va_list args;
        va_start(args, format);
        log(LogLevel::WARNING, format, args);
        va_end(args);
    }

    void error(const char* format, ...) {
        va_list args;
        va_start(args, format);
        log(LogLevel::ERROR, format, args);
        va_end(args);
    }

    void critical(const char* format, ...) {
        va_list args;
        va_start(args, format);
        log(LogLevel::CRITICAL, format, args);
        va_end(args);
    }

    /*std::vector<LogEntry> getLogEntries() const {
        std::vector<LogEntry> entriesCopy;
        
        if (logMutex != nullptr && xSemaphoreTake(logMutex, portMAX_DELAY) == pdTRUE) {
            entriesCopy = logEntries;
            xSemaphoreGive(logMutex);
        }
        
        return entriesCopy;
    }

    void clearLogEntries() {
        if (logMutex != nullptr && xSemaphoreTake(logMutex, portMAX_DELAY) == pdTRUE) {
            logEntries.clear();
            xSemaphoreGive(logMutex);
        }
    }*/

private:
    std::vector<LogEntry> logEntries;
    SemaphoreHandle_t logMutex;

    void log(LogLevel level, const char* format, va_list args) {
        if (level < LOG_LEVEL_SERIAL && level < LOG_LEVEL_GUI) {
            return;
        }

        char buffer[256];
        vsnprintf(buffer, sizeof(buffer), format, args);
        
        unsigned long currentTime = millis();
        
        if (level >= LOG_LEVEL_SERIAL) {
            unsigned long seconds = currentTime / 1000;
            unsigned long minutes = seconds / 60;
            seconds %= 60;
            unsigned long milliseconds = currentTime % 1000;
            
            Serial.printf("%lum %lu.%02lus %s: %s\n", minutes, seconds, milliseconds / 10, 
                          levelToString(level), buffer);
        }
        
        /*if (level >= LOG_LEVEL_GUI) {
            if (logMutex != nullptr && xSemaphoreTake(logMutex, portMAX_DELAY) == pdTRUE) {
                LogEntry entry;
                entry.timestamp = currentTime;
                entry.level = level;
                entry.message = buffer;
                logEntries.push_back(entry);
                
                constexpr size_t MAX_LOG_ENTRIES = 100;
                if (logEntries.size() > MAX_LOG_ENTRIES) {
                    logEntries.erase(logEntries.begin());
                }
                
                xSemaphoreGive(logMutex);
            }
        }*/
    }
    
    const char* levelToString(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG:    return "DEBUG";
            case LogLevel::INFO:     return "INFO";
            case LogLevel::WARNING:  return "WARNING";
            case LogLevel::ERROR:    return "ERROR";
            case LogLevel::CRITICAL: return "CRITICAL";
            default:       return "UNKNOWN";
        }
    }
};

inline LogClass& getLogInstance() {
    static LogClass instance;
    return instance;
}

#define Log getLogInstance()