#include <Arduino.h>
#include "buttons.h"
#include "pins.h"
#include "ui.h" // Include the new UI header
#include "main.h"

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

// --- Placeholder/Empty handlers for other actions ---
void button1DoubleClick() { Serial.println("Button 1 double clicked"); }
void button1LongPressStart() { Serial.println("Button 1 long pressed"); }
void button2DoubleClick() { Serial.println("Button 2 double clicked"); }
void button2LongPressStart() { Serial.println("Button 2 long pressed"); }
void button3DoubleClick() { Serial.println("Button 3 double clicked"); }
void button3LongPressStart() { Serial.println("Button 3 long pressed"); }

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
    button1.setDebounceMs(50);
    button2.setDebounceMs(50);
    button3.setDebounceMs(50);
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