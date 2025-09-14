#include "EPaperGui.h"
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <SPI.h>


static constexpr float TEMP_THRESHOLD = 0.2f;
static constexpr float HUMIDITY_THRESHOLD = 3.0f;
static constexpr float BATTERY_CRITICAL = 20.0f; // 20% battery critical

#include "EPaperGui.h"

EPaperGui::EPaperGui()
    : display(nullptr), displayInitialized(false), tempInside(0), tempOutside(0), 
      humidityInside(0), humidityOutside(0), batteryInside(100), batteryOutside(100) {
    
    if (!rtcData.initialized) {
        rtcData.tempInside = -999;
        rtcData.tempOutside = -999;
        rtcData.humidityInside = -999;
        rtcData.humidityOutside = -999;
        rtcData.batteryInside = 100;
        rtcData.batteryOutside = 100;
        rtcData.initialized = true;
    }
}

EPaperGui::~EPaperGui() {
    end();
}

void EPaperGui::initializeDisplay() {
    if (displayInitialized) {
        return;
    }
    
    // Initialize pins
    pinMode(DISPLAY_CS, OUTPUT);
    pinMode(DISPLAY_DC, OUTPUT);
    pinMode(DISPLAY_RST, OUTPUT);
    pinMode(DISPLAY_BUSY, INPUT);
    
    // Create display instance
    display = new GxEPD2_BW<GxEPD2_213_BN, GxEPD2_213_BN::HEIGHT>(
        GxEPD2_213_BN(DISPLAY_CS, DISPLAY_DC, DISPLAY_RST, DISPLAY_BUSY)
    );
    
    // Initialize SPI
    SPI.begin(DISPLAY_SCL, -1 /* MISO not used */, DISPLAY_SDA, DISPLAY_CS);
    
    // Initialize display
    display->init(115200, true, 50, false);
    display->setRotation(1);
    display->setTextColor(GxEPD_BLACK);
    
    displayInitialized = true;
}

void EPaperGui::end() {
    if (displayInitialized && display) {
        display->hibernate();
        delete display;
        display = nullptr;
        displayInitialized = false;
        SPI.end();
    }
}

bool EPaperGui::shouldUpdate() {
    return (abs(tempInside - rtcData.tempInside) > TEMP_THRESHOLD ||
            abs(tempOutside - rtcData.tempOutside) > TEMP_THRESHOLD ||
            abs(humidityInside - rtcData.humidityInside) > HUMIDITY_THRESHOLD ||
            abs(humidityOutside - rtcData.humidityOutside) > HUMIDITY_THRESHOLD ||
            (batteryInside <= BATTERY_CRITICAL && rtcData.batteryInside > BATTERY_CRITICAL) ||
            (batteryOutside <= BATTERY_CRITICAL && rtcData.batteryOutside > BATTERY_CRITICAL) ||
            (batteryInside > BATTERY_CRITICAL && rtcData.batteryInside <= BATTERY_CRITICAL) ||
            (batteryOutside > BATTERY_CRITICAL && rtcData.batteryOutside <= BATTERY_CRITICAL));
}

void EPaperGui::updateGui() {
    if (!shouldUpdate()) {
        return; // No significant changes, skip update
    }
    
    // Initialize display only when we need to update
    initializeDisplay();
    
    display->setFullWindow();
    display->firstPage();
    
    do {
        display->fillScreen(GxEPD_WHITE);
        
        // New layout: Icons on left, values on right
        drawNewLayout();
        
    } while (display->nextPage());
    
    // Update RTC memory with current values
    rtcData.tempInside = tempInside;
    rtcData.tempOutside = tempOutside;
    rtcData.humidityInside = humidityInside;
    rtcData.humidityOutside = humidityOutside;
    rtcData.batteryInside = batteryInside;
    rtcData.batteryOutside = batteryOutside;
    
    // Put display to sleep after update
    end();
}

void EPaperGui::drawNewLayout() {
    // Display dimensions when rotated: 250x122
    // Layout: [Icon] [Inside Value] [Outside Value]
    
    int iconAreaWidth = 35;  // Space for icons on the left
    int valueAreaWidth = (display->width() - iconAreaWidth) / 2; // Split remaining space
    int insideX = iconAreaWidth;
    int outsideX = iconAreaWidth + valueAreaWidth;
    
    // Draw column headers
    display->setFont(&FreeSans9pt7b);
    drawCenteredText("INSIDE", insideX, 15, valueAreaWidth, &FreeSans9pt7b);
    drawCenteredText("OUTSIDE", outsideX, 15, valueAreaWidth, &FreeSans9pt7b);
    
    // Draw vertical separator lines
    display->drawLine(iconAreaWidth, 0, iconAreaWidth, display->height(), GxEPD_BLACK);
    display->drawLine(outsideX, 0, outsideX, display->height(), GxEPD_BLACK);
    
    // Temperature row
    int tempRowY = 25;
    drawValueRow(epd_icon_thermometer, tempRowY, 
                formatTemperature(tempInside), formatTemperature(tempOutside),
                &FreeSans18pt7b);
    
    // Humidity/Battery row
    int humidityRowY = tempRowY + 50; // Leave space for temperature
    
    // Check if either battery is critical
    bool insideBattCritical = batteryInside <= BATTERY_CRITICAL;
    bool outsideBattCritical = batteryOutside <= BATTERY_CRITICAL;
    
    if (insideBattCritical || outsideBattCritical) {
        // Show battery row
        String insideValue = insideBattCritical ? 
            String(batteryInside, 0) + "%" : formatHumidity(humidityInside);
        String outsideValue = outsideBattCritical ? 
            String(batteryOutside, 0) + "%" : formatHumidity(humidityOutside);
            
        drawValueRow(epd_icon_batteryempty, humidityRowY, 
                    insideValue, outsideValue, &FreeSans12pt7b);
    } else {
        // Show humidity row
        drawValueRow(epd_icon_humidity, humidityRowY, 
                    formatHumidity(humidityInside), formatHumidity(humidityOutside),
                    &FreeSans12pt7b);
    }
}

void EPaperGui::drawValueRow(const Icon& icon, int y, const String& insideValue, 
                            const String& outsideValue, const GFXfont* font) {
    int iconAreaWidth = 50;
    int valueAreaWidth = (display->width() - iconAreaWidth) / 2;
    int insideX = iconAreaWidth;
    int outsideX = iconAreaWidth + valueAreaWidth;
    
    // Draw icon centered in icon area
    int iconX = (iconAreaWidth - icon.width) / 2;
    int iconY = y;
    display->drawBitmap(iconX, iconY, icon.data, icon.width, icon.height, 
                       GxEPD_BLACK, GxEPD_WHITE);
    
    // Draw values
    display->setFont(font);
    
    // Inside value
    drawCenteredText(insideValue.c_str(), insideX, y + icon.height/2 + 6, 
                    valueAreaWidth, font);
    
    // Outside value  
    drawCenteredText(outsideValue.c_str(), outsideX, y + icon.height/2 + 6, 
                    valueAreaWidth, font);
}

void EPaperGui::drawCenteredText(const char* text, int x, int y, int width, const GFXfont* font) {
    display->setFont(font);
    int16_t tbx, tby;
    uint16_t tbw, tbh;
    display->getTextBounds(text, 0, 0, &tbx, &tby, &tbw, &tbh);
    
    int textX = x + (width - tbw) / 2 - tbx;
    display->setCursor(textX, y);
    display->print(text);
}

String EPaperGui::formatTemperature(float temp) {
    return String(temp, 1) + "°C";
}

String EPaperGui::formatHumidity(float humidity) {
    return String(humidity, 0) + "%";
}

void EPaperGui::setValues(float tempIn, float tempOut, float humIn, float humOut, 
                         float battIn, float battOut) {
    tempInside = tempIn;
    tempOutside = tempOut;
    humidityInside = humIn;
    humidityOutside = humOut;
    batteryInside = battIn;
    batteryOutside = battOut;
}