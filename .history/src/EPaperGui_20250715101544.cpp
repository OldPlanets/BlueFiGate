#include "EPaperGui.h"
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <SPI.h>
#include "./assets/icons.h"

static constexpr float TEMP_THRESHOLD = 0.2f;
static constexpr float HUMIDITY_THRESHOLD = 3.0f;
static constexpr float BATTERY_CRITICAL = 20.0f; // 20% battery critical

EPaperGui::EPaperGui()
    : display(nullptr), displayInitialized(false), tempInside(0), tempOutside(0),
      humidityInside(0), humidityOutside(0), batteryInside(100), batteryOutside(100)
{

    if (!rtcData.initialized)
    {
        rtcData.tempInside = -999;
        rtcData.tempOutside = -999;
        rtcData.humidityInside = -999;
        rtcData.humidityOutside = -999;
        rtcData.batteryInside = 100;
        rtcData.batteryOutside = 100;
        rtcData.initialized = true;
    }
}

EPaperGui::~EPaperGui()
{
    end();
}

void EPaperGui::initializeDisplay()
{
    if (displayInitialized)
    {
        return;
    }

    // Initialize pins
    pinMode(DISPLAY_CS, OUTPUT);
    pinMode(DISPLAY_DC, OUTPUT);
    pinMode(DISPLAY_RST, OUTPUT);
    pinMode(DISPLAY_BUSY, INPUT);

    // Create display instance
    display = new GxEPD2_BW<GxEPD2_213_BN, GxEPD2_213_BN::HEIGHT>(
        GxEPD2_213_BN(DISPLAY_CS, DISPLAY_DC, DISPLAY_RST, DISPLAY_BUSY));

    // Initialize SPI
    SPI.begin(DISPLAY_SCL, -1 /* MISO not used */, DISPLAY_SDA, DISPLAY_CS);

    // Initialize display
    display->init(115200, true, 50, false);
    display->setRotation(1);
    display->setTextColor(GxEPD_BLACK);

    displayInitialized = true;
}

void EPaperGui::end()
{
    if (displayInitialized && display)
    {
        display->hibernate();
        delete display;
        display = nullptr;
        displayInitialized = false;
        SPI.end();
    }
}

bool EPaperGui::shouldUpdate()
{
    return (abs(tempInside - rtcData.tempInside) > TEMP_THRESHOLD ||
            abs(tempOutside - rtcData.tempOutside) > TEMP_THRESHOLD ||
            abs(humidityInside - rtcData.humidityInside) > HUMIDITY_THRESHOLD ||
            abs(humidityOutside - rtcData.humidityOutside) > HUMIDITY_THRESHOLD ||
            (batteryInside <= BATTERY_CRITICAL && rtcData.batteryInside > BATTERY_CRITICAL) ||
            (batteryOutside <= BATTERY_CRITICAL && rtcData.batteryOutside > BATTERY_CRITICAL) ||
            (batteryInside > BATTERY_CRITICAL && rtcData.batteryInside <= BATTERY_CRITICAL) ||
            (batteryOutside > BATTERY_CRITICAL && rtcData.batteryOutside <= BATTERY_CRITICAL));
}

void EPaperGui::updateGui()
{
    if (!shouldUpdate())
    {
        return; // No significant changes, skip update
    }

    // Initialize display only when we need to update
    initializeDisplay();

    display->setFullWindow();
    display->firstPage();

    do
    {
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
    // More compact layout for 122px height
    int currentY = y + 2;
    
    // Title (FreeSans9pt7b)
    display->setFont(&FreeSans9pt7b);
    drawCenteredText(title, x, currentY + 12, width, &FreeSans9pt7b);
    currentY += 18; // 18px total
    
    // Temperature section with icon
    int iconX = x + (width - epd_icon_thermometer.width) / 2;
    display->drawBitmap(iconX, currentY, epd_icon_thermometer.data, 
                       epd_icon_thermometer.width, epd_icon_thermometer.height, 
                       GxEPD_BLACK, GxEPD_WHITE);
    currentY += epd_icon_thermometer.height + 1; // 38px total
    
    // Temperature text (FreeSans18pt7b - this is the priority)
    display->setFont(&FreeSans18pt7b);
    String tempStr = formatTemperature(temp);
    drawCenteredText(tempStr.c_str(), x, currentY + 18, width, &FreeSans18pt7b);
    currentY += 24; // 24px total
    
    // Running total: 18 + 38 + 24 = 80px
    // Remaining: 122 - 80 = 42px
    
    // Humidity/Battery section - simplified (no icon for humidity to save space)
    display->setFont(&FreeSans12pt7b);
    
    if (battery <= BATTERY_CRITICAL) {
        // Show battery warning with icon
        int battIconX = x + (width - epd_icon_batteryempty.width) / 2;
        display->drawBitmap(battIconX, currentY, epd_icon_batteryempty.data, 
                           epd_icon_batteryempty.width, epd_icon_batteryempty.height, 
                           GxEPD_BLACK, GxEPD_WHITE);
        currentY += epd_icon_batteryempty.height + 1; // 26px
        
        String battStr = "BATT: " + String(battery, 0) + "%";
        drawCenteredText(battStr.c_str(), x, currentY + 12, width, &FreeSans12pt7b);
        // Total: 26 + 16 = 42px (perfect!)
    } else {
        // Show humidity without icon to save space
        String humStr = "RH: " + formatHumidity(humidity);
        drawCenteredText(humStr.c_str(), x, currentY + 12, width, &FreeSans12pt7b);
        currentY += 16;
        
        // Optional: small humidity icon if there's space
        if (currentY + epd_icon_humidity.height <= y + height - 5) {
            int humIconX = x + (width - epd_icon_humidity.width) / 2;
            display->drawBitmap(humIconX, currentY, epd_icon_humidity.data, 
                               epd_icon_humidity.width, epd_icon_humidity.height, 
                               GxEPD_BLACK, GxEPD_WHITE);
        }
    }
}

void EPaperGui::drawCenteredText(const char *text, int x, int y, int width, const GFXfont *font)
{
    display->setFont(font);
    int16_t tbx, tby;
    uint16_t tbw, tbh;
    display->getTextBounds(text, 0, 0, &tbx, &tby, &tbw, &tbh);

    int textX = x + (width - tbw) / 2 - tbx;
    display->setCursor(textX, y);
    display->print(text);
}

String EPaperGui::formatTemperature(float temp)
{
    return String(temp, 1) + "°C";
}

String EPaperGui::formatHumidity(float humidity)
{
    return String(humidity, 0) + "%";
}

void EPaperGui::setValues(float tempIn, float tempOut, float humIn, float humOut,
                          float battIn, float battOut)
{
    tempInside = tempIn;
    tempOutside = tempOut;
    humidityInside = humIn;
    humidityOutside = humOut;
    batteryInside = battIn;
    batteryOutside = battOut;
}