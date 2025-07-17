# E-Reader Project

A comprehensive e-reader firmware for ESP32-based hardware with temperature/humidity monitoring, RTC functionality, and future support for novel and manga reading.

## Hardware Specifications

- **MCU**: ESP32 WROOM 32U
- **Display**: UC8253 e-ink display controller (240 × 416 pixels, 127 DPI)
- **Outer Size**: 93mm × 53mm
- **Display Size**: 81.54mm × 47.04mm
- **Sensors**: 
  - AHT30 temperature/humidity sensor
  - RX8025T real-time clock
- **Storage**: SD card slot for user data
- **Connectivity**: WiFi for API access

## Current Implementation (v1.0)

### ✅ Completed Features
- **Sensor Integration**: AHT30 temperature/humidity sensor reading
- **RTC Support**: RX8025T real-time clock functionality
- **SD Card**: Data logging and storage
- **GPIO**: Button inputs and LED indicators
- **Serial Debug**: Comprehensive debug output
- **Data Logging**: JSON format sensor data logging to SD card

### 🔧 Hardware Connections

```cpp
// Display (UC8253)
#define EPD_CS     5    // SPI Chip Select
#define EPD_DC     17   // Data/Command
#define EPD_RST    16   // Reset
#define EPD_BUSY   4    // Busy signal
#define EPD_SCLK   18   // SPI Clock
#define EPD_MOSI   23   // SPI Data

// I2C Bus (RTC + Temperature Sensor)
#define I2C_SDA    21   // I2C Data
#define I2C_SCL    22   // I2C Clock

// User Buttons
#define BTN_OK     33   // Select/OK button
#define BTN_MENU   32   // Menu/Back button
```

### 📁 Project Structure

```
E-Reader/
├── include/
│   ├── pins.h          # GPIO pin definitions
│   ├── sensors.h       # Sensor interface declarations
│   ├── power.h         # Power management (future)
│   └── config.h        # Application configuration
├── src/
│   ├── main.cpp        # Main application entry point
│   └── sensors.cpp     # Sensor implementation
├── lib/                # External libraries (PlatformIO)
├── test/               # Unit tests
└── platformio.ini      # PlatformIO configuration
```

### 🚀 Getting Started

1. **Hardware Setup**:
   - Connect AHT30 sensor to I2C bus (SDA: GPIO21, SCL: GPIO22)
   - Connect RX8025T RTC to I2C bus (shared with AHT30)
   - Insert SD card into slot
   - Connect buttons to designated GPIO pins

2. **Software Setup**:
   ```bash
   # Install PlatformIO
   pip install platformio
   
   # Clone project and build
   git clone <repository-url>
   cd E-Reader
   pio run
   
   # Upload to device
   pio run --target upload
   
   # Monitor serial output
   pio device monitor
   ```

3. **Testing**:
   - Power on device
   - Monitor serial output for sensor initialization
   - Press OK button for immediate sensor readings
   - Press MENU button for system status

### 📊 Data Format

Sensor data is logged to SD card in JSON format:
```json
{
  "timestamp": 12345,
  "temperature": 23.45,
  "humidity": 56.78,
  "rtc_year": 2024,
  "rtc_month": 3,
  "rtc_day": 15,
  "rtc_hour": 14,
  "rtc_minute": 30,
  "rtc_second": 45
}
```

## Future Roadmap

### 📱 Planned Applications

1. **Home Screen**
   - Date/time display using RTC
   - Temperature/humidity from sensors
   - Recent books and reading progress
   - Battery status and charging indicator

2. **Screen Saver**
   - Clock display with temperature/humidity
   - Automatic activation after inactivity
   - Power-saving display updates

3. **Settings App**
   - WiFi configuration
   - Display brightness/contrast
   - Power management settings
   - System information

4. **Novel Reader**
   - TXT file support
   - EPUB reader implementation
   - Bookmarks and reading progress
   - Adjustable font size and style

5. **Manga Reader**
   - Integration with [Manga Hook API](https://mangahook-api.vercel.app)
   - Chapter navigation and bookmarks
   - Offline reading with caching
   - Zoom and pan functionality

### 🔧 Technical Improvements

- **Display Integration**: UC8253 e-ink display driver
- **Power Management**: Deep sleep modes and battery optimization
- **WiFi Integration**: Network connectivity for API access
- **User Interface**: Touch/button navigation system
- **File Management**: Book library and file organization

### 📡 API Integration

The manga reader will integrate with the Manga Hook API:
- Base URL: `https://mangahook-api.vercel.app`
- Features: Search, chapter lists, manga metadata
- Caching: Offline reading support
- Authentication: API key management

## Development Guidelines

### 🔨 Code Standards
- Follow Arduino C++ style guide
- Use proper header guards and includes
- Implement error handling for all sensor operations
- Add comprehensive serial debug output
- Structure code in logical modules

### 🛠 Testing
- Test all sensor readings independently
- Verify SD card read/write operations
- Test button debouncing and responsiveness
- Monitor power consumption
- Validate API integration

### 📚 Dependencies
- **Platform**: ESP32 (espressif32)
- **Framework**: Arduino
- **Libraries**: 
  - Adafruit AHTX0 (temperature/humidity)
  - RTClib (real-time clock)
  - ArduinoJson (data serialization)
  - Adafruit GFX (future display support)

## Troubleshooting

### Common Issues

1. **Sensor Not Found**:
   - Check I2C wiring (SDA: GPIO21, SCL: GPIO22)
   - Verify sensor power supply (3.3V)
   - Check I2C address (AHT30: 0x38)

2. **SD Card Issues**:
   - Ensure SD card is formatted (FAT32 recommended)
   - Check SPI connections
   - Verify SD_CS pin (GPIO14)

3. **RTC Time Issues**:
   - RTC may need initial time setting
   - Check backup battery if available
   - Verify I2C communication

### Debug Tips
- Enable serial monitor at 115200 baud
- Check sensor status with MENU button
- Monitor SD card file operations
- Use LED indicators for system status

## License

This project is open source. Please check the LICENSE file for details.

## Contributing

Contributions are welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Test thoroughly
4. Submit a pull request

## Support

For issues and questions:
- Check the troubleshooting section
- Review hardware connections
- Enable debug output
- Check component datasheets 