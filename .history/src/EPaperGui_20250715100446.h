#pragma once

#include <GxEPD2_BW.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <SPI.h>
#include "./assets/icons.h"

// Pin definitions - you may need to adjust these
#ifndef DISPLAY_CS
#define DISPLAY_CS 5
#endif
#ifndef DISPLAY_DC
#define DISPLAY_DC 17
#endif
#ifndef DISPLAY_RST
#define DISPLAY_RST 16
#endif
#ifndef DISPLAY_BUSY
#define DISPLAY_BUSY 4
#endif
#ifndef DISPLAY_SCL
#define DISPLAY_SCL 18
#endif
#ifndef DISPLAY_SDA
#define DISPLAY_SDA 23
#endif

#define DISPLAY_CS 7
#define DISPLAY_DC 9
#define DISPLAY_RST 0
#define DISPLAY_BUSY 4
#define DISPLAY_SDA 6
#define DISPLAY_SCL 5

// RTC memory structure to persist across deep sleep
RTC_DATA_ATTR struct {
    float tempInside;
    float tempOutside;
    float humidityInside;
    float humidityOutside;
    float batteryInside;
    float batteryOutside;
    bool initialized;
} rtcData = {0};

class EPaperGui {
private:
    GxEPD2_BW<GxEPD2_213_BN, GxEPD2_213_BN::HEIGHT>* display;
    bool displayInitialized;
    
    // Current values
    float tempInside;
    float tempOutside;
    float humidityInside;
    float humidityOutside;
    float batteryInside;
    float batteryOutside;
    
    // Thresholds
    static constexpr float TEMP_THRESHOLD = 0.2f;
    static constexpr float HUMIDITY_THRESHOLD = 3.0f;
    static constexpr float BATTERY_CRITICAL = 20.0f; // 20% battery critical
    
    // Helper methods
    bool shouldUpdate();
    void initializeDisplay();
    void drawSection(const char* title, float temp, float humidity, float battery, 
                    int x, int y, int width, int height);
    void drawCenteredText(const char* text, int x, int y, int width, const GFXfont* font);
    String formatTemperature(float temp);
    String formatHumidity(float humidity);
    
public:
    EPaperGui();
    ~EPaperGui();
    
    void updateGui();
    void end();
    
    // Setters
    void setTemperatureInside(float temp) { tempInside = temp; }
    void setTemperatureOutside(float temp) { tempOutside = temp; }
    void setHumidityInside(float humidity) { humidityInside = humidity; }
    void setHumidityOutside(float humidity) { humidityOutside = humidity; }
    void setBatteryInside(float battery) { batteryInside = battery; }
    void setBatteryOutside(float battery) { batteryOutside = battery; }
    
    // Convenience method to set all values at once
    void setValues(float tempIn, float tempOut, float humIn, float humOut, 
                   float battIn, float battOut);
};
