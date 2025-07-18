#include "storage.h"
#include "pins.h"
#include <Arduino.h>

// Global variables
static SDCardStatus sd_status = SD_NOT_INITIALIZED;
static String lastError = "";
static bool sd_powered = false;
static SPIClass sdSPI(HSPI); // Use HSPI for SD card

/**
 * Initialize SD card storage system
 */
bool initStorage()
{
    Serial.println("Initializing SD Card storage...");

    // Configure SD card power control pin
    pinMode(WAKE_SDIO, OUTPUT);

    // Power on SD card
    if (!powerOnSDCard())
    {
        lastError = "Failed to power on SD card";
        sd_status = SD_POWER_OFF;
        return false;
    }

    delay(100); // Allow power to stabilize

    // Initialize SPI for SD card with custom pins
    sdSPI.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);

    // Try to initialize SD card
    if (!SD.begin(SD_CS, sdSPI))
    {
        lastError = "SD card initialization failed";
        sd_status = SD_CARD_NOT_FOUND;
        Serial.println("SD Card initialization failed!");

        // Try different SPI speeds
        if (!SD.begin(SD_CS, sdSPI, 4000000))
        { // Try 4MHz
            if (!SD.begin(SD_CS, sdSPI, 1000000))
            { // Try 1MHz
                Serial.println("SD Card not found or corrupted");
                powerOffSDCard();
                return false;
            }
        }
    }

    sd_status = SD_READY;
    Serial.println("SD Card initialized successfully!");

    // Print initial info
    printStorageInfo();

    // Create default directories
    createDirectory("/logs");
    createDirectory("/books");
    createDirectory("/manga");
    createDirectory("/config");
    createDirectory("/temp");

    return true;
}

/**
 * Deinitialize SD card storage
 */
void deinitStorage()
{
    Serial.println("Deinitializing SD Card storage...");

    // End SD operations
    SD.end();

    // Power off SD card
    powerOffSDCard();

    sd_status = SD_NOT_INITIALIZED;
    Serial.println("SD Card deinitialized");
}

/**
 * Power on SD card via MOSFET control
 */
bool powerOnSDCard()
{
    Serial.print("Powering on SD card... ");

    // Set WAKE_SDIO high to enable power (Q10 MOSFET)
    digitalWrite(WAKE_SDIO, HIGH);
    sd_powered = true;

    delay(50); // Allow power to stabilize

    Serial.println("Done");
    return true;
}

/**
 * Power off SD card via MOSFET control
 */
void powerOffSDCard()
{
    Serial.println("[STORAGE] powerOffSDCard() - Shutting down SD card");

    // Set WAKE_SDIO low to disable power
    digitalWrite(WAKE_SDIO, LOW);
    sd_powered = false;

    Serial.println("[STORAGE] SD card powered off successfully");
}

/**
 * Get current SD card status
 */
SDCardStatus getSDCardStatus()
{
    return sd_status;
}

/**
 * Get SD card information
 */
SDCardInfo getSDCardInfo()
{
    SDCardInfo info;
    info.isValid = false;

    if (sd_status != SD_READY)
    {
        return info;
    }

    info.cardSize = SD.cardSize();
    info.usedBytes = SD.usedBytes();
    info.freeBytes = info.cardSize - info.usedBytes;
    info.cardType = SD.cardType();

    switch (info.cardType)
    {
    case CARD_MMC:
        info.cardTypeString = "MMC";
        break;
    case CARD_SD:
        info.cardTypeString = "SDSC";
        break;
    case CARD_SDHC:
        info.cardTypeString = "SDHC";
        break;
    default:
        info.cardTypeString = "UNKNOWN";
        break;
    }

    info.isValid = true;
    return info;
}

/**
 * Create a new file
 */
bool createFile(const String &filename)
{
    if (sd_status != SD_READY)
    {
        lastError = "SD card not ready";
        return false;
    }

    File file = SD.open(filename, FILE_WRITE);
    if (!file)
    {
        lastError = "Failed to create file: " + filename;
        return false;
    }

    file.close();
    return true;
}

/**
 * Delete a file
 */
bool deleteFile(const String &filename)
{
    if (sd_status != SD_READY)
    {
        lastError = "SD card not ready";
        return false;
    }

    if (!SD.remove(filename))
    {
        lastError = "Failed to delete file: " + filename;
        return false;
    }

    return true;
}

/**
 * Check if file exists
 */
bool fileExists(const String &filename)
{
    if (sd_status != SD_READY)
    {
        return false;
    }

    return SD.exists(filename);
}

/**
 * Get file size
 */
size_t getFileSize(const String &filename)
{
    if (sd_status != SD_READY)
    {
        return 0;
    }

    File file = SD.open(filename, FILE_READ);
    if (!file)
    {
        return 0;
    }

    size_t size = file.size();
    file.close();
    return size;
}

/**
 * Create directory
 */
bool createDirectory(const String &dirPath)
{
    if (sd_status != SD_READY)
    {
        lastError = "SD card not ready";
        return false;
    }

    if (SD.mkdir(dirPath))
    {
        Serial.println("Directory created: " + dirPath);
        return true;
    }
    else
    {
        // Directory might already exist, check
        if (directoryExists(dirPath))
        {
            return true; // Already exists, that's fine
        }
        lastError = "Failed to create directory: " + dirPath;
        return false;
    }
}

/**
 * Delete directory
 */
bool deleteDirectory(const String &dirPath)
{
    if (sd_status != SD_READY)
    {
        lastError = "SD card not ready";
        return false;
    }

    if (!SD.rmdir(dirPath))
    {
        lastError = "Failed to delete directory: " + dirPath;
        return false;
    }

    return true;
}

/**
 * Check if directory exists
 */
bool directoryExists(const String &dirPath)
{
    if (sd_status != SD_READY)
    {
        return false;
    }

    File dir = SD.open(dirPath);
    if (!dir)
    {
        return false;
    }

    bool isDir = dir.isDirectory();
    dir.close();
    return isDir;
}

/**
 * List directory contents
 */
void listDirectory(const String &dirPath, bool recursive)
{
    if (sd_status != SD_READY)
    {
        Serial.println("SD card not ready");
        return;
    }

    File dir = SD.open(dirPath);
    if (!dir)
    {
        Serial.println("Failed to open directory: " + dirPath);
        return;
    }

    if (!dir.isDirectory())
    {
        Serial.println("Not a directory: " + dirPath);
        dir.close();
        return;
    }

    Serial.println("Directory listing: " + dirPath);
    Serial.println("==================");

    File file = dir.openNextFile();
    while (file)
    {
        if (file.isDirectory())
        {
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if (recursive)
            {
                String subDir = String(dirPath) + "/" + String(file.name());
                listDirectory(subDir, true);
            }
        }
        else
        {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print(" (");
            Serial.print(file.size());
            Serial.println(" bytes)");
        }
        file = dir.openNextFile();
    }

    dir.close();
}

/**
 * Read entire file as string
 */
String readFile(const String &filename)
{
    String content;
    readFile(filename, content);
    return content;
}

/**
 * Read file into provided string reference
 */
bool readFile(const String &filename, String &content)
{
    if (sd_status != SD_READY)
    {
        lastError = "SD card not ready";
        return false;
    }

    File file = SD.open(filename, FILE_READ);
    if (!file)
    {
        lastError = "Failed to open file for reading: " + filename;
        return false;
    }

    content = "";
    while (file.available())
    {
        content += (char)file.read();
    }

    file.close();
    return true;
}

/**
 * Read file as bytes
 */
bool readFileBytes(const String &filename, uint8_t *buffer, size_t bufferSize, size_t &bytesRead)
{
    if (sd_status != SD_READY)
    {
        lastError = "SD card not ready";
        return false;
    }

    File file = SD.open(filename, FILE_READ);
    if (!file)
    {
        lastError = "Failed to open file for reading: " + filename;
        return false;
    }

    bytesRead = file.read(buffer, bufferSize);
    file.close();

    return true;
}

/**
 * Write string to file
 */
bool writeFile(const String &filename, const String &content, bool append)
{
    if (sd_status != SD_READY)
    {
        lastError = "SD card not ready";
        return false;
    }

    const char *mode = append ? FILE_APPEND : FILE_WRITE;
    File file = SD.open(filename, mode);

    if (!file)
    {
        lastError = "Failed to open file for writing: " + filename;
        return false;
    }

    size_t bytesWritten = file.print(content);
    file.close();

    if (bytesWritten != content.length())
    {
        lastError = "Write operation incomplete";
        return false;
    }

    return true;
}

/**
 * Write bytes to file
 */
bool writeFileBytes(const String &filename, const uint8_t *data, size_t dataSize, bool append)
{
    if (sd_status != SD_READY)
    {
        lastError = "SD card not ready";
        return false;
    }

    const char *mode = append ? FILE_APPEND : FILE_WRITE;
    File file = SD.open(filename, mode);

    if (!file)
    {
        lastError = "Failed to open file for writing: " + filename;
        return false;
    }

    size_t bytesWritten = file.write(data, dataSize);
    file.close();

    if (bytesWritten != dataSize)
    {
        lastError = "Write operation incomplete";
        return false;
    }

    return true;
}

/**
 * Append content to file
 */
bool appendToFile(const String &filename, const String &content)
{
    return writeFile(filename, content, true);
}

/**
 * Write JSON string to file
 */
bool writeJSON(const String &filename, const String &jsonString)
{
    return writeFile(filename, jsonString, false);
}

/**
 * Read JSON string from file
 */
String readJSON(const String &filename)
{
    return readFile(filename);
}

/**
 * Log sensor data in JSON format
 */
bool logSensorData(const String &timestamp, float temperature, float humidity, const String &filename)
{
    if (sd_status != SD_READY)
    {
        return false;
    }

    String jsonEntry = "{\n";
    jsonEntry += "  \"timestamp\": \"" + timestamp + "\",\n";
    jsonEntry += "  \"temperature\": " + String(temperature, 2) + ",\n";
    jsonEntry += "  \"humidity\": " + String(humidity, 2) + "\n";
    jsonEntry += "},\n";

    return appendToFile(filename, jsonEntry);
}

/**
 * Log system events
 */
bool logSystemEvent(const String &event, const String &details, const String &filename)
{
    if (sd_status != SD_READY)
    {
        return false;
    }

    String logEntry = "[" + String(millis()) + "] " + event;
    if (details.length() > 0)
    {
        logEntry += " - " + details;
    }
    logEntry += "\n";

    return appendToFile(filename, logEntry);
}

/**
 * Format SD card (WARNING: Erases all data!)
 */
void formatSDCard()
{
    Serial.println("WARNING: Formatting SD card will erase all data!");
    Serial.println("This operation is not implemented for safety.");
    // Implementation would go here if needed
}

/**
 * Print storage information
 */
void printStorageInfo()
{
    if (sd_status != SD_READY)
    {
        Serial.println("SD Card not ready");
        return;
    }

    SDCardInfo info = getSDCardInfo();
    if (!info.isValid)
    {
        Serial.println("Failed to get SD card info");
        return;
    }

    Serial.println("\n=== SD Card Information ===");
    Serial.print("Card Type: ");
    Serial.println(info.cardTypeString);

    Serial.print("Total Size: ");
    Serial.print(info.cardSize / (1024 * 1024));
    Serial.println(" MB");

    Serial.print("Used Space: ");
    Serial.print(info.usedBytes / (1024 * 1024));
    Serial.println(" MB");

    Serial.print("Free Space: ");
    Serial.print(info.freeBytes / (1024 * 1024));
    Serial.println(" MB");

    Serial.print("Usage: ");
    Serial.print((info.usedBytes * 100) / info.cardSize);
    Serial.println("%");

    Serial.print("Power Status: ");
    Serial.println(sd_powered ? "ON" : "OFF");

    Serial.println("===========================\n");
}

/**
 * Print directory tree
 */
void printDirectoryTree(const String &startPath)
{
    Serial.println("SD Card Directory Tree:");
    Serial.println("======================");
    listDirectory(startPath, true);
    Serial.println("======================");
}

/**
 * Enable SD card wakeup
 */
void enableSDCardWakeup()
{
    powerOnSDCard();
}

/**
 * Disable SD card wakeup
 */
void disableSDCardWakeup()
{
    Serial.println("[STORAGE] disableSDCardWakeup() - Disabling SD card wakeup capability");
    powerOffSDCard();
}

/**
 * Check if SD card is powered
 */
bool isSDCardPowered()
{
    return sd_powered;
}

/**
 * Get last error message
 */
String getLastError()
{
    return lastError;
}

/**
 * Clear error messages
 */
void clearErrors()
{
    lastError = "";
}

/**
 * Run SD card diagnostics
 */
bool runSDCardDiagnostics()
{
    Serial.println("\n=== SD Card Diagnostics ===");

    // Check power status
    Serial.print("Power Status: ");
    Serial.println(sd_powered ? "ON" : "OFF");

    // Check initialization status
    Serial.print("Initialization: ");
    Serial.println(sd_status == SD_READY ? "OK" : "FAILED");

    if (sd_status != SD_READY)
    {
        Serial.println("Last Error: " + lastError);
        Serial.println("============================");
        return false;
    }

    // Test write operation
    Serial.print("Write Test: ");
    if (writeFile("/test_write.txt", "SD card test data"))
    {
        Serial.println("PASS");
    }
    else
    {
        Serial.println("FAIL - " + lastError);
        return false;
    }

    // Test read operation
    Serial.print("Read Test: ");
    String testContent = readFile("/test_write.txt");
    if (testContent == "SD card test data")
    {
        Serial.println("PASS");
    }
    else
    {
        Serial.println("FAIL - Content mismatch");
        return false;
    }

    // Clean up test file
    deleteFile("/test_write.txt");

    // Print card info
    printStorageInfo();

    Serial.println("All diagnostics PASSED");
    Serial.println("============================");

    return true;
}