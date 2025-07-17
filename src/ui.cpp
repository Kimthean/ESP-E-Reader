#include "ui.h"
#include "display.h"
#include "icons.h"
#include "power.h"
#include "sensors.h"
#include "main.h"
#include <Arduino.h>
#include <WiFi.h>

extern EinkDisplayManager display;

// --- UI State Management ---
AppScreen current_screen = SCREEN_MAIN_MENU;
int main_menu_selection = 0; // 0: Books, 1: Settings, 2: Wifi, 3: Clock
unsigned long last_status_update = 0;
const unsigned long STATUS_UPDATE_INTERVAL = 60000; // 1 minute

// --- Helper Functions ---
bool isWifiConnected()
{
    return WiFi.status() == WL_CONNECTED;
}

// --- Menu Definition ---
struct MenuItem
{
    const char *label;
    const unsigned char *icon;
    AppScreen screen;
};

MenuItem main_menu_items[] = {
    {"Books", books_icon, SCREEN_BOOKS},
    {"Settings", settings_icon, SCREEN_SETTINGS},
    {"Wifi", wifi_icon, SCREEN_WIFI},
    {"Clock", clock_icon, SCREEN_CLOCK}};
const int main_menu_item_count = sizeof(main_menu_items) / sizeof(main_menu_items[0]);

void initializeUI()
{
    drawMainMenu(EinkDisplayManager::UPDATE_FULL);
}

void updateUI()
{
    if (millis() - last_status_update > STATUS_UPDATE_INTERVAL)
    {
        // Only redraw the status bar, using a partial update
        display.startDrawing();
        drawStatusBar();
        display.endDrawing();
        display.update(EinkDisplayManager::UPDATE_PARTIAL);
        last_status_update = millis();
    }
}

void handleButtonPress(int button)
{
    resetActivityTimer();

    if (current_screen == SCREEN_MAIN_MENU)
    {
        int old_selection = main_menu_selection;
        if (button == 3) // UP
        {
            main_menu_selection = (main_menu_selection - 1 + main_menu_item_count) % main_menu_item_count;
        }
        else if (button == 1) // DOWN
        {
            main_menu_selection = (main_menu_selection + 1) % main_menu_item_count;
        }
        else if (button == 2) // SELECT
        {
            current_screen = main_menu_items[main_menu_selection].screen;
            // Redraw the whole screen for the new page (placeholder)
            Serial.printf("Entering screen: %d\n", current_screen);
            drawMainMenu(EinkDisplayManager::UPDATE_FULL);
            return;
        }

        if (old_selection != main_menu_selection)
        {
            drawMainMenu(EinkDisplayManager::UPDATE_PARTIAL);
        }
    }
}

void drawMainMenu(EinkDisplayManager::DisplayUpdateMode mode)
{
    display.startDrawing();
    drawStatusBar();

    int grid_x[] = {40, 150};
    int grid_y[] = {80, 200};

    for (int i = 0; i < main_menu_item_count; i++)
    {
        int x = grid_x[i % 2];
        int y = grid_y[i / 2];
        const char *label = main_menu_items[i].label;
        const unsigned char *icon = main_menu_items[i].icon;

        display.m_display.drawXBitmap(x, y, icon, 48, 48, GxEPD_BLACK);
        // display.drawCenteredText(label, y + 60, &FreeMonoBold12pt7b);
        const GFXfont *font = &FreeMonoBold12pt7b;
        display.m_display.setFont(font);
        int16_t x1, y1;
        uint16_t w, h;
        display.m_display.getTextBounds(label, 0, 0, &x1, &y1, &w, &h);
        display.m_display.setCursor(x + (48 - w) / 2, y + 60 + h);
        display.m_display.print(label);

        if (i == main_menu_selection)
        {
            display.m_display.drawRect(x - 10, y - 10, 68, 88, GxEPD_BLACK);
        }
    }

    display.endDrawing();
    display.update(mode);
}

void drawStatusBar()
{
    float battery_voltage = getBatteryVoltage();
    bool charging = isCharging();
    bool wifi_connected = isWifiConnected();
    SensorData sensor_data = readAllSensors();
    String time_str = formatTime(sensor_data.rtc_time);

    display.m_display.fillRect(0, 0, display.m_display.width(), 20, GxEPD_WHITE);

    // Reposition battery icon
    display.drawBatteryIcon(display.m_display.width() - 25, 4, battery_voltage, charging);

    // Draw Wifi Icon
    display.drawWifiIcon(display.m_display.width() - 50, 4, wifi_connected);

    // Use a smaller font for the time
    display.m_display.setFont(&FreeMonoBold9pt7b);
    display.m_display.setCursor(5, 14);
    display.m_display.print(time_str);

    display.m_display.drawLine(0, 20, display.m_display.width(), 20, GxEPD_BLACK);
}