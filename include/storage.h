#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <FS.h>

// SD Card status enumeration
enum SDCardStatus
{
    SD_NOT_INITIALIZED,
    SD_READY,
    SD_CARD_NOT_FOUND,
    SD_MOUNT_FAILED,
    SD_POWER_OFF,
    SD_ERROR
};

// File operation results
enum FileResult
{
    FILE_SUCCESS,
    FILE_NOT_FOUND,
    FILE_WRITE_ERROR,
    FILE_READ_ERROR,
    FILE_CREATE_ERROR,
    FILE_DELETE_ERROR,
    FILE_PERMISSION_ERROR
};

// SD Card information structure
struct SDCardInfo
{
    uint64_t cardSize;
    uint64_t usedBytes;
    uint64_t freeBytes;
    uint8_t cardType;
    String cardTypeString;
    bool isValid;
};

// Storage management functions
bool initStorage();
void deinitStorage();
bool powerOnSDCard();
void powerOffSDCard();
SDCardStatus getSDCardStatus();
SDCardInfo getSDCardInfo();

// File operations
bool createFile(const String &filename);
bool deleteFile(const String &filename);
bool fileExists(const String &filename);
size_t getFileSize(const String &filename);

// Directory operations
bool createDirectory(const String &dirPath);
bool deleteDirectory(const String &dirPath);
bool directoryExists(const String &dirPath);
void listDirectory(const String &dirPath, bool recursive = false);

// Read operations
String readFile(const String &filename);
bool readFile(const String &filename, String &content);
bool readFileBytes(const String &filename, uint8_t *buffer, size_t bufferSize, size_t &bytesRead);

// Write operations
bool writeFile(const String &filename, const String &content, bool append = false);
bool writeFileBytes(const String &filename, const uint8_t *data, size_t dataSize, bool append = false);
bool appendToFile(const String &filename, const String &content);

// JSON operations (for structured data)
bool writeJSON(const String &filename, const String &jsonString);
String readJSON(const String &filename);

// Data logging functions
bool logSensorData(const String &timestamp, float temperature, float humidity, const String &filename = "/sensor_log.json");
bool logSystemEvent(const String &event, const String &details, const String &filename = "/system_log.txt");

// File system utilities
void formatSDCard();
bool repairFileSystem();
void printStorageInfo();
void printDirectoryTree(const String &startPath = "/");

// Power management integration
void enableSDCardWakeup();
void disableSDCardWakeup();
bool isSDCardPowered();

// Error handling and diagnostics
String getLastError();
void clearErrors();
bool runSDCardDiagnostics();

#endif // STORAGE_H