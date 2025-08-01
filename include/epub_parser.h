#ifndef EPUB_PARSER_H
#define EPUB_PARSER_H

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <vector>
#include <functional>
#include <tinyxml2.h>

// Progress callback function type
// Parameters: progress (0.0-1.0), message, shouldAbort (returns true if should abort)
typedef std::function<bool(float, const String &)> ProgressCallback;

// ePub chapter structure
struct EpubChapter
{
    String id;
    String href;
    String title;
    String content;
    int order;
};

// ePub metadata structure
struct EpubMetadata
{
    String title;
    String author;
    String publisher;
    String language;
    String identifier;
    String description;
    String coverImagePath;
};

// ePub manifest item
struct EpubManifestItem
{
    String id;
    String href;
    String mediaType;
};

// ePub spine item
struct EpubSpineItem
{
    String idref;
    bool linear;
};

class EpubParser
{
public:
    EpubParser();
    ~EpubParser();

    // Main parsing functions
    bool parseEpub(const String &filepath);
    bool parseEpubForStreaming(const String &filepath);                                                                   // Lightweight parsing for streaming
    bool extractEpubToSD(const String &epubPath, const String &extractPath, ProgressCallback progressCallback = nullptr); // Extract EPUB to SD card
    bool parseExtractedEpub(const String &extractedPath);                                                                 // Parse from extracted folder
    void cleanup();

    // Getters
    EpubMetadata getMetadata() const { return m_metadata; }
    std::vector<EpubChapter> getChapters() const { return m_chapters; }
    String getFullText() const;
    int getChapterCount() const { return m_chapters.size(); }
    EpubChapter getChapter(int index) const;
    String getChapterContent(int chapterIndex);

    // Streaming functions
    bool extractChapterToFile(int chapterIndex, const String &outputPath);
    std::vector<EpubSpineItem> getSpineItems() const { return m_spine; }
    std::vector<EpubManifestItem> getManifestItems() const { return m_manifest; }
    String getChapterHref(int chapterIndex) const;

    // Utility functions
    bool isValidEpub() const { return m_isValid; }
    String getLastError() const { return m_lastError; }
    static String getMemoryInfo();
    static String getRecommendedStrategy(size_t fileSize);

private:
    // Internal structures
    EpubMetadata m_metadata;
    std::vector<EpubChapter> m_chapters;
    std::vector<EpubManifestItem> m_manifest;
    std::vector<EpubSpineItem> m_spine;
    String m_rootPath;
    String m_epubFilePath;
    bool m_isValid;
    String m_lastError;

    // ZIP file handling (optimized for large files)
    bool extractFile(const String &zipPath, const String &filename, String &content);
    bool extractFileStreaming(const String &zipPath, const String &filename, String &content);
    bool extractFileToSD(const String &zipPath, const String &filename, const String &outputPath);
    bool extractLargeFileToSD(const String &zipPath, const String &filename, const String &outputPath);
    bool extractAllFilesToSD(const String &zipPath, const String &extractPath, ProgressCallback progressCallback = nullptr);
    bool extractLargeEpubToSD(const String &zipPath, const String &extractPath, ProgressCallback progressCallback = nullptr);
    bool readFileFromSD(const String &filePath, String &content);
    bool findFileInZip(const String &zipPath, const String &filename);

    // Streaming ZIP extraction functions (minimal memory usage)
    bool extractStreamingZipToSD(const String &zipPath, const String &extractPath, ProgressCallback progressCallback = nullptr);
    bool findZipCentralDirectory(File &zipFile, size_t zipSize);
    bool extractZipEntriesStreaming(File &zipFile, const String &extractPath, uint8_t *buffer, size_t bufferSize, int &filesExtracted, ProgressCallback progressCallback = nullptr);
    bool extractFileStreaming(File &zipFile, const String &filename, const String &extractPath, size_t dataOffset, size_t fileSize, uint8_t *buffer, size_t bufferSize);
    bool extractCompressedFileStreaming(File &zipFile, const String &filename, const String &extractPath, size_t dataOffset, size_t compressedSize, size_t uncompressedSize, uint8_t *buffer, size_t bufferSize);

    // XML parsing functions
    bool parseContainer(const String &containerXml);
    bool parseContentOpf(const String &contentOpf);
    bool parseMetadata(tinyxml2::XMLElement *metadataElement);
    bool parseManifest(tinyxml2::XMLElement *manifestElement);
    bool parseSpine(tinyxml2::XMLElement *spineElement);

    // Content processing
    bool loadChapterContent();
    String processHtmlContent(const String &htmlContent);
    String stripHtmlTags(const String &html);
    String decodeHtmlEntities(const String &text);

    // Helper functions
    bool createFallbackExtraction(const String &extractPath);
    bool createFallbackContent(const String &filename, String &content);
    String getChapterPath(const String &href);

    // Utility functions
    void setError(const String &error);
    String joinPath(const String &base, const String &relative);
    bool isHtmlFile(const String &filename);
};

#endif // EPUB_PARSER_H