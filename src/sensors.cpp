#include "sensors.h"
#include "pins.h"

// Global sensor objects
Adafruit_AHTX0 aht;

// Sensor availability flags
bool aht30_available = false;
bool rtc_available = false;

/**
 * Initialize all sensors
 */
bool initializeSensors()
{
    Serial.println("Initializing sensors...");

    // Initialize I2C
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000); // 100kHz I2C clock

    bool aht_init = initAHT30();
    bool rtc_init = initRTC();

    if (aht_init && rtc_init)
    {
        Serial.println("All sensors initialized successfully");
        return true;
    }
    else
    {
        Serial.println("Some sensors failed to initialize");
        return false;
    }
}

/**
 * Initialize AHT30 temperature/humidity sensor
 */
bool initAHT30()
{
    Serial.print("Initializing AHT30 sensor... ");

    if (aht.begin())
    {
        aht30_available = true;
        Serial.println("Success!");

        // Print sensor info
        Serial.print("AHT30 sensor found: ");
        Serial.print("Temperature range: -40°C to 85°C, ");
        Serial.println("Humidity range: 0% to 100%");

        return true;
    }
    else
    {
        aht30_available = false;
        Serial.println("Failed!");
        Serial.println("Check wiring and I2C address (0x38)");
        return false;
    }
}

/**
 * Initialize RX8025T RTC using basic I2C detection
 */
bool initRTC()
{
    Serial.print("Initializing RX8025T RTC... ");

    // Use basic I2C detection since library may not be working
    Wire.beginTransmission(0x32); // RX8025T I2C address
    byte error = Wire.endTransmission();

    if (error == 0)
    {
        rtc_available = true;
        Serial.println("Success!");
        Serial.println("RX8025T detected via I2C scan");
        return true;
    }
    else
    {
        rtc_available = false;
        Serial.println("Failed!");
        Serial.print("I2C error: ");
        Serial.println(error);
        Serial.println("Check wiring and I2C address (0x32)");
        return false;
    }
}

/**
 * Read AHT30 sensor data
 */
SensorData readAHT30()
{
    SensorData data;
    data.temperature_valid = false;
    data.humidity_valid = false;
    data.rtc_valid = false;

    if (!aht30_available)
    {
        Serial.println("AHT30 not available");
        return data;
    }

    sensors_event_t humidity_event, temperature_event;

    if (aht.getEvent(&humidity_event, &temperature_event))
    {
        data.temperature = temperature_event.temperature;
        data.humidity = humidity_event.relative_humidity;
        data.temperature_valid = true;
        data.humidity_valid = true;

        Serial.print("AHT30 - Temperature: ");
        Serial.print(data.temperature, 2);
        Serial.print("°C, Humidity: ");
        Serial.print(data.humidity, 2);
        Serial.println("%");
    }
    else
    {
        Serial.println("Failed to read AHT30 sensor");
    }

    return data;
}

/**
 * Get current RTC time (basic implementation)
 */
time_t getRTCTime()
{
    if (!rtc_available)
    {
        return 0;
    }

    // For now, return a basic timestamp
    // TODO: Implement proper RTC reading when library is working
    return millis() / 1000;
}

/**
 * Set RTC time (basic implementation)
 */
bool setRTCTime(time_t time)
{
    if (!rtc_available)
    {
        Serial.println("RTC not available - cannot set time");
        return false;
    }

    // TODO: Implement proper RTC setting when library is working
    Serial.println("RTC time setting not implemented yet");
    return false;
}

/**
 * Read all sensors
 */
SensorData readAllSensors()
{
    SensorData data;

    // Read AHT30
    SensorData aht_data = readAHT30();
    data.temperature = aht_data.temperature;
    data.humidity = aht_data.humidity;
    data.temperature_valid = aht_data.temperature_valid;
    data.humidity_valid = aht_data.humidity_valid;

    // Read RTC
    if (rtc_available)
    {
        data.rtc_time = getRTCTime();
        data.rtc_valid = true;

        Serial.print("RTC - Current time: ");
        Serial.println(formatTime(data.rtc_time));
    }
    else
    {
        data.rtc_valid = false;
    }

    return data;
}

/**
 * Check if AHT30 is available
 */
bool isAHT30Available()
{
    return aht30_available;
}

/**
 * Check if RTC is available
 */
bool isRTCAvailable()
{
    return rtc_available;
}

/**
 * Print sensor status
 */
void printSensorStatus()
{
    Serial.println("=== Sensor Status ===");
    Serial.print("AHT30 Temperature/Humidity: ");
    Serial.println(aht30_available ? "Available" : "Not Available");

    Serial.print("RX8025T RTC: ");
    Serial.println(rtc_available ? "Available" : "Not Available");
    Serial.println("Using basic I2C communication");

    if (aht30_available || rtc_available)
    {
        Serial.println("Reading sensors...");
        SensorData data = readAllSensors();

        Serial.println("=== Current Readings ===");
        if (data.temperature_valid)
        {
            Serial.print("Temperature: ");
            Serial.print(data.temperature, 2);
            Serial.println("°C");
        }

        if (data.humidity_valid)
        {
            Serial.print("Humidity: ");
            Serial.print(data.humidity, 2);
            Serial.println("%");
        }

        if (data.rtc_valid)
        {
            Serial.print("Date/Time: ");
            Serial.println(formatTime(data.rtc_time));
        }
    }
    Serial.println("=====================");
}

/**
 * Format time_t as readable string
 */
String formatTime(time_t time)
{
    struct tm *timeinfo;
    timeinfo = localtime(&time);
    char buffer[6];
    strftime(buffer, sizeof(buffer), "%H:%M", timeinfo);
    return String(buffer);
}

/**
 * Format time_t as date string
 */
String formatDate(time_t time)
{
    if (time == 0)
    {
        return "Date not set";
    }

    struct tm *timeinfo = localtime(&time);

    if (timeinfo == nullptr)
    {
        return "Invalid date";
    }

    String dateStr = "";
    dateStr += String(timeinfo->tm_year + 1900) + "/";
    dateStr += String(timeinfo->tm_mon + 1) + "/";
    dateStr += String(timeinfo->tm_mday);

    return dateStr;
}