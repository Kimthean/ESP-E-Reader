#ifndef UI_H
#define UI_H

#include "display.h"

// Enum to define all possible application screens
enum AppScreen
{
    SCREEN_MAIN_MENU,
    SCREEN_BOOKS,
    SCREEN_SETTINGS,
    SCREEN_WIFI,
    SCREEN_CLOCK
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
 * @brief Draws the status bar at the top of the screen.
 */
void drawStatusBar();

#endif // UI_H