#include <Arduino.h>
#include "pins.h"
#include "display.h"
#include "ui.h"
#include "power.h"
#include "buttons.h"
#include "sensors.h"

EinkDisplayManager display;

// --- Timers ---
unsigned long last_activity_time = 0;
const unsigned long DEEP_SLEEP_TIMEOUT = 10 * 60 * 1000; // 10 minutes

/**
 * Setup function - runs once
 */
void setup()
{
  Serial.begin(115200);
  Serial.println("Booting up...");

  initPowerManagement();
  handleWakeup();

  display.begin();
  initializeButtons();
  initializeSensors();
  initializeUI();

  last_activity_time = millis();
}

/**
 * Main loop function - runs continuously
 */
void loop()
{
  updateButtons();
  updateUI();
  updatePowerStatus();

  if (millis() - last_activity_time > DEEP_SLEEP_TIMEOUT)
  {
    Serial.println("Inactivity timeout, entering deep sleep...");
    display.sleep();
    enterDeepSleep(0);
  }
}

void resetActivityTimer()
{
  last_activity_time = millis();
}