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
        
        // Draw vertical divider line
        int centerX = display->width() / 2;
        display->drawLine(centerX, 0, centerX, display->height(), GxEPD_BLACK);
        
        // Draw inside section (left half)
        drawSection("INSIDE", tempInside, humidityInside, batteryInside, 
                   0, 0, centerX - 1, display->height());
        
        // Draw outside section (right half)
        drawSection("OUTSIDE", tempOutside, humidityOutside, batteryOutside, 
                   centerX + 1, 0, centerX - 1, display->height());
        
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

void EPaperGui::drawSection(const char* title, float temp, float humidity, float battery, 
                           int x, int y, int width, int height) {
    // Draw title
    display->setFont(&FreeSans9pt7b);
    drawCenteredText(title, x, y + 15, width, &FreeSans9pt7b);
    
    // Temperature section (top priority, large)
    int tempY = y + 35;
    display->setFont(&FreeSans18pt7b);
    
    // Draw thermometer icon
    int iconX = x + (width - epd_icon_thermometer.width) / 2;
    display->drawBitmap(iconX, tempY, epd_icon_thermometer.data, 
                       epd_icon_thermometer.width, epd_icon_thermometer.height, 
                       GxEPD_BLACK, GxEPD_WHITE);
    
    // Draw temperature text
    String tempStr = formatTemperature(temp);
    drawCenteredText(tempStr.c_str(), x, tempY + epd_icon_thermometer.height + 25, 
                    width, &FreeSans18pt7b);
    
    // Humidity/Battery section (lower priority, smaller)
    int secondaryY = tempY + epd_icon_thermometer.height + 50;
    display->setFont(&FreeSans12pt7b);
    
    if (battery <= BATTERY_CRITICAL) {
        // Show battery warning instead of humidity
        int battIconX = x + (width - epd_icon_batteryempty.width) / 2;
        display->drawBitmap(battIconX, secondaryY, epd_icon_batteryempty.data, 
                           epd_icon_batteryempty.width, epd_icon_batteryempty.height, 
                           GxEPD_BLACK, GxEPD_WHITE);
        
        String battStr = String(battery, 0) + "%";
        drawCenteredText(battStr.c_str(), x, secondaryY + epd_icon_batteryempty.height + 20, 
                        width, &FreeSans12pt7b);
    } else {
        // Show humidity
        int humIconX = x + (width - epd_icon_humidity.width) / 2;
        display->drawBitmap(humIconX, secondaryY, epd_icon_humidity.data, 
                           epd_icon_humidity.width, epd_icon_humidity.height, 
                           GxEPD_BLACK, GxEPD_WHITE);
        
        String humStr = formatHumidity(humidity);
        drawCenteredText(humStr.c_str(), x, secondaryY + epd_icon_humidity.height + 20, 
                        width, &FreeSans12pt7b);
    }
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