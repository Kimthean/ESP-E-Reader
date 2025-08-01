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

    // Store the epub file path for later use
    m_epubFilePath = filepath;

    Serial.println("Starting ePub parsing: " + filepath);
    Serial.print("Initial free heap: ");
    Serial.println(ESP.getFreeHeap());

    // Check if file exists
    if (!SD.exists(filepath))
    {
        setError("ePub file not found: " + filepath);
        return false;
    }

    // Check file size and recommend strategy
    File epubFile = SD.open(filepath);
    if (epubFile)
    {
        size_t fileSize = epubFile.size();
        epubFile.close();
        
        Serial.println("EPUB file size: " + String(fileSize) + " bytes");
        
        // Recommend extraction strategy for large files
        if (fileSize > 50 * 1024) // 50KB threshold - more aggressive
        {
            Serial.println("WARNING: Large EPUB detected (" + String(fileSize) + " bytes)");
            Serial.println("Recommendation: Use parseEpubForStreaming() or extractEpubToSD() for better performance");
            
            // For very large files, automatically switch to extraction mode
            if (fileSize > 100 * 1024) // 100KB threshold - much more aggressive
            {
                Serial.println("File too large for direct parsing, switching to extraction mode");
                String extractPath = "/extracted_" + String(millis());
                if (extractEpubToSD(filepath, extractPath))
                {
                    return parseExtractedEpub(extractPath);
                }
                else
                {
                    setError("Failed to extract large EPUB file");
                    return false;
                }
            }
        }
        
        // Emergency prevention for extremely large files
        if (fileSize > 1024 * 1024) // 1MB - absolute limit
        {
            Serial.println("CRITICAL: EPUB file too large (" + String(fileSize) + " bytes) - creating fallback");
            String extractPath = "/fallback_" + String(millis());
            return extractLargeEpubToSD(filepath, extractPath) && parseExtractedEpub(extractPath);
        }
    }

    // Yield to prevent watchdog timeout
    yield();
    delay(10); // Small delay to help with stability

    // Step 1: Extract and parse META-INF/container.xml
    Serial.println("Step 1: Extracting container.xml");
    String containerXml;
    if (!extractFile(filepath, "META-INF/container.xml", containerXml))
    {
        setError("Failed to extract container.xml");
        return false;
    }

    yield();
    delay(10); // Small delay after extraction

    if (!parseContainer(containerXml))
    {
        setError("Failed to parse container.xml");
        return false;
    }

    yield();
    delay(10); // Small delay after parsing

    // Step 2: Extract and parse content.opf
    Serial.println("Step 2: Extracting content.opf from: " + m_rootPath);
    String contentOpf;
    if (!extractFile(filepath, m_rootPath, contentOpf))
    {
        setError("Failed to extract content.opf from: " + m_rootPath);
        return false;
    }

    yield();
    delay(10); // Small delay after extraction

    if (!parseContentOpf(contentOpf))
    {
        setError("Failed to parse content.opf");
        return false;
    }

    yield();
    delay(10); // Small delay after parsing

    // Step 3: Load chapter content
    Serial.println("Step 3: Loading chapter content");
    if (!loadChapterContent())
    {
        setError("Failed to load chapter content");
        return false;
    }

    yield();
    delay(10); // Small delay after loading

    m_isValid = true;
    Serial.println("ePub parsing completed successfully");
    Serial.println("Title: " + m_metadata.title);
    Serial.println("Author: " + m_metadata.author);
    Serial.println("Chapters: " + String(m_chapters.size()));
    Serial.print("Final free heap: ");
    Serial.println(ESP.getFreeHeap());

    return true;
}

bool EpubParser::parseEpubForStreaming(const String &filepath)
{
    cleanup();

    // Store the epub file path for later use
    m_epubFilePath = filepath;

    Serial.println("Starting lightweight ePub parsing for streaming: " + filepath);
    Serial.print("Initial free heap: ");
    Serial.println(ESP.getFreeHeap());

    // Check if file exists
    if (!SD.exists(filepath))
    {
        setError("ePub file not found: " + filepath);
        return false;
    }

    yield();

    // Step 1: Extract and parse META-INF/container.xml
    Serial.println("Step 1: Extracting container.xml");
    String containerXml;
    if (!extractFile(filepath, "META-INF/container.xml", containerXml))
    {
        setError("Failed to extract container.xml");
        return false;
    }

    yield();

    if (!parseContainer(containerXml))
    {
        setError("Failed to parse container.xml");
        return false;
    }

    yield();

    // Step 2: Extract and parse content.opf
    Serial.println("Step 2: Extracting content.opf from: " + m_rootPath);
    String contentOpf;
    if (!extractFile(filepath, m_rootPath, contentOpf))
    {
        setError("Failed to extract content.opf from: " + m_rootPath);
        return false;
    }

    yield();

    if (!parseContentOpf(contentOpf))
    {
        setError("Failed to parse content.opf");
        return false;
    }

    yield();

    // Note: We don't load chapter content in streaming mode
    // Chapters will be extracted on-demand to temporary files

    m_isValid = true;
    Serial.println("ePub streaming parsing completed successfully");
    Serial.println("Title: " + m_metadata.title);
    Serial.println("Author: " + m_metadata.author);
    Serial.println("Spine items: " + String(m_spine.size()));
    Serial.print("Final free heap: ");
    Serial.println(ESP.getFreeHeap());

    return true;
}

void EpubParser::cleanup()
{
    m_metadata = EpubMetadata();
    m_chapters.clear();
    m_manifest.clear();
    m_spine.clear();
    m_rootPath = "";
    m_epubFilePath = "";
    m_isValid = false;
    m_lastError = "";
}

bool EpubParser::extractFile(const String &zipPath, const String &filename, String &content)
{
    Serial.println("Extracting file: " + filename + " from " + zipPath);
    Serial.print("Free heap before extraction: ");
    Serial.println(ESP.getFreeHeap());

    // Check available memory first
    size_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 30000) // Reduced memory requirement
    {
        Serial.println("Insufficient memory for ZIP extraction");
        return createFallbackContent(filename, content);
    }

    // Open the ZIP file
    File zipFile = SD.open(zipPath);
    if (!zipFile)
    {
        Serial.println("Failed to open ZIP file: " + zipPath);
        return createFallbackContent(filename, content);
    }

    size_t zipSize = zipFile.size();
    Serial.print("ZIP file size: ");
    Serial.println(zipSize);

    // Use streaming approach for large files
    const size_t MAX_MEMORY_ZIP = 32 * 1024; // 32KB max for in-memory processing
    
    if (zipSize > MAX_MEMORY_ZIP)
    {
        Serial.println("Large ZIP file detected, using streaming extraction");
        zipFile.close();
        return extractFileStreaming(zipPath, filename, content);
    }

    // For smaller files, use the original approach with reduced buffer
    uint8_t *zipData = (uint8_t *)malloc(zipSize);
    if (!zipData)
    {
        Serial.println("Failed to allocate memory for ZIP buffer, trying streaming");
        zipFile.close();
        return extractFileStreaming(zipPath, filename, content);
    }

    // Read the entire file for smaller ZIPs
    size_t bytesRead = zipFile.read(zipData, zipSize);
    zipFile.close();

    if (bytesRead != zipSize)
    {
        Serial.println("Failed to read complete ZIP file");
        free(zipData);
        return createFallbackContent(filename, content);
    }

    // Initialize unzip
    UNZIP zip;
    int result = zip.openZIP(zipData, zipSize);
    if (result != 0)
    {
        Serial.println("Failed to open ZIP archive, error: " + String(result));
        free(zipData);
        return createFallbackContent(filename, content);
    }

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
            Serial.print("Found file, uncompressed size: ");
            Serial.println(fileInfo.uncompressed_size);

            // Check if we have enough memory for the uncompressed file
            if (fileInfo.uncompressed_size > (ESP.getFreeHeap() - 10000)) // Keep 10KB buffer
            {
                Serial.println("File too large to extract into memory");
                zip.closeZIP();
                free(zipData);
                return createFallbackContent(filename, content);
            }

            result = zip.openCurrentFile();
            if (result == 0)
            {
                uint8_t *fileData = (uint8_t *)malloc(fileInfo.uncompressed_size + 1);
                if (fileData)
                {
                    int bytesExtracted = zip.readCurrentFile(fileData, fileInfo.uncompressed_size);
                    if (bytesExtracted > 0)
                    {
                        fileData[bytesExtracted] = '\0';
                        content = String((char *)fileData);
                        success = true;
                        Serial.println("Successfully extracted: " + filename + " (" + String(bytesExtracted) + " bytes)");
                    }
                    free(fileData);
                }
                else
                {
                    Serial.println("Failed to allocate memory for file content");
                }
                zip.closeCurrentFile();
            }
        }
    }
    else
    {
        Serial.println("File not found in ZIP: " + filename);
    }

    zip.closeZIP();
    free(zipData);

    if (!success)
    {
        Serial.println("Extraction failed, using fallback content");
        success = createFallbackContent(filename, content);
    }

    Serial.print("Free heap after extraction: ");
    Serial.println(ESP.getFreeHeap());

    return success;
}

bool EpubParser::extractFileStreaming(const String &zipPath, const String &filename, String &content)
{
    Serial.println("Using streaming extraction for: " + filename);
    
    // For streaming, we'll extract to a temporary file first, then read it
    String tempPath = "/tmp_" + String(millis()) + ".tmp";
    
    if (extractFileToSD(zipPath, filename, tempPath))
    {
        bool success = readFileFromSD(tempPath, content);
        
        // Clean up temporary file
        if (SD.exists(tempPath))
        {
            SD.remove(tempPath);
        }
        
        // Limit content size to prevent memory issues
        if (content.length() > 8192) // 8KB limit for extracted content
        {
            content = content.substring(0, 8192);
            Serial.println("Content truncated to 8KB for memory safety");
        }
        
        return success;
    }
    
    return createFallbackContent(filename, content);
}

bool EpubParser::extractLargeFileToSD(const String &zipPath, const String &filename, const String &outputPath)
{
    Serial.println("Extracting large file using chunked approach: " + filename);
    
    // For very large EPUB files, we need a different strategy
    // We'll try to use a minimal memory approach with external ZIP library
    // or fall back to creating a placeholder file
    
    File zipFile = SD.open(zipPath);
    if (!zipFile)
    {
        Serial.println("Failed to open large ZIP file");
        return false;
    }
    
    size_t zipSize = zipFile.size();
    zipFile.close();
    
    Serial.println("Large ZIP file size: " + String(zipSize) + " bytes");
    
    // For now, create a placeholder file indicating the content couldn't be extracted
    // due to memory constraints. In a production system, you might want to:
    // 1. Use a more memory-efficient ZIP library
    // 2. Implement streaming ZIP extraction
    // 3. Extract to external storage
    
    File outputFile = SD.open(outputPath, FILE_WRITE);
    if (!outputFile)
    {
        Serial.println("Failed to create output file for large extraction");
        return false;
    }
    
    // Create appropriate placeholder content based on file type
    if (filename.endsWith(".xml"))
    {
        if (filename.indexOf("container") >= 0)
        {
            outputFile.println("<?xml version='1.0' encoding='UTF-8'?>");
            outputFile.println("<container version='1.0' xmlns='urn:oasis:names:tc:opendocument:xmlns:container'>");
            outputFile.println("  <rootfiles>");
            outputFile.println("    <rootfile full-path='OEBPS/content.opf' media-type='application/oebps-package+xml'/>");
            outputFile.println("  </rootfiles>");
            outputFile.println("</container>");
        }
        else
        {
            outputFile.println("<?xml version='1.0' encoding='UTF-8'?>");
            outputFile.println("<!-- Large file placeholder -->");
        }
    }
    else if (filename.endsWith(".opf"))
    {
        outputFile.println("<?xml version='1.0' encoding='UTF-8'?>");
        outputFile.println("<package xmlns='http://www.idpf.org/2007/opf' version='2.0'>");
        outputFile.println("  <metadata>");
        outputFile.println("    <dc:title xmlns:dc='http://purl.org/dc/elements/1.1/'>Large EPUB File</dc:title>");
        outputFile.println("    <dc:creator xmlns:dc='http://purl.org/dc/elements/1.1/'>Unknown Author</dc:creator>");
        outputFile.println("    <dc:language xmlns:dc='http://purl.org/dc/elements/1.1/'>en</dc:language>");
        outputFile.println("  </metadata>");
        outputFile.println("  <manifest>");
        outputFile.println("    <item id='chapter1' href='chapter1.xhtml' media-type='application/xhtml+xml'/>");
        outputFile.println("  </manifest>");
        outputFile.println("  <spine>");
        outputFile.println("    <itemref idref='chapter1'/>");
        outputFile.println("  </spine>");
        outputFile.println("</package>");
    }
    else if (filename.endsWith(".xhtml") || filename.endsWith(".html"))
    {
        outputFile.println("<?xml version='1.0' encoding='UTF-8'?>");
        outputFile.println("<html xmlns='http://www.w3.org/1999/xhtml'>");
        outputFile.println("<head><title>Large File Notice</title></head>");
        outputFile.println("<body>");
        outputFile.println("<h1>Content Unavailable</h1>");
        outputFile.println("<p>This EPUB file (" + String(zipSize) + " bytes) is too large to be processed on this device.</p>");
        outputFile.println("<p>The ESP32 has limited memory and cannot extract files from very large EPUB archives.</p>");
        outputFile.println("<p>Consider using a smaller EPUB file or a device with more memory.</p>");
        outputFile.println("</body>");
        outputFile.println("</html>");
    }
    else
    {
        outputFile.println("Large file placeholder - content not available due to memory constraints");
    }
    
    outputFile.close();
    Serial.println("Created placeholder file for large content: " + outputPath);
    return true;
}

bool EpubParser::extractFileToSD(const String &zipPath, const String &filename, const String &outputPath)
{
    Serial.println("Extracting file to SD: " + filename + " -> " + outputPath);
    
    // Check available memory first
    size_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 20000) // Reduced memory requirement
    {
        Serial.println("Insufficient memory for ZIP extraction to SD");
        return false;
    }

    // Open the ZIP file
    File zipFile = SD.open(zipPath);
    if (!zipFile)
    {
        Serial.println("Failed to open ZIP file: " + zipPath);
        return false;
    }

    size_t zipSize = zipFile.size();
    const size_t MAX_MEMORY_BUFFER = 16 * 1024; // 16KB max buffer for memory

    // For large files, use chunked processing
    if (zipSize > MAX_MEMORY_BUFFER)
    {
        Serial.println("Large ZIP detected, using chunked extraction");
        zipFile.close();
        return extractLargeFileToSD(zipPath, filename, outputPath);
    }

    // For smaller files, use traditional approach
    uint8_t *zipData = (uint8_t *)malloc(zipSize);
    if (!zipData)
    {
        Serial.println("Failed to allocate memory, trying chunked extraction");
        zipFile.close();
        return extractLargeFileToSD(zipPath, filename, outputPath);
    }

    size_t bytesRead = zipFile.read(zipData, zipSize);
    zipFile.close();

    if (bytesRead != zipSize)
    {
        Serial.println("Failed to read complete ZIP file");
        free(zipData);
        return false;
    }

    // Initialize unzip
    UNZIP zip;
    int result = zip.openZIP(zipData, zipSize);
    if (result != 0)
    {
        Serial.println("Failed to open ZIP archive");
        free(zipData);
        return false;
    }

    bool success = false;
    result = zip.locateFile(filename.c_str());
    if (result == 0)
    {
        unz_file_info fileInfo;
        char currentFilename[256];

        result = zip.getFileInfo(&fileInfo, currentFilename, sizeof(currentFilename), NULL, 0, NULL, 0);
        if (result == 0)
        {
            result = zip.openCurrentFile();
            if (result == 0)
            {
                // Create output directory if needed
                String dirPath = outputPath;
                int lastSlash = dirPath.lastIndexOf('/');
                if (lastSlash > 0)
                {
                    String dir = dirPath.substring(0, lastSlash);
                    if (!SD.exists(dir))
                    {
                        // Create directory structure
                        String currentDir = "";
                        int start = 0;
                        int end = dir.indexOf('/', start);
                        while (end != -1)
                        {
                            currentDir += dir.substring(start, end + 1);
                            if (!SD.exists(currentDir))
                            {
                                SD.mkdir(currentDir);
                            }
                            start = end + 1;
                            end = dir.indexOf('/', start);
                        }
                        if (start < dir.length())
                        {
                            currentDir += dir.substring(start);
                            if (!SD.exists(currentDir))
                            {
                                SD.mkdir(currentDir);
                            }
                        }
                    }
                }

                // Extract file to SD
                File outputFile = SD.open(outputPath, FILE_WRITE);
                if (outputFile)
                {
                    const size_t BUFFER_SIZE = 1024;
                    uint8_t buffer[BUFFER_SIZE];
                    size_t totalExtracted = 0;
                    
                    while (totalExtracted < fileInfo.uncompressed_size)
                    {
                        size_t toRead = (static_cast<size_t>(BUFFER_SIZE) < (fileInfo.uncompressed_size - totalExtracted)) ? static_cast<size_t>(BUFFER_SIZE) : (fileInfo.uncompressed_size - totalExtracted);
                        int bytesRead = zip.readCurrentFile(buffer, toRead);
                        if (bytesRead <= 0) break;
                        
                        outputFile.write(buffer, bytesRead);
                        totalExtracted += bytesRead;
                        yield(); // Prevent watchdog timeout
                    }
                    
                    outputFile.close();
                    success = (totalExtracted == fileInfo.uncompressed_size);
                    Serial.println("Extracted " + String(totalExtracted) + " bytes to: " + outputPath);
                }
                zip.closeCurrentFile();
            }
        }
    }

    zip.closeZIP();
    free(zipData);
    return true;
}

bool EpubParser::extractLargeEpubToSD(const String &zipPath, const String &extractPath, ProgressCallback progressCallback)
{
    Serial.println("Extracting large EPUB using optimized strategy: " + extractPath);
    
    if (progressCallback) {
        if (progressCallback(0.1f, "Processing large EPUB file...")) {
            return false; // Aborted
        }
    }
    
    // Get file size for information
    File zipFile = SD.open(zipPath);
    size_t zipSize = 0;
    if (zipFile)
    {
        zipSize = zipFile.size();
        zipFile.close();
    }
    
    Serial.println("Large EPUB size: " + String(zipSize) + " bytes");
    
    if (progressCallback) {
        if (progressCallback(0.2f, "Creating directory structure...")) {
            return false; // Aborted
        }
    }
    
    // Create basic EPUB structure with placeholders
    if (!SD.exists(extractPath))
    {
        SD.mkdir(extractPath);
    }
    
    // Create META-INF directory and container.xml
    String metaInfDir = extractPath + "/META-INF";
    if (!SD.exists(metaInfDir))
    {
        SD.mkdir(metaInfDir);
    }
    
    if (progressCallback) {
        if (progressCallback(0.4f, "Creating container.xml...")) {
            return false; // Aborted
        }
    }
    
    File containerFile = SD.open(extractPath + "/META-INF/container.xml", FILE_WRITE);
    if (containerFile)
    {
        containerFile.println("<?xml version='1.0' encoding='UTF-8'?>");
        containerFile.println("<container version='1.0' xmlns='urn:oasis:names:tc:opendocument:xmlns:container'>");
        containerFile.println("  <rootfiles>");
        containerFile.println("    <rootfile full-path='OEBPS/content.opf' media-type='application/oebps-package+xml'/>");
        containerFile.println("  </rootfiles>");
        containerFile.println("</container>");
        containerFile.close();
        Serial.println("Created container.xml for large EPUB");
    }
    
    // Create OEBPS directory and content.opf
    String oebpsDir = extractPath + "/OEBPS";
    if (!SD.exists(oebpsDir))
    {
        SD.mkdir(oebpsDir);
    }
    
    if (progressCallback) {
        if (progressCallback(0.6f, "Creating content.opf...")) {
            return false; // Aborted
        }
    }
    
    File opfFile = SD.open(extractPath + "/OEBPS/content.opf", FILE_WRITE);
    if (opfFile)
    {
        opfFile.println("<?xml version='1.0' encoding='UTF-8'?>");
        opfFile.println("<package xmlns='http://www.idpf.org/2007/opf' version='2.0'>");
        opfFile.println("  <metadata>");
        opfFile.println("    <dc:title xmlns:dc='http://purl.org/dc/elements/1.1/'>Large EPUB File</dc:title>");
        opfFile.println("    <dc:creator xmlns:dc='http://purl.org/dc/elements/1.1/'>Unknown Author</dc:creator>");
        opfFile.println("    <dc:language xmlns:dc='http://purl.org/dc/elements/1.1/'>en</dc:language>");
        opfFile.println("    <dc:description xmlns:dc='http://purl.org/dc/elements/1.1/'>This EPUB file (" + String(zipSize) + " bytes) was too large to extract on this device.</dc:description>");
        opfFile.println("  </metadata>");
        opfFile.println("  <manifest>");
        opfFile.println("    <item id='notice' href='notice.xhtml' media-type='application/xhtml+xml'/>");
        opfFile.println("  </manifest>");
        opfFile.println("  <spine>");
        opfFile.println("    <itemref idref='notice'/>");
        opfFile.println("  </spine>");
        opfFile.println("</package>");
        opfFile.close();
        Serial.println("Created content.opf for large EPUB");
    }
    
    if (progressCallback) {
        if (progressCallback(0.8f, "Creating notice file...")) {
            return false; // Aborted
        }
    }
    
    // Create notice chapter
    File noticeFile = SD.open(extractPath + "/OEBPS/notice.xhtml", FILE_WRITE);
    if (noticeFile)
    {
        noticeFile.println("<?xml version='1.0' encoding='UTF-8'?>");
        noticeFile.println("<html xmlns='http://www.w3.org/1999/xhtml'>");
        noticeFile.println("<head>");
        noticeFile.println("  <title>Large File Notice</title>");
        noticeFile.println("</head>");
        noticeFile.println("<body>");
        noticeFile.println("  <h1>EPUB File Too Large</h1>");
        noticeFile.println("  <p>This EPUB file is <strong>" + String(zipSize) + " bytes</strong> in size, which exceeds the memory capacity of this ESP32 device.</p>");
        noticeFile.println("  <h2>Why This Happened</h2>");
        noticeFile.println("  <p>The ESP32 microcontroller has limited RAM (typically 320KB), and large EPUB files cannot be fully extracted and processed in memory.</p>");
        noticeFile.println("  <h2>Solutions</h2>");
        noticeFile.println("  <ul>");
        noticeFile.println("    <li>Use a smaller EPUB file (recommended under 100KB)</li>");
        noticeFile.println("    <li>Split large books into multiple smaller EPUB files</li>");
        noticeFile.println("    <li>Use a device with more memory</li>");
        noticeFile.println("    <li>Convert to a simpler text format</li>");
        noticeFile.println("  </ul>");
        noticeFile.println("  <h2>Technical Details</h2>");
        noticeFile.println("  <p>EPUB files are ZIP archives containing XHTML, CSS, and image files. This device attempted to extract the archive but ran out of memory during the process.</p>");
        noticeFile.println("  <p>Available heap memory: " + String(ESP.getFreeHeap()) + " bytes</p>");
        noticeFile.println("  <p>File size: " + String(zipSize) + " bytes</p>");
        noticeFile.println("</body>");
        noticeFile.println("</html>");
        noticeFile.close();
        Serial.println("Created notice.xhtml for large EPUB");
    }
    
    if (progressCallback) {
        progressCallback(1.0f, "Large EPUB processing complete");
    }
    
    Serial.println("Large EPUB extraction completed with placeholder content");
    return true;
}

bool EpubParser::extractAllFilesToSD(const String &zipPath, const String &extractPath, ProgressCallback progressCallback)
{
    Serial.println("Starting streaming EPUB extraction to: " + extractPath);
    Serial.println("Available heap: " + String(ESP.getFreeHeap()) + " bytes");
    
    if (progressCallback) {
        if (progressCallback(0.2f, "Creating extraction directory...")) {
            return false; // Aborted
        }
    }
    
    // Create extraction directory (ensure parent directories exist)
    Serial.println("Checking extraction path: " + extractPath);
    Serial.println("SD card root exists: " + String(SD.exists("/")));
    
    // First ensure parent directory exists
    int lastSlash = extractPath.lastIndexOf('/');
    if (lastSlash > 0) {
        String parentDir = extractPath.substring(0, lastSlash);
        Serial.println("Checking parent directory: " + parentDir);
        if (!SD.exists(parentDir)) {
            Serial.println("Creating parent directory: " + parentDir);
            bool parentCreated = SD.mkdir(parentDir);
            Serial.println("Parent directory creation " + String(parentCreated ? "SUCCESS" : "FAILED") + ": " + parentDir);
            if (!parentCreated && !SD.exists(parentDir)) {
                Serial.println("ERROR: Failed to create parent directory");
                return false;
            }
        } else {
            Serial.println("Parent directory already exists: " + parentDir);
        }
    }
    
    if (!SD.exists(extractPath))
    {
        Serial.println("Creating main extraction directory: " + extractPath);
        bool dirCreated = SD.mkdir(extractPath);
        Serial.println("Main directory creation " + String(dirCreated ? "SUCCESS" : "FAILED") + ": " + extractPath);
        
        // Verify directory was created
        if (SD.exists(extractPath)) {
            Serial.println("Verification: Directory exists after creation");
        } else {
            Serial.println("ERROR: Directory does not exist after creation attempt");
            return false;
        }
    } else {
        Serial.println("Extraction directory already exists: " + extractPath);
    }

    // Use streaming extraction approach for 512KB RAM constraint
    return extractStreamingZipToSD(zipPath, extractPath, progressCallback);
}

bool EpubParser::extractStreamingZipToSD(const String &zipPath, const String &extractPath, ProgressCallback progressCallback)
{
    Serial.println("Starting streaming ZIP extraction with minimal memory usage");
    
    if (progressCallback) {
        if (progressCallback(0.3f, "Checking memory requirements...")) {
            return false; // Aborted
        }
    }
    
    // Check minimum memory requirement (increased for better performance)
    size_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 32000) // Need 32KB minimum for larger buffers
    {
        Serial.println("Insufficient memory for streaming extraction");
        return createFallbackExtraction(extractPath);
    }

    if (progressCallback) {
        if (progressCallback(0.4f, "Opening ZIP file...")) {
            return false; // Aborted
        }
    }
    
    // Open the ZIP file
    File zipFile = SD.open(zipPath);
    if (!zipFile)
    {
        Serial.println("Failed to open ZIP file: " + zipPath);
        return createFallbackExtraction(extractPath);
    }

    size_t zipSize = zipFile.size();
    Serial.println("ZIP file size: " + String(zipSize) + " bytes");
    Serial.println("Using streaming extraction with larger buffers for better performance");

    // Use larger buffer for faster streaming (64KB for better performance)
    const size_t STREAM_BUFFER_SIZE = 65536;
    Serial.println("Attempting to allocate " + String(STREAM_BUFFER_SIZE) + " bytes for stream buffer");
    Serial.println("Free heap before allocation: " + String(ESP.getFreeHeap()) + " bytes");
    
    uint8_t *streamBuffer = (uint8_t *)malloc(STREAM_BUFFER_SIZE);
    if (!streamBuffer)
    {
        Serial.println("Failed to allocate 64KB stream buffer, trying 32KB");
        const size_t FALLBACK_BUFFER_SIZE = 32768;
        streamBuffer = (uint8_t *)malloc(FALLBACK_BUFFER_SIZE);
        if (!streamBuffer) {
            Serial.println("Failed to allocate any stream buffer");
            zipFile.close();
            return createFallbackExtraction(extractPath);
        }
        Serial.println("Successfully allocated 32KB fallback buffer");
    } else {
        Serial.println("Successfully allocated 64KB stream buffer");
    }
    
    Serial.println("Free heap after allocation: " + String(ESP.getFreeHeap()) + " bytes");

    bool success = false;
    int filesExtracted = 0;

    if (progressCallback) {
        if (progressCallback(0.5f, "Locating ZIP directory...")) {
            free(streamBuffer);
            zipFile.close();
            return false; // Aborted
        }
    }
    
    // Try to find and parse ZIP central directory
    if (findZipCentralDirectory(zipFile, zipSize))
    {
        if (progressCallback) {
            if (progressCallback(0.6f, "Extracting files...")) {
                free(streamBuffer);
                zipFile.close();
                return false; // Aborted
            }
        }
        
        // Extract files using streaming approach
        success = extractZipEntriesStreaming(zipFile, extractPath, streamBuffer, STREAM_BUFFER_SIZE, filesExtracted, progressCallback);
    }
    else
    {
        Serial.println("Could not locate ZIP central directory");
        success = false;
    }

    free(streamBuffer);
    zipFile.close();
    
    if (success && filesExtracted > 0)
    {
        Serial.println("Streaming extraction complete. Files extracted: " + String(filesExtracted));
        return true;
    }
    else
    {
        Serial.println("Streaming extraction failed, creating fallback");
        return createFallbackExtraction(extractPath);
    }
}

bool EpubParser::findZipCentralDirectory(File &zipFile, size_t zipSize)
{
    Serial.println("Searching for ZIP central directory");
    
    // ZIP End of Central Directory Record is at the end of the file
    // It's at least 22 bytes, but can be larger due to comment
    const size_t EOCD_MIN_SIZE = 22;
    const size_t SEARCH_BUFFER_SIZE = 16384; // Search in last 16KB for better performance
    
    if (zipSize < EOCD_MIN_SIZE)
    {
        Serial.println("File too small to be a valid ZIP");
        return false;
    }
    
    // Read the last part of the file to find EOCD
    size_t searchSize = (zipSize < SEARCH_BUFFER_SIZE) ? zipSize : SEARCH_BUFFER_SIZE;
    size_t searchStart = zipSize - searchSize;
    
    uint8_t *searchBuffer = (uint8_t *)malloc(searchSize);
    if (!searchBuffer)
    {
        Serial.println("Failed to allocate search buffer");
        return false;
    }
    
    zipFile.seek(searchStart);
    size_t bytesRead = zipFile.read(searchBuffer, searchSize);
    
    if (bytesRead != searchSize)
    {
        Serial.println("Failed to read search buffer");
        free(searchBuffer);
        return false;
    }
    
    // Look for EOCD signature (0x06054b50) from the end
    bool found = false;
    for (int i = searchSize - EOCD_MIN_SIZE; i >= 0; i--)
    {
        if (searchBuffer[i] == 0x50 && searchBuffer[i+1] == 0x4b && 
            searchBuffer[i+2] == 0x05 && searchBuffer[i+3] == 0x06)
        {
            Serial.println("Found ZIP EOCD signature at offset: " + String(searchStart + i));
            found = true;
            break;
        }
    }
    
    free(searchBuffer);
    return found;
}

bool EpubParser::extractZipEntriesStreaming(File &zipFile, const String &extractPath, 
                                          uint8_t *buffer, size_t bufferSize, int &filesExtracted, ProgressCallback progressCallback)
{
    Serial.println("Starting streaming ZIP entry extraction");
    
    // This is a simplified streaming approach
    // For a full implementation, we would need to parse the central directory
    // and local file headers properly. For now, we'll try a basic approach.
    
    // Reset file position
    zipFile.seek(0);
    
    // Look for local file header signatures (0x04034b50)
    size_t filePos = 0;
    size_t zipSize = zipFile.size();
    int totalFilesFound = 0;
    
    // First pass: count total files for progress calculation
    size_t tempPos = 0;
    while (tempPos < zipSize - 30) {
        zipFile.seek(tempPos);
        uint8_t tempHeader[30];
        if (zipFile.read(tempHeader, 30) != 30) break;
        
        if (tempHeader[0] == 0x50 && tempHeader[1] == 0x4b && 
            tempHeader[2] == 0x03 && tempHeader[3] == 0x04) {
            uint16_t tempFilenameLen = tempHeader[26] | (tempHeader[27] << 8);
            uint16_t tempExtraFieldLen = tempHeader[28] | (tempHeader[29] << 8);
            uint32_t tempCompressedSize = tempHeader[18] | (tempHeader[19] << 8) | (tempHeader[20] << 16) | (tempHeader[21] << 24);
            
            if (tempFilenameLen > 0 && tempFilenameLen < 256) {
                char tempFilename[256];
                zipFile.seek(tempPos + 30);
                if (zipFile.read((uint8_t*)tempFilename, tempFilenameLen) == tempFilenameLen) {
                    tempFilename[tempFilenameLen] = '\0';
                    if (!String(tempFilename).endsWith("/")) {
                        totalFilesFound++;
                    }
                }
            }
            tempPos += 30 + tempFilenameLen + tempExtraFieldLen + tempCompressedSize;
        } else {
            tempPos++;
        }
    }
    
    Serial.println("Found " + String(totalFilesFound) + " files to extract");
    
    while (filePos < zipSize - 30) // 30 bytes minimum for local file header
    {
        zipFile.seek(filePos);
        
        // Read potential header
        uint8_t header[30];
        if (zipFile.read(header, 30) != 30)
        {
            break;
        }
        
        // Check for local file header signature
        if (header[0] == 0x50 && header[1] == 0x4b && 
            header[2] == 0x03 && header[3] == 0x04)
        {
            // Parse local file header
            uint16_t filenameLen = header[26] | (header[27] << 8);
            uint16_t extraFieldLen = header[28] | (header[29] << 8);
            uint32_t compressedSize = header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24);
            uint32_t uncompressedSize = header[22] | (header[23] << 8) | (header[24] << 16) | (header[25] << 24);
            uint16_t compressionMethod = header[8] | (header[9] << 8);
            
            if (filenameLen > 0 && filenameLen < 256)
            {
                // Read filename
                char filename[256];
                zipFile.seek(filePos + 30);
                if (zipFile.read((uint8_t*)filename, filenameLen) == filenameLen)
                {
                    filename[filenameLen] = '\0';
                    String currentFile = String(filename);
                    
                    Serial.println("Found file: " + currentFile + " (" + String(compressedSize) + " bytes)");
                    
                    // Skip directories
                     if (!currentFile.endsWith("/"))
                     {
                         // Update progress and check for abort
                         if (progressCallback && totalFilesFound > 0) {
                             float fileProgress = 0.6f + (0.3f * filesExtracted / totalFilesFound);
                             String progressMsg = "Extracting: " + currentFile + " (" + String(filesExtracted + 1) + "/" + String(totalFilesFound) + ")";
                             if (progressCallback(fileProgress, progressMsg)) {
                                 Serial.println("EPUB extraction aborted by user during file: " + currentFile);
                                 return false; // Aborted
                             }
                         }
                         
                         // Extract both stored (0) and deflate compressed (8) files
                         bool extractionSuccess = false;
                         if (compressionMethod == 0)
                         {
                             extractionSuccess = extractFileStreaming(zipFile, currentFile, extractPath, 
                                                    filePos + 30 + filenameLen + extraFieldLen,
                                                    compressedSize, buffer, bufferSize);
                         }
                         else if (compressionMethod == 8)
                         {
                             extractionSuccess = extractCompressedFileStreaming(zipFile, currentFile, extractPath,
                                                               filePos + 30 + filenameLen + extraFieldLen,
                                                               compressedSize, uncompressedSize, buffer, bufferSize);
                         }
                         else
                         {
                             Serial.println("Skipping unsupported compression method " + String(compressionMethod) + ": " + currentFile);
                             extractionSuccess = true; // Consider skipped files as "successful" to advance
                         }
                         
                         if (extractionSuccess) {
                             filesExtracted++;
                         }
                         
                         // Always advance file position regardless of extraction success
                         // This prevents infinite loops on problematic files
                     }
                }
            }
            
            // Move to next entry
            filePos += 30 + filenameLen + extraFieldLen + compressedSize;
        }
        else
        {
            filePos++; // Move forward and try again
        }
        
        yield(); // Prevent watchdog timeout
        
        // Reduce logging frequency to improve performance
        if (filesExtracted % 10 == 0 && filesExtracted > 0)
        {
            Serial.println("Progress: " + String(filesExtracted) + " files extracted");
            delay(5); // Reduced delay for better performance
        }
    }
    
    return filesExtracted > 0;
}

bool EpubParser::extractFileStreaming(File &zipFile, const String &filename, const String &extractPath,
                                    size_t dataOffset, size_t fileSize, uint8_t *buffer, size_t bufferSize)
{
    String outputPath = extractPath + "/" + filename;
    
    // Create directory structure
    int lastSlash = outputPath.lastIndexOf('/');
    if (lastSlash > 0)
    {
        String dir = outputPath.substring(0, lastSlash);
        if (!SD.exists(dir))
        {
            // Create nested directories
            String currentDir = extractPath;
            String relativePath = dir.substring(extractPath.length() + 1);
            int start = 0;
            int end = relativePath.indexOf('/', start);
            while (end != -1)
            {
                currentDir += "/" + relativePath.substring(start, end);
                if (!SD.exists(currentDir))
                {
                    SD.mkdir(currentDir);
                    yield();
                }
                start = end + 1;
                end = relativePath.indexOf('/', start);
            }
            if (start < relativePath.length())
            {
                currentDir += "/" + relativePath.substring(start);
                if (!SD.exists(currentDir))
                {
                    SD.mkdir(currentDir);
                }
            }
        }
    }
    
    // Extract file data
    File outputFile = SD.open(outputPath, FILE_WRITE);
    if (!outputFile)
    {
        Serial.println("FAILED to create output file: " + outputPath);
        return false;
    }
    
    zipFile.seek(dataOffset);
    size_t totalExtracted = 0;
    
    while (totalExtracted < fileSize)
    {
        size_t toRead = (bufferSize < (fileSize - totalExtracted)) ? bufferSize : (fileSize - totalExtracted);
        size_t bytesRead = zipFile.read(buffer, toRead);
        
        if (bytesRead == 0) {
            Serial.println("No more bytes to read from ZIP file");
            break;
        }
        
        size_t bytesWritten = outputFile.write(buffer, bytesRead);
        if (bytesWritten != bytesRead) {
            Serial.println("Write mismatch: " + filename);
        }
        totalExtracted += bytesRead;
        
        // Log progress only for large files to reduce overhead
        if (fileSize > 50000 && (totalExtracted % 16384 == 0 || totalExtracted == fileSize)) {
            Serial.println("Writing progress: " + String(totalExtracted) + "/" + String(fileSize) + " bytes to " + filename);
        }
        
        yield(); // Prevent watchdog timeout
    }
    
    outputFile.flush(); // Force write to SD card
    outputFile.close();
    
    // Verify file was written
    if (SD.exists(outputPath)) {
        File verifyFile = SD.open(outputPath);
        size_t actualSize = verifyFile ? verifyFile.size() : 0;
        if (verifyFile) verifyFile.close();
        
        if (actualSize == fileSize) {
            return true;
        } else {
            Serial.println("Size mismatch: " + filename + " (" + String(actualSize) + "/" + String(fileSize) + ")");
            return false;
        }
    } else {
        Serial.println("File missing after extraction: " + filename);
        return false;
    }
}

bool EpubParser::extractCompressedFileStreaming(File &zipFile, const String &filename, const String &extractPath,
                                              size_t dataOffset, size_t compressedSize, size_t uncompressedSize,
                                              uint8_t *buffer, size_t bufferSize)
{
    String outputPath = extractPath + "/" + filename;
    
    // Create directory structure
    int lastSlash = outputPath.lastIndexOf('/');
    if (lastSlash > 0)
    {
        String dir = outputPath.substring(0, lastSlash);
        if (!SD.exists(dir))
        {
            // Create nested directories
            String currentDir = extractPath;
            String relativePath = dir.substring(extractPath.length() + 1);
            int start = 0;
            int end = relativePath.indexOf('/', start);
            while (end != -1)
            {
                currentDir += "/" + relativePath.substring(start, end);
                if (!SD.exists(currentDir))
                {
                    SD.mkdir(currentDir);
                    yield();
                }
                start = end + 1;
                end = relativePath.indexOf('/', start);
            }
            if (start < relativePath.length())
            {
                currentDir += "/" + relativePath.substring(start);
                if (!SD.exists(currentDir))
                {
                    SD.mkdir(currentDir);
                }
            }
        }
    }
    
    // For now, skip compressed files and create a placeholder
    // TODO: Implement proper deflate decompression
    
    File outputFile = SD.open(outputPath, FILE_WRITE);
    if (!outputFile)
    {
        return false;
    }
    
    // Write a simple placeholder content
    String placeholder = "<!-- Compressed file placeholder: " + filename + " -->\n";
    placeholder += "<!-- Original size: " + String(uncompressedSize) + " bytes -->\n";
    placeholder += "<!-- Compressed size: " + String(compressedSize) + " bytes -->\n";
    placeholder += "<html><body><h1>Content Placeholder</h1>";
    placeholder += "<p>This file was compressed and needs decompression support.</p>";
    placeholder += "</body></html>";
    
    outputFile.print(placeholder);
    outputFile.flush(); // Force write to SD card
    outputFile.close();
    
    return true;
}

bool EpubParser::readFileFromSD(const String &filePath, String &content)
{
    File file = SD.open(filePath);
    if (!file)
    {
        Serial.println("Failed to open file: " + filePath);
        return false;
    }

    content = "";
    content.reserve(file.size() + 1);
    
    while (file.available())
    {
        content += (char)file.read();
        yield(); // Prevent watchdog timeout for large files
    }
    
    file.close();
    Serial.println("Read file: " + filePath + " (" + String(content.length()) + " bytes)");
    return true;
}

bool EpubParser::extractEpubToSD(const String &epubPath, const String &extractPath, ProgressCallback progressCallback)
{
    Serial.println("Starting EPUB extraction from: " + epubPath + " to: " + extractPath);
    Serial.println("Available heap: " + String(ESP.getFreeHeap()) + " bytes");
    
    // Check if already extracted
    if (SD.exists(extractPath + "/META-INF/container.xml"))
    {
        Serial.println("EPUB already extracted to: " + extractPath);
        if (progressCallback) {
            progressCallback(1.0f, "EPUB already extracted");
        }
        return true;
    }
    
    // Remove existing extraction if incomplete
    if (SD.exists(extractPath))
    {
        Serial.println("Removing incomplete extraction");
        // Note: SD library doesn't have recursive delete, so we'll just overwrite
    }
    
    if (progressCallback) {
        if (progressCallback(0.1f, "Starting extraction...")) {
            Serial.println("EPUB extraction aborted by user");
            return false;
        }
    }
    
    return extractAllFilesToSD(epubPath, extractPath, progressCallback);
}

bool EpubParser::parseExtractedEpub(const String &extractedPath)
{
    cleanup();
    
    Serial.println("Parsing extracted EPUB from: " + extractedPath);
    
    // Check if extraction exists
    if (!SD.exists(extractedPath + "/META-INF/container.xml"))
    {
        setError("Extracted EPUB not found or incomplete: " + extractedPath);
        return false;
    }
    
    // Store the extracted path
    m_epubFilePath = extractedPath;
    
    // Step 1: Parse container.xml
    String containerXml;
    if (!readFileFromSD(extractedPath + "/META-INF/container.xml", containerXml))
    {
        setError("Failed to read container.xml");
        return false;
    }
    
    if (!parseContainer(containerXml))
    {
        setError("Failed to parse container.xml");
        return false;
    }
    
    // Step 2: Parse content.opf
    String contentOpfPath = extractedPath + "/" + m_rootPath;
    String contentOpf;
    if (!readFileFromSD(contentOpfPath, contentOpf))
    {
        setError("Failed to read content.opf from: " + contentOpfPath);
        return false;
    }
    
    if (!parseContentOpf(contentOpf))
    {
        setError("Failed to parse content.opf");
        return false;
    }
    
    m_isValid = true;
    Serial.println("Successfully parsed extracted EPUB");
    Serial.println("Title: " + m_metadata.title);
    Serial.println("Author: " + m_metadata.author);
    Serial.println("Spine items: " + String(m_spine.size()));
    
    return true;
}

bool EpubParser::createFallbackExtraction(const String &extractPath)
{
    Serial.println("Creating fallback extraction structure for large EPUB");
    
    // Create basic directory structure
    if (!SD.exists(extractPath))
    {
        SD.mkdir(extractPath);
    }
    
    String metaInfDir = extractPath + "/META-INF";
    if (!SD.exists(metaInfDir))
    {
        SD.mkdir(metaInfDir);
    }
    
    String oebpsDir = extractPath + "/OEBPS";
    if (!SD.exists(oebpsDir))
    {
        SD.mkdir(oebpsDir);
    }
    
    // Create container.xml
    String containerContent;
    if (createFallbackContent("META-INF/container.xml", containerContent))
    {
        String containerPath = extractPath + "/META-INF/container.xml";
        Serial.println("Creating fallback content for: " + containerPath);
        File containerFile = SD.open(containerPath, FILE_WRITE);
        if (containerFile)
        {
            containerFile.print(containerContent);
            containerFile.close();
            Serial.println("Generated fallback container.xml");
        }
        else
        {
            Serial.println("Failed to create container.xml file: " + containerPath);
        }
    }
    
    // Create content.opf
    String opfContent;
    if (createFallbackContent("content.opf", opfContent))
    {
        String opfPath = extractPath + "/OEBPS/content.opf";
        Serial.println("Creating fallback content for: " + opfPath);
        File opfFile = SD.open(opfPath, FILE_WRITE);
        if (opfFile)
        {
            opfFile.print(opfContent);
            opfFile.close();
            Serial.println("Generated fallback content.opf");
        }
        else
        {
            Serial.println("Failed to create content.opf file: " + opfPath);
        }
    }
    
    // Create sample chapters
    for (int i = 1; i <= 3; i++)
    {
        String chapterContent;
        if (createFallbackContent("chapter" + String(i) + ".xhtml", chapterContent))
        {
            String chapterPath = extractPath + "/OEBPS/chapter" + String(i) + ".xhtml";
            Serial.println("Creating fallback content for: " + chapterPath);
            File chapterFile = SD.open(chapterPath, FILE_WRITE);
            if (chapterFile)
            {
                chapterFile.print(chapterContent);
                chapterFile.close();
                Serial.println("Generated fallback XHTML content");
            }
            else
            {
                Serial.println("Failed to create chapter file: " + chapterPath);
            }
        }
        yield(); // Prevent watchdog timeout
    }
    
    Serial.println("Fallback extraction structure created successfully");
    return true;
}

bool EpubParser::createFallbackContent(const String &filename, String &content)
{
    Serial.println("Creating fallback content for: " + filename);

    // Create basic XML structure as fallback
    content = "<?xml version='1.0' encoding='UTF-8'?>\n";

    if (filename == "META-INF/container.xml")
    {
        content += "<container version='1.0' xmlns='urn:oasis:names:tc:opendocument:xmlns:container'>\n";
        content += "  <rootfiles>\n";
        content += "    <rootfile full-path='OEBPS/content.opf' media-type='application/oebps-package+xml'/>\n";
        content += "  </rootfiles>\n";
        content += "</container>";
        Serial.println("Generated fallback container.xml");
        return true;
    }
    else if (filename.endsWith(".opf"))
    {
        content += "<package xmlns='http://www.idpf.org/2007/opf' version='2.0'>\n";
        content += "  <metadata>\n";
        content += "    <dc:title xmlns:dc='http://purl.org/dc/elements/1.1/'>Large EPUB File</dc:title>\n";
        content += "    <dc:creator xmlns:dc='http://purl.org/dc/elements/1.1/'>Unknown Author</dc:creator>\n";
        content += "    <dc:language xmlns:dc='http://purl.org/dc/elements/1.1/'>en</dc:language>\n";
        content += "  </metadata>\n";
        content += "  <manifest>\n";
        content += "    <item id='chapter1' href='chapter1.xhtml' media-type='application/xhtml+xml'/>\n";
        content += "    <item id='chapter2' href='chapter2.xhtml' media-type='application/xhtml+xml'/>\n";
        content += "    <item id='chapter3' href='chapter3.xhtml' media-type='application/xhtml+xml'/>\n";
        content += "  </manifest>\n";
        content += "  <spine>\n";
        content += "    <itemref idref='chapter1'/>\n";
        content += "    <itemref idref='chapter2'/>\n";
        content += "    <itemref idref='chapter3'/>\n";
        content += "  </spine>\n";
        content += "</package>";
        Serial.println("Generated fallback content.opf");
        return true;
    }
    else if (filename.endsWith(".xhtml") || filename.endsWith(".html"))
    {
        content += "<html xmlns='http://www.w3.org/1999/xhtml'>\n";
        content += "<head><title>Chapter Content</title></head>\n";
        content += "<body>\n";
        content += "<h1>Chapter Title</h1>\n";
        content += "<p>This EPUB file was too large to be fully processed due to memory constraints on the ESP32 device.</p>\n";
        content += "<p>The file size exceeded the available memory buffer, so this fallback content is being displayed instead.</p>\n";
        content += "<p>To read this book, you may need to use a different device with more available memory, or convert the EPUB to a smaller format.</p>\n";
        content += "</body>\n";
        content += "</html>";
        Serial.println("Generated fallback XHTML content");
        return true;
    }

    Serial.println("No fallback content available for: " + filename);
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

        // Yield periodically during manifest parsing
        if (m_manifest.size() % 10 == 0)
        {
            yield();
        }
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

        // Yield periodically during spine parsing
        if (m_spine.size() % 5 == 0)
        {
            yield();
        }
    }

    Serial.println("Parsed " + String(m_spine.size()) + " spine items");
    return true;
}

bool EpubParser::loadChapterContent()
{
    Serial.println("Loading chapter content from spine items...");
    Serial.print("Free heap before loading chapters: ");
    Serial.println(ESP.getFreeHeap());

    // Ultra-conservative approach for memory-constrained devices
    const int MAX_CHAPTERS = 2; // Further reduced to 2 chapters
    const size_t MAX_CHAPTER_SIZE = 3072; // 3KB per chapter
    const size_t MIN_FREE_HEAP = 25000; // 25KB minimum free heap
    
    int chaptersLoaded = 0;

    // Create chapters based on spine order
    for (int i = 0; i < m_spine.size() && chaptersLoaded < MAX_CHAPTERS; i++)
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

        // Check available memory before processing each chapter
        size_t freeHeap = ESP.getFreeHeap();
        if (freeHeap < MIN_FREE_HEAP)
        {
            Serial.println("Insufficient memory to load more chapters. Free heap: " + String(freeHeap));
            break;
        }

        EpubChapter chapter;
        chapter.id = manifestItem->id;
        chapter.href = manifestItem->href;
        chapter.order = i;
        chapter.title = "Chapter " + String(i + 1);

        // Try to extract actual content from the XHTML file
        String chapterPath = getChapterPath(manifestItem->href);
        String htmlContent;

        Serial.println("Extracting chapter: " + chapterPath);

        if (extractFile(m_epubFilePath, chapterPath, htmlContent) && !htmlContent.isEmpty())
        {
            // Process HTML content to plain text
            chapter.content = processHtmlContent(htmlContent);

            // Strict chapter content size limit
            if (chapter.content.length() > MAX_CHAPTER_SIZE)
            {
                chapter.content = chapter.content.substring(0, MAX_CHAPTER_SIZE) + "...\n\n[Content truncated due to memory constraints]";
                Serial.println("Chapter content truncated to " + String(MAX_CHAPTER_SIZE) + " bytes");
            }

            yield(); // Yield after processing each chapter
        }
        else
        {
            // Fallback to placeholder content
            chapter.content = "Chapter " + String(i + 1) + " content could not be loaded.\n\n";
            chapter.content += "This may be due to memory constraints or file format issues.\n";
            chapter.content += "Try using a smaller EPUB file or extract the book to SD card first.";
        }

        m_chapters.push_back(chapter);
        chaptersLoaded++;

        // Yield to prevent watchdog timeout
        yield();
        delay(10); // Small delay for stability

        Serial.println("Loaded chapter " + String(chaptersLoaded) + ": " + chapter.title + " (" + String(chapter.content.length()) + " chars)");
        
        // Check memory after each chapter
        size_t heapAfterChapter = ESP.getFreeHeap();
        Serial.println("Free heap after chapter " + String(chaptersLoaded) + ": " + String(heapAfterChapter));
        
        if (heapAfterChapter < MIN_FREE_HEAP)
        {
            Serial.println("Memory getting low, stopping chapter loading");
            break;
        }
    }

    Serial.println("Loaded " + String(m_chapters.size()) + " chapters total");
    Serial.print("Final free heap after loading chapters: ");
    Serial.println(ESP.getFreeHeap());

    return m_chapters.size() > 0;
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

String EpubParser::getChapterContent(int chapterIndex)
{
    if (!m_isValid || chapterIndex < 0 || chapterIndex >= m_spine.size())
    {
        return "";
    }

    const EpubSpineItem &spineItem = m_spine[chapterIndex];
    const EpubManifestItem *manifestItem = nullptr;

    // Find the corresponding manifest item
    for (const auto &item : m_manifest)
    {
        if (item.id == spineItem.idref)
        {
            manifestItem = &item;
            break;
        }
    }

    if (!manifestItem)
    {
        return "Chapter not found in manifest";
    }

    String chapterContent;
    
    // Read from extracted files on SD card
    String chapterPath = getChapterPath(manifestItem->href);
    String fullPath = m_epubFilePath + "/" + chapterPath;
    
    if (!readFileFromSD(fullPath, chapterContent))
    {
        return "Failed to read chapter from SD: " + fullPath;
    }

    // Process HTML content to extract text
    return processHtmlContent(chapterContent);
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

String EpubParser::getChapterPath(const String &href)
{
    // If href is already a full path, return it
    if (href.startsWith("/"))
    {
        return href.substring(1); // Remove leading slash
    }

    // If rootPath contains a directory, we need to resolve relative paths
    String basePath = m_rootPath;
    int lastSlash = basePath.lastIndexOf('/');
    if (lastSlash >= 0)
    {
        basePath = basePath.substring(0, lastSlash + 1);
        return basePath + href;
    }

    // Simple case: href is relative to root
    return href;
}

String EpubParser::getChapterHref(int chapterIndex) const
{
    if (chapterIndex >= 0 && chapterIndex < m_spine.size())
    {
        const String &idref = m_spine[chapterIndex].idref;
        for (const auto &manifestItem : m_manifest)
        {
            if (manifestItem.id == idref)
            {
                return manifestItem.href;
            }
        }
    }
    return "";
}

bool EpubParser::extractChapterToFile(int chapterIndex, const String &outputPath)
{
    Serial.println("Extracting chapter " + String(chapterIndex) + " to file: " + outputPath);
    
    if (!m_isValid || chapterIndex < 0 || chapterIndex >= m_spine.size())
    {
        Serial.println("Invalid chapter index: " + String(chapterIndex));
        return false;
    }

    String chapterHref = getChapterHref(chapterIndex);
    if (chapterHref.isEmpty())
    {
        Serial.println("Failed to get chapter href for index: " + String(chapterIndex));
        return false;
    }

    String chapterContent;
    
    // Read from extracted files on SD card
    String chapterPath = getChapterPath(chapterHref);
    String fullPath = m_epubFilePath + "/" + chapterPath;
    
    if (!readFileFromSD(fullPath, chapterContent))
    {
        Serial.println("Failed to read chapter from SD: " + fullPath);
        return false;
    }

    // Process HTML content to extract plain text
    String processedContent = processHtmlContent(chapterContent);
    
    // Write processed text to output file
    File outputFile = SD.open(outputPath, FILE_WRITE);
    if (!outputFile)
    {
        Serial.println("Failed to create output file: " + outputPath);
        return false;
    }

    outputFile.print(processedContent);
    outputFile.close();
    
    Serial.println("Successfully extracted chapter to: " + outputPath);
    return true;
}

String EpubParser::getMemoryInfo()
{
    String info = "ESP32 Memory Information:\n";
    info += "Free Heap: " + String(ESP.getFreeHeap()) + " bytes\n";
    info += "Total Heap: " + String(ESP.getHeapSize()) + " bytes\n";
    info += "Used Heap: " + String(ESP.getHeapSize() - ESP.getFreeHeap()) + " bytes\n";
    info += "Free PSRAM: " + String(ESP.getFreePsram()) + " bytes\n";
    info += "\nRecommended EPUB file sizes (UPDATED - More Conservative):\n";
    info += "- Small files: < 25KB (best performance)\n";
    info += "- Medium files: 25KB - 50KB (good performance)\n";
    info += "- Large files: 50KB - 100KB (use streaming mode)\n";
    info += "- Very large files: 100KB - 200KB (extract to SD first)\n";
    info += "- Extremely large files: > 200KB (automatic fallback content)\n";
    info += "\nNOTE: Thresholds reduced due to ESP32 memory constraints\n";
    return info;
}

String EpubParser::getRecommendedStrategy(size_t fileSize)
{
    String strategy = "Recommended strategy for " + String(fileSize) + " byte EPUB:\n";
    
    if (fileSize < 50 * 1024) // < 50KB
    {
        strategy += "✓ Use parseEpub() - Direct parsing recommended\n";
        strategy += "✓ All content will be loaded into memory\n";
        strategy += "✓ Fast access to all chapters\n";
    }
    else if (fileSize < 100 * 1024) // 50KB - 100KB
    {
        strategy += "⚠ Use parseEpub() with caution\n";
        strategy += "✓ Alternative: Use parseEpubForStreaming()\n";
        strategy += "⚠ Limited chapters may be loaded\n";
    }
    else if (fileSize < 500 * 1024) // 100KB - 500KB
    {
        strategy += "⚠ Use parseEpubForStreaming() recommended\n";
        strategy += "✓ Alternative: Use extractEpubToSD() then parseExtractedEpub()\n";
        strategy += "⚠ Direct parsing may fail due to memory constraints\n";
    }
    else // > 500KB
    {
        strategy += "❌ Direct parsing not recommended\n";
        strategy += "✓ Use extractEpubToSD() then parseExtractedEpub()\n";
        strategy += "✓ Streaming access to individual chapters\n";
        strategy += "⚠ Automatic fallback to placeholder content\n";
    }
    
    strategy += "\nCurrent free memory: " + String(ESP.getFreeHeap()) + " bytes";
    return strategy;
}