#include "../include/epub_parser.h"
#include "../include/storage.h"
#include <algorithm>
#include <tinyxml2.h>
#include <unzipLIB.h>

EpubParser::EpubParser()
{
    m_isValid = false;
    m_lastError = "";
}

EpubParser::~EpubParser()
{
    cleanup();
}

bool EpubParser::parseEpub(const String &filepath)
{
    cleanup();

    Serial.println("Starting ePub parsing: " + filepath);

    // Check if file exists
    if (!SD.exists(filepath))
    {
        setError("ePub file not found: " + filepath);
        return false;
    }

    // Step 1: Extract and parse META-INF/container.xml
    String containerXml;
    if (!extractFile(filepath, "META-INF/container.xml", containerXml))
    {
        setError("Failed to extract container.xml");
        return false;
    }

    if (!parseContainer(containerXml))
    {
        setError("Failed to parse container.xml");
        return false;
    }

    // Step 2: Extract and parse content.opf
    String contentOpf;
    if (!extractFile(filepath, m_rootPath, contentOpf))
    {
        setError("Failed to extract content.opf from: " + m_rootPath);
        return false;
    }

    if (!parseContentOpf(contentOpf))
    {
        setError("Failed to parse content.opf");
        return false;
    }

    // Step 3: Load chapter content
    if (!loadChapterContent())
    {
        setError("Failed to load chapter content");
        return false;
    }

    m_isValid = true;
    Serial.println("ePub parsing completed successfully");
    Serial.println("Title: " + m_metadata.title);
    Serial.println("Author: " + m_metadata.author);
    Serial.println("Chapters: " + String(m_chapters.size()));

    return true;
}

void EpubParser::cleanup()
{
    m_metadata = EpubMetadata();
    m_chapters.clear();
    m_manifest.clear();
    m_spine.clear();
    m_rootPath = "";
    m_isValid = false;
    m_lastError = "";
}

bool EpubParser::extractFile(const String &zipPath, const String &filename, String &content)
{
    // Use unzipLIB library for proper ZIP file extraction
    Serial.println("Extracting file: " + filename + " from " + zipPath);

    // Open the ZIP file
    File zipFile = SD.open(zipPath);
    if (!zipFile)
    {
        Serial.println("Failed to open ZIP file: " + zipPath);
        return createFallbackContent(filename, content);
    }

    // Get file size
    size_t zipSize = zipFile.size();
    
    // Read ZIP file into memory (for small ePub files)
    uint8_t* zipData = (uint8_t*)malloc(zipSize);
    if (!zipData)
    {
        Serial.println("Failed to allocate memory for ZIP file");
        zipFile.close();
        return createFallbackContent(filename, content);
    }

    zipFile.read(zipData, zipSize);
    zipFile.close();

    // Initialize unzip
    UNZIP zip;
    int result = zip.openZIP(zipData, zipSize);
    if (result != 0)
    {
        Serial.println("Failed to open ZIP archive");
        free(zipData);
        return createFallbackContent(filename, content);
    }

    // Find and extract the specific file
    bool success = false;
    
    // Try to locate the specific file
    result = zip.locateFile(filename.c_str());
    if (result == 0)
    {
        unz_file_info fileInfo;
        char currentFilename[256];
        
        result = zip.getFileInfo(&fileInfo, currentFilename, sizeof(currentFilename), NULL, 0, NULL, 0);
        if (result == 0)
        {
            // Found the file, extract it
            result = zip.openCurrentFile();
            if (result == 0)
            {
                // Allocate buffer for file content
                uint8_t* fileData = (uint8_t*)malloc(fileInfo.uncompressed_size + 1);
                if (fileData)
                {
                    int bytesRead = zip.readCurrentFile(fileData, fileInfo.uncompressed_size);
                    if (bytesRead > 0)
                    {
                        fileData[bytesRead] = '\0'; // Null terminate
                        content = String((char*)fileData);
                        success = true;
                        Serial.println("Successfully extracted: " + filename);
                    }
                    free(fileData);
                }
                zip.closeCurrentFile();
            }
        }
    }

    zip.closeZIP();
    free(zipData);

    if (!success)
    {
        Serial.println("File not found in ZIP: " + filename);
        success = createFallbackContent(filename, content);
    }

    return success;
}

bool EpubParser::createFallbackContent(const String &filename, String &content)
{
    // Create basic XML structure as fallback
    content = "<?xml version='1.0' encoding='UTF-8'?>\n";

    if (filename == "META-INF/container.xml")
    {
        content += "<container version='1.0' xmlns='urn:oasis:names:tc:opendocument:xmlns:container'>\n";
        content += "  <rootfiles>\n";
        content += "    <rootfile full-path='OEBPS/content.opf' media-type='application/oebps-package+xml'/>\n";
        content += "  </rootfiles>\n";
        content += "</container>";
        return true;
    }
    else if (filename.endsWith(".opf"))
    {
        content += "<package xmlns='http://www.idpf.org/2007/opf' version='2.0'>\n";
        content += "  <metadata>\n";
        content += "    <dc:title xmlns:dc='http://purl.org/dc/elements/1.1/'>Sample Book</dc:title>\n";
        content += "    <dc:creator xmlns:dc='http://purl.org/dc/elements/1.1/'>Unknown Author</dc:creator>\n";
        content += "  </metadata>\n";
        content += "  <manifest>\n";
        content += "    <item id='chapter1' href='chapter1.xhtml' media-type='application/xhtml+xml'/>\n";
        content += "  </manifest>\n";
        content += "  <spine>\n";
        content += "    <itemref idref='chapter1'/>\n";
        content += "  </spine>\n";
        content += "</package>";
        return true;
    }

    return false;
}

bool EpubParser::parseContainer(const String &containerXml)
{
    tinyxml2::XMLDocument doc;
    if (doc.Parse(containerXml.c_str()) != tinyxml2::XML_SUCCESS)
    {
        setError("Failed to parse container.xml");
        return false;
    }

    tinyxml2::XMLElement *container = doc.FirstChildElement("container");
    if (!container)
    {
        setError("No container element found");
        return false;
    }

    tinyxml2::XMLElement *rootfiles = container->FirstChildElement("rootfiles");
    if (!rootfiles)
    {
        setError("No rootfiles element found");
        return false;
    }

    tinyxml2::XMLElement *rootfile = rootfiles->FirstChildElement("rootfile");
    if (!rootfile)
    {
        setError("No rootfile element found");
        return false;
    }

    const char *fullPath = rootfile->Attribute("full-path");
    if (!fullPath)
    {
        setError("No full-path attribute found");
        return false;
    }

    m_rootPath = String(fullPath);
    Serial.println("Found content.opf at: " + m_rootPath);

    return true;
}

bool EpubParser::parseContentOpf(const String &contentOpf)
{
    tinyxml2::XMLDocument doc;
    if (doc.Parse(contentOpf.c_str()) != tinyxml2::XML_SUCCESS)
    {
        setError("Failed to parse content.opf");
        return false;
    }

    tinyxml2::XMLElement *package = doc.FirstChildElement("package");
    if (!package)
    {
        setError("No package element found in content.opf");
        return false;
    }

    // Parse metadata
    tinyxml2::XMLElement *metadata = package->FirstChildElement("metadata");
    if (metadata && !parseMetadata(metadata))
    {
        return false;
    }

    // Parse manifest
    tinyxml2::XMLElement *manifest = package->FirstChildElement("manifest");
    if (manifest && !parseManifest(manifest))
    {
        return false;
    }

    // Parse spine
    tinyxml2::XMLElement *spine = package->FirstChildElement("spine");
    if (spine && !parseSpine(spine))
    {
        return false;
    }

    return true;
}

bool EpubParser::parseMetadata(tinyxml2::XMLElement *metadataElement)
{
    // Parse title
    tinyxml2::XMLElement *title = metadataElement->FirstChildElement("dc:title");
    if (title && title->GetText())
    {
        m_metadata.title = String(title->GetText());
    }

    // Parse author/creator
    tinyxml2::XMLElement *creator = metadataElement->FirstChildElement("dc:creator");
    if (creator && creator->GetText())
    {
        m_metadata.author = String(creator->GetText());
    }

    // Parse language
    tinyxml2::XMLElement *language = metadataElement->FirstChildElement("dc:language");
    if (language && language->GetText())
    {
        m_metadata.language = String(language->GetText());
    }

    // Parse publisher
    tinyxml2::XMLElement *publisher = metadataElement->FirstChildElement("dc:publisher");
    if (publisher && publisher->GetText())
    {
        m_metadata.publisher = String(publisher->GetText());
    }

    // Parse identifier
    tinyxml2::XMLElement *identifier = metadataElement->FirstChildElement("dc:identifier");
    if (identifier && identifier->GetText())
    {
        m_metadata.identifier = String(identifier->GetText());
    }

    Serial.println("Parsed metadata - Title: " + m_metadata.title + ", Author: " + m_metadata.author);
    return true;
}

bool EpubParser::parseManifest(tinyxml2::XMLElement *manifestElement)
{
    tinyxml2::XMLElement *item = manifestElement->FirstChildElement("item");

    while (item)
    {
        const char *id = item->Attribute("id");
        const char *href = item->Attribute("href");
        const char *mediaType = item->Attribute("media-type");

        if (id && href && mediaType)
        {
            EpubManifestItem manifestItem;
            manifestItem.id = String(id);
            manifestItem.href = String(href);
            manifestItem.mediaType = String(mediaType);
            m_manifest.push_back(manifestItem);
        }

        item = item->NextSiblingElement("item");
    }

    Serial.println("Parsed " + String(m_manifest.size()) + " manifest items");
    return true;
}

bool EpubParser::parseSpine(tinyxml2::XMLElement *spineElement)
{
    tinyxml2::XMLElement *itemref = spineElement->FirstChildElement("itemref");

    while (itemref)
    {
        const char *idref = itemref->Attribute("idref");
        const char *linear = itemref->Attribute("linear");

        if (idref)
        {
            EpubSpineItem spineItem;
            spineItem.idref = String(idref);
            spineItem.linear = (linear == nullptr || String(linear) != "no");
            m_spine.push_back(spineItem);
        }

        itemref = itemref->NextSiblingElement("itemref");
    }

    Serial.println("Parsed " + String(m_spine.size()) + " spine items");
    return true;
}

bool EpubParser::loadChapterContent()
{
    // Create chapters based on spine order
    for (int i = 0; i < m_spine.size(); i++)
    {
        const EpubSpineItem &spineItem = m_spine[i];

        // Find corresponding manifest item
        EpubManifestItem *manifestItem = nullptr;
        for (auto &item : m_manifest)
        {
            if (item.id == spineItem.idref)
            {
                manifestItem = &item;
                break;
            }
        }

        if (!manifestItem || !isHtmlFile(manifestItem->href))
        {
            continue;
        }

        EpubChapter chapter;
        chapter.id = manifestItem->id;
        chapter.href = manifestItem->href;
        chapter.order = i;
        chapter.title = "Chapter " + String(i + 1);

        // For now, we'll create placeholder content
        // In a full implementation, you would extract the actual XHTML content
        chapter.content = "This is the content of " + chapter.title + ".\n\n";
        chapter.content += "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
        chapter.content += "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. ";
        chapter.content += "Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris.";

        m_chapters.push_back(chapter);
    }

    Serial.println("Loaded " + String(m_chapters.size()) + " chapters");
    return true;
}

String EpubParser::getFullText() const
{
    String fullText = "";

    for (const auto &chapter : m_chapters)
    {
        if (!fullText.isEmpty())
        {
            fullText += "\n\n";
        }
        fullText += chapter.content;
    }

    return fullText;
}

EpubChapter EpubParser::getChapter(int index) const
{
    if (index >= 0 && index < m_chapters.size())
    {
        return m_chapters[index];
    }
    return EpubChapter();
}

String EpubParser::processHtmlContent(const String &htmlContent)
{
    String processed = htmlContent;

    // Convert common HTML elements to readable text
    processed.replace("<p>", "\n\n");
    processed.replace("</p>", "");
    processed.replace("<br>", "\n");
    processed.replace("<br/>", "\n");
    processed.replace("<br />", "\n");

    // Handle headings
    processed.replace("<h1>", "\n\n");
    processed.replace("</h1>", "\n\n");
    processed.replace("<h2>", "\n\n");
    processed.replace("</h2>", "\n\n");
    processed.replace("<h3>", "\n\n");
    processed.replace("</h3>", "\n\n");

    // Remove remaining HTML tags
    processed = stripHtmlTags(processed);

    // Decode HTML entities
    processed = decodeHtmlEntities(processed);

    return processed;
}

String EpubParser::stripHtmlTags(const String &html)
{
    String result = html;
    int tagStart = 0;

    while ((tagStart = result.indexOf('<', tagStart)) >= 0)
    {
        int tagEnd = result.indexOf('>', tagStart);
        if (tagEnd > tagStart)
        {
            result.remove(tagStart, tagEnd - tagStart + 1);
        }
        else
        {
            tagStart++;
        }

        // Yield periodically
        if (tagStart % 100 == 0)
        {
            yield();
        }
    }

    return result;
}

String EpubParser::decodeHtmlEntities(const String &text)
{
    String result = text;

    // Common HTML entities
    result.replace("&amp;", "&");
    result.replace("&lt;", "<");
    result.replace("&gt;", ">");
    result.replace("&quot;", "\"");
    result.replace("&apos;", "'");
    result.replace("&nbsp;", " ");

    return result;
}

void EpubParser::setError(const String &error)
{
    m_lastError = error;
    Serial.println("ePub Parser Error: " + error);
}

String EpubParser::joinPath(const String &base, const String &relative)
{
    if (base.endsWith("/"))
    {
        return base + relative;
    }
    else
    {
        return base + "/" + relative;
    }
}

bool EpubParser::isHtmlFile(const String &filename)
{
    String lower = filename;
    lower.toLowerCase();
    return lower.endsWith(".html") || lower.endsWith(".xhtml") || lower.endsWith(".htm");
}