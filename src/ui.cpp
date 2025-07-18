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
    // Clear screen to eliminate any startup ghosting
    display.wipeScreen();
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
            Serial.printf("Entering screen: %d\n", current_screen);

            // Clear screen to eliminate ghosting before drawing new screen
            display.wipeScreen();

            // Draw the appropriate screen based on selection
            switch (current_screen)
            {
            case SCREEN_BOOKS:
                drawBooksScreen(EinkDisplayManager::UPDATE_FAST);
                break;
            case SCREEN_SETTINGS:
                drawSettingsScreen(EinkDisplayManager::UPDATE_FAST);
                break;
            case SCREEN_WIFI:
                drawWifiScreen(EinkDisplayManager::UPDATE_FAST);
                break;
            case SCREEN_CLOCK:
                drawClockScreen(EinkDisplayManager::UPDATE_FAST);
                break;
            default:
                drawMainMenu(EinkDisplayManager::UPDATE_FAST);
                break;
            }
            return;
        }

        if (old_selection != main_menu_selection)
        {
            drawMainMenu(EinkDisplayManager::UPDATE_PARTIAL);
        }
    }
    else
    {
        // Handle button presses on sub-screens based on navigation strategy
        if (button == 3) // UP button - always goes back
        {
            current_screen = SCREEN_MAIN_MENU;
            Serial.println("UP pressed - returning to main menu");
            
            // Clear screen to eliminate ghosting before returning to main menu
            display.wipeScreen();
            drawMainMenu(EinkDisplayManager::UPDATE_FAST);
        }
        else if (button == 2) // SELECT button - context-specific action
        {
            handleSelectAction();
        }
        else if (button == 1) // DOWN button - context-specific navigation
        {
            handleDownAction();
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

    // Draw button hints for main menu
    drawButtonHints("Up", "Select", "Down");
    
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

void drawButtonHints(const char* leftHint, const char* centerHint, const char* rightHint)
{
    int screenWidth = display.m_display.width();
    int bottomY = display.m_display.height() - 5;
    
    display.m_display.setFont(&FreeMonoBold9pt7b);
    
    // Left hint (UP button)
    display.m_display.setCursor(5, bottomY);
    display.m_display.print("[UP: ");
    display.m_display.print(leftHint);
    display.m_display.print("]");
    
    // Center hint (SELECT button)
    String centerText = String("[SEL: ") + centerHint + "]";
    int16_t x1, y1;
    uint16_t w, h;
    display.m_display.getTextBounds(centerText.c_str(), 0, 0, &x1, &y1, &w, &h);
    display.m_display.setCursor((screenWidth - w) / 2, bottomY);
    display.m_display.print(centerText);
    
    // Right hint (DOWN button)
    String rightText = String("[DN: ") + rightHint + "]";
    display.m_display.getTextBounds(rightText.c_str(), 0, 0, &x1, &y1, &w, &h);
    display.m_display.setCursor(screenWidth - w - 5, bottomY);
    display.m_display.print(rightText);
}

void handleSelectAction()
{
    Serial.printf("SELECT pressed on screen: %d\n", current_screen);
    
    switch (current_screen)
    {
        case SCREEN_BOOKS:
            // Future: Open book selection or reading interface
            Serial.println("Books: SELECT action - placeholder");
            break;
            
        case SCREEN_SETTINGS:
            // Future: Enter settings menu
            Serial.println("Settings: SELECT action - placeholder");
            break;
            
        case SCREEN_WIFI:
            // Toggle WiFi connection
            Serial.println("WiFi: Toggling connection");
            // TODO: Implement WiFi toggle
            drawWifiScreen(EinkDisplayManager::UPDATE_PARTIAL);
            break;
            
        case SCREEN_CLOCK:
            // Future: Enter time setting mode
            Serial.println("Clock: SELECT action - placeholder");
            break;
            
        default:
            break;
    }
}

void handleDownAction()
{
    Serial.printf("DOWN pressed on screen: %d\n", current_screen);
    
    switch (current_screen)
    {
        case SCREEN_BOOKS:
            // Future: Next book or page
            Serial.println("Books: DOWN action - placeholder");
            break;
            
        case SCREEN_SETTINGS:
            // Future: Next setting item
            Serial.println("Settings: DOWN action - placeholder");
            break;
            
        case SCREEN_WIFI:
            // Scan for networks
            Serial.println("WiFi: Scanning for networks");
            // TODO: Implement WiFi scan
            break;
            
        case SCREEN_CLOCK:
            // Future: Change time format
            Serial.println("Clock: DOWN action - placeholder");
            break;
            
        default:
            break;
    }
}

// --- Screen Drawing Functions ---

void drawBooksScreen(EinkDisplayManager::DisplayUpdateMode mode)
{
    display.startDrawing();
    drawStatusBar();
    
    display.m_display.setFont(&FreeMonoBold18pt7b);
    display.drawCenteredText("Books", 100, &FreeMonoBold18pt7b);
    display.drawCenteredText("Coming Soon...", 150, &FreeMonoBold12pt7b);
    
    // Draw button hints at bottom
    drawButtonHints("Back", "OK", "Next");
    
    display.endDrawing();
    display.update(mode);
}

void drawSettingsScreen(EinkDisplayManager::DisplayUpdateMode mode)
{
    display.startDrawing();
    drawStatusBar();
    
    display.m_display.setFont(&FreeMonoBold18pt7b);
    display.drawCenteredText("Settings", 100, &FreeMonoBold18pt7b);
    display.drawCenteredText("Coming Soon...", 150, &FreeMonoBold12pt7b);
    
    // Draw button hints
    drawButtonHints("Back", "OK", "Next");
    
    display.endDrawing();
    display.update(mode);
}

void drawWifiScreen(EinkDisplayManager::DisplayUpdateMode mode)
{
    display.startDrawing();
    drawStatusBar();
    
    display.m_display.setFont(&FreeMonoBold18pt7b);
    display.drawCenteredText("WiFi", 100, &FreeMonoBold18pt7b);
    
    String status = isWifiConnected() ? "Connected" : "Disconnected";
    display.drawCenteredText(status.c_str(), 150, &FreeMonoBold12pt7b);
    
    // Draw button hints
    drawButtonHints("Back", "Toggle", "Scan");
    
    display.endDrawing();
    display.update(mode);
}

void drawClockScreen(EinkDisplayManager::DisplayUpdateMode mode)
{
    display.startDrawing();
    drawStatusBar();
    
    display.m_display.setFont(&FreeMonoBold18pt7b);
    display.drawCenteredText("Clock", 100, &FreeMonoBold18pt7b);
    display.drawCenteredText("Coming Soon...", 150, &FreeMonoBold12pt7b);
    
    // Draw button hints
    drawButtonHints("Back", "Set", "Format");
    
    display.endDrawing();
    display.update(mode);
}