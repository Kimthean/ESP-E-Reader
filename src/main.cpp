#include <Arduino.h>
#include <esp_sleep.h>
#include <SPIFFS.h>
#include "pins.h"
#include "display.h"
#include "ui.h"
#include "power.h"
#include "buttons.h"
#include "sensors.h"
#include "storage.h"

EinkDisplayManager display;

// --- Timers ---
unsigned long last_activity_time = 0;
unsigned long last_time_update = 0;
const unsigned long DEEP_SLEEP_TIMEOUT = 10 * 60 * 1000; // 10 minutes
const unsigned long TIME_UPDATE_INTERVAL = 30 * 1000; // Check for time updates every 30 seconds

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

  // Check if we're waking from deep sleep
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  if (wakeup_reason != ESP_SLEEP_WAKEUP_UNDEFINED)
  {
    Serial.println("Waking from deep sleep - restoring display");
    display.wake();
  }

  // Initialize SPIFFS for WiFi configuration storage
  if (!SPIFFS.begin(true))
  {
    Serial.println("SPIFFS initialization failed");
  }
  else
  {
    Serial.println("SPIFFS initialized successfully");
  }

  initializeButtons();
  initializeSensors();
  initStorage();
  initializeUI();
  
  // Initialize time synchronization system
  initTimeSync();

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
  
  // Periodic time synchronization check
  if (millis() - last_time_update > TIME_UPDATE_INTERVAL)
  {
    updateTimeFromNTP();
    last_time_update = millis();
  }
  
  if (millis() - last_activity_time > DEEP_SLEEP_TIMEOUT)
  {
    Serial.println("[MAIN] Inactivity timeout detected, entering deep sleep...");
    Serial.printf("[MAIN] Last activity was %lu ms ago (timeout: %lu ms)\n",
                  millis() - last_activity_time, DEEP_SLEEP_TIMEOUT);
    display.sleep();
    Serial.println("[MAIN] Calling enterDeepSleep(0)");
    enterDeepSleep(0);
  }
}

void resetActivityTimer()
{
  last_activity_time = millis();
}