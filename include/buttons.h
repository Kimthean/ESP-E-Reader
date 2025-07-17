#ifndef BUTTONS_H
#define BUTTONS_H

#include <OneButton.h>

// Function declarations
void initializeButtons();
void updateButtons();

// Button event handlers
void button1Click();
void button1DoubleClick();
void button1LongPressStart();
void button1MultiClick();
void button2Click();
void button2DoubleClick();
void button2LongPressStart();
void button2MultiClick();
void button3Click();
void button3DoubleClick();
void button3LongPressStart();
void button3LongPressStop();
void button3MultiClick();

#endif // BUTTONS_H