#ifndef UI_H
#define UI_H

#include "display.h"

// Enum to define all possible application screens
enum AppScreen
{
    SCREEN_MAIN_MENU,
    SCREEN_BOOKS,
    SCREEN_BOOK_READER,
    SCREEN_BOOK_MENU,
    SCREEN_SETTINGS,
    SCREEN_WIFI,
    SCREEN_CLOCK,
    SCREEN_FILES,
    SCREEN_CLOCK_SAVER, // New screen saver mode
};

/**
 * @brief Initializes the User Interface manager.
 */
void initializeUI();

/**
 * @brief Main UI loop function to be called repeatedly.
 * Handles drawing the current screen and any necessary updates.
 */
void updateUI();

/**
 * @brief Handles button presses for UI navigation.
 * @param button The button that was pressed (1, 2, or 3).
 */
void handleButtonPress(int button);

// --- Screen-specific functions ---

/**
 * @brief Draws the main menu grid.
 * @param mode The display update mode to use.
 */
void drawMainMenu(EinkDisplayManager::DisplayUpdateMode mode);

/**
 * @brief Draws the current screen based on the current_screen state.
 * @param mode The display update mode to use.
 */
void drawCurrentScreen(EinkDisplayManager::DisplayUpdateMode mode);

/**
 * @brief Draws the status bar at the top of the screen.
 */
void drawStatusBar();

/**
 * @brief Draws the Books screen.
 * @param mode The display update mode to use.
 */
void drawBooksScreen(EinkDisplayManager::DisplayUpdateMode mode);

/**
 * @brief Draws the Settings screen.
 * @param mode The display update mode to use.
 */
void drawSettingsScreen(EinkDisplayManager::DisplayUpdateMode mode);

/**
 * @brief Draws the WiFi screen.
 * @param mode The display update mode to use.
 */
void drawWifiScreen(EinkDisplayManager::DisplayUpdateMode mode);

/**
 * @brief Draws the Clock screen.
 * @param mode The display update mode to use.
 */
void drawClockScreen(EinkDisplayManager::DisplayUpdateMode mode);

/**
 * @brief Draws the horizontal clock screen saver with sensors.
 * @param mode The display update mode to use.
 */
void drawClockSaverScreen(EinkDisplayManager::DisplayUpdateMode mode);

/**
 * @brief Enters clock saver mode instead of deep sleep.
 */
void enterClockSaverMode();

/**
 * @brief Exits clock saver mode and returns to main menu.
 */
void exitClockSaverMode();

/**
 * @brief Checks if currently in clock saver mode.
 */
bool isInClockSaverMode();

/**
 * @brief Draws the Files screen.
 * @param mode The display update mode to use.
 */
void drawFilesScreen(EinkDisplayManager::DisplayUpdateMode mode);

// Book reader and menu drawing is now handled by BookScreen class

/**
 * @brief Handles SELECT button press based on current screen context.
 */
void handleSelectAction();

/**
 * @brief Handles DOWN button press based on current screen context.
 */
void handleDownAction();

/**
 * @brief Handles long press in main menu to go directly to clock screen saver.
 */
void handleMainMenuLongPress();

#endif // UI_H