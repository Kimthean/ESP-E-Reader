#ifndef POWER_H
#define POWER_H

#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_pm.h>

// Power management modes
enum PowerMode
{
    POWER_ACTIVE,
    POWER_LIGHT_SLEEP,
    POWER_DEEP_SLEEP
};

// Power management functions
void initPowerManagement();
void enterLightSleep(uint64_t sleep_time_us);
void enterDeepSleep(uint64_t sleep_time_us);
void setupWakeupSources();
void enableGPIOWakeup();
void enableTimerWakeup(uint64_t sleep_time_us);

// Battery monitoring
float getBatteryVoltage();
int getBatteryPercentage();
bool isCharging();
bool isUSBConnected();

// Power status update and LED control
void updatePowerStatus();
void handlePowerLED();
void setPowerLEDState(bool state);
void blinkPowerLED(int times);
void printPowerStatus();

// Power state management
void setLowPowerMode(bool enable);
void optimizePowerConsumption();

// Wake up handling
void handleWakeup();
esp_sleep_wakeup_cause_t getWakeupCause();

#endif // POWER_H