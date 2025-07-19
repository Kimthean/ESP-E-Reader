#ifndef EPUB_PARSER_H
#define EPUB_PARSER_H

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <vector>
#include <tinyxml2.h>

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
    void cleanup();

    // Getters
    EpubMetadata getMetadata() const { return m_metadata; }
    std::vector<EpubChapter> getChapters() const { return m_chapters; }
    String getFullText() const;
    int getChapterCount() const { return m_chapters.size(); }
    EpubChapter getChapter(int index) const;

    // Utility functions
    bool isValidEpub() const { return m_isValid; }
    String getLastError() const { return m_lastError; }

private:
    // Internal structures
    EpubMetadata m_metadata;
    std::vector<EpubChapter> m_chapters;
    std::vector<EpubManifestItem> m_manifest;
    std::vector<EpubSpineItem> m_spine;
    String m_rootPath;
    bool m_isValid;
    String m_lastError;

    // ZIP file handling (simplified for ESP32)
    bool extractFile(const String &zipPath, const String &filename, String &content);
    bool findFileInZip(const String &zipPath, const String &filename);

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
    bool createFallbackContent(const String &filename, String &content);

    // Utility functions
    void setError(const String &error);
    String joinPath(const String &base, const String &relative);
    bool isHtmlFile(const String &filename);
};

#endif // EPUB_PARSER_H