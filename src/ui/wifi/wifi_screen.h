#ifndef WIFI_SCREEN_H
#define WIFI_SCREEN_H

#include "../../../include/display.h"
#include <WiFi.h>
#include <vector>

// WiFi network structure
struct WiFiNetwork
{
    String ssid;
    int32_t rssi;
    wifi_auth_mode_t encryptionType;
    bool isConnected;
};

class WiFiScreen
{
public:
    WiFiScreen();

    // Screen drawing
    void draw(EinkDisplayManager::DisplayUpdateMode mode = EinkDisplayManager::UPDATE_PARTIAL);

    // Button handlers
    void handleSelectAction(); // Toggle WiFi or connect to network
    void handleDownAction();   // Scan for networks or navigate list
    void handleUpAction();     // Navigate list up

    // WiFi functionality
    void toggleWiFi();
    void scanNetworks();
    void connectToNetwork(const String &ssid, const String &password = "");
    void disconnect();

    // State management
    bool isWiFiEnabled() const;
    bool isConnected() const;
    String getConnectedSSID() const;
    int getSignalStrength() const;

    // Update function for main loop
    void update();

    // Configuration management
    void saveWiFiConfig(const String &ssid, const String &password);
    void loadWiFiConfig();

    // Settings management
    void showSavedNetworks();
    void selectSavedNetwork(int index);
    void deleteSavedNetwork(int index);
    void toggleAutoConnect(int index);
    void changePriority(int index, bool increase);

    // Hotspot management
    void startHotspot();
    void stopHotspot();

private:
    // UI state
    int selectedNetworkIndex;
    bool isScanning;
    bool showNetworkList;
    bool isConnecting;
    bool showSavedNetworksList;
    int selectedSavedNetworkIndex;
    int selectedMainItemIndex; // For main view navigation (saved networks + AP mode)

    // Network data
    std::vector<WiFiNetwork> availableNetworks;

    // Drawing helpers
    void drawHeader();
    void drawStatus();
    void drawNetworkList();
    void drawSavedNetworksList();
    void drawSavedNetworksMain();
    void drawScanningIndicator();
    void drawConnectionStatus();

    // WiFi helpers
    void updateNetworkList();
    String encryptionTypeToString(wifi_auth_mode_t type);
    int getNetworkRSSI(const String &ssid);

    // Web server helpers
    void setupWebServer();
    String getConfigPageHTML();
    void handleFileUpload();
    void scanNetworksForWeb();
};

#endif // WIFI_SCREEN_H