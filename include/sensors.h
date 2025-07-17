#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>

#define RX8025T_I2C_ADDRESS 0x32

struct SensorData
{
    float temperature;
    float humidity;
    time_t rtc_time;
    bool temperature_valid;
    bool humidity_valid;
    bool rtc_valid;
};

bool initializeSensors();

bool initAHT30();
SensorData readAHT30();
bool isAHT30Available();

bool initRTC();
time_t getRTCTime();
bool setRTCTime(time_t time);
bool isRTCAvailable();

SensorData readAllSensors();

void printSensorStatus();
String formatTime(time_t time);
String formatDate(time_t time);

#endif