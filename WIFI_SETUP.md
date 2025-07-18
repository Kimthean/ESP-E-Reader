# WiFi Setup Guide

## Overview

The E-Reader now supports WiFi configuration through a hotspot mode for initial setup. This allows you to:

1. **Configure WiFi credentials** through a web interface
2. **Upload files** (TXT, EPUB, PDF) directly to the device
3. **Manage network connections** without hardcoding credentials

## How to Use

### Initial Setup (First Time)

1. **Navigate to WiFi Screen**
   - From the main menu, select "WiFi" using the navigation buttons
   - Press SELECT to enter the WiFi screen

2. **Start Hotspot Mode**
   - If no WiFi is configured, press SELECT to start hotspot mode
   - The device will create a WiFi access point named "E-Reader-Setup"
   - The screen will show "Setup Mode Active"

3. **Connect Your Phone/Computer**
   - Connect to the "E-Reader-Setup" WiFi network (no password required)
   - Open a web browser and go to `192.168.4.1`
   - You should see the E-Reader setup page

4. **Configure WiFi**
   - Click "Scan for Networks" to see available WiFi networks
   - Click on your desired network to auto-fill the SSID
   - Enter your WiFi password
   - Click "Save WiFi Settings"
   - The device will restart and connect to your WiFi

### File Upload

While in setup mode, you can also upload files:

1. **Access the Web Interface**
   - Connect to "E-Reader-Setup" and go to `192.168.4.1`

2. **Upload Files**
   - Scroll down to the "File Upload" section
   - Click "Choose File" and select your book file
   - Supported formats: TXT, EPUB, PDF
   - Click "Upload File"
   - Files are stored on the SD card (if available) or internal storage

### Managing WiFi After Setup

- **View Status**: The WiFi screen shows connection status and signal strength
- **Disconnect**: Press SELECT to disconnect from current network
- **Reconnect**: Press SELECT again to reconnect to saved network
- **Scan Networks**: Press DOWN to scan for available networks
- **Navigate Networks**: Use UP/DOWN to browse scanned networks
- **Reconfigure**: Start hotspot mode again to change WiFi settings

## Button Controls

### In WiFi Screen:
- **UP**: Navigate up in network list or return to main menu
- **SELECT**: Toggle WiFi, connect to network, or start/stop hotspot
- **DOWN**: Scan for networks or navigate down in network list

### In Hotspot Mode:
- **SELECT**: Stop hotspot and return to normal WiFi mode
- **UP**: Return to main menu

## Technical Details

### Storage
- WiFi credentials are stored in `/wifi_config.json`
- Stored on SD card if available, otherwise in SPIFFS
- Configuration persists across reboots

### Security
- Hotspot mode is open (no password) for easy setup
- Only active when explicitly started by user
- Automatically stops when WiFi is configured
- Web interface is only accessible when in hotspot mode

### File Management
- Uploaded files are stored in the root directory
- SD card is preferred over internal storage
- File size limited by available storage space

## Troubleshooting

### Can't Connect to Hotspot
- Ensure the device shows "Setup Mode Active"
- Try forgetting and reconnecting to "E-Reader-Setup"
- Check that your device supports 2.4GHz WiFi

### Web Interface Not Loading
- Try going directly to `192.168.4.1`
- Clear your browser cache
- Ensure you're connected to "E-Reader-Setup" network

### WiFi Connection Fails
- Double-check password (case-sensitive)
- Ensure network is 2.4GHz (5GHz not supported)
- Check signal strength - move closer to router

### File Upload Issues
- Check available storage space
- Ensure file format is supported (TXT, EPUB, PDF)
- Try smaller files first

## Future Enhancements

- Support for WPA2-Enterprise networks
- Automatic network reconnection
- File management through web interface
- OTA (Over-The-Air) firmware updates
- Book library synchronization

## API Endpoints

When in hotspot mode, the following endpoints are available:

- `GET /` - Main configuration page
- `POST /configure` - Save WiFi configuration
- `POST /upload` - Upload files
- `GET /scan` - Scan for WiFi networks (JSON response)

Example scan response:
```json
{
  "networks": [
    {
      "ssid": "MyNetwork",
      "rssi": -45,
      "encryption": true
    }
  ]
}
```