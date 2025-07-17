#ifndef PINS_H
#define PINS_H

// Display pins (UC8253 e-ink display) - CORRECTED per schematic analysis
#define EPD_CS 5    // SPI Chip Select (CS signal)
#define EPD_DC 19   // Data/Command (DC signal)
#define EPD_RST 16  // Reset (RST signal)
#define EPD_BUSY 17 // Busy signal (BUSY signal)
#define EPD_SCLK 18 // SPI Clock (CLK signal)
#define EPD_MOSI 23 // SPI Data (SDI signal)

// I2C Bus pins (RTC + Temperature Sensor)
#define I2C_SDA 21 // I2C Data (IIC_SDA) - Pin 33
#define I2C_SCL 22 // I2C Clock (IIC_SCL) - Pin 36

// SD Card pins - CORRECTED per ESP32-WROOM-32E/32UE pinout
#define SD_CS 4      // SD Card Chip Select (Pin 6 - IO4)
#define SD_MISO 0    // SD Card MISO (Pin 24 - IO 0)
#define SD_MOSI 2    // SD Card MOSI (Pin 24 - IO2)
#define SD_CLK 15    // SD Card Clock (Pin 23 - IO15)
#define WAKE_SDIO 25 // SD Card Power Control (Pin 10 - IO25)

// User interface buttons - Based on pinout
#define BTN_KEY1 34 // Button 1 (Pin 6 - IO34, input-only pin)
#define BTN_KEY2 39 // Button 2 (Pin 39 is GND, need to verify this)
#define BTN_KEY3 35 // Button 3 (Pin 7 - IO35, input-only pin)

// Button state definitions - Based on actual hardware behavior
// Buttons connected to GND, signal goes 0→1 when pressed (active HIGH)
#define BTN_PRESSED 1  // Button is pressed when pin reads HIGH
#define BTN_RELEASED 0 // Button is released when pin reads LOW

// For convenience - map to common names
#define BTN_OK BTN_KEY1    // OK/Select button
#define BTN_MENU BTN_KEY2  // Menu/Back button
#define BTN_POWER BTN_KEY3 // Power button

// Status LEDs - Based on schematic analysis
#define LED_POWER 2   // Power LED (needs pin verification)
#define LED_CHARGE 32 // Charging LED (Pin 8 - IO32)

// Power management and other signals
#define POWER_ENABLE 0 // Power enable pin (Pin 25 - IO0/EN signal)
#define RTC_INT 4      // RTC interrupt (Pin 26 - IO4)
#define BAT_ADC 36     // Battery voltage monitoring (Pin 4 - SENSOR_VP)

#endif // PINS_H