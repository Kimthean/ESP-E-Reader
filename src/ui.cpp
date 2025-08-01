#include "ui.h"
#include "display.h"
#include "power.h"
#include "sensors.h"
#include "storage.h"
#include "main.h"
#include "ui/wifi/wifi_screen.h"
#include "ui/files/files_screen.h"
#include "ui/books/book_screen.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

extern EinkDisplayManager display;
extern bool low_power_mode; // Access to power management state

// Screen instances
WiFiScreen wifiScreen;
FilesScreen filesScreen;
BookScreen bookScreen;

// --- UI State Management ---
AppScreen current_screen = SCREEN_MAIN_MENU;
AppScreen previous_screen = SCREEN_MAIN_MENU; // Store previous screen for clock saver exit
int main_menu_selection = 0;                  // 0: Books, 1: Settings, 2: Wifi, 3: Clock
unsigned long last_status_update = 0;
const unsigned long STATUS_UPDATE_INTERVAL = 300000; // 5 minutes

// Clock saver state
static bool clock_saver_active = false;
static unsigned long last_clock_update = 0;
const unsigned long CLOCK_UPDATE_INTERVAL = 60000; // Update every minute

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

// Settings menu state
static int settings_menu_selection = 0;
static const int settings_menu_item_count = 2;
static const char *settings_menu_items[] = {
    "Time Sync",
    "Back to Main Menu"};

void initializeUI()
{
    // Clear screen to eliminate any startup ghosting
    display.wipeScreen();

    // Reload WiFi configuration now that SPIFFS is initialized
    wifiScreen.loadWiFiConfig();
    
    // Attempt auto-connection to saved networks after initialization
    wifiScreen.autoConnectToSavedNetworks();

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
        bookScreen.drawBookList(mode);
        break;
    case SCREEN_BOOK_READER:
        bookScreen.drawBookReader(mode);
        break;
    case SCREEN_BOOK_MENU:
        bookScreen.drawBookMenu(mode);
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
    case SCREEN_CLOCK_SAVER:
        drawClockSaverScreen(mode);
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

    // Update clock saver screen every minute
    if (clock_saver_active && millis() - last_clock_update > CLOCK_UPDATE_INTERVAL)
    {
        Serial.println("[UI] Updating clock saver display...");

        // Add safety check to prevent crashes during low power mode
        if (!low_power_mode)
        {
            drawClockSaverScreen(EinkDisplayManager::UPDATE_PARTIAL);
        }
        else
        {
            Serial.println("[UI] Skipping clock update during low power mode");
        }

        last_clock_update = millis();
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

    // Exit clock saver mode on any button press
    if (clock_saver_active)
    {
        exitClockSaverMode();
        return;
    }

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
                bookScreen.drawBookList(EinkDisplayManager::UPDATE_FAST);
                break;
            case SCREEN_SETTINGS:
                drawSettingsScreen(EinkDisplayManager::UPDATE_FAST);
                break;
            case SCREEN_WIFI:
                drawWifiScreen(EinkDisplayManager::UPDATE_FAST);
                break;
            case SCREEN_CLOCK:
                // Go directly to clock screen saver
                enterClockSaverMode();
                return; // Don't continue with normal screen drawing
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
            else if (current_screen == SCREEN_BOOKS)
            {
                // Handle up action in book screen
                bookScreen.handleUpAction();
            }
            else if (current_screen == SCREEN_BOOK_READER)
            {
                // Handle up action in book reader
                bookScreen.handleUpAction();
            }
            else if (current_screen == SCREEN_BOOK_MENU)
            {
                // Handle up action in book menu
                bookScreen.handleUpAction();
            }
            else if (current_screen == SCREEN_SETTINGS)
            {
                // Navigate up in settings menu or go back to main menu
                int old_selection = settings_menu_selection;
                settings_menu_selection = (settings_menu_selection - 1 + settings_menu_item_count) % settings_menu_item_count;
                if (old_selection != settings_menu_selection)
                {
                    drawSettingsScreen(EinkDisplayManager::UPDATE_PARTIAL);
                }
            }
            else
            {
                // For other screens, go back to main menu or previous screen
                if (current_screen == SCREEN_BOOK_READER || current_screen == SCREEN_BOOK_MENU)
                {
                    // Handle back action in book screen
                    bookScreen.handleBackAction();
                    // Update current screen based on book screen mode
                    if (bookScreen.getCurrentMode() == BookScreen::MODE_BOOK_LIST)
                    {
                        current_screen = SCREEN_BOOKS;
                    }
                }
                else
                {
                    // Return to main menu
                    current_screen = SCREEN_MAIN_MENU;
                    Serial.println("UP pressed - returning to main menu");
                    display.wipeScreen();
                    drawMainMenu(EinkDisplayManager::UPDATE_FAST);
                }
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

    // Calculate battery percentage
    int battery_percent = (int)((battery_voltage - 3.0) / (4.2 - 3.0) * 100);
    battery_percent = constrain(battery_percent, 0, 100);
    String battery_text = String(battery_percent) + "%";

    // Get battery text dimensions for proper positioning
    display.m_display.getTextBounds(battery_text.c_str(), 0, 0, &x1, &y1, &w, &h);

    // Position battery elements from right edge
    int battery_icon_x = display.m_display.width() - 25; // Battery icon position
    int battery_text_x = battery_icon_x - w - 8;         // Battery percentage text

    // Draw battery icon and percentage
    display.drawBatteryIcon(battery_icon_x, 7, battery_voltage, charging);
    display.m_display.setCursor(battery_text_x, 16);
    display.m_display.print(battery_text);

    // WiFi status indicator with custom icon (if connected)
    if (wifi_connected)
    {
        int wifi_icon_x = battery_text_x - 20; // Position WiFi icon with proper spacing
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
        // Handle select action in book screen
        bookScreen.handleSelectAction();
        // Update current screen based on book screen mode
        if (bookScreen.getCurrentMode() == BookScreen::MODE_BOOK_READER)
        {
            current_screen = SCREEN_BOOK_READER;
        }
        else if (bookScreen.getCurrentMode() == BookScreen::MODE_BOOK_MENU)
        {
            current_screen = SCREEN_BOOK_MENU;
        }
        break;

    case SCREEN_BOOK_READER:
        // Handle select action in book reader
        bookScreen.handleSelectAction();
        // Update current screen based on book screen mode
        if (bookScreen.getCurrentMode() == BookScreen::MODE_BOOK_MENU)
        {
            current_screen = SCREEN_BOOK_MENU;
        }
        break;

    case SCREEN_BOOK_MENU:
        // Handle select action in book menu
        bookScreen.handleSelectAction();
        // Update current screen based on book screen mode
        if (bookScreen.getCurrentMode() == BookScreen::MODE_BOOK_READER)
        {
            current_screen = SCREEN_BOOK_READER;
        }
        else if (bookScreen.getCurrentMode() == BookScreen::MODE_BOOK_LIST)
        {
            current_screen = SCREEN_BOOKS;
        }
        break;

    case SCREEN_SETTINGS:
        // Handle settings menu selection
        if (settings_menu_selection == 0) // Time Sync
        {
            if (isWifiConnected())
            {
                Serial.println("[Settings] Manual NTP sync requested");
                if (syncTimeWithNTP())
                {
                    Serial.println("[Settings] Manual sync successful");
                }
                else
                {
                    Serial.println("[Settings] Manual sync failed");
                }
                // Redraw screen to show updated status
                drawSettingsScreen(EinkDisplayManager::UPDATE_PARTIAL);
            }
            else
            {
                Serial.println("[Settings] Cannot sync - WiFi not connected");
            }
        }
        else if (settings_menu_selection == 1) // Back to Main Menu
        {
            current_screen = SCREEN_MAIN_MENU;
            display.wipeScreen();
            drawMainMenu(EinkDisplayManager::UPDATE_FAST);
        }
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
        // Clock screen now goes directly to clock saver, no actions needed
        Serial.println("[Clock] Already in clock saver mode");
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
        // Handle down action in book screen
        bookScreen.handleDownAction();
        break;

    case SCREEN_BOOK_READER:
        // Handle down action in book reader
        bookScreen.handleDownAction();
        break;

    case SCREEN_BOOK_MENU:
        // Handle down action in book menu
        bookScreen.handleDownAction();
        break;

    case SCREEN_SETTINGS:
        // Navigate down in settings menu
        {
            int old_selection = settings_menu_selection;
            settings_menu_selection = (settings_menu_selection + 1) % settings_menu_item_count;
            if (old_selection != settings_menu_selection)
            {
                drawSettingsScreen(EinkDisplayManager::UPDATE_PARTIAL);
            }
        }
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
        // Clock screen now goes directly to clock saver, no actions needed
        Serial.println("[Clock] Already in clock saver mode");
        break;

    default:
        break;
    }
}

// --- Screen Drawing Functions ---

// Book screen drawing is now handled by BookScreen class

void drawSettingsScreen(EinkDisplayManager::DisplayUpdateMode mode)
{
    display.startDrawing();
    drawStatusBar();

    display.m_display.setFont(&FreeMonoBold18pt7b);
    display.drawCenteredText("Settings", 80, &FreeMonoBold18pt7b);

    // Draw settings menu items
    int start_y = 120;
    int item_height = 35;
    int margin = 30;
    int selection_padding = 6;

    for (int i = 0; i < settings_menu_item_count; i++)
    {
        int y = start_y + (i * item_height);
        const char *label = settings_menu_items[i];

        display.m_display.setFont(&FreeMono12pt7b);

        // Calculate text dimensions for proper centering
        int16_t x1, y1;
        uint16_t w, h;
        display.m_display.getTextBounds(label, 0, 0, &x1, &y1, &w, &h);

        // Draw selection indicator
        if (i == settings_menu_selection)
        {
            int rect_x = margin;
            int rect_y = y - h - selection_padding;
            int rect_w = display.m_display.width() - (2 * margin);
            int rect_h = h + (2 * selection_padding);

            display.m_display.fillRect(rect_x, rect_y, rect_w, rect_h, GxEPD_BLACK);
            display.m_display.setTextColor(GxEPD_WHITE);
        }
        else
        {
            display.m_display.setTextColor(GxEPD_BLACK);
        }

        // Center the text
        int text_x = (display.m_display.width() - w) / 2;
        display.m_display.setCursor(text_x, y);
        display.m_display.print(label);
    }

    // Instructions
    display.m_display.setFont(&FreeMono9pt7b);
    display.m_display.setTextColor(GxEPD_BLACK);
    display.drawCenteredText("UP/DOWN: Navigate, SELECT: Choose, UP: Back", 250, &FreeMono9pt7b);

    display.endDrawing();
    display.update(mode);
}

void drawWifiScreen(EinkDisplayManager::DisplayUpdateMode mode)
{
    wifiScreen.draw(mode);
}

void drawClockScreen(EinkDisplayManager::DisplayUpdateMode mode)
{
    // This function is kept for compatibility but clock menu now goes directly to clock saver
    // If somehow called, redirect to clock saver mode
    Serial.println("[Clock] drawClockScreen called - redirecting to clock saver");
    enterClockSaverMode();
}

void drawFilesScreen(EinkDisplayManager::DisplayUpdateMode mode)
{
    filesScreen.draw(mode);
}

void drawClockSaverScreen(EinkDisplayManager::DisplayUpdateMode mode)
{
    // Feed watchdog to prevent timeout during display operations
    esp_task_wdt_reset();

    display.startDrawing();

    // Set display to horizontal orientation for clock saver
    display.m_display.setRotation(3); // Rotate 90 degrees for horizontal layout

    // Clear the screen
    display.m_display.fillScreen(GxEPD_WHITE);

    // Get current time and sensor data
    time_t current_time = getCurrentTime();
    SensorData sensor_data = readAllSensors();

    // Format time and date
    struct tm *timeinfo = localtime(&current_time);
    char time_buffer[16];
    char date_buffer[32];
    char day_buffer[16];

    // Format time in 12-hour format
    strftime(time_buffer, sizeof(time_buffer), "%I:%M %p", timeinfo);
    strftime(date_buffer, sizeof(date_buffer), "%B %d, %Y", timeinfo);
    strftime(day_buffer, sizeof(day_buffer), "%A", timeinfo);

    // Main time display (large, centered)
    display.m_display.setFont(&FreeMonoBold18pt7b);
    display.m_display.setTextColor(GxEPD_BLACK);

    // Calculate center positions for horizontal layout (416x240 rotated)
    int16_t x, y;
    uint16_t w, h;

    // Display time in center
    display.m_display.getTextBounds(time_buffer, 0, 0, &x, &y, &w, &h);
    display.m_display.setCursor((416 - w) / 2, 120 + h / 2);
    display.m_display.print(time_buffer);

    // Display date below time
    display.m_display.setFont(&FreeMono12pt7b);
    display.m_display.getTextBounds(date_buffer, 0, 0, &x, &y, &w, &h);
    display.m_display.setCursor((416 - w) / 2, 150 + h);
    display.m_display.print(date_buffer);

    // Display day of week above time
    display.m_display.getTextBounds(day_buffer, 0, 0, &x, &y, &w, &h);
    display.m_display.setCursor((416 - w) / 2, 80);
    display.m_display.print(day_buffer);

    // Display sensor data on the sides
    display.m_display.setFont(&FreeMono9pt7b);

    // Left side - Temperature
    if (sensor_data.temperature_valid)
    {
        char temp_str[16];
        snprintf(temp_str, sizeof(temp_str), "%.1f°C", sensor_data.temperature);
        display.m_display.setCursor(20, 60);
        display.m_display.print("TEMP");
        display.m_display.setCursor(20, 80);
        display.m_display.print(temp_str);
    }
    else
    {
        display.m_display.setCursor(20, 60);
        display.m_display.print("TEMP");
        display.m_display.setCursor(20, 80);
        display.m_display.print("--°C");
    }

    // Right side - Humidity
    if (sensor_data.humidity_valid)
    {
        char hum_str[16];
        snprintf(hum_str, sizeof(hum_str), "%.1f%%", sensor_data.humidity);
        display.m_display.setCursor(350, 60);
        display.m_display.print("HUMID");
        display.m_display.setCursor(350, 80);
        display.m_display.print(hum_str);
    }
    else
    {
        display.m_display.setCursor(350, 60);
        display.m_display.print("HUMID");
        display.m_display.setCursor(350, 80);
        display.m_display.print("--%");
    }

    // Bottom status information
    display.m_display.setFont(&FreeMono9pt7b);

    // Battery status (bottom left)
    float battery_voltage = getBatteryVoltage();
    int battery_percentage = getBatteryPercentage();
    char battery_str[32];
    snprintf(battery_str, sizeof(battery_str), "Battery: %d%% (%.2fV)", battery_percentage, battery_voltage);
    display.m_display.setCursor(20, 220);
    display.m_display.print(battery_str);

    // Clock saver indicator (bottom right)
    display.m_display.setCursor(280, 220);
    display.m_display.print("Press any key to exit");

    // Reset rotation back to normal
    display.m_display.setRotation(0);

    display.endDrawing();
    display.update(mode);
}

void enterClockSaverMode()
{
    Serial.println("[UI] Entering clock saver mode...");

    // Store current screen to return to later
    previous_screen = current_screen;

    // Switch to clock saver screen
    current_screen = SCREEN_CLOCK_SAVER;
    clock_saver_active = true;
    last_clock_update = millis();

    // Draw the clock saver screen
    drawClockSaverScreen(EinkDisplayManager::UPDATE_FAST);

    Serial.println("[UI] Clock saver mode activated");
}

void exitClockSaverMode()
{
    Serial.println("[UI] Exiting clock saver mode...");

    clock_saver_active = false;

    // Return to main menu (safer than returning to previous screen)
    current_screen = SCREEN_MAIN_MENU;

    // Redraw the main menu
    drawMainMenu(EinkDisplayManager::UPDATE_FULL);

    Serial.println("[UI] Clock saver mode deactivated");
}

bool isInClockSaverMode()
{
    return clock_saver_active;
}

void handleMainMenuLongPress()
{
    Serial.println("[UI] Long press detected in main menu - entering clock screen saver");
    if (current_screen == SCREEN_MAIN_MENU && !clock_saver_active)
    {
        enterClockSaverMode();
    }
}

// Book reader and menu drawing is now handled by BookScreen class