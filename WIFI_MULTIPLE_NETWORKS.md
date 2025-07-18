# Multiple WiFi Networks Setup Guide

## Overview
The E-Reader now supports saving and managing multiple WiFi networks on the SD card. Users can configure multiple networks with different priorities and auto-connect settings.

## Configuration File Format

### File Location
- **File Name**: `wifi_networks.json`
- **Location**: Root directory of SD card (`/wifi_networks.json`)
- **Storage**: SD card only (SPIFFS not supported for multiple networks)

### JSON Structure
```json
{
  "networks": [
    {
      "ssid": "NetworkName",
      "password": "network_password",
      "autoConnect": true,
      "priority": 10
    }
  ]
}
```

### Field Descriptions
- **ssid**: WiFi network name (required)
- **password**: WiFi password (required, use empty string "" for open networks)
- **autoConnect**: Whether to automatically connect to this network (default: true)
- **priority**: Connection priority (higher numbers = higher priority, default: 0)

## Features

### 1. Automatic Connection
- Device automatically connects to the highest priority network with `autoConnect: true`
- Networks are sorted by priority (highest first)
- Only attempts connection to networks with `autoConnect` enabled

### 2. Manual Network Selection
- Press **UP** button from main WiFi screen to view saved networks
- Use **UP/DOWN** buttons to navigate through saved networks
- Press **SELECT** to connect to highlighted network
- Networks display: `NetworkName [P:priority] ✓/✗ (Connected)`
  - `[P:X]`: Priority level
  - `✓`: Auto-connect enabled
  - `✗`: Auto-connect disabled
  - `(Connected)`: Currently connected network

### 3. Network Management
- **Add Networks**: Connect to new networks via web interface (hotspot mode)
- **Priority Management**: Higher priority networks are attempted first
- **Auto-Connect Toggle**: Control which networks connect automatically
- **Status Display**: Shows number of saved networks and highest priority network

## Usage Instructions

### Initial Setup
1. Create `wifi_networks.json` file on SD card root
2. Add your WiFi networks using the JSON format above
3. Insert SD card into E-Reader
4. Device will automatically load and connect to highest priority network

### Adding New Networks
1. From WiFi screen, press **SELECT** to enter setup mode
2. Connect to "E-Reader-Setup" hotspot
3. Navigate to `192.168.4.1` in web browser
4. Select network and enter password
5. New network is automatically added to configuration

### Viewing Saved Networks
1. From main WiFi screen, press **UP** button
2. Navigate through saved networks with **UP/DOWN**
3. Press **SELECT** to connect to highlighted network
4. Press **UP** again to return to main screen

## Example Configuration

See `wifi_networks_example.json` for a complete example with three networks:
- Home network (highest priority, auto-connect)
- Office network (medium priority, auto-connect)
- Guest network (lowest priority, manual connect only)

## Technical Details

### Priority System
- Networks are sorted by priority in descending order
- New networks get priority equal to current network count
- Priority can be any positive integer
- Higher numbers = higher priority

### Auto-Connect Behavior
- Only networks with `autoConnect: true` are attempted automatically
- Connection attempts follow priority order
- First successful connection stops the process
- Failed connections move to next priority network

### File Management
- Configuration is saved to SD card only
- File is updated when new networks are added
- Manual editing of JSON file is supported
- Invalid JSON will be ignored with error logging

## Troubleshooting

### No Networks Loading
- Verify SD card is properly inserted and powered
- Check `wifi_networks.json` exists in SD card root
- Validate JSON syntax using online JSON validator
- Check serial output for parsing errors

### Connection Issues
- Verify network passwords are correct
- Check network is in range and operational
- Ensure `autoConnect` is set to `true` for automatic connection
- Review priority settings for connection order

### File Format Errors
- Use double quotes for all string values
- Ensure proper JSON comma placement
- Validate boolean values are `true`/`false` (lowercase)
- Check integer values for priority (no quotes)

## Migration from Single Network

If upgrading from single network configuration:
1. Old `wifi_config.json` files are no longer used
2. Create new `wifi_networks.json` with array format
3. Convert single network to array with one element
4. Add priority and autoConnect fields

Example migration:
```json
// Old format (wifi_config.json)
{
  "ssid": "MyNetwork",
  "password": "mypassword",
  "configured": true
}

// New format (wifi_networks.json)
{
  "networks": [
    {
      "ssid": "MyNetwork",
      "password": "mypassword",
      "autoConnect": true,
      "priority": 10
    }
  ]
}
```