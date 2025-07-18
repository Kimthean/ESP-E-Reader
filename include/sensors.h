#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <WiFi.h>
#include <time.h>

#define RX8025T_I2C_ADDRESS 0x32

// NTP Configuration
#define NTP_SERVER3 "pool.ntp.org"
#define NTP_SERVER2 "time.nist.gov"
#define NTP_SERVER1 "time.google.com"
#define DEFAULT_GMT_OFFSET_SEC (7 * 3600)     // Cambodia Time (UTC+7)
#define DEFAULT_DAYLIGHT_OFFSET_SEC 0         // Cambodia doesn't use DST
#define NTP_SYNC_INTERVAL_MS (60 * 60 * 1000) // Sync every hour

// Common timezone offsets (in seconds)
#define TZ_UTC 0
#define TZ_EST (-5 * 3600)  // Eastern Standard Time
#define TZ_PST (-8 * 3600)  // Pacific Standard Time
#define TZ_CET (1 * 3600)   // Central European Time
#define TZ_JST (9 * 3600)   // Japan Standard Time
#define TZ_AEST (10 * 3600) // Australian Eastern Standard Time
#define TZ_ICT (7 * 3600)   // Indochina Time (Cambodia, Vietnam, Thailand)

struct SensorData
{
    float temperature;
    float humidity;
    time_t rtc_time;
    bool temperature_valid;
    bool humidity_valid;
    bool rtc_valid;
};

struct TimeStatus
{
    bool ntp_synced;
    bool rtc_available;
    time_t last_ntp_sync;
    time_t current_time;
    String time_source; // "NTP", "RTC", or "SYSTEM"
};

// Sensor functions
bool initializeSensors();

bool initAHT30();
SensorData readAHT30();
bool isAHT30Available();

bool initRTC();
time_t getRTCTime();
bool setRTCTime(time_t time);
bool isRTCAvailable();

SensorData readAllSensors();

// Time management functions
bool initTimeSync();
bool startNTPSync();
bool checkNTPSync();
bool syncTimeWithNTP();
TimeStatus getTimeStatus();
time_t getCurrentTime();
bool shouldSyncNTP();
void updateTimeFromNTP();

void printSensorStatus();
String formatTime(time_t time);
String formatDate(time_t time);

#endif