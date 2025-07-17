#ifndef CONFIG_H
#define CONFIG_H

// Application version
#define APP_VERSION "1.0.0"
#define APP_NAME "E-Reader"

// Screen specifications
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 416
#define SCREEN_DPI 127

// Hardware specifications
#define BOARD_WIDTH_MM 93
#define BOARD_HEIGHT_MM 53
#define DISPLAY_WIDTH_MM 81.54
#define DISPLAY_HEIGHT_MM 47.04

// API Configuration
#define MANGA_API_BASE_URL "https://mangahook-api.vercel.app"
#define API_TIMEOUT_MS 30000

// File system paths
#define SD_ROOT_PATH "/"
#define BOOKS_PATH "/books"
#define MANGA_PATH "/manga"
#define SETTINGS_PATH "/settings"
#define CACHE_PATH "/cache"
#define LOGS_PATH "/logs"

// Application states
enum AppState
{
    STATE_BOOT,
    STATE_HOME,
    STATE_SCREENSAVER,
    STATE_SETTINGS,
    STATE_NOVEL_READER,
    STATE_MANGA_READER,
    STATE_SLEEP
};

// Supported file formats
enum FileFormat
{
    FORMAT_TXT,
    FORMAT_EPUB,
    FORMAT_PDF,
    FORMAT_MANGA_JSON
};

// Screen saver configuration
#define SCREENSAVER_TIMEOUT_MS 300000        // 5 minutes
#define SCREENSAVER_UPDATE_INTERVAL_MS 60000 // 1 minute

// Power management
#define DEEP_SLEEP_TIMEOUT_MS 900000 // 15 minutes
#define LIGHT_SLEEP_TIMEOUT_MS 30000 // 30 seconds

// Sensor reading intervals
#define SENSOR_READ_INTERVAL_MS 10000 // 10 seconds
#define SENSOR_LOG_INTERVAL_MS 60000  // 1 minute

// Button debounce
#define BUTTON_DEBOUNCE_MS 50

// WiFi configuration
#define WIFI_CONNECT_TIMEOUT_MS 20000 // 20 seconds
#define WIFI_RETRY_INTERVAL_MS 30000  // 30 seconds

// Display update settings
#define DISPLAY_PARTIAL_UPDATE_LIMIT 10 // Full refresh after 10 partial updates
#define DISPLAY_CONTRAST_DEFAULT 128

// Future app features (for reference)
/*
Apps to implement:
1. Home Screen
   - Date/time display
   - Weather info (temp/humidity)
   - Recent books
   - Battery status
   - Quick settings

2. Screen Saver
   - Clock display
   - Temperature/humidity
   - Rotating backgrounds
   - Battery conservation

3. Settings
   - WiFi configuration
   - Display settings
   - Power management
   - System info
   - Factory reset

4. Novel Reader
   - TXT file support
   - EPUB support
   - Bookmarks
   - Font size/style
   - Page navigation

5. Manga Reader
   - Manga API integration
   - Chapter management
   - Bookmark support
   - Zoom/pan functionality
   - Offline reading

6. SD Card Management
   - File browser
   - User data storage
   - Cache management
   - Logs
*/

#endif // CONFIG_H