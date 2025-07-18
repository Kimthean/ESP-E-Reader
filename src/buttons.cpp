#include <Arduino.h>
#include "buttons.h"
#include "pins.h"
#include "ui.h" // Include the new UI header
#include "main.h"
#include "ui/files/files_screen.h" // Include FilesScreen header

// OneButton instances for each button
OneButton button1; // DOWN
OneButton button2; // SELECT
OneButton button3; // UP

// --- New Button Actions for Menu Navigation ---

void button1Click() // DOWN
{
    ::handleButtonPress(1);
}

void button2Click() // SELECT
{
    ::handleButtonPress(2);
}

void button3Click() // UP
{
    ::handleButtonPress(3);
}

// --- Advanced Button Actions ---
void button1DoubleClick() // DOWN double click
{
    Serial.println("DOWN double click - Quick navigation down");
    extern AppScreen current_screen;

    if (current_screen == SCREEN_FILES)
    {
        extern FilesScreen filesScreen;
        // Quick scroll down (jump 5 items)
        filesScreen.handleQuickDownAction(5);
    }
}

void button1LongPressStart() // DOWN long press
{
    Serial.println("DOWN long press - Context action placeholder");
    // Future: Context-specific long press action
}

void button2DoubleClick() // SELECT double click
{
    Serial.println("SELECT double click - Quick confirm");
    // Future: Quick confirm/execute action
}

void button2LongPressStart() // SELECT long press
{
    Serial.println("SELECT long press - Global back to main menu");
    // Global navigation: Long press SELECT always returns to main menu
    extern AppScreen current_screen;
    if (current_screen != SCREEN_MAIN_MENU)
    {
        current_screen = SCREEN_MAIN_MENU;
        drawMainMenu(EinkDisplayManager::UPDATE_FAST);
    }
}

void button3DoubleClick() // UP double click
{
    Serial.println("UP double click - Quick navigation up");
    extern AppScreen current_screen;

    if (current_screen == SCREEN_FILES)
    {
        extern FilesScreen filesScreen;
        // Quick scroll up (jump 5 items)
        filesScreen.handleQuickUpAction(5);
    }
}

void button3LongPressStart() // UP long press
{
    Serial.println("UP long press - Opening global menu");
    extern AppScreen current_screen;

    // Open global menu for files screen
    if (current_screen == SCREEN_FILES)
    {
        extern FilesScreen filesScreen;
        filesScreen.showGlobalMenu();
    }
    else
    {
        // For other screens, system menu placeholder
        Serial.println("System menu placeholder");
    }
}

/**
 * Initialize all buttons with proper hardware configuration
 */
void initializeButtons()
{
    // Button 3 (UP) - IO35
    button3.setup(BTN_KEY3, INPUT, false);
    button3.attachClick(button3Click);
    button3.attachDoubleClick(button3DoubleClick);
    button3.attachLongPressStart(button3LongPressStart);

    // Button 1 (DOWN) - IO34
    button1.setup(BTN_KEY1, INPUT, false);
    button1.attachClick(button1Click);
    button1.attachDoubleClick(button1DoubleClick);
    button1.attachLongPressStart(button1LongPressStart);

    // Button 2 (SELECT) - IO39
    button2.setup(BTN_KEY2, INPUT, false);
    button2.attachClick(button2Click);
    button2.attachDoubleClick(button2DoubleClick);
    button2.attachLongPressStart(button2LongPressStart);

    // Adjust timing for better responsiveness
    button1.setDebounceMs(30); // Reduced debounce for better responsiveness
    button2.setDebounceMs(30);
    button3.setDebounceMs(30);

    // Configure long press timing
    button1.setLongPressIntervalMs(800); // 800ms for long press
    button2.setLongPressIntervalMs(800);
    button3.setLongPressIntervalMs(800);

    // Configure double click timing
    button1.setClickMs(150); // Max time for single click
    button2.setClickMs(150);
    button3.setClickMs(150);

    // Note: setDoubleClickMs not available in this OneButton version
    // Using default double click timing
}

/**
 * Update all button states - call this in main loop
 */
void updateButtons()
{
    button1.tick();
    button2.tick();
    button3.tick();
}