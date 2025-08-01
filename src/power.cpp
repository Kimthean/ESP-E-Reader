#include "power.h"
#include "pins.h"
#include "storage.h"
#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_pm.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include <WiFi.h>

// Power management state
static PowerMode current_power_mode = POWER_ACTIVE;
bool low_power_mode = false; // Made non-static for external access

// Battery monitoring variables
static float battery_voltage = 0.0;
static int battery_percentage = 0;
static bool charging_status = false;
static bool usb_connected = false;

// Battery voltage thresholds (for Li-ion battery)
const float BATTERY_MAX_VOLTAGE = 4.2;     // Fully charged
const float BATTERY_MIN_VOLTAGE = 3.0;     // Empty (protection threshold)
const float BATTERY_NOMINAL_VOLTAGE = 3.7; // Nominal voltage
const float CHARGING_THRESHOLD = 0.1;      // Voltage increase threshold for charging detection

// Previous voltage for charging detection
static float previous_voltage = 0.0;
static unsigned long last_voltage_check = 0;
static const unsigned long VOLTAGE_CHECK_INTERVAL = 5000; // Check every 5 seconds

// LED control variables
static bool power_led_state = false;
static unsigned long last_led_blink = 0;
static const unsigned long LED_BLINK_INTERVAL = 1000; // Blink every second

/**
 * Initialize power management system
 */
void initPowerManagement()
{
    Serial.println("Initializing power management...");

    // Initialize ADC for battery voltage monitoring
    analogReadResolution(12);       // 12-bit ADC resolution
    analogSetAttenuation(ADC_11db); // For 3.3V reference

    // Configure power LED pin
    pinMode(LED_POWER, OUTPUT);
    digitalWrite(LED_POWER, LOW);

    // Configure charging LED pin (read-only for status)
    pinMode(LED_CHARGE, INPUT);

    // Read initial battery voltage
    battery_voltage = getBatteryVoltage();
    battery_percentage = getBatteryPercentage();
    previous_voltage = battery_voltage;

    // Check initial USB and charging status
    usb_connected = isUSBConnected();
    charging_status = isCharging();

    // Initialize ESP32 power management with reduced frequencies to prevent overheating
    esp_pm_config_esp32_t pm_config = {
        .max_freq_mhz = 160,  // Reduced from 240MHz to 160MHz to reduce heat
        .min_freq_mhz = 80,
        .light_sleep_enable = true};
    esp_pm_configure(&pm_config);
    
    // Set initial CPU frequency to a moderate level
    setCpuFrequencyMhz(160);  // Start at 160MHz instead of 240MHz

    // Set up initial power state
    setPowerLEDState(true); // Power on indicator

    Serial.println("Power management initialized");
    printPowerStatus();
}

/**
 * Get battery voltage from ADC
 */
float getBatteryVoltage()
{
    // Read ADC value (0-4095 for 12-bit)
    int adc_value = analogRead(BAT_ADC);

    // Convert to voltage (3.3V reference, 11dB attenuation allows up to ~3.3V)
    // With voltage divider (R21=R22=100K), actual battery voltage is 2x measured
    float measured_voltage = (adc_value * 3.3) / 4095.0;
    float actual_voltage = measured_voltage * 2.0; // Account for voltage divider

    // Apply calibration if needed (ESP32 ADC can be inaccurate)
    // This might need adjustment based on actual hardware
    actual_voltage = actual_voltage * 1.1; // Rough calibration factor

    return actual_voltage;
}

/**
 * Calculate battery percentage based on voltage
 */
int getBatteryPercentage()
{
    float voltage = getBatteryVoltage();

    if (voltage >= BATTERY_MAX_VOLTAGE)
    {
        return 100;
    }
    else if (voltage <= BATTERY_MIN_VOLTAGE)
    {
        return 0;
    }
    else
    {
        // Linear interpolation between min and max voltage
        float percentage = ((voltage - BATTERY_MIN_VOLTAGE) / (BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE)) * 100.0;
        return (int)constrain(percentage, 0, 100);
    }
}

/**
 * Check if device is charging
 */
bool isCharging()
{
    // Method 1: Check voltage increase over time
    unsigned long current_time = millis();
    if (current_time - last_voltage_check > VOLTAGE_CHECK_INTERVAL)
    {
        float current_voltage = getBatteryVoltage();

        // If voltage is increasing significantly, likely charging
        if (current_voltage > previous_voltage + CHARGING_THRESHOLD)
        {
            charging_status = true;
        }
        else if (current_voltage < previous_voltage - CHARGING_THRESHOLD)
        {
            charging_status = false;
        }

        previous_voltage = current_voltage;
        last_voltage_check = current_time;
    }

    // Method 2: Check if USB is connected and battery is not full
    bool usb_present = isUSBConnected();
    int battery_level = getBatteryPercentage();

    if (usb_present && battery_level < 100)
    {
        charging_status = true;
    }
    else if (!usb_present || battery_level >= 100)
    {
        charging_status = false;
    }

    return charging_status;
}

/**
 * Check if USB is connected
 */
bool isUSBConnected()
{
    // Check if voltage is above battery maximum (indicates external power)
    float voltage = getBatteryVoltage();
    return voltage > BATTERY_MAX_VOLTAGE + 0.2; // Allow some margin
}

/**
 * Update power status and LED indicators
 */
void updatePowerStatus()
{
    // Update battery readings
    battery_voltage = getBatteryVoltage();
    battery_percentage = getBatteryPercentage();
    usb_connected = isUSBConnected();
    charging_status = isCharging();

    // Handle power LED indication
    handlePowerLED();

    // Thermal management - automatically enable low power mode if overheating suspected
    // This helps prevent device from running too hot
    static unsigned long last_thermal_check = 0;
    if (millis() - last_thermal_check > 60000) // Check every minute
    {
        // Only enable thermal protection if battery is very low (below 20%) and not charging
        // This prevents unnecessary thermal protection during normal operation
        if (!low_power_mode && !charging_status && battery_percentage < 20)
        {
            Serial.println("[THERMAL] Enabling thermal protection mode due to low battery");
            setLowPowerMode(true);
        }
        last_thermal_check = millis();
    }

    // Check for low battery warning
    if (battery_percentage < 15 && !charging_status)
    {
        Serial.println("WARNING: Low battery! Please charge soon.");
        blinkPowerLED(3); // Triple blink for warning
    }

    // Check for critical battery
    if (battery_percentage < 5 && !charging_status)
    {
        Serial.println("CRITICAL: Battery critically low! Entering power save mode.");
        setLowPowerMode(true);
    }
}

/**
 * Handle power LED based on status
 */
void handlePowerLED()
{
    unsigned long current_time = millis();

    if (charging_status)
    {
        // Slow blink when charging
        if (current_time - last_led_blink > LED_BLINK_INTERVAL)
        {
            power_led_state = !power_led_state;
            digitalWrite(LED_POWER, power_led_state);
            last_led_blink = current_time;
        }
    }
    else if (battery_percentage < 15)
    {
        // Fast blink when low battery
        if (current_time - last_led_blink > LED_BLINK_INTERVAL / 2)
        {
            power_led_state = !power_led_state;
            digitalWrite(LED_POWER, power_led_state);
            last_led_blink = current_time;
        }
    }
    else
    {
        // Solid on when normal operation
        setPowerLEDState(true);
    }
}

/**
 * Set power LED state
 */
void setPowerLEDState(bool state)
{
    power_led_state = state;
    digitalWrite(LED_POWER, state);
}

/**
 * Blink power LED specified number of times
 */
void blinkPowerLED(int times)
{
    for (int i = 0; i < times; i++)
    {
        digitalWrite(LED_POWER, HIGH);
        delay(200);
        digitalWrite(LED_POWER, LOW);
        delay(200);
    }
    // Restore previous state
    digitalWrite(LED_POWER, power_led_state);
}

/**
 * Print current power status
 */
void printPowerStatus()
{
    Serial.println("\n=== Power Status ===");
    Serial.print("Battery Voltage: ");
    Serial.print(battery_voltage, 2);
    Serial.println("V");

    Serial.print("Battery Level: ");
    Serial.print(battery_percentage);
    Serial.println("%");

    Serial.print("USB Connected: ");
    Serial.println(usb_connected ? "Yes" : "No");

    Serial.print("Charging: ");
    Serial.println(charging_status ? "Yes" : "No");

    Serial.print("Hardware Charge LED: ");
    Serial.println(digitalRead(LED_CHARGE) ? "OFF" : "ON"); // TP4054 drives low when charging

    Serial.print("Power Mode: ");
    switch (current_power_mode)
    {
    case POWER_ACTIVE:
        Serial.println("Active");
        break;
    case POWER_LIGHT_SLEEP:
        Serial.println("Light Sleep");
        break;
    case POWER_DEEP_SLEEP:
        Serial.println("Deep Sleep");
        break;
    }
    Serial.println("==================\n");
}

/**
 * Enter light sleep mode
 */
void enterLightSleep(uint64_t sleep_time_us)
{
    Serial.printf("[POWER] enterLightSleep() called with %llu microseconds\n", sleep_time_us);
    Serial.println("[POWER] Entering light sleep mode...");
    current_power_mode = POWER_LIGHT_SLEEP;

    // Configure wake up sources
    esp_sleep_enable_timer_wakeup(sleep_time_us);
    enableGPIOWakeup();

    // Enter light sleep
    Serial.println("[POWER] Calling esp_light_sleep_start()...");
    esp_light_sleep_start();

    current_power_mode = POWER_ACTIVE;
    Serial.println("[POWER] Woken up from light sleep");
}

/**
 * Enter deep sleep mode
 */
void enterDeepSleep(uint64_t sleep_time_us)
{
    Serial.printf("[POWER] enterDeepSleep() called with %llu microseconds\n", sleep_time_us);
    Serial.println("[POWER] Entering deep sleep mode...");
    current_power_mode = POWER_DEEP_SLEEP;

    // Properly shut down peripherals to prevent crashes
    Serial.println("[POWER] Shutting down WiFi and Bluetooth...");
    esp_wifi_stop();
    esp_bt_controller_disable();
    
    // Power down SD card to prevent conflicts
    Serial.println("[POWER] Powering down SD card...");
    disableSDCardWakeup(); // This calls powerOffSDCard() from storage module
    
    // Save critical data to RTC memory if needed

    // Configure wake up sources
    if (sleep_time_us > 0) {
        esp_sleep_enable_timer_wakeup(sleep_time_us);
    }
    enableGPIOWakeup();

    // Turn off power LED
    setPowerLEDState(false);

    // Enter deep sleep
    Serial.println("[POWER] Calling esp_deep_sleep_start() - device will not return until wakeup");
    Serial.flush(); // Ensure log message is sent before sleep
    esp_deep_sleep_start();

    // This point is never reached (ESP32 resets on wake)
}

/**
 * Enable GPIO wakeup sources
 */
void enableGPIOWakeup()
{
    // Wake up from any of the three buttons
    // Note: Using ESP_EXT1_WAKEUP_ANY_HIGH since buttons are active HIGH according to pins.h
    esp_sleep_enable_ext1_wakeup(
        (1ULL << BTN_KEY1) | (1ULL << BTN_KEY2) | (1ULL << BTN_KEY3),
        ESP_EXT1_WAKEUP_ANY_HIGH);

    // Note: Commenting out RTC wakeup due to pin conflict with SD_CS (both use pin 4)
    // This conflict could cause crashes when going to sleep
    // esp_sleep_enable_ext0_wakeup((gpio_num_t)RTC_INT, 0);
}

/**
 * Enable timer wakeup
 */
void enableTimerWakeup(uint64_t sleep_time_us)
{
    esp_sleep_enable_timer_wakeup(sleep_time_us);
}

/**
 * Set low power mode
 */
void setLowPowerMode(bool enable)
{
    low_power_mode = enable;

    if (enable)
    {
        Serial.println("Enabling low power mode...");

        // Reduce CPU frequency significantly for thermal management
        setCpuFrequencyMhz(80);

        // Safely disable WiFi and Bluetooth with error handling
        Serial.println("[POWER] Disabling WiFi and Bluetooth for low power mode");
        
        // Check WiFi state before stopping
        wifi_mode_t wifi_mode;
        if (esp_wifi_get_mode(&wifi_mode) == ESP_OK && wifi_mode != WIFI_MODE_NULL)
        {
            esp_err_t wifi_result = esp_wifi_stop();
            if (wifi_result == ESP_OK)
            {
                Serial.println("[POWER] WiFi stopped successfully");
            }
            else
            {
                Serial.printf("[POWER] WiFi stop failed: %s\n", esp_err_to_name(wifi_result));
            }
        }
        else
        {
            Serial.println("[POWER] WiFi already stopped or not initialized");
        }
        
        // Check Bluetooth state before disabling
        if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED)
        {
            esp_err_t bt_result = esp_bt_controller_disable();
            if (bt_result == ESP_OK)
            {
                Serial.println("[POWER] Bluetooth disabled successfully");
            }
            else
            {
                Serial.printf("[POWER] Bluetooth disable failed: %s\n", esp_err_to_name(bt_result));
            }
        }
        else
        {
            Serial.println("[POWER] Bluetooth already disabled or not initialized");
        }

        // Disable unnecessary peripherals
        optimizePowerConsumption();

        Serial.println("Low power mode enabled - CPU at 80MHz for thermal management");
    }
    else
    {
        Serial.println("Disabling low power mode...");

        // Restore moderate CPU frequency (not maximum to prevent overheating)
        setCpuFrequencyMhz(160);  // Use 160MHz instead of 240MHz

        Serial.println("Low power mode disabled - CPU at 160MHz for balanced performance");
    }
}

/**
 * Optimize power consumption
 */
void optimizePowerConsumption()
{
    // Disable unused GPIO pins
    for (int i = 0; i < 40; i++)
    {
        if (i != LED_POWER && i != LED_CHARGE && i != BAT_ADC &&
            i != BTN_KEY1 && i != BTN_KEY2 && i != BTN_KEY3 &&
            i != I2C_SDA && i != I2C_SCL && i != RTC_INT)
        {
            pinMode(i, INPUT);
        }
    }

    // Configure ADC for lower power
    analogSetAttenuation(ADC_0db);
}

/**
 * Handle wakeup from sleep
 */
void handleWakeup()
{
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    switch (wakeup_reason)
    {
    case ESP_SLEEP_WAKEUP_EXT0:
        Serial.println("Wakeup from GPIO");
        break;
    case ESP_SLEEP_WAKEUP_EXT1:
        Serial.println("Wakeup from GPIO");
        break;
    case ESP_SLEEP_WAKEUP_TIMER:
        Serial.println("Wakeup from timer");
        break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
        Serial.println("Wakeup from touchpad");
        break;
    case ESP_SLEEP_WAKEUP_ULP:
        Serial.println("Wakeup from ULP");
        break;
    default:
        Serial.println("Wakeup from reset");
        break;
    }
}

/**
 * Get wakeup cause
 */
esp_sleep_wakeup_cause_t getWakeupCause()
{
    return esp_sleep_get_wakeup_cause();
}

/**
 * Enable thermal protection manually
 * This function can be called when overheating is detected
 */
void enableThermalProtection()
{
    Serial.println("[THERMAL] Manual thermal protection activated");
    Serial.println("[THERMAL] Reducing CPU frequency and enabling power saving features");
    
    // Enable low power mode for thermal management
    setLowPowerMode(true);
    
    // Additional thermal protection measures
    Serial.println("[THERMAL] Thermal protection measures:");
    Serial.println("  - CPU frequency reduced to 80MHz");
    Serial.println("  - WiFi and Bluetooth disabled");
    Serial.println("  - Unused GPIO pins optimized");
    Serial.println("  - Power consumption minimized");
    Serial.println("[THERMAL] Device should cool down within a few minutes");
}