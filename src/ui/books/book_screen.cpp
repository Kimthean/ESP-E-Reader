#include "book_screen.h"
#include "../../../include/storage.h"
#include "../../../include/power.h"
#include "../../../include/display.h"
#include "../../../include/epub_parser.h"
#include <SD.h>
#include <algorithm>
#include <ArduinoJson.h>

// Static constants
const String BookScreen::HISTORY_FILE = "/reading_history.json";

// Forward declarations for storage functions
extern bool powerOnSDCard();
extern bool isSDCardPowered();
extern EinkDisplayManager display;

BookScreen::BookScreen()
{
    m_currentMode = MODE_BOOK_LIST;
    m_selectedBookIndex = 0;
    m_isLoading = false;
    m_isInitialized = false;
    m_bookLoaded = false;

    // Initialize pagination
    m_currentBookPage = 0;
    m_booksPerPage = 5;
    m_totalBookPages = 0;

    // Initialize text settings
    initializeTextSettings();

    // Initialize page info
    m_pageInfo.currentPage = 0;
    m_pageInfo.totalPages = 0;
    m_pageInfo.startPosition = 0;
    m_pageInfo.endPosition = 0;

    // Initialize streaming structure
    m_bookStream.isOpen = false;
    m_bookStream.currentChunk.isLoaded = false;
    m_estimatedTotalPages = 0;

    // Initialize book menu
    initializeBookMenu();
}

void BookScreen::draw(EinkDisplayManager::DisplayUpdateMode mode)
{
    // Lazy initialization on first access
    if (!m_isInitialized)
    {
        m_isInitialized = true;
        refreshBookList();
    }

    display.startDrawing();
    drawHeader();

    switch (m_currentMode)
    {
    case MODE_BOOK_LIST:
        if (m_isLoading)
        {
            drawLoadingIndicator();
        }
        else
        {
            drawBookListContent();
        }
        break;

    case MODE_BOOK_READER:
        drawBookReaderContent();
        break;

    case MODE_BOOK_MENU:
        drawBookMenuDialog();
        break;
    }

    display.endDrawing();
    display.update(mode);
}

void BookScreen::drawBookList(EinkDisplayManager::DisplayUpdateMode mode)
{
    m_currentMode = MODE_BOOK_LIST;
    draw(mode);
}

void BookScreen::drawBookReader(EinkDisplayManager::DisplayUpdateMode mode)
{
    m_currentMode = MODE_BOOK_READER;
    draw(mode);
}

void BookScreen::drawBookMenu(EinkDisplayManager::DisplayUpdateMode mode)
{
    m_currentMode = MODE_BOOK_MENU;
    draw(mode);
}

void BookScreen::handleSelectAction()
{
    switch (m_currentMode)
    {
    case MODE_BOOK_LIST:
        // Select and load a book
        if (!m_availableBooks.empty() && m_selectedBookIndex < m_availableBooks.size())
        {
            const BookInfo &book = m_availableBooks[m_selectedBookIndex];

            // Show loading indicator
            display.wipeScreen();
            display.m_display.setFont(&FreeMono9pt7b);
            display.drawCenteredText("Loading book...", 150, &FreeMono9pt7b);
            display.m_display.setFont(&FreeMono9pt7b);
            display.drawCenteredText("Please wait", 170, &FreeMono9pt7b);
            display.update(EinkDisplayManager::UPDATE_PARTIAL);

            if (loadBook(book.filename))
            {
                setMode(MODE_BOOK_READER);
                display.wipeScreen();
                draw(EinkDisplayManager::UPDATE_PARTIAL);
            }
            else
            {
                // Show error message
                display.wipeScreen();
                display.m_display.setFont(&FreeMono9pt7b);
                display.drawCenteredText("Failed to load book", 150, &FreeMono9pt7b);
                display.m_display.setFont(&FreeMono9pt7b);
                display.drawCenteredText("Press any button to continue", 170, &FreeMono9pt7b);
                display.update(EinkDisplayManager::UPDATE_PARTIAL);
                delay(2000);
                draw(EinkDisplayManager::UPDATE_PARTIAL);
            }
        }
        break;

    case MODE_BOOK_READER:
        // Show book menu
        showBookMenu();
        break;

    case MODE_BOOK_MENU:
        // Handle menu selection
        handleBookMenuSelect();
        break;
    }
}

void BookScreen::handleDownAction()
{
    switch (m_currentMode)
    {
    case MODE_BOOK_LIST:
        // Navigate down in book list
        if (!m_availableBooks.empty())
        {
            if (m_selectedBookIndex < m_availableBooks.size() - 1)
            {
                m_selectedBookIndex++;
                draw(EinkDisplayManager::UPDATE_PARTIAL);
            }
            else if (m_currentBookPage < m_totalBookPages - 1)
            {
                // Move to next page
                nextBookPage();
                draw(EinkDisplayManager::UPDATE_PARTIAL);
            }
        }
        break;

    case MODE_BOOK_READER:
        // Next page
        if (nextPage())
        {
            draw(EinkDisplayManager::UPDATE_PARTIAL);
        }
        break;

    case MODE_BOOK_MENU:
        // Navigate down in menu
        if (!m_bookMenu.options.empty())
        {
            m_bookMenu.selectedOption = (m_bookMenu.selectedOption + 1) % m_bookMenu.options.size();
            draw(EinkDisplayManager::UPDATE_PARTIAL);
        }
        break;
    }
}

void BookScreen::handleUpAction()
{
    switch (m_currentMode)
    {
    case MODE_BOOK_LIST:
        // Navigate up in book list
        if (!m_availableBooks.empty())
        {
            if (m_selectedBookIndex > 0)
            {
                m_selectedBookIndex--;
                draw(EinkDisplayManager::UPDATE_PARTIAL);
            }
            else if (m_currentBookPage > 0)
            {
                // Move to previous page
                previousBookPage();
                m_selectedBookIndex = m_availableBooks.size() - 1; // Select last book on previous page
                draw(EinkDisplayManager::UPDATE_PARTIAL);
            }
        }
        break;

    case MODE_BOOK_READER:
        // Previous page
        if (previousPage())
        {
            draw(EinkDisplayManager::UPDATE_PARTIAL);
        }
        break;

    case MODE_BOOK_MENU:
        // Navigate up in menu or close menu
        if (m_bookMenu.selectedOption > 0)
        {
            m_bookMenu.selectedOption--;
            draw(EinkDisplayManager::UPDATE_PARTIAL);
        }
        else
        {
            hideBookMenu();
        }
        break;
    }
}

void BookScreen::handleBackAction()
{
    switch (m_currentMode)
    {
    case MODE_BOOK_READER:
    case MODE_BOOK_MENU:
        // Return to book list
        closeBook();
        setMode(MODE_BOOK_LIST);
        display.wipeScreen();
        draw(EinkDisplayManager::UPDATE_PARTIAL);
        break;

    case MODE_BOOK_LIST:
        // Already at top level - no action
        break;
    }
}

bool BookScreen::loadBook(const String &filepath)
{
    if (!fileExists(filepath))
    {
        Serial.println("Book file not found: " + filepath);
        return false;
    }

    // Check available memory before loading
    size_t freeHeap = ESP.getFreeHeap();
    Serial.println("Free heap before loading: " + String(freeHeap) + " bytes");

    if (freeHeap < 10000) // Need at least 10KB free for streaming
    {
        Serial.println("Insufficient memory to load book");
        return false;
    }

    m_currentBookInfo.filename = filepath;
    m_currentBookInfo.format = detectBookFormat(filepath);
    m_currentBookInfo.fileSize = getFileSize(filepath);

    // Use streaming approach for all files
    Serial.println("Using streaming approach for file: " + String(m_currentBookInfo.fileSize) + " bytes");

    // Initialize streaming
    if (!initializeBookStream(filepath))
    {
        Serial.println("Failed to initialize book stream");
        return false;
    }

    // Show pagination progress
    display.wipeScreen();
    display.m_display.setFont(&FreeMono9pt7b);
    display.drawCenteredText("Processing book...", 150, &FreeMono9pt7b);
    display.m_display.setFont(&FreeMono9pt7b);
    display.drawCenteredText("Building page index", 170, &FreeMono9pt7b);
    display.update(EinkDisplayManager::UPDATE_PARTIAL);

    // Build page index for streaming
    if (!buildPageIndex())
    {
        Serial.println("Failed to build page index");
        closeBookStream();
        return false;
    }

    m_bookLoaded = true;
    m_pageInfo.currentPage = 0;
    m_pageInfo.totalPages = m_estimatedTotalPages; // Use estimated total for UI

    // Extract title from filename if not set
    if (m_currentBookInfo.title.isEmpty())
    {
        int lastSlash = filepath.lastIndexOf('/');
        int lastDot = filepath.lastIndexOf('.');
        if (lastSlash >= 0 && lastDot > lastSlash)
        {
            m_currentBookInfo.title = filepath.substring(lastSlash + 1, lastDot);
        }
        else
        {
            m_currentBookInfo.title = filepath;
        }
    }

    // Try to load reading history and restore last position
    if (loadReadingHistory(filepath))
    {
        Serial.println("Restored reading position from history");
    }
    else
    {
        Serial.println("No reading history found, starting from beginning");
    }

    Serial.println("Book loaded successfully: " + m_currentBookInfo.title);
    Serial.println("Total pages: " + String(m_pageInfo.totalPages));
    Serial.println("Current page: " + String(m_pageInfo.currentPage + 1));
    Serial.println("Memory used: ~" + String(m_bookStream.pagePositions.size() * sizeof(size_t)) + " bytes for page index");

    return true;
}

void BookScreen::closeBook()
{
    // Save reading history before closing
    if (m_bookLoaded)
    {
        saveReadingHistory();
    }

    m_bookLoaded = false;

    // Clear legacy content and free memory
    m_bookContent = "";
    // m_pages.clear(); // Legacy - no longer used with streaming
    std::vector<String>().swap(m_pages); // Clear legacy pages vector

    // Close streaming resources
    closeBookStream();

    m_pageInfo.currentPage = 0;
    m_pageInfo.totalPages = 0;
    m_estimatedTotalPages = 0;
    m_currentBookInfo = BookInfo();

    Serial.println("Book closed and memory freed");
}

bool BookScreen::isBookLoaded() const
{
    return m_bookLoaded;
}

bool BookScreen::nextPage()
{
    if (!m_bookLoaded || !m_bookStream.isOpen)
    {
        return false;
    }

    // Check if we need to expand the page index
    if (m_pageInfo.currentPage + 1 >= m_bookStream.pagePositions.size())
    {
        if (!expandPageIndex(m_pageInfo.currentPage + 10)) // Expand 10 pages ahead
        {
            return false; // Reached end of book
        }
    }

    if (m_pageInfo.currentPage >= m_pageInfo.totalPages - 1)
    {
        return false;
    }

    m_pageInfo.currentPage++;

    // Auto-save reading progress every few pages
    if (m_pageInfo.currentPage % 5 == 0)
    {
        saveReadingHistory();
    }

    return true;
}

bool BookScreen::previousPage()
{
    if (!m_bookLoaded || !m_bookStream.isOpen || m_pageInfo.currentPage <= 0)
    {
        return false;
    }

    m_pageInfo.currentPage--;

    // Auto-save reading progress every few pages
    if (m_pageInfo.currentPage % 5 == 0)
    {
        saveReadingHistory();
    }

    return true;
}

bool BookScreen::goToPage(int pageNumber)
{
    if (!m_bookLoaded || pageNumber < 0)
    {
        return false;
    }

    // Check if we need to expand the page index for the target page
    if (pageNumber >= m_bookStream.pagePositions.size())
    {
        if (!expandPageIndex(pageNumber + 5)) // Expand a bit beyond target
        {
            return false; // Cannot reach that page
        }
    }

    if (pageNumber >= m_pageInfo.totalPages)
    {
        return false;
    }

    m_pageInfo.currentPage = pageNumber;

    // Save reading progress when jumping to a specific page
    saveReadingHistory();

    return true;
}

void BookScreen::increaseFontSize()
{
    if (m_textSettings.font == &FreeMono9pt7b)
    {
        m_textSettings.font = &FreeMono12pt7b;
        m_textSettings.fontSize = 12;
        m_textSettings.lineHeight = 18; // Medium font spacing
    }
    else if (m_textSettings.font == &FreeMono12pt7b)
    {
        m_textSettings.font = &FreeMono18pt7b;
        m_textSettings.fontSize = 18;
        m_textSettings.lineHeight = 26; // Large font spacing
    }
    // If already at 18pt font, stay at 18pt (max size)

    m_textSettings.wordsPerPage = calculateWordsPerPage();
    if (m_bookLoaded && m_bookStream.isOpen)
    {
        // Rebuild page index with new font settings
        buildPageIndex();
        m_pageInfo.totalPages = m_bookStream.pagePositions.size();
        // Ensure current page is still valid
        if (m_pageInfo.currentPage >= m_pageInfo.totalPages)
        {
            m_pageInfo.currentPage = m_pageInfo.totalPages - 1;
        }
    }
}

void BookScreen::decreaseFontSize()
{
    if (m_textSettings.font == &FreeMono18pt7b)
    {
        m_textSettings.font = &FreeMono12pt7b;
        m_textSettings.fontSize = 12;
        m_textSettings.lineHeight = 18; // Medium font spacing
    }
    else if (m_textSettings.font == &FreeMono12pt7b)
    {
        m_textSettings.font = &FreeMono9pt7b;
        m_textSettings.fontSize = 9;
        m_textSettings.lineHeight = 14; // Small font spacing
    }
    // If already at 9pt font, stay at 9pt (min size for readability)

    m_textSettings.wordsPerPage = calculateWordsPerPage();
    if (m_bookLoaded && m_bookStream.isOpen)
    {
        // Rebuild page index with new font settings
        buildPageIndex();
        m_pageInfo.totalPages = m_bookStream.pagePositions.size();
        // Ensure current page is still valid
        if (m_pageInfo.currentPage >= m_pageInfo.totalPages)
        {
            m_pageInfo.currentPage = m_pageInfo.totalPages - 1;
        }
    }
}

void BookScreen::setFont(const GFXfont *font)
{
    m_textSettings.font = font;
    m_textSettings.wordsPerPage = calculateWordsPerPage();
    if (m_bookLoaded && m_bookStream.isOpen)
    {
        // Rebuild page index with new font settings
        buildPageIndex();
        m_pageInfo.totalPages = m_bookStream.pagePositions.size();
        // Ensure current page is still valid
        if (m_pageInfo.currentPage >= m_pageInfo.totalPages)
        {
            m_pageInfo.currentPage = m_pageInfo.totalPages - 1;
        }
    }
}

void BookScreen::showBookMenu()
{
    m_bookMenu.isVisible = true;
    m_bookMenu.selectedOption = 0;
    setMode(MODE_BOOK_MENU);
    draw(EinkDisplayManager::UPDATE_PARTIAL);
}

void BookScreen::hideBookMenu()
{
    m_bookMenu.isVisible = false;
    setMode(MODE_BOOK_READER);
    draw(EinkDisplayManager::UPDATE_PARTIAL);
}

void BookScreen::handleBookMenuSelect()
{
    if (m_bookMenu.selectedOption < m_bookMenu.options.size())
    {
        String selectedOption = m_bookMenu.options[m_bookMenu.selectedOption];

        if (selectedOption == "Increase Font")
        {
            increaseFontSize();
            draw(EinkDisplayManager::UPDATE_PARTIAL);
        }
        else if (selectedOption == "Decrease Font")
        {
            decreaseFontSize();
            draw(EinkDisplayManager::UPDATE_PARTIAL);
        }
        else if (selectedOption == "Return to Reading")
        {
            hideBookMenu();
        }
        else if (selectedOption == "Close Book")
        {
            handleBackAction();
        }
    }
}

BookScreen::ScreenMode BookScreen::getCurrentMode() const
{
    return m_currentMode;
}

void BookScreen::setMode(ScreenMode mode)
{
    m_currentMode = mode;
}

void BookScreen::refreshBookList()
{
    m_isLoading = true;
    m_availableBooks = scanBooksDirectory();
    ensureValidBookSelection();
    m_isLoading = false;
}

std::vector<BookInfo> BookScreen::getAvailableBooks() const
{
    return m_availableBooks;
}

int BookScreen::getSelectedBookIndex() const
{
    return m_selectedBookIndex;
}

void BookScreen::setSelectedBookIndex(int index)
{
    if (index >= 0 && index < m_availableBooks.size())
    {
        m_selectedBookIndex = index;
    }
}

void BookScreen::nextBookPage()
{
    if (m_currentBookPage < m_totalBookPages - 1)
    {
        m_currentBookPage++;
        m_selectedBookIndex = 0; // Reset selection to first book on new page
        refreshBookList();
    }
}

void BookScreen::previousBookPage()
{
    if (m_currentBookPage > 0)
    {
        m_currentBookPage--;
        m_selectedBookIndex = 0; // Reset selection to first book on new page
        refreshBookList();
    }
}

int BookScreen::getCurrentBookPage() const
{
    return m_currentBookPage;
}

int BookScreen::getTotalBookPages() const
{
    return m_totalBookPages;
}

BookInfo BookScreen::getBookInfo() const
{
    return m_currentBookInfo;
}

PageInfo BookScreen::getPageInfo() const
{
    return m_pageInfo;
}

TextSettings BookScreen::getTextSettings() const
{
    return m_textSettings;
}

std::vector<BookInfo> BookScreen::scanBooksDirectory()
{
    std::vector<BookInfo> books;
    std::vector<BookInfo> allBooks;

    // Check if SD card is available
    if (getSDCardStatus() != SD_READY)
    {
        Serial.println("SD card not ready");
        return books;
    }

    if (!directoryExists("/books"))
    {
        Serial.println("Books directory not found on SD card");
        return books;
    }

    File dir = SD.open("/books");
    if (!dir)
    {
        Serial.println("Failed to open books directory on SD card");
        return books;
    }

    // First pass: count all books and collect basic info
    File file = dir.openNextFile();
    while (file)
    {
        if (!file.isDirectory())
        {
            String filename = String(file.name());
            String fullPath = "/books/" + filename;

            BookFormat format = detectBookFormat(filename);
            if (format != FORMAT_UNKNOWN)
            {
                BookInfo book;
                book.filename = fullPath;
                book.format = format;
                book.fileSize = file.size();
                book.isValid = true;

                // Extract title from filename
                int lastDot = filename.lastIndexOf('.');
                if (lastDot > 0)
                {
                    book.title = filename.substring(0, lastDot);
                }
                else
                {
                    book.title = filename;
                }

                allBooks.push_back(book);
            }
        }
        file = dir.openNextFile();
    }
    dir.close();

    // Calculate pagination
    m_totalBookPages = (allBooks.size() + m_booksPerPage - 1) / m_booksPerPage;
    if (m_totalBookPages == 0)
        m_totalBookPages = 1;

    // Ensure current page is valid
    if (m_currentBookPage >= m_totalBookPages)
    {
        m_currentBookPage = m_totalBookPages - 1;
    }
    if (m_currentBookPage < 0)
    {
        m_currentBookPage = 0;
    }

    // Get books for current page
    int startIndex = m_currentBookPage * m_booksPerPage;
    int endIndex = std::min((int)allBooks.size(), startIndex + m_booksPerPage);

    for (int i = startIndex; i < endIndex; i++)
    {
        books.push_back(allBooks[i]);
        Serial.println("Loaded book: " + allBooks[i].title + " (" + String(allBooks[i].fileSize) + " bytes)");
    }

    Serial.println("Page " + String(m_currentBookPage + 1) + "/" + String(m_totalBookPages) + ", showing " + String(books.size()) + " of " + String(allBooks.size()) + " books");
    return books;
}

BookFormat BookScreen::detectBookFormat(const String &filename)
{
    String lowerFilename = filename;
    lowerFilename.toLowerCase();

    if (lowerFilename.endsWith(".txt"))
    {
        return FORMAT_TXT;
    }
    else if (lowerFilename.endsWith(".epub"))
    {
        return FORMAT_EPUB;
    }

    return FORMAT_UNKNOWN;
}

void BookScreen::drawHeader()
{
    // Draw status bar first
    extern void drawStatusBar();
    drawStatusBar();

    // Draw screen title based on mode
    display.m_display.setFont(&FreeMono9pt7b);
    switch (m_currentMode)
    {
    case MODE_BOOK_LIST:
        if (m_totalBookPages > 1)
        {
            String title = "Books (" + String(m_currentBookPage + 1) + "/" + String(m_totalBookPages) + ")";
            display.drawCenteredText(title.c_str(), 50, &FreeMono9pt7b);
        }
        else
        {
            display.drawCenteredText("Books", 50, &FreeMono9pt7b);
        }
        break;
    case MODE_BOOK_READER:
        // Title is drawn in drawBookReaderContent
        break;
    case MODE_BOOK_MENU:
        display.drawCenteredText("Reading Menu", 50, &FreeMono9pt7b);
        break;
    }
}

void BookScreen::drawBookListContent()
{
    if (m_availableBooks.empty())
    {
        display.m_display.setFont(&FreeMono9pt7b);

        // Check SD card status
        SDCardStatus sdStatus = getSDCardStatus();
        if (sdStatus != SD_READY)
        {
            display.drawCenteredText("SD Card Error", 150, &FreeMono9pt7b);
            display.m_display.setFont(&FreeMono9pt7b);
            display.drawCenteredText("Please insert SD card", 170, &FreeMono9pt7b);
            display.drawCenteredText("and restart device", 190, &FreeMono9pt7b);
        }
        else
        {
            display.drawCenteredText("No books found", 150, &FreeMono9pt7b);
            display.m_display.setFont(&FreeMono9pt7b);
            display.drawCenteredText("Place .txt or .epub files", 170, &FreeMono9pt7b);
            display.drawCenteredText("in SD:/books/ folder", 190, &FreeMono9pt7b);
        }
    }
    else
    {
        // Display book list
        int startY = 80;
        int lineHeight = 25;
        int availableHeight = display.m_display.height() - startY - 40;
        int maxVisible = availableHeight / lineHeight;

        // Calculate scroll offset
        int scrollOffset = 0;
        if (m_selectedBookIndex >= maxVisible)
        {
            scrollOffset = m_selectedBookIndex - maxVisible + 1;
        }

        for (int i = 0; i < std::min((int)m_availableBooks.size(), maxVisible); i++)
        {
            int bookIndex = i + scrollOffset;
            if (bookIndex >= m_availableBooks.size())
                break;

            const BookInfo &book = m_availableBooks[bookIndex];
            int y = startY + (i * lineHeight);

            // Highlight selected book
            if (bookIndex == m_selectedBookIndex)
            {
                display.m_display.fillRect(5, y - 18, display.m_display.width() - 10, lineHeight, GxEPD_BLACK);
                display.m_display.setTextColor(GxEPD_WHITE);
            }
            else
            {
                display.m_display.setTextColor(GxEPD_BLACK);
            }

            display.m_display.setFont(&FreeMono9pt7b);

            // Truncate title if too long
            String displayTitle = book.title;
            if (displayTitle.length() > 30)
            {
                displayTitle = displayTitle.substring(0, 14) + "...";
            }

            display.m_display.setCursor(10, y);
            display.m_display.print(displayTitle);

            // Draw file size (right-aligned)
            String sizeStr = formatFileSize(book.fileSize);
            int16_t x1, y1;
            uint16_t w, h;
            display.m_display.getTextBounds(sizeStr.c_str(), 0, 0, &x1, &y1, &w, &h);
            display.m_display.setCursor(display.m_display.width() - w - 10, y);
            display.m_display.print(sizeStr);

            // Reset text color
            display.m_display.setTextColor(GxEPD_BLACK);
        }
    }
}

void BookScreen::drawBookReaderContent()
{
    if (!m_bookLoaded || !m_bookStream.isOpen)
    {
        display.m_display.setFont(&FreeMono9pt7b);
        display.drawCenteredText("No book loaded", 200, &FreeMono9pt7b);
        return;
    }

    // Check memory before rendering
    size_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 5000)
    {
        Serial.println("Warning: Low memory (" + String(freeHeap) + " bytes) - skipping render");
        display.m_display.setFont(&FreeMono9pt7b);
        display.drawCenteredText("Low Memory", 200, &FreeMono9pt7b);
        return;
    }

    // Set font for book content (no title display)
    display.m_display.setFont(m_textSettings.font);
    display.m_display.setTextColor(GxEPD_BLACK);

    // Get page content using streaming
    String currentPageContent = getPageContentStreaming(m_pageInfo.currentPage);

    if (currentPageContent.length() > 0)
    {
        // Fixed text rendering for 240x416 display
        int yPos = 57;          // Start below status bar + font baseline offset (45 + 12)
        int xPos = 6;           // Left margin for readability
        int maxLineWidth = 210; // Reduced from 228 to prevent text overflow
        int maxY = 406;         // 416 - 10 (leave small bottom margin)

        // Use dynamic line height based on current font
        int lineHeight = m_textSettings.lineHeight;

        // Font baseline adjustment for FreeMono9pt7b
        // GFX fonts use baseline positioning, so we need proper offset

        // Improved text rendering with proper line spacing and word wrapping
        int contentLength = currentPageContent.length();
        int currentPos = 0;
        int linesDrawn = 0;
        const int maxLines = (maxY - yPos) / lineHeight;

        // Process text word by word with proper spacing
        while (currentPos < contentLength && linesDrawn < maxLines)
        {
            String currentLine = "";
            int lineStartPos = currentPos;
            bool lineComplete = false;

            // Build line word by word until it would exceed width
            while (currentPos < contentLength && !lineComplete)
            {
                // Skip leading whitespace at start of line
                while (currentPos < contentLength &&
                       (currentPageContent.charAt(currentPos) == ' ' ||
                        currentPageContent.charAt(currentPos) == '\t'))
                {
                    currentPos++;
                }

                if (currentPos >= contentLength)
                    break;

                // Handle explicit newlines
                if (currentPageContent.charAt(currentPos) == '\n')
                {
                    currentPos++;
                    lineComplete = true;
                    break;
                }

                // Find next word
                int wordStart = currentPos;
                while (currentPos < contentLength &&
                       currentPageContent.charAt(currentPos) != ' ' &&
                       currentPageContent.charAt(currentPos) != '\n' &&
                       currentPageContent.charAt(currentPos) != '\t')
                {
                    currentPos++;
                }

                String word = currentPageContent.substring(wordStart, currentPos);
                if (word.isEmpty())
                    break;

                // Test if word fits on current line
                String testLine = currentLine;
                if (!currentLine.isEmpty())
                {
                    testLine += " ";
                }
                testLine += word;

                // Check if test line fits within width
                int16_t x1, y1;
                uint16_t w, h;
                display.m_display.getTextBounds(testLine.c_str(), 0, 0, &x1, &y1, &w, &h);

                if (w <= maxLineWidth)
                {
                    // Word fits, add to current line
                    currentLine = testLine;
                }
                else
                {
                    // Word doesn't fit
                    if (currentLine.isEmpty())
                    {
                        // Single word too long, force it on line anyway
                        currentLine = word;
                        lineComplete = true;
                    }
                    else
                    {
                        // Start new line with this word
                        currentPos = wordStart;
                        lineComplete = true;
                    }
                }
            }

            // Draw the completed line with proper baseline positioning
            if (!currentLine.isEmpty() && yPos <= maxY)
            {
                // Debug logging for line rendering
                Serial.print("Line ");
                Serial.print(linesDrawn + 1);
                Serial.print(" at Y=");
                Serial.print(yPos);
                Serial.print(": '");
                Serial.print(currentLine);
                Serial.println("'");

                display.m_display.setCursor(xPos, yPos);
                display.m_display.print(currentLine);
            }

            // Move to next line position
            yPos += lineHeight;
            linesDrawn++;

            // Yield periodically
            if (linesDrawn % 5 == 0)
            {
                yield();
            }
        }

        // Clear content string to free memory
        currentPageContent = "";

        // Final yield to ensure rendering completes
        yield();
    }
}

void BookScreen::drawBookMenuDialog()
{
    // Draw semi-transparent background
    display.m_display.fillRect(20, 80, display.m_display.width() - 40, 200, GxEPD_WHITE);
    display.m_display.drawRect(20, 80, display.m_display.width() - 40, 200, GxEPD_BLACK);

    display.m_display.setFont(&FreeMono9pt7b);

    // Menu options
    int yPos = 110;
    int lineHeight = 25;

    for (int i = 0; i < m_bookMenu.options.size(); i++)
    {
        // Highlight selected option
        if (i == m_bookMenu.selectedOption)
        {
            display.m_display.fillRect(25, yPos - 18, display.m_display.width() - 50, lineHeight, GxEPD_BLACK);
            display.m_display.setTextColor(GxEPD_WHITE);
        }
        else
        {
            display.m_display.setTextColor(GxEPD_BLACK);
        }

        display.m_display.setCursor(30, yPos);
        display.m_display.print(m_bookMenu.options[i]);
        yPos += lineHeight;

        // Reset text color
        display.m_display.setTextColor(GxEPD_BLACK);
    }
}

void BookScreen::drawLoadingIndicator()
{
    display.m_display.setFont(&FreeMono9pt7b);
    display.drawCenteredText("Loading books...", 150, &FreeMono9pt7b);
}

void BookScreen::drawStatusBar()
{
    // This will be handled by the main status bar function
}

bool BookScreen::loadTxtBook(const String &filepath)
{
    // Check if SD card is ready
    if (getSDCardStatus() != SD_READY)
    {
        Serial.println("SD card not ready for reading");
        return false;
    }

    File file = SD.open(filepath);
    if (!file)
    {
        Serial.println("Failed to open TXT file: " + filepath);
        return false;
    }

    // Read file in smaller chunks to prevent memory issues
    m_bookContent = "";

    // Don't pre-allocate for very large files to avoid memory overflow
    size_t fileSize = file.size();
    if (fileSize < 50000) // Only pre-allocate for smaller files
    {
        m_bookContent.reserve(fileSize);
    }

    const size_t CHUNK_SIZE = 128; // Smaller chunks to prevent crashes
    char buffer[CHUNK_SIZE + 1];
    size_t totalRead = 0;

    while (file.available())
    {
        size_t bytesRead = file.readBytes(buffer, CHUNK_SIZE);
        buffer[bytesRead] = '\0';
        m_bookContent += buffer;
        totalRead += bytesRead;

        // Yield more frequently to prevent watchdog timeout
        if (totalRead % 512 == 0)
        {
            yield();
            Serial.print("."); // Progress indicator
        }

        // Safety check for very large files
        if (totalRead > 200000) // Limit to ~200KB
        {
            Serial.println("\nFile too large, truncating at 200KB");
            break;
        }
    }
    file.close();

    if (m_bookContent.isEmpty())
    {
        Serial.println("Failed to read TXT file");
        return false;
    }

    Serial.println("Loaded TXT book: " + String(m_bookContent.length()) + " characters");
    m_currentBookInfo.isValid = true;
    return true;
}

bool BookScreen::loadEpubBook(const String &filepath)
{
    // Check if SD card is ready
    if (getSDCardStatus() != SD_READY)
    {
        Serial.println("SD card not ready for reading");
        return false;
    }

    // Create ePub parser instance
    EpubParser epubParser;
    
    Serial.println("Parsing EPUB file: " + filepath);
    
    // Parse the ePub file
    if (!epubParser.parseEpub(filepath))
    {
        Serial.println("Failed to parse EPUB file: " + filepath);
        return false;
    }
    
    // Get metadata and update book info
    EpubMetadata metadata = epubParser.getMetadata();
    m_currentBookInfo.title = metadata.title.isEmpty() ? extractTitleFromFilename(filepath) : metadata.title;
    m_currentBookInfo.author = metadata.author;
    m_currentBookInfo.publisher = metadata.publisher;
    m_currentBookInfo.language = metadata.language;
    m_currentBookInfo.coverImagePath = metadata.coverImagePath;
    
    // Get chapters
    std::vector<EpubChapter> chapters = epubParser.getChapters();
    m_currentBookInfo.totalChapters = chapters.size();
    
    if (chapters.empty())
    {
        Serial.println("No chapters found in EPUB file");
        return false;
    }
    
    Serial.println("Found " + String(chapters.size()) + " chapters in EPUB");
    Serial.println("Title: " + m_currentBookInfo.title);
    Serial.println("Author: " + m_currentBookInfo.author);
    
    // Combine all chapter content
    m_bookContent = "";
    m_currentBookInfo.chapters.clear();
    m_currentBookInfo.chapterSizes.clear();
    
    for (size_t i = 0; i < chapters.size(); i++)
    {
        const EpubChapter& chapter = chapters[i];
        
        // Add chapter title as a header
        if (!chapter.title.isEmpty())
        {
            m_bookContent += "\n\n=== " + chapter.title + " ===\n\n";
        }
        
        // Store chapter info
        m_currentBookInfo.chapters.push_back(chapter.title);
        size_t chapterStartPos = m_bookContent.length();
        
        // Add chapter content
        m_bookContent += chapter.content;
        m_bookContent += "\n\n";
        
        // Store chapter size
        size_t chapterSize = m_bookContent.length() - chapterStartPos;
        m_currentBookInfo.chapterSizes.push_back(chapterSize);
        
        Serial.println("Chapter " + String(i + 1) + ": " + chapter.title + " (" + String(chapterSize) + " chars)");
        
        // Yield to prevent watchdog timeout
        yield();
    }
    
    // Clean up the combined content
    m_bookContent.trim();
    
    // Remove excessive line breaks
    while (m_bookContent.indexOf("\n\n\n") >= 0)
    {
        m_bookContent.replace("\n\n\n", "\n\n");
    }
    
    Serial.println("EPUB content processed, final length: " + String(m_bookContent.length()) + " characters");
    Serial.println("Total chapters: " + String(m_currentBookInfo.totalChapters));
    
    m_currentBookInfo.isValid = true;
    return true;
}

void BookScreen::calculatePages()
{
    // Pagination is now handled by the streaming approach in loadBook()
}

String BookScreen::extractTextFromPage(const String &content, int startPos, int maxChars)
{
    if (startPos >= content.length())
    {
        return "";
    }

    int endPos = std::min(startPos + maxChars, (int)content.length());
    return content.substring(startPos, endPos);
}

int BookScreen::calculateWordsPerPage()
{
    // Calculate based on fixed 240x416 display layout
    int displayWidth = 210;  // Reduced from 228 to prevent text overflow
    int displayHeight = 349; // 406 - 57 (from baseline-adjusted top margin to max Y)

    // Dynamic character width and line height based on current font
    int charWidth, lineHeight;
    if (m_textSettings.font == &FreeMono9pt7b)
    {
        charWidth = 6;   // Character width for FreeMono 9pt (monospace)
        lineHeight = 14; // Line height for FreeMono 9pt
    }
    else if (m_textSettings.font == &FreeMono12pt7b)
    {
        charWidth = 8;   // Character width for FreeMono 12pt (monospace)
        lineHeight = 18; // Line height for FreeMono 12pt
    }
    else if (m_textSettings.font == &FreeMono18pt7b)
    {
        charWidth = 12;  // Character width for FreeMono 18pt (monospace)
        lineHeight = 26; // Line height for FreeMono 18pt
    }
    else // Default fallback
    {
        charWidth = 6;   // Standard character width
        lineHeight = 16; // Standard line height
    }

    int charsPerLine = displayWidth / charWidth;
    int linesPerPage = displayHeight / lineHeight;

    // Estimate words (average 5 characters per word)
    return (charsPerLine * linesPerPage) / 5;
}

void BookScreen::initializeTextSettings()
{
    m_textSettings.font = &FreeMono9pt7b;
    m_textSettings.fontSize = 9;    // Small readable font
    m_textSettings.lineHeight = 14; // Comfortable line spacing
    m_textSettings.margin = 5;
    m_textSettings.wordsPerPage = calculateWordsPerPage();
}

void BookScreen::ensureValidBookSelection()
{
    if (m_availableBooks.empty())
    {
        m_selectedBookIndex = 0;
    }
    else if (m_selectedBookIndex >= m_availableBooks.size())
    {
        m_selectedBookIndex = m_availableBooks.size() - 1;
    }
    else if (m_selectedBookIndex < 0)
    {
        m_selectedBookIndex = 0;
    }
}

void BookScreen::scrollToSelection()
{
    // This is handled in drawBookListContent() with scroll offset calculation
}

void BookScreen::initializeBookMenu()
{
    m_bookMenu.isVisible = false;
    m_bookMenu.selectedOption = 0;
    m_bookMenu.title = "Reading Menu";
    m_bookMenu.options.clear();
    m_bookMenu.options.push_back("Increase Font");
    m_bookMenu.options.push_back("Decrease Font");
    m_bookMenu.options.push_back("Return to Reading");
    m_bookMenu.options.push_back("Close Book");
}

String BookScreen::formatFileSize(size_t bytes)
{
    if (bytes < 1024)
    {
        return String(bytes) + "B";
    }
    else if (bytes < 1024 * 1024)
    {
        return String(bytes / 1024) + "KB";
    }
    else if (bytes < 1024 * 1024 * 1024)
    {
        return String(bytes / (1024 * 1024)) + "MB";
    }
    else
    {
        return String(bytes / (1024 * 1024 * 1024)) + "GB";
    }
}

String BookScreen::extractTitleFromFilename(const String &filepath)
{
    int lastSlash = filepath.lastIndexOf('/');
    int lastDot = filepath.lastIndexOf('.');
    
    if (lastSlash >= 0 && lastDot > lastSlash)
    {
        return filepath.substring(lastSlash + 1, lastDot);
    }
    else if (lastSlash >= 0)
    {
        return filepath.substring(lastSlash + 1);
    }
    else if (lastDot > 0)
    {
        return filepath.substring(0, lastDot);
    }
    else
    {
        return filepath;
    }
}

String BookScreen::getBookIcon(const BookInfo &book)
{
    switch (book.format)
    {
    case FORMAT_TXT:
        return "[TXT]";
    case FORMAT_EPUB:
        return "[EPUB]";
    default:
        return "[BOOK]";
    }
}

// Streaming book management implementation
bool BookScreen::initializeBookStream(const String &filepath)
{
    // Initialize stream structure
    m_bookStream.filepath = filepath;
    m_bookStream.fileSize = getFileSize(filepath);
    m_bookStream.chunkSize = CHUNK_SIZE;
    m_bookStream.currentPosition = 0;
    m_bookStream.pagePositions.clear();
    m_bookStream.currentChunk.isLoaded = false;
    m_bookStream.isOpen = true;

    Serial.println("Initialized book stream for: " + filepath);
    Serial.println("File size: " + String(m_bookStream.fileSize) + " bytes");

    return true;
}

void BookScreen::closeBookStream()
{
    m_bookStream.pagePositions.clear();
    std::vector<size_t>().swap(m_bookStream.pagePositions);
    m_bookStream.currentChunk.content = "";
    m_bookStream.currentChunk.isLoaded = false;
    m_bookStream.isOpen = false;

    Serial.println("Book stream closed");
}

bool BookScreen::loadChunkAtPosition(size_t position)
{
    if (!m_bookStream.isOpen || position >= m_bookStream.fileSize)
    {
        return false;
    }

    // Check if chunk is already loaded
    if (m_bookStream.currentChunk.isLoaded &&
        position >= m_bookStream.currentChunk.filePosition &&
        position < m_bookStream.currentChunk.filePosition + m_bookStream.currentChunk.chunkSize)
    {
        return true; // Already have the right chunk
    }

    // Load new chunk
    size_t chunkStart = (position / CHUNK_SIZE) * CHUNK_SIZE;
    size_t chunkSize = std::min(CHUNK_SIZE, m_bookStream.fileSize - chunkStart);

    String content = readChunkFromFile(chunkStart, chunkSize);
    if (content.isEmpty())
    {
        return false;
    }

    m_bookStream.currentChunk.filePosition = chunkStart;
    m_bookStream.currentChunk.chunkSize = chunkSize;
    m_bookStream.currentChunk.content = content;
    m_bookStream.currentChunk.isLoaded = true;

    return true;
}

String BookScreen::readChunkFromFile(size_t position, size_t size)
{
    if (getSDCardStatus() != SD_READY)
    {
        return "";
    }

    File file = SD.open(m_bookStream.filepath);
    if (!file)
    {
        Serial.println("Failed to open file for chunk reading");
        return "";
    }

    if (!file.seek(position))
    {
        file.close();
        return "";
    }

    String content = "";
    content.reserve(size + 1);

    char buffer[128];
    size_t remaining = size;

    while (remaining > 0 && file.available())
    {
        size_t toRead = std::min(remaining, (size_t)127);
        size_t bytesRead = file.readBytes(buffer, toRead);
        buffer[bytesRead] = '\0';
        content += buffer;
        remaining -= bytesRead;

        if (bytesRead < toRead)
        {
            break; // End of file
        }
    }

    file.close();
    return content;
}

// Reading History Management Implementation
bool BookScreen::saveReadingHistory()
{
    if (!m_bookLoaded || m_bookStream.filepath.isEmpty())
    {
        return false;
    }

    // Load existing history
    std::vector<ReadingHistory> allHistory = getAllReadingHistory();

    // Create or update current book's history
    ReadingHistory currentHistory;
    currentHistory.filepath = m_bookStream.filepath;
    currentHistory.filename = m_currentBookInfo.filename;
    currentHistory.lastPage = m_pageInfo.currentPage;
    currentHistory.lastPosition = (m_pageInfo.currentPage < m_bookStream.pagePositions.size()) ? m_bookStream.pagePositions[m_pageInfo.currentPage] : 0;
    currentHistory.readingProgress = calculateReadingProgress();

    // Get current timestamp
    // Note: You might want to use RTC for actual timestamp
    currentHistory.lastReadTime = String(millis());

    // Find and update existing entry or add new one
    bool found = false;
    for (auto &history : allHistory)
    {
        if (history.filepath == currentHistory.filepath)
        {
            history = currentHistory;
            found = true;
            break;
        }
    }

    if (!found)
    {
        allHistory.push_back(currentHistory);
    }

    // Limit history to last 50 books
    if (allHistory.size() > 50)
    {
        allHistory.erase(allHistory.begin(), allHistory.begin() + (allHistory.size() - 50));
    }

    // Save to JSON
    JsonDocument doc;
    JsonArray historyArray = doc["reading_history"].to<JsonArray>();

    for (const auto &history : allHistory)
    {
        JsonObject historyObj = historyArray.add<JsonObject>();
        historyObj["filepath"] = history.filepath;
        historyObj["filename"] = history.filename;
        historyObj["last_page"] = history.lastPage;
        historyObj["last_position"] = history.lastPosition;
        historyObj["last_read_time"] = history.lastReadTime;
        historyObj["reading_progress"] = history.readingProgress;
    }

    String jsonString;
    serializeJson(doc, jsonString);

    bool success = writeJSON(HISTORY_FILE, jsonString);
    if (success)
    {
        Serial.println("Reading history saved for: " + currentHistory.filename);
    }
    else
    {
        Serial.println("Failed to save reading history");
    }

    return success;
}

bool BookScreen::loadReadingHistory(const String &filepath)
{
    ReadingHistory history = getReadingHistory(filepath);

    if (history.filepath.isEmpty())
    {
        return false; // No history found
    }

    // Apply the saved reading position
    if (history.lastPage >= 0 && history.lastPage < m_pageInfo.totalPages)
    {
        m_pageInfo.currentPage = history.lastPage;
        Serial.println("Restored reading position: page " + String(history.lastPage + 1) +
                       " (" + String(history.readingProgress * 100, 1) + "% complete)");
        return true;
    }

    return false;
}

ReadingHistory BookScreen::getReadingHistory(const String &filepath)
{
    ReadingHistory emptyHistory;
    emptyHistory.filepath = "";

    std::vector<ReadingHistory> allHistory = getAllReadingHistory();

    for (const auto &history : allHistory)
    {
        if (history.filepath == filepath)
        {
            return history;
        }
    }

    return emptyHistory;
}

std::vector<ReadingHistory> BookScreen::getAllReadingHistory()
{
    std::vector<ReadingHistory> history;

    if (!fileExists(HISTORY_FILE))
    {
        return history; // Return empty vector if no history file
    }

    String jsonString = readJSON(HISTORY_FILE);
    if (jsonString.isEmpty())
    {
        return history;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonString);

    if (error)
    {
        Serial.println("Failed to parse reading history JSON: " + String(error.c_str()));
        return history;
    }

    JsonArray historyArray = doc["reading_history"];

    for (JsonObject historyObj : historyArray)
    {
        ReadingHistory item;
        item.filepath = historyObj["filepath"].as<String>();
        item.filename = historyObj["filename"].as<String>();
        item.lastPage = historyObj["last_page"].as<int>();
        item.lastPosition = historyObj["last_position"].as<size_t>();
        item.lastReadTime = historyObj["last_read_time"].as<String>();
        item.readingProgress = historyObj["reading_progress"].as<float>();

        history.push_back(item);
    }

    return history;
}

bool BookScreen::clearReadingHistory(const String &filepath)
{
    if (filepath.isEmpty())
    {
        // Clear all history
        bool success = deleteFile(HISTORY_FILE);
        if (success)
        {
            Serial.println("All reading history cleared");
        }
        return success;
    }
    else
    {
        // Clear specific book's history
        std::vector<ReadingHistory> allHistory = getAllReadingHistory();

        auto it = std::remove_if(allHistory.begin(), allHistory.end(),
                                 [&filepath](const ReadingHistory &h)
                                 {
                                     return h.filepath == filepath;
                                 });

        if (it != allHistory.end())
        {
            allHistory.erase(it, allHistory.end());

            // Save updated history
            JsonDocument doc;
            JsonArray historyArray = doc["reading_history"].to<JsonArray>();

            for (const auto &history : allHistory)
            {
                JsonObject historyObj = historyArray.add<JsonObject>();
                historyObj["filepath"] = history.filepath;
                historyObj["filename"] = history.filename;
                historyObj["last_page"] = history.lastPage;
                historyObj["last_position"] = history.lastPosition;
                historyObj["last_read_time"] = history.lastReadTime;
                historyObj["reading_progress"] = history.readingProgress;
            }

            String jsonString;
            serializeJson(doc, jsonString);

            bool success = writeJSON(HISTORY_FILE, jsonString);
            if (success)
            {
                Serial.println("Reading history cleared for: " + filepath);
            }
            return success;
        }
    }

    return false;
}

float BookScreen::calculateReadingProgress() const
{
    if (!m_bookLoaded || m_pageInfo.totalPages <= 0)
    {
        return 0.0f;
    }

    return (float)(m_pageInfo.currentPage + 1) / (float)m_pageInfo.totalPages;
}

bool BookScreen::buildPageIndex()
{
    m_bookStream.pagePositions.clear();
    m_bookStream.pagePositions.push_back(0); // First page starts at beginning

    // Calculate page size parameters (fixed for 240x416 display)
    int displayHeight = 349; // 406 - 57 (from baseline-adjusted top margin to max Y)

    // Use dynamic font parameters
    int lineHeight, charWidth;
    if (m_textSettings.font == &FreeSans9pt7b)
    {
        lineHeight = 14;
        charWidth = 5;
    }
    else if (m_textSettings.font == &FreeSans12pt7b)
    {
        lineHeight = 18;
        charWidth = 7;
    }
    else if (m_textSettings.font == &FreeSans18pt7b)
    {
        lineHeight = 26;
        charWidth = 10;
    }
    else
    {
        lineHeight = 16;
        charWidth = 6;
    }

    int linesPerPage = displayHeight / lineHeight;
    int approxCharsPerLine = 210 / charWidth; // 210px width / char width (reduced to prevent overflow)
    int charsPerPage = linesPerPage * approxCharsPerLine * 0.5;

    // Ensure minimum and maximum page sizes
    charsPerPage = std::max(200, std::min(charsPerPage, 1500));

    Serial.println("Building initial page index - chars per page: " + String(charsPerPage));
    Serial.println("Lines per page: " + String(linesPerPage) + ", Line height: " + String(lineHeight));

    // Build only initial pages for fast loading (first 10-20 pages)
    size_t currentPosition = 0;
    const int initialPagesToLoad = 20; // Load only first 20 pages initially
    int pagesBuilt = 0;

    while (currentPosition < m_bookStream.fileSize && pagesBuilt < initialPagesToLoad)
    {
        size_t nextPagePos = findNextPageBreak(currentPosition + charsPerPage);
        if (nextPagePos <= currentPosition || nextPagePos >= m_bookStream.fileSize)
        {
            break;
        }

        m_bookStream.pagePositions.push_back(nextPagePos);
        currentPosition = nextPagePos;
        pagesBuilt++;

        // Yield occasionally
        if (pagesBuilt % 5 == 0)
        {
            yield();
        }
    }

    // Estimate total pages based on file size and average page size
    if (pagesBuilt > 1)
    {
        size_t avgPageSize = currentPosition / pagesBuilt;
        int estimatedTotalPages = (m_bookStream.fileSize / avgPageSize) + 1;

        // Store the estimated total for UI purposes
        m_estimatedTotalPages = estimatedTotalPages;

        Serial.println("Built initial " + String(pagesBuilt) + " pages");
        Serial.println("Estimated total pages: " + String(estimatedTotalPages));
    }
    else
    {
        m_estimatedTotalPages = 1;
        Serial.println("Built minimal page index");
    }

    // Calculate memory usage
    size_t memoryUsed = m_bookStream.pagePositions.size() * sizeof(size_t);
    Serial.println("Memory used: ~" + String(memoryUsed) + " bytes for initial page index");

    return true;
}

bool BookScreen::expandPageIndex(int targetPage)
{
    // Check if we already have enough pages
    if (targetPage < m_bookStream.pagePositions.size())
    {
        return true;
    }

    // Calculate how many more pages we need
    int currentPages = m_bookStream.pagePositions.size();
    int pagesToAdd = targetPage - currentPages + 5; // Add a few extra

    Serial.println("Expanding page index from " + String(currentPages) + " to " + String(targetPage + 5) + " pages");

    // Calculate page size parameters (fixed for 240x416 display)
    int displayHeight = 349; // 406 - 57 (from baseline-adjusted top margin to max Y)

    // Use dynamic font parameters
    int lineHeight, charWidth;
    if (m_textSettings.font == &FreeSans9pt7b)
    {
        lineHeight = 14;
        charWidth = 5;
    }
    else if (m_textSettings.font == &FreeSans12pt7b)
    {
        lineHeight = 18;
        charWidth = 7;
    }
    else if (m_textSettings.font == &FreeSans18pt7b)
    {
        lineHeight = 26;
        charWidth = 10;
    }
    else
    {
        lineHeight = 16;
        charWidth = 6;
    }

    int linesPerPage = displayHeight / lineHeight;
    int approxCharsPerLine = 210 / charWidth; // 210px width / char width (reduced to prevent overflow)
    int charsPerPage = linesPerPage * approxCharsPerLine * 0.5;
    charsPerPage = std::max(200, std::min(charsPerPage, 1500));

    // Start from the last known position
    size_t currentPosition = m_bookStream.pagePositions.back();
    int pagesAdded = 0;

    while (currentPosition < m_bookStream.fileSize && pagesAdded < pagesToAdd)
    {
        size_t nextPagePos = findNextPageBreak(currentPosition + charsPerPage);
        if (nextPagePos <= currentPosition || nextPagePos >= m_bookStream.fileSize)
        {
            // Reached end of file
            m_estimatedTotalPages = m_bookStream.pagePositions.size();
            m_pageInfo.totalPages = m_estimatedTotalPages;
            break;
        }

        m_bookStream.pagePositions.push_back(nextPagePos);
        currentPosition = nextPagePos;
        pagesAdded++;

        // Yield occasionally to prevent freezing
        if (pagesAdded % 3 == 0)
        {
            yield();
        }
    }

    // Update total pages if we've built more than estimated
    if (m_bookStream.pagePositions.size() > m_estimatedTotalPages)
    {
        m_estimatedTotalPages = m_bookStream.pagePositions.size();
        m_pageInfo.totalPages = m_estimatedTotalPages;
    }

    Serial.println("Added " + String(pagesAdded) + " pages, total now: " + String(m_bookStream.pagePositions.size()));

    return targetPage < m_bookStream.pagePositions.size();
}

size_t BookScreen::findNextPageBreak(size_t startPosition)
{
    if (startPosition >= m_bookStream.fileSize)
    {
        return m_bookStream.fileSize;
    }

    // Load chunk containing the target position
    if (!loadChunkAtPosition(startPosition))
    {
        return startPosition;
    }

    // Find a good break point (space, newline, or sentence end)
    size_t searchStart = startPosition - m_bookStream.currentChunk.filePosition;
    size_t searchEnd = std::min(searchStart + 200, (size_t)m_bookStream.currentChunk.content.length());

    // Look for paragraph break first
    for (size_t i = searchStart; i < searchEnd; i++)
    {
        if (m_bookStream.currentChunk.content.charAt(i) == '\n' &&
            i + 1 < m_bookStream.currentChunk.content.length() &&
            m_bookStream.currentChunk.content.charAt(i + 1) == '\n')
        {
            // Start next page after the double newline
            return m_bookStream.currentChunk.filePosition + i + 2;
        }
    }

    // Look for sentence end
    for (size_t i = searchStart; i < searchEnd; i++)
    {
        char c = m_bookStream.currentChunk.content.charAt(i);
        if (c == '.' || c == '!' || c == '?')
        {
            if (i + 1 < m_bookStream.currentChunk.content.length() &&
                m_bookStream.currentChunk.content.charAt(i + 1) == ' ')
            {
                // Start next page after the space following punctuation
                return m_bookStream.currentChunk.filePosition + i + 2;
            }
        }
    }

    // Look for word boundary - don't skip the space
    for (size_t i = searchStart; i < searchEnd; i++)
    {
        if (m_bookStream.currentChunk.content.charAt(i) == ' ')
        {
            // Start next page at the space (don't skip it)
            return m_bookStream.currentChunk.filePosition + i;
        }
    }

    // Fallback to exact position
    return startPosition;
}

String BookScreen::getPageContentStreaming(int pageNumber)
{
    if (pageNumber < 0 || pageNumber >= m_bookStream.pagePositions.size())
    {
        return "";
    }

    size_t pageStart = m_bookStream.pagePositions[pageNumber];
    size_t pageEnd = (pageNumber + 1 < m_bookStream.pagePositions.size()) ? m_bookStream.pagePositions[pageNumber + 1] : m_bookStream.fileSize;

    size_t pageLength = pageEnd - pageStart;

    // Debug logging for page boundaries
    Serial.print("Page ");
    Serial.print(pageNumber);
    Serial.print(": start=");
    Serial.print(pageStart);
    Serial.print(", end=");
    Serial.print(pageEnd);
    Serial.print(", length=");
    Serial.println(pageLength);

    // More aggressive page size limiting for better performance
    if (pageLength > 2048) // Reduced from 4096 to 2048
    {
        pageLength = 2048;
        pageEnd = pageStart + pageLength;
    }

    String content = "";
    content.reserve(pageLength + 1);

    size_t currentPos = pageStart;
    size_t bytesRead = 0;
    const size_t maxBytesToRead = 2048; // Safety limit

    while (currentPos < pageEnd && bytesRead < maxBytesToRead)
    {
        if (!loadChunkAtPosition(currentPos))
        {
            break;
        }

        size_t chunkOffset = currentPos - m_bookStream.currentChunk.filePosition;
        size_t availableInChunk = m_bookStream.currentChunk.content.length() - chunkOffset;
        size_t toCopy = std::min({availableInChunk, pageEnd - currentPos, maxBytesToRead - bytesRead});

        if (toCopy == 0)
        {
            break;
        }

        content += m_bookStream.currentChunk.content.substring(chunkOffset, chunkOffset + toCopy);
        currentPos += toCopy;
        bytesRead += toCopy;

        // Yield every 512 bytes to prevent freezing
        if (bytesRead % 512 == 0)
        {
            yield();
        }
    }

    return content;
}