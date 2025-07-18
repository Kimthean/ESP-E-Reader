#include "wifi_screen.h"
#include "../../../include/storage.h"
#include "../../../include/power.h"
#include "../../../include/display.h"
#include "../../../include/sensors.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <SD.h>
#include <algorithm>

// Forward declarations for storage functions
extern bool powerOnSDCard();
extern bool isSDCardPowered();

// Web server and DNS for captive portal
WebServer webServer(80);
DNSServer dnsServer;

// WiFi configuration structures
struct SavedWiFiNetwork
{
    String ssid;
    String password;
    bool autoConnect;
    int priority; // Higher number = higher priority
};

struct WiFiConfig
{
    std::vector<SavedWiFiNetwork> savedNetworks;
    int activeNetworkIndex; // Currently connected network index (-1 if none)
    bool isConfigured;
};

static WiFiConfig savedConfig;
static bool apModeActive = false;
static bool webServerRunning = false;
const char *AP_SSID = "E-Reader";
const char *AP_PASSWORD = "";
const byte DNS_PORT = 53;

// File upload handling
File uploadFile;

WiFiScreen::WiFiScreen()
{
    selectedNetworkIndex = 0;
    isScanning = false;
    showNetworkList = false;
    isConnecting = false;
    showSavedNetworksList = false;
    selectedSavedNetworkIndex = 0;
    selectedMainItemIndex = 0;

    // Initialize WiFi config
    savedConfig.activeNetworkIndex = -1;
    savedConfig.isConfigured = false;

    // Load saved WiFi configuration
    loadWiFiConfig();
}

void WiFiScreen::draw(EinkDisplayManager::DisplayUpdateMode mode)
{
    extern EinkDisplayManager display;

    display.startDrawing();
    drawHeader();
    drawStatus();

    if (isScanning)
    {
        drawScanningIndicator();
    }
    else if (showSavedNetworksList)
    {
        drawSavedNetworksList();
    }
    else if (showNetworkList && !availableNetworks.empty())
    {
        drawNetworkList();
    }
    else
    {
        // Always show saved networks in main view
        drawSavedNetworksMain();
    }

    display.endDrawing();
    display.update(mode);
}

void WiFiScreen::handleSelectAction()
{
    if (apModeActive)
    {
        // In AP mode, toggle back to station mode
        stopHotspot();
        return;
    }

    if (showSavedNetworksList && !savedConfig.savedNetworks.empty())
    {
        // Connect to selected saved network
        if (selectedSavedNetworkIndex < savedConfig.savedNetworks.size())
        {
            selectSavedNetwork(selectedSavedNetworkIndex);
        }
    }
    else if (showNetworkList && !availableNetworks.empty())
    {
        // Connect to selected network
        if (selectedNetworkIndex < availableNetworks.size())
        {
            WiFiNetwork &network = availableNetworks[selectedNetworkIndex];
            if (network.encryptionType == WIFI_AUTH_OPEN)
            {
                connectToNetwork(network.ssid, "");
            }
            else
            {
                // For encrypted networks, we'll use the web interface
                Serial.println("Encrypted network - use web interface to enter password");
            }
        }
    }
    else
    {
        // Main view selection
        if (selectedMainItemIndex < savedConfig.savedNetworks.size())
        {
            // Connect to selected saved network
            selectSavedNetwork(selectedMainItemIndex);
        }
        else if (selectedMainItemIndex == savedConfig.savedNetworks.size())
        {
            // Start AP mode
            startHotspot();
        }
    }
}

void WiFiScreen::handleDownAction()
{
    if (apModeActive)
    {
        return; // No navigation in AP mode
    }

    if (showSavedNetworksList && !savedConfig.savedNetworks.empty())
    {
        // Navigate down in saved networks list
        selectedSavedNetworkIndex = (selectedSavedNetworkIndex + 1) % savedConfig.savedNetworks.size();
    }
    else if (showNetworkList && !availableNetworks.empty())
    {
        // Navigate down in network list
        selectedNetworkIndex = (selectedNetworkIndex + 1) % availableNetworks.size();
    }
    else
    {
        // Navigate down in main view (saved networks + AP mode)
        int totalItems = savedConfig.savedNetworks.size() + 1; // +1 for AP mode
        if (totalItems > 1)
        {
            selectedMainItemIndex = (selectedMainItemIndex + 1) % totalItems;
        }
        else
        {
            // If no saved networks, scan for networks
            scanNetworks();
        }
    }
}

void WiFiScreen::handleUpAction()
{
    if (apModeActive)
    {
        return; // No navigation in AP mode
    }

    if (showSavedNetworksList && !savedConfig.savedNetworks.empty())
    {
        // Navigate up in saved networks list
        selectedSavedNetworkIndex = (selectedSavedNetworkIndex - 1 + savedConfig.savedNetworks.size()) % savedConfig.savedNetworks.size();
    }
    else if (showNetworkList && !availableNetworks.empty())
    {
        // Navigate up in network list
        selectedNetworkIndex = (selectedNetworkIndex - 1 + availableNetworks.size()) % availableNetworks.size();
    }
    else
    {
        // Navigate up in main view (saved networks + AP mode)
        int totalItems = savedConfig.savedNetworks.size() + 1; // +1 for AP mode
        if (totalItems > 1)
        {
            selectedMainItemIndex = (selectedMainItemIndex - 1 + totalItems) % totalItems;
        }
        else
        {
            // Show saved networks settings if available
            if (!savedConfig.savedNetworks.empty())
            {
                showSavedNetworks();
            }
        }
    }
}

void WiFiScreen::startHotspot()
{
    Serial.println("Starting WiFi hotspot for setup...");

    // Stop any existing WiFi connection
    WiFi.disconnect();
    delay(100);

    // Start Access Point
    WiFi.mode(WIFI_AP);
    bool success = WiFi.softAP(AP_SSID, AP_PASSWORD);

    if (success)
    {
        apModeActive = true;

        // Start DNS server for captive portal
        dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

        // Initialize SPIFFS for web files
        if (!SPIFFS.begin(true))
        {
            Serial.println("SPIFFS initialization failed");
        }

        // Setup web server routes
        setupWebServer();
        webServer.begin();
        webServerRunning = true;

        Serial.print("Hotspot started. IP: ");
        Serial.println(WiFi.softAPIP());
        Serial.println("Connect to 'E-Reader-Setup' and go to 192.168.4.1");
    }
    else
    {
        Serial.println("Failed to start hotspot");
    }
}

void WiFiScreen::stopHotspot()
{
    Serial.println("Stopping hotspot...");

    if (webServerRunning)
    {
        webServer.stop();
        webServerRunning = false;
    }

    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    apModeActive = false;

    // Try to connect to saved network if available
    if (savedConfig.isConfigured && !savedConfig.savedNetworks.empty())
    {
        connectToNetwork(savedConfig.savedNetworks[0].ssid, savedConfig.savedNetworks[0].password);
    }
}

void WiFiScreen::setupWebServer()
{
    // Serve main configuration page
    webServer.on("/", HTTP_GET, [this]()
                 {
        String html = getConfigPageHTML();
        webServer.send(200, "text/html", html); });

    // Handle WiFi configuration
    webServer.on("/configure", HTTP_POST, [this]()
                 {
        if (webServer.hasArg("ssid") && webServer.hasArg("password")) {
            String ssid = webServer.arg("ssid");
            String password = webServer.arg("password");
            
            // Save configuration
            saveWiFiConfig(ssid, password);
            
            webServer.send(200, "text/html", 
                "<html><body><h2>Configuration Saved!</h2>"
                "<p>WiFi credentials saved. Device will restart and connect.</p>"
                "</body></html>");
            
            // Restart and connect
            delay(2000);
            ESP.restart();
        } else {
            webServer.send(400, "text/html", "<html><body><h2>Error</h2><p>Missing parameters</p></body></html>");
        } });

    // Handle file uploads
    webServer.on("/upload", HTTP_POST, [this]()
                 { webServer.send(200, "text/html",
                                  "<html><body><h2>Upload Complete</h2>"
                                  "<p>File uploaded successfully!</p>"
                                  "<a href='/'>Back to main page</a>"
                                  "</body></html>"); }, [this]()
                 { handleFileUpload(); });

    // Scan networks API
    webServer.on("/scan", HTTP_GET, [this]()
                 { scanNetworksForWeb(); });

    // Captive portal redirect
    webServer.onNotFound([this]()
                         {
        String html = getConfigPageHTML();
        webServer.send(200, "text/html", html); });
}

String WiFiScreen::getConfigPageHTML()
{
    String html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>E-Reader Setup</title>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }
        .container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; }
        h1 { color: #333; text-align: center; }
        .section { margin: 20px 0; padding: 15px; border: 1px solid #ddd; border-radius: 5px; }
        input, select, button { width: 100%; padding: 10px; margin: 5px 0; border: 1px solid #ccc; border-radius: 3px; }
        button { background: #007cba; color: white; cursor: pointer; }
        button:hover { background: #005a87; }
        .network-list { max-height: 200px; overflow-y: auto; }
        .network-item { padding: 10px; border-bottom: 1px solid #eee; cursor: pointer; }
        .network-item:hover { background: #f5f5f5; }
        .upload-area { border: 2px dashed #ccc; padding: 20px; text-align: center; }
    </style>
</head>
<body>
    <div class='container'>
        <h1>E-Reader Setup</h1>
        
        <div class='section'>
            <h3>WiFi Configuration</h3>
            <button onclick='scanNetworks()'>Scan for Networks</button>
            <div id='networks' class='network-list'></div>
            
            <form action='/configure' method='post'>
                <input type='text' name='ssid' id='ssid' placeholder='WiFi Network Name (SSID)' required>
                <input type='password' name='password' id='password' placeholder='WiFi Password'>
                <button type='submit'>Save WiFi Settings</button>
            </form>
        </div>
        
        <div class='section'>
            <h3>File Upload</h3>
            <div class='upload-area'>
                <form action='/upload' method='post' enctype='multipart/form-data'>
                    <input type='file' name='file' accept='.txt,.epub,.pdf' required>
                    <br><br>
                    <button type='submit'>Upload File</button>
                </form>
                <p><small>Supported formats: TXT, EPUB, PDF</small></p>
            </div>
        </div>
        
        <div class='section'>
            <h3>Device Info</h3>
            <p><strong>Device:</strong> E-Reader</p>
            <p><strong>IP Address:</strong> )" +
                  WiFi.softAPIP().toString() + R"(</p>
            <p><strong>Status:</strong> Setup Mode</p>
        </div>
    </div>
    
    <script>
        function scanNetworks() {
            fetch('/scan')
                .then(response => response.json())
                .then(data => {
                    const networksDiv = document.getElementById('networks');
                    networksDiv.innerHTML = '';
                    data.networks.forEach(network => {
                        const div = document.createElement('div');
                        div.className = 'network-item';
                        div.innerHTML = `<strong>${network.ssid}</strong> (${network.rssi}dBm) ${network.encryption ? '🔒' : '🔓'}`;
                        div.onclick = () => {
                            document.getElementById('ssid').value = network.ssid;
                        };
                        networksDiv.appendChild(div);
                    });
                })
                .catch(err => console.error('Scan failed:', err));
        }
        
        // Auto-scan on page load
        window.onload = () => scanNetworks();
    </script>
</body>
</html>
)";
    return html;
}

void WiFiScreen::handleFileUpload()
{
    HTTPUpload &upload = webServer.upload();

    if (upload.status == UPLOAD_FILE_START)
    {
        String filename = "/" + upload.filename;
        Serial.printf("Upload start: %s\n", filename.c_str());

        // Try SD card first, then SPIFFS
        if (isSDCardPowered())
        {
            uploadFile = SD.open(filename, FILE_WRITE);
        }
        else
        {
            uploadFile = SPIFFS.open(filename, FILE_WRITE);
        }

        if (!uploadFile)
        {
            Serial.println("Failed to open file for upload");
        }
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
        if (uploadFile)
        {
            uploadFile.write(upload.buf, upload.currentSize);
        }
    }
    else if (upload.status == UPLOAD_FILE_END)
    {
        if (uploadFile)
        {
            uploadFile.close();
            Serial.printf("Upload complete: %s (%u bytes)\n", upload.filename.c_str(), upload.totalSize);
        }
    }
}

void WiFiScreen::scanNetworksForWeb()
{
    int n = WiFi.scanNetworks();

    JsonDocument JsonDocument;
    JsonArray networks = JsonDocument["networks"].to<JsonArray>();

    for (int i = 0; i < n; i++)
    {
        JsonObject network = networks.add<JsonObject>();
        network["ssid"] = WiFi.SSID(i);
        network["rssi"] = WiFi.RSSI(i);
        network["encryption"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }

    String response;
    serializeJson(JsonDocument, response);
    webServer.send(200, "application/json", response);
}

void WiFiScreen::saveWiFiConfig(const String &ssid, const String &password)
{
    // Ensure SD card is powered on for saving config
    if (!isSDCardPowered())
    {
        Serial.println("[WiFi] SD card not powered, attempting to power on...");
        if (!powerOnSDCard())
        {
            Serial.println("[WiFi] Failed to power on SD card for saving config");
            return;
        }
        delay(100); // Allow power to stabilize
    }

    // Check if network already exists
    bool networkExists = false;
    for (auto &network : savedConfig.savedNetworks)
    {
        if (network.ssid == ssid)
        {
            network.password = password;
            networkExists = true;
            Serial.println("[WiFi] Updated existing network: " + ssid);
            break;
        }
    }

    // Add new network if it doesn't exist
    if (!networkExists)
    {
        SavedWiFiNetwork newNetwork;
        newNetwork.ssid = ssid;
        newNetwork.password = password;
        newNetwork.autoConnect = true;
        newNetwork.priority = savedConfig.savedNetworks.size(); // Lower priority for new networks

        savedConfig.savedNetworks.push_back(newNetwork);
        Serial.println("[WiFi] Added new network: " + ssid);
    }

    savedConfig.isConfigured = true;

    // Save all networks to SD card
    JsonDocument doc;
    JsonArray networks = doc["networks"].to<JsonArray>();

    for (const auto &network : savedConfig.savedNetworks)
    {
        JsonObject netObj = networks.add<JsonObject>();
        netObj["ssid"] = network.ssid;
        netObj["password"] = network.password;
        netObj["autoConnect"] = network.autoConnect;
        netObj["priority"] = network.priority;
    }

    String configJson;
    serializeJson(doc, configJson);

    // Ensure config directory exists
    if (!SD.exists("/config"))
    {
        SD.mkdir("/config");
        Serial.println("[WiFi] Created config directory");
    }

    File configFile = SD.open("/config/wifi_networks.json", FILE_WRITE);
    if (configFile)
    {
        configFile.print(configJson);
        configFile.close();
        Serial.println("[WiFi] Configuration saved successfully");
        Serial.println("[WiFi] Saved JSON: " + configJson);
    }
    else
    {
        Serial.println("[WiFi] Failed to save configuration");
    }
}

void WiFiScreen::loadWiFiConfig()
{
    Serial.println("[WiFi] Loading WiFi configuration...");

    // Clear existing configuration
    savedConfig.savedNetworks.clear();
    savedConfig.activeNetworkIndex = -1;
    savedConfig.isConfigured = false;

    // Ensure SD card is powered on for loading config
    if (!isSDCardPowered())
    {
        Serial.println("[WiFi] SD card not powered, attempting to power on...");
        if (!powerOnSDCard())
        {
            Serial.println("[WiFi] Failed to power on SD card for loading config");
            return;
        }
        delay(100); // Allow power to stabilize
    }

    // Check if config file exists
    if (!SD.exists("/config/wifi_networks.json"))
    {
        Serial.println("[WiFi] No config file found on SD card");
        return;
    }

    File configFile = SD.open("/config/wifi_networks.json", FILE_READ);
    if (!configFile)
    {
        Serial.println("[WiFi] Failed to open config file");
        return;
    }

    String configJson = configFile.readString();
    configFile.close();

    Serial.println("[WiFi] Config file content: " + configJson);

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, configJson);

    if (error)
    {
        Serial.println("[WiFi] Failed to parse JSON: " + String(error.c_str()));
        return;
    }

    JsonArray networks = doc["networks"];
    if (networks.isNull())
    {
        Serial.println("[WiFi] No networks array found in config");
        return;
    }

    // Load all saved networks
    for (JsonObject network : networks)
    {
        SavedWiFiNetwork savedNet;
        savedNet.ssid = network["ssid"].as<String>();
        savedNet.password = network["password"].as<String>();
        savedNet.autoConnect = network["autoConnect"] | true; // Default to true
        savedNet.priority = network["priority"] | 0;          // Default priority 0

        if (!savedNet.ssid.isEmpty())
        {
            savedConfig.savedNetworks.push_back(savedNet);
            Serial.println("[WiFi] Loaded network: " + savedNet.ssid + ", Priority: " + String(savedNet.priority));
        }
    }

    if (!savedConfig.savedNetworks.empty())
    {
        savedConfig.isConfigured = true;

        // Sort networks by priority (highest first)
        std::sort(savedConfig.savedNetworks.begin(), savedConfig.savedNetworks.end(),
                  [](const SavedWiFiNetwork &a, const SavedWiFiNetwork &b)
                  {
                      return a.priority > b.priority;
                  });

        // Auto-connect to highest priority network that has autoConnect enabled
        for (size_t i = 0; i < savedConfig.savedNetworks.size(); i++)
        {
            if (savedConfig.savedNetworks[i].autoConnect)
            {
                Serial.println("[WiFi] Auto-connecting to: " + savedConfig.savedNetworks[i].ssid);
                connectToNetwork(savedConfig.savedNetworks[i].ssid, savedConfig.savedNetworks[i].password);
                break;
            }
        }
    }

    Serial.println("[WiFi] Loaded " + String(savedConfig.savedNetworks.size()) + " networks");
}

void WiFiScreen::toggleWiFi()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        disconnect();
    }
    else if (savedConfig.isConfigured && !savedConfig.savedNetworks.empty())
    {
        // Try to connect to highest priority network with autoConnect enabled
        for (const auto &network : savedConfig.savedNetworks)
        {
            if (network.autoConnect)
            {
                connectToNetwork(network.ssid, network.password);
                break;
            }
        }
    }
    else
    {
        startHotspot();
    }
}

void WiFiScreen::scanNetworks()
{
    if (apModeActive)
        return;

    isScanning = true;
    availableNetworks.clear();

    // Start async scan
    WiFi.scanNetworks(true);

    // Check scan completion in main loop
    // For now, do synchronous scan
    int n = WiFi.scanComplete();
    if (n >= 0)
    {
        for (int i = 0; i < n; i++)
        {
            WiFiNetwork network;
            network.ssid = WiFi.SSID(i);
            network.rssi = WiFi.RSSI(i);
            network.encryptionType = WiFi.encryptionType(i);
            network.isConnected = (network.ssid == WiFi.SSID());
            availableNetworks.push_back(network);
        }
        showNetworkList = true;
        selectedNetworkIndex = 0;
    }

    isScanning = false;
}

void WiFiScreen::connectToNetwork(const String &ssid, const String &password)
{
    Serial.printf("[WiFi] Connecting to %s...\n", ssid.c_str());

    isConnecting = true;
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    // Wait for connection with timeout
    unsigned long startTime = millis();
    const unsigned long timeout = 15000; // 15 seconds timeout

    while (WiFi.status() != WL_CONNECTED && millis() - startTime < timeout)
    {
        delay(500);
        Serial.print(".");
    }

    isConnecting = false;

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.printf("\n[WiFi] Connected successfully! IP: %s\n", WiFi.localIP().toString().c_str());

        // Start non-blocking NTP time synchronization when WiFi connects
        Serial.println("[WiFi] Starting NTP time synchronization...");
        if (startNTPSync())
        {
            Serial.println("[WiFi] NTP sync initiated (non-blocking)");
        }
        else
        {
            Serial.println("[WiFi] Failed to start NTP sync");
        }

        // Find and set active network index
        savedConfig.activeNetworkIndex = -1;
        for (size_t i = 0; i < savedConfig.savedNetworks.size(); i++)
        {
            if (savedConfig.savedNetworks[i].ssid == ssid)
            {
                savedConfig.activeNetworkIndex = i;
                break;
            }
        }

        // Save config if this is a new network or password changed
        bool isNewNetwork = (savedConfig.activeNetworkIndex == -1);
        bool passwordChanged = false;

        if (!isNewNetwork)
        {
            passwordChanged = (savedConfig.savedNetworks[savedConfig.activeNetworkIndex].password != password);
        }

        if (isNewNetwork || passwordChanged)
        {
            saveWiFiConfig(ssid, password);
        }
    }
    else
    {
        Serial.printf("\n[WiFi] Connection failed to %s (timeout or wrong credentials)\n", ssid.c_str());
        WiFi.disconnect();
        savedConfig.activeNetworkIndex = -1;
    }
}

void WiFiScreen::disconnect()
{
    WiFi.disconnect();
    showNetworkList = false;
}

bool WiFiScreen::isWiFiEnabled() const
{
    return WiFi.getMode() != WIFI_OFF;
}

bool WiFiScreen::isConnected() const
{
    return WiFi.status() == WL_CONNECTED;
}

String WiFiScreen::getConnectedSSID() const
{
    return WiFi.SSID();
}

int WiFiScreen::getSignalStrength() const
{
    return WiFi.RSSI();
}

// Drawing helper implementations
void WiFiScreen::drawHeader()
{
    extern EinkDisplayManager display;

    // Draw status bar first
    extern void drawStatusBar();
    drawStatusBar();

    // Draw WiFi screen title with smaller font
    display.m_display.setFont(&FreeMonoBold12pt7b);
    display.drawCenteredText("WiFi Setup", 80, &FreeMonoBold12pt7b);
}

void WiFiScreen::drawStatus()
{
    extern EinkDisplayManager display;

    String status;
    if (apModeActive)
    {
        status = "Setup Mode Active";
        display.drawCenteredText(status.c_str(), 105, &FreeMono9pt7b);
        display.drawCenteredText("Connect to 'E-Reader'", 120, &FreeMono9pt7b);
        display.drawCenteredText("Go to 192.168.4.1", 135, &FreeMono9pt7b);
    }
    else if (isConnecting)
    {
        status = "Connecting...";
        display.drawCenteredText(status.c_str(), 105, &FreeMono9pt7b);
        display.drawCenteredText("Please wait...", 120, &FreeMono9pt7b);
    }
    else if (isConnected())
    {
        String connectedSSID = getConnectedSSID();
        // Truncate long WiFi names for status display
        if (connectedSSID.length() > 20)
        {
            connectedSSID = connectedSSID.substring(0, 17) + "...";
        }
        status = String("Connected: ") + connectedSSID;
        display.drawCenteredText(status.c_str(), 105, &FreeMono9pt7b);
    }
    else
    {
        status = "Disconnected";
        display.drawCenteredText(status.c_str(), 105, &FreeMono9pt7b);
    }
}

void WiFiScreen::drawSavedNetworksMain()
{
    extern EinkDisplayManager display;

    int startY = 150;
    int lineHeight = 16;
    int maxVisible = 8;

    if (savedConfig.savedNetworks.empty())
    {
        display.drawCenteredText("No saved networks", startY, &FreeMono9pt7b);
        display.drawCenteredText("Press DOWN to scan or SELECT for setup", startY + 16, &FreeMono9pt7b);
        return;
    }

    display.m_display.setFont(&FreeMono9pt7b);
    display.m_display.setCursor(10, startY - 5);
    display.m_display.print("Saved Networks:");

    int totalItems = savedConfig.savedNetworks.size() + 1; // +1 for AP mode
    int visibleNetworks = min((int)savedConfig.savedNetworks.size(), maxVisible);

    for (int i = 0; i < visibleNetworks; i++)
    {
        const SavedWiFiNetwork &network = savedConfig.savedNetworks[i];
        int yPos = startY + (i + 1) * lineHeight;

        // Highlight selected item
        if (selectedMainItemIndex == i)
        {
            display.m_display.fillRect(8, yPos - 12, 384, 14, GxEPD_BLACK);
            display.m_display.setTextColor(GxEPD_WHITE);
        }
        else
        {
            display.m_display.setTextColor(GxEPD_BLACK);
        }

        display.m_display.setCursor(12, yPos);

        String networkText = "• " + network.ssid;

        // Truncate long network names to fit display
        if (networkText.length() > 30)
        {
            networkText = networkText.substring(0, 27) + "...";
        }

        if (network.autoConnect)
        {
            networkText += " ✓";
        }

        // Show if currently connected
        if (isConnected() && getConnectedSSID() == network.ssid)
        {
            networkText += " ✓";
        }

        display.m_display.print(networkText);
    }

    // Show AP mode option at the bottom
    int apModeY = startY + (visibleNetworks + 1) * lineHeight;

    // Highlight AP mode if selected
    if (selectedMainItemIndex == savedConfig.savedNetworks.size())
    {
        display.m_display.fillRect(8, apModeY - 12, 384, 14, GxEPD_BLACK);
        display.m_display.setTextColor(GxEPD_WHITE);
    }
    else
    {
        display.m_display.setTextColor(GxEPD_BLACK);
    }

    display.m_display.setCursor(12, apModeY);
    display.m_display.print("• Setup Mode (AP)");

    // Reset text color
    display.m_display.setTextColor(GxEPD_BLACK);
}

void WiFiScreen::drawNetworkList()
{
    extern EinkDisplayManager display;

    int startY = 150;
    int lineHeight = 16;
    int maxVisible = 10;

    for (int i = 0; i < min((int)availableNetworks.size(), maxVisible); i++)
    {
        WiFiNetwork &network = availableNetworks[i];

        // Highlight selected network
        if (i == selectedNetworkIndex)
        {
            display.m_display.fillRect(8, startY + i * lineHeight - 12,
                                       384, 14, GxEPD_BLACK);
            display.m_display.setTextColor(GxEPD_WHITE);
        }
        else
        {
            display.m_display.setTextColor(GxEPD_BLACK);
        }

        display.m_display.setFont(&FreeMono9pt7b);
        display.m_display.setCursor(12, startY + i * lineHeight);

        String networkText = network.ssid;

        // Truncate long network names to fit display
        if (networkText.length() > 25)
        {
            networkText = networkText.substring(0, 17) + "...";
        }

        if (network.encryptionType != WIFI_AUTH_OPEN)
        {
            networkText += " 🔒";
        }
        networkText += " (" + String(network.rssi) + ")";

        display.m_display.print(networkText);

        // Reset text color
        display.m_display.setTextColor(GxEPD_BLACK);
    }
}

void WiFiScreen::drawScanningIndicator()
{
    extern EinkDisplayManager display;

    display.drawCenteredText("Scanning...", 150, &FreeMono9pt7b);

    // Simple animation dots
    static int dots = 0;
    dots = (dots + 1) % 4;

    String animation = "";
    for (int i = 0; i < dots; i++)
    {
        animation += ".";
    }

    display.drawCenteredText(animation.c_str(), 165, &FreeMono9pt7b);
}

void WiFiScreen::drawConnectionStatus()
{
    extern EinkDisplayManager display;

    if (WiFi.status() == WL_CONNECTED)
    {
        display.m_display.setFont(&FreeMono9pt7b);
        display.m_display.setTextColor(GxEPD_BLACK);
        display.m_display.setCursor(10, 320);

        String connectedSSID = WiFi.SSID();
        // Truncate long WiFi names for connection status
        if (connectedSSID.length() > 25)
        {
            connectedSSID = connectedSSID.substring(0, 17) + "...";
        }
        display.m_display.print("Connected: " + connectedSSID);

        display.m_display.setCursor(10, 335);
        display.m_display.print("IP: " + WiFi.localIP().toString());
    }
    else if (WiFi.status() == WL_CONNECT_FAILED)
    {
        display.m_display.setFont(&FreeMono9pt7b);
        display.m_display.setTextColor(GxEPD_BLACK);
        display.m_display.setCursor(10, 320);
        display.m_display.print("Connection failed!");
    }
    else if (WiFi.status() == WL_DISCONNECTED)
    {
        display.m_display.setFont(&FreeMono9pt7b);
        display.m_display.setTextColor(GxEPD_BLACK);
        display.m_display.setCursor(10, 320);
        display.m_display.print("Disconnected");
    }
}

void WiFiScreen::drawSavedNetworksList()
{
    extern EinkDisplayManager display;

    int startY = 180;
    int lineHeight = 25;
    int maxVisible = 6;

    display.drawCenteredText("Saved Networks", 150, &FreeMonoBold12pt7b);

    for (int i = 0; i < min((int)savedConfig.savedNetworks.size(), maxVisible); i++)
    {
        const SavedWiFiNetwork &network = savedConfig.savedNetworks[i];

        // Highlight selected network
        if (i == selectedSavedNetworkIndex)
        {
            display.m_display.fillRect(5, startY + i * lineHeight - 18,
                                       display.m_display.width() - 10, lineHeight, GxEPD_BLACK);
            display.m_display.setTextColor(GxEPD_WHITE);
        }
        else
        {
            display.m_display.setTextColor(GxEPD_BLACK);
        }

        display.m_display.setFont(&FreeMono9pt7b);
        display.m_display.setCursor(10, startY + i * lineHeight);

        // Show network info: SSID, priority, autoConnect status
        String networkText = network.ssid;

        // Truncate long network names to fit display
        if (networkText.length() > 20)
        {
            networkText = networkText.substring(0, 17) + "...";
        }

        networkText += " [P:" + String(network.priority) + "]";
        if (network.autoConnect)
        {
            networkText += " ✓";
        }
        else
        {
            networkText += " ✗";
        }

        // Show if currently connected
        if (isConnected() && getConnectedSSID() == network.ssid)
        {
            networkText += " ✓";
        }

        display.m_display.print(networkText);

        // Reset text color
        display.m_display.setTextColor(GxEPD_BLACK);
    }

    // Show instructions
    display.m_display.setFont(&FreeMono9pt7b);
    display.drawCenteredText("SELECT: Connect  UP/DOWN: Navigate", 350, &FreeMono9pt7b);
}

// Update function to be called in main loop
void WiFiScreen::update()
{
    if (apModeActive && webServerRunning)
    {
        dnsServer.processNextRequest();
        webServer.handleClient();
    }
}

// Helper functions
void WiFiScreen::updateNetworkList()
{
    // Implementation for updating network list
}

String WiFiScreen::encryptionTypeToString(wifi_auth_mode_t type)
{
    switch (type)
    {
    case WIFI_AUTH_OPEN:
        return "Open";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE:
        return "WPA2-ENT";
    default:
        return "Unknown";
    }
}

int WiFiScreen::getNetworkRSSI(const String &ssid)
{
    for (const auto &network : availableNetworks)
    {
        if (network.ssid == ssid)
        {
            return network.rssi;
        }
    }
    return -100; // Not found
}

// Settings management implementations
void WiFiScreen::showSavedNetworks()
{
    showSavedNetworksList = true;
    showNetworkList = false;
    selectedSavedNetworkIndex = 0;
    Serial.println("[WiFi] Showing saved networks list");
}

void WiFiScreen::selectSavedNetwork(int index)
{
    if (index >= 0 && index < savedConfig.savedNetworks.size())
    {
        const SavedWiFiNetwork &network = savedConfig.savedNetworks[index];
        Serial.println("[WiFi] Connecting to saved network: " + network.ssid);
        connectToNetwork(network.ssid, network.password);
        showSavedNetworksList = false;
    }
}

void WiFiScreen::deleteSavedNetwork(int index)
{
    if (index >= 0 && index < savedConfig.savedNetworks.size())
    {
        String deletedSSID = savedConfig.savedNetworks[index].ssid;
        savedConfig.savedNetworks.erase(savedConfig.savedNetworks.begin() + index);

        // Update active network index if needed
        if (savedConfig.activeNetworkIndex == index)
        {
            savedConfig.activeNetworkIndex = -1;
        }
        else if (savedConfig.activeNetworkIndex > index)
        {
            savedConfig.activeNetworkIndex--;
        }

        // Adjust selected index if needed
        if (selectedSavedNetworkIndex >= savedConfig.savedNetworks.size() && !savedConfig.savedNetworks.empty())
        {
            selectedSavedNetworkIndex = savedConfig.savedNetworks.size() - 1;
        }

        // Save updated configuration
        if (isSDCardPowered())
        {
            JsonDocument doc;
            JsonArray networks = doc["networks"].to<JsonArray>();

            for (const auto &network : savedConfig.savedNetworks)
            {
                JsonObject netObj = networks.add<JsonObject>();
                netObj["ssid"] = network.ssid;
                netObj["password"] = network.password;
                netObj["autoConnect"] = network.autoConnect;
                netObj["priority"] = network.priority;
            }

            String configJson;
            serializeJson(doc, configJson);

            // Ensure config directory exists
            if (!SD.exists("/config"))
            {
                SD.mkdir("/config");
            }

            File configFile = SD.open("/config/wifi_networks.json", FILE_WRITE);
            if (configFile)
            {
                configFile.print(configJson);
                configFile.close();
                Serial.println("[WiFi] Deleted network: " + deletedSSID);
            }
        }

        // Update configuration status
        savedConfig.isConfigured = !savedConfig.savedNetworks.empty();
    }
}

void WiFiScreen::toggleAutoConnect(int index)
{
    if (index >= 0 && index < savedConfig.savedNetworks.size())
    {
        savedConfig.savedNetworks[index].autoConnect = !savedConfig.savedNetworks[index].autoConnect;

        // Save updated configuration
        if (isSDCardPowered())
        {
            JsonDocument doc;
            JsonArray networks = doc["networks"].to<JsonArray>();

            for (const auto &network : savedConfig.savedNetworks)
            {
                JsonObject netObj = networks.add<JsonObject>();
                netObj["ssid"] = network.ssid;
                netObj["password"] = network.password;
                netObj["autoConnect"] = network.autoConnect;
                netObj["priority"] = network.priority;
            }

            String configJson;
            serializeJson(doc, configJson);

            // Ensure config directory exists
            if (!SD.exists("/config"))
            {
                SD.mkdir("/config");
            }

            File configFile = SD.open("/config/wifi_networks.json", FILE_WRITE);
            if (configFile)
            {
                configFile.print(configJson);
                configFile.close();
                Serial.println("[WiFi] Toggled autoConnect for: " + savedConfig.savedNetworks[index].ssid);
            }
        }
    }
}

void WiFiScreen::changePriority(int index, bool increase)
{
    if (index >= 0 && index < savedConfig.savedNetworks.size())
    {
        if (increase)
        {
            savedConfig.savedNetworks[index].priority++;
        }
        else
        {
            savedConfig.savedNetworks[index].priority = max(0, savedConfig.savedNetworks[index].priority - 1);
        }

        // Re-sort networks by priority
        std::sort(savedConfig.savedNetworks.begin(), savedConfig.savedNetworks.end(),
                  [](const SavedWiFiNetwork &a, const SavedWiFiNetwork &b)
                  {
                      return a.priority > b.priority;
                  });

        // Save updated configuration
        if (isSDCardPowered())
        {
            JsonDocument doc;
            JsonArray networks = doc["networks"].to<JsonArray>();

            for (const auto &network : savedConfig.savedNetworks)
            {
                JsonObject netObj = networks.add<JsonObject>();
                netObj["ssid"] = network.ssid;
                netObj["password"] = network.password;
                netObj["autoConnect"] = network.autoConnect;
                netObj["priority"] = network.priority;
            }

            String configJson;
            serializeJson(doc, configJson);

            // Ensure config directory exists
            if (!SD.exists("/config"))
            {
                SD.mkdir("/config");
            }

            File configFile = SD.open("/config/wifi_networks.json", FILE_WRITE);
            if (configFile)
            {
                configFile.print(configJson);
                configFile.close();
                Serial.println("[WiFi] Updated priority for: " + savedConfig.savedNetworks[index].ssid);
            }
        }
    }
}