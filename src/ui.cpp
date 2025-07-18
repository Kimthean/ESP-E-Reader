#include "ui.h"
#include "display.h"
#include "power.h"
#include "sensors.h"
#include "main.h"
#include "ui/wifi/wifi_screen.h"
#include "ui/files/files_screen.h"
#include <Arduino.h>
#include <WiFi.h>

extern EinkDisplayManager display;

// Screen instances
WiFiScreen wifiScreen;
FilesScreen filesScreen;

// --- UI State Management ---
AppScreen current_screen = SCREEN_MAIN_MENU;
int main_menu_selection = 0; // 0: Books, 1: Settings, 2: Wifi, 3: Clock
unsigned long last_status_update = 0;
const unsigned long STATUS_UPDATE_INTERVAL = 300000; // 5 minutes

// --- Helper Functions ---
bool isWifiConnected()
{
    return WiFi.status() == WL_CONNECTED;
}

// --- Menu Definition ---
struct MenuItem
{
    const char *label;
    AppScreen screen;
};

MenuItem main_menu_items[] = {
    {"Books", SCREEN_BOOKS},
    {"Files", SCREEN_FILES},
    {"Clock", SCREEN_CLOCK},
    {"Wifi", SCREEN_WIFI},
    {"Settings", SCREEN_SETTINGS},
};
const int main_menu_item_count = sizeof(main_menu_items) / sizeof(main_menu_items[0]);

void initializeUI()
{
    // Clear screen to eliminate any startup ghosting
    display.wipeScreen();

    // Reload WiFi configuration now that SPIFFS is initialized
    wifiScreen.loadWiFiConfig();

    // drawCurrentScreen(EinkDisplayManager::UPDATE_FULL);
}

void drawCurrentScreen(EinkDisplayManager::DisplayUpdateMode mode)
{
    // Draw the current screen based on the current_screen state
    switch (current_screen)
    {
    case SCREEN_MAIN_MENU:
        drawMainMenu(mode);
        break;
    case SCREEN_BOOKS:
        drawBooksScreen(mode);
        break;
    case SCREEN_SETTINGS:
        drawSettingsScreen(mode);
        break;
    case SCREEN_WIFI:
        drawWifiScreen(mode);
        break;
    case SCREEN_CLOCK:
        drawClockScreen(mode);
        break;

    case SCREEN_FILES:
        drawFilesScreen(mode);
        break;
    default:
        current_screen = SCREEN_MAIN_MENU;
        drawMainMenu(mode);
        break;
    }
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

    // Update WiFi screen if active (for web server handling)
    if (current_screen == SCREEN_WIFI)
    {
        wifiScreen.update();
    }
}

void handleButtonPress(int button)
{
    // Reset activity timer on any button press
    resetActivityTimer();

    // Reset partial update counter to prevent auto-wipe during navigation
    display.resetPartialUpdateCount();

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
            case SCREEN_FILES:
                drawFilesScreen(EinkDisplayManager::UPDATE_FAST);
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
        if (button == 3) // UP button - context-specific or back to menu
        {
            if (current_screen == SCREEN_WIFI)
            {
                // Handle WiFi screen up action (navigation within WiFi screen)
                wifiScreen.handleUpAction();
                wifiScreen.draw(EinkDisplayManager::UPDATE_PARTIAL);
            }
            else if (current_screen == SCREEN_FILES)
            {
                // Handle Files screen up action (navigation within Files screen)
                filesScreen.handleUpAction();
            }
            else
            {
                // For other screens, go back to main menu
                current_screen = SCREEN_MAIN_MENU;
                Serial.println("UP pressed - returning to main menu");

                // Clear screen to eliminate ghosting before returning to main menu
                display.wipeScreen();
                drawMainMenu(EinkDisplayManager::UPDATE_FAST);
            }
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

    // Improved layout parameters (adjusted for new status bar height)
    int start_y = 85;
    int item_height = 45;
    int margin = 30;
    int selection_padding = 8;

    // Draw menu items in vertical stack
    for (int i = 0; i < main_menu_item_count; i++)
    {
        int y = start_y + (i * item_height);
        const char *label = main_menu_items[i].label;

        // Set font for menu items
        display.m_display.setFont(&FreeMono12pt7b);

        // Calculate text dimensions for proper centering
        int16_t x1, y1;
        uint16_t w, h;
        display.m_display.getTextBounds(label, 0, 0, &x1, &y1, &w, &h);

        // Draw selection indicator with better styling
        if (i == main_menu_selection)
        {
            // Draw rounded selection background
            int rect_x = margin;
            int rect_y = y - h - selection_padding;
            int rect_w = display.m_display.width() - (2 * margin);
            int rect_h = h + (2 * selection_padding);

            // Draw selection rectangle with border
            display.m_display.fillRect(rect_x, rect_y, rect_w, rect_h, GxEPD_BLACK);
            display.m_display.drawRect(rect_x - 1, rect_y - 1, rect_w + 2, rect_h + 2, GxEPD_BLACK);
            display.m_display.setTextColor(GxEPD_WHITE);

            // Add selection arrows
            display.m_display.setCursor(rect_x + 5, y);
            display.m_display.print("> ");
            display.m_display.setCursor(rect_x + rect_w - 15, y);
            display.m_display.print(" <");
        }
        else
        {
            display.m_display.setTextColor(GxEPD_BLACK);
            // Draw subtle border for unselected items
            int rect_x = margin + 5;
            int rect_y = y - h - selection_padding + 2;
            int rect_w = display.m_display.width() - (2 * margin) - 10;
            int rect_h = h + (2 * selection_padding) - 4;
            display.m_display.drawRect(rect_x, rect_y, rect_w, rect_h, GxEPD_BLACK);
        }

        // Draw menu item text with proper centering
        int text_x = (display.m_display.width() - w) / 2;
        if (i == main_menu_selection)
        {
            text_x = (display.m_display.width() - w) / 2; // Keep centered even with arrows
        }
        display.m_display.setCursor(text_x, y);
        display.m_display.print(label);

        // Reset text color
        display.m_display.setTextColor(GxEPD_BLACK);
    }

    display.endDrawing();
    display.update(mode);
}

void drawStatusBar()
{
    float battery_voltage = getBatteryVoltage();
    bool charging = isCharging();
    bool wifi_connected = isWifiConnected();
    time_t current_time = getCurrentTime();
    String time_str = formatTime(current_time);
    TimeStatus time_status = getTimeStatus();

    // Improved status bar height and styling
    int status_height = 20;
    display.m_display.fillRect(0, 0, display.m_display.width(), status_height, GxEPD_WHITE);

    // Time display with better font and positioning
    display.m_display.setFont(&FreeMono9pt7b);
    int16_t x1, y1;
    uint16_t w, h;
    display.m_display.getTextBounds(time_str.c_str(), 0, 0, &x1, &y1, &w, &h);
    display.m_display.setCursor(8, 16);
    display.m_display.print(time_str);
    
    // Add small time source indicator
    display.m_display.setFont();
    String source_indicator = "";
    if (time_status.time_source == "NTP") {
        source_indicator = "●"; // Solid dot for NTP (most accurate)
    } else if (time_status.time_source == "RTC") {
        source_indicator = "○"; // Hollow dot for RTC
    } else {
        source_indicator = "?"; // Question mark for system time
    }
    display.m_display.setCursor(8 + w + 3, 16);
    display.m_display.print(source_indicator);
    display.m_display.setFont(&FreeMono9pt7b);

    // Calculate battery percentage
    int battery_percent = (int)((battery_voltage - 3.0) / (4.2 - 3.0) * 100);
    battery_percent = constrain(battery_percent, 0, 100);
    String battery_text = String(battery_percent) + "%";

    // Get battery text dimensions for proper positioning
    display.m_display.getTextBounds(battery_text.c_str(), 0, 0, &x1, &y1, &w, &h);
    
    // Position battery elements from right edge
    int battery_icon_x = display.m_display.width() - 25;  // Battery icon position
    int battery_text_x = battery_icon_x - w - 8;          // Battery percentage text
    
    // Draw battery icon and percentage
    display.drawBatteryIcon(battery_icon_x, 5, battery_voltage, charging);
    display.m_display.setCursor(battery_text_x, 16);
    display.m_display.print(battery_text);

    // WiFi status indicator with custom icon (if connected)
    if (wifi_connected)
    {
        int wifi_icon_x = battery_text_x - 20;  // Position WiFi icon with proper spacing
        display.drawWifiIcon(wifi_icon_x, 5, true);
    }

    // Draw separator line with improved styling
    display.m_display.drawLine(0, status_height, display.m_display.width(), status_height, GxEPD_BLACK);
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
        // Handle WiFi screen select action
        wifiScreen.handleSelectAction();
        wifiScreen.draw(EinkDisplayManager::UPDATE_PARTIAL);
        break;

    case SCREEN_FILES:
        // Handle Files screen select action
        filesScreen.handleSelectAction();
        break;

    case SCREEN_CLOCK:
        // Manual time synchronization
        if (isWifiConnected())
        {
            Serial.println("[Clock] Manual NTP sync requested");
            if (syncTimeWithNTP())
            {
                Serial.println("[Clock] Manual sync successful");
            }
            else
            {
                Serial.println("[Clock] Manual sync failed");
            }
            // Redraw screen to show updated time status
            drawClockScreen(EinkDisplayManager::UPDATE_PARTIAL);
        }
        else
        {
            Serial.println("[Clock] Cannot sync - WiFi not connected");
        }
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
        // Handle WiFi screen down action
        wifiScreen.handleDownAction();
        wifiScreen.draw(EinkDisplayManager::UPDATE_PARTIAL);
        break;

    case SCREEN_FILES:
        // Handle Files screen down action
        filesScreen.handleDownAction();
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
    display.drawCenteredText("Coming Soon...", 150, &FreeMono12pt7b);

    display.endDrawing();
    display.update(mode);
}

void drawSettingsScreen(EinkDisplayManager::DisplayUpdateMode mode)
{
    display.startDrawing();
    drawStatusBar();

    display.m_display.setFont(&FreeMonoBold18pt7b);
    display.drawCenteredText("Settings", 100, &FreeMonoBold18pt7b);
    display.drawCenteredText("Coming Soon...", 150, &FreeMono12pt7b);

    display.endDrawing();
    display.update(mode);
}

void drawWifiScreen(EinkDisplayManager::DisplayUpdateMode mode)
{
    wifiScreen.draw(mode);
}

void drawClockScreen(EinkDisplayManager::DisplayUpdateMode mode)
{
    display.startDrawing();
    drawStatusBar();

    display.m_display.setFont(&FreeMonoBold18pt7b);
    display.drawCenteredText("Time & Date", 100, &FreeMonoBold18pt7b);
    
    // Get current time and status
    time_t current_time = getCurrentTime();
    TimeStatus time_status = getTimeStatus();
    
    // Display current time and date
    display.m_display.setFont(&FreeMonoBold12pt7b);
    String time_str = formatTime(current_time);
    String date_str = formatDate(current_time);
    
    display.drawCenteredText(time_str.c_str(), 140, &FreeMonoBold12pt7b);
    display.drawCenteredText(date_str.c_str(), 160, &FreeMono9pt7b);
    
    // Display time source and sync status
    display.m_display.setFont(&FreeMono9pt7b);
    String source_text = "Source: " + time_status.time_source;
    display.drawCenteredText(source_text.c_str(), 190, &FreeMono9pt7b);
    
    if (time_status.ntp_synced && time_status.last_ntp_sync > 0)
    {
        String last_sync = "Last NTP: " + formatTime(time_status.last_ntp_sync);
        display.drawCenteredText(last_sync.c_str(), 210, &FreeMono9pt7b);
    }
    else if (isWifiConnected())
    {
        display.drawCenteredText("NTP: Not synced", 210, &FreeMono9pt7b);
    }
    else
    {
        display.drawCenteredText("WiFi: Disconnected", 210, &FreeMono9pt7b);
    }
    
    // Instructions
    display.drawCenteredText("SELECT: Manual sync (if WiFi connected)", 250, &FreeMono9pt7b);
    display.drawCenteredText("UP: Return to main menu", 270, &FreeMono9pt7b);

    display.endDrawing();
    display.update(mode);
}

void drawFilesScreen(EinkDisplayManager::DisplayUpdateMode mode)
{
    filesScreen.draw(mode);
}