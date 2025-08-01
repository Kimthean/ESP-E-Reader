#include "book_screen.h"
#include "../../../include/storage.h"
#include "../../../include/power.h"
#include "../../../include/display.h"
#include "../../../include/epub_parser.h"
#include <SD.h>
#include <algorithm>
#include <ArduinoJson.h>

// Static constants
const String BookScreen::HISTORY_FILE = "/config/reading_history.json";

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

    // Draw progress dialog overlay if visible
    if (m_progressDialog.isVisible)
    {
        drawProgressDialog();
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
    // Handle progress dialog abort
    if (m_progressDialog.isVisible && m_progressDialog.canAbort)
    {
        m_progressDialog.abortRequested = true;
        return;
    }

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
    // Handle progress dialog abort
    if (m_progressDialog.isVisible && m_progressDialog.canAbort)
    {
        m_progressDialog.abortRequested = true;
        return;
    }

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
    // Handle progress dialog abort
    if (m_progressDialog.isVisible && m_progressDialog.canAbort)
    {
        m_progressDialog.abortRequested = true;
        return;
    }

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
    // Handle progress dialog abort
    if (m_progressDialog.isVisible && m_progressDialog.canAbort)
    {
        m_progressDialog.abortRequested = true;
        return;
    }

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

    // Handle different book formats
    if (m_currentBookInfo.format == FORMAT_EPUB)
    {
        Serial.println("Loading EPUB file: " + String(m_currentBookInfo.fileSize) + " bytes");
        return loadEpubBook(filepath);
    }
    else
    {
        // Use streaming approach for text files
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
    if (!m_bookLoaded)
    {
        return false;
    }

    // For streaming books (TXT files)
    if (m_bookStream.isOpen)
    {
        // Check if we need to expand the page index
        if (m_pageInfo.currentPage + 1 >= m_bookStream.pagePositions.size())
        {
            if (!expandPageIndex(m_pageInfo.currentPage + 10)) // Expand 10 pages ahead
            {
                return false; // Reached end of book
            }
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
    if (!m_bookLoaded || m_pageInfo.currentPage <= 0)
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

    // For streaming books (TXT files)
    if (m_bookStream.isOpen)
    {
        // Check if we need to expand the page index for the target page
        if (pageNumber >= m_bookStream.pagePositions.size())
        {
            if (!expandPageIndex(pageNumber + 5)) // Expand a bit beyond target
            {
                return false; // Cannot reach that page
            }
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
    if (m_bookLoaded)
    {
        if (m_bookStream.isOpen)
        {
            // Rebuild page index with new font settings for streaming books
            buildPageIndex();
            m_pageInfo.totalPages = m_bookStream.pagePositions.size();
        }
        else if (!m_bookContent.isEmpty())
        {
            // Recalculate pagination for loaded content (ePub files)
            int wordsPerPage = calculateWordsPerPage();
            int charsPerPage = wordsPerPage * 5;
            m_pageInfo.totalPages = (m_bookContent.length() + charsPerPage - 1) / charsPerPage;
        }

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
    if (m_bookLoaded)
    {
        if (m_bookStream.isOpen)
        {
            // Rebuild page index with new font settings for streaming books
            buildPageIndex();
            m_pageInfo.totalPages = m_bookStream.pagePositions.size();
        }
        else if (!m_bookContent.isEmpty())
        {
            // Recalculate pagination for loaded content (ePub files)
            int wordsPerPage = calculateWordsPerPage();
            int charsPerPage = wordsPerPage * 5;
            m_pageInfo.totalPages = (m_bookContent.length() + charsPerPage - 1) / charsPerPage;
        }

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
    if (m_bookLoaded)
    {
        if (m_bookStream.isOpen)
        {
            // Rebuild page index with new font settings for streaming books
            buildPageIndex();
            m_pageInfo.totalPages = m_bookStream.pagePositions.size();
        }
        else if (!m_bookContent.isEmpty())
        {
            // Recalculate pagination for loaded content (ePub files)
            int wordsPerPage = calculateWordsPerPage();
            int charsPerPage = wordsPerPage * 5;
            m_pageInfo.totalPages = (m_bookContent.length() + charsPerPage - 1) / charsPerPage;
        }

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
    if (!m_bookLoaded)
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

    // Get page content based on book type
    String currentPageContent;
    if (m_bookStream.isOpen)
    {
        // Streaming mode for TXT files
        currentPageContent = getPageContentStreaming(m_pageInfo.currentPage);
    }
    else if (!m_bookContent.isEmpty())
    {
        // Loaded content mode for ePub files
        currentPageContent = getPageContentLoaded(m_pageInfo.currentPage);
    }
    else
    {
        display.m_display.setFont(&FreeMono9pt7b);
        display.drawCenteredText("No content available", 200, &FreeMono9pt7b);
        return;
    }

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

    // Check available memory before loading
    size_t freeHeap = ESP.getFreeHeap();
    Serial.println("Free heap before EPUB loading: " + String(freeHeap) + " bytes");

    if (freeHeap < 20000) // Need at least 20KB free for EPUB streaming
    {
        Serial.println("Insufficient memory to load EPUB book");
        return false;
    }

    // Initialize EPUB streaming
    if (!initializeEpubStream(filepath))
    {
        Serial.println("Failed to initialize EPUB streaming");
        return false;
    }

    // Set up book loading state
    m_currentBookInfo.isValid = true;
    m_bookLoaded = true;

    // Try to load reading history and restore last position
    if (loadReadingHistory(filepath))
    {
        Serial.println("Restored reading position from history");
    }
    else
    {
        Serial.println("No reading history found, starting from beginning");
    }

    Serial.println("EPUB book loaded successfully: " + m_currentBookInfo.title);
    Serial.println("Total pages: " + String(m_pageInfo.totalPages));
    Serial.println("Current page: " + String(m_pageInfo.currentPage + 1));

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

    // Initialize EPUB-specific fields
    m_bookStream.isEpubStream = false;
    m_bookStream.tempDir = "";
    m_bookStream.chapterRefs.clear();
    m_bookStream.currentChapterIndex = 0;
    m_bookStream.globalPageOffset = 0;

    Serial.println("Initialized book stream for: " + filepath);
    Serial.println("File size: " + String(m_bookStream.fileSize) + " bytes");

    return true;
}

bool BookScreen::initializeEpubStream(const String &filepath)
{
    // Close any existing stream
    closeBookStream();

    // Create EPUB parser instance
    EpubParser epubParser;

    Serial.println("Initializing EPUB stream: " + filepath);

    // Create extraction path based on EPUB filename
    String fileName = filepath;
    int lastSlash = fileName.lastIndexOf('/');
    if (lastSlash >= 0)
    {
        fileName = fileName.substring(lastSlash + 1);
    }
    int lastDot = fileName.lastIndexOf('.');
    if (lastDot >= 0)
    {
        fileName = fileName.substring(0, lastDot);
    }

    // Limit filename length to prevent filesystem path issues
    // ESP32 has path length limitations, so we'll use a shortened name
    String shortName = fileName;
    if (shortName.length() > 50)
    {
        // Use first 30 chars + hash of full name for uniqueness
        shortName = fileName.substring(0, 30);

        // Simple hash function to ensure uniqueness
        unsigned long hash = 0;
        for (int i = 0; i < fileName.length(); i++)
        {
            hash = hash * 31 + fileName.charAt(i);
        }
        shortName += "_" + String(hash, HEX);
    }

    String extractPath = "/extracted_epubs/" + shortName;

    // Show progress dialog for extraction
    showProgressDialog("Extracting EPUB", "Preparing to extract...", true);

    // Create progress callback
    auto progressCallback = [this](float progress, const String &message) -> bool
    {
        updateProgressDialog(progress, message);
        return isProgressDialogAborted();
    };

    // First extract EPUB to SD card with progress tracking
    if (!epubParser.extractEpubToSD(filepath, extractPath, progressCallback))
    {
        hideProgressDialog();
        Serial.println("Failed to extract EPUB to SD: " + epubParser.getLastError());
        return false;
    }

    // Check if extraction was aborted
    if (isProgressDialogAborted())
    {
        hideProgressDialog();
        Serial.println("EPUB extraction aborted by user");
        return false;
    }

    updateProgressDialog(0.5f, "Parsing extracted files...");

    // Then parse from extracted files
    if (!epubParser.parseExtractedEpub(extractPath))
    {
        hideProgressDialog();
        Serial.println("Failed to parse extracted EPUB: " + epubParser.getLastError());
        return false;
    }

    // Check for abort after parsing
    if (isProgressDialogAborted())
    {
        hideProgressDialog();
        Serial.println("EPUB parsing aborted by user");
        return false;
    }

    updateProgressDialog(0.7f, "Loading metadata...");

    // Get metadata and update book info
    EpubMetadata metadata = epubParser.getMetadata();
    m_currentBookInfo.title = metadata.title.isEmpty() ? extractTitleFromFilename(filepath) : metadata.title;
    m_currentBookInfo.author = metadata.author;
    m_currentBookInfo.publisher = metadata.publisher;
    m_currentBookInfo.language = metadata.language;
    m_currentBookInfo.coverImagePath = metadata.coverImagePath;

    // Get spine items for chapter references
    std::vector<EpubSpineItem> spineItems = epubParser.getSpineItems();
    std::vector<EpubManifestItem> manifestItems = epubParser.getManifestItems();

    if (spineItems.empty())
    {
        hideProgressDialog();
        Serial.println("No spine items found in EPUB file");
        return false;
    }

    updateProgressDialog(0.8f, "Building chapter index...");

    // Initialize EPUB stream properties
    m_bookStream.filepath = filepath;
    m_bookStream.fileSize = 0; // Will be calculated as chapters are loaded
    m_bookStream.chunkSize = CHUNK_SIZE;
    m_bookStream.currentPosition = 0;
    m_bookStream.pagePositions.clear();
    m_bookStream.currentChunk.isLoaded = false;
    m_bookStream.isOpen = true;
    m_bookStream.isEpubStream = true;

    // Use extracted directory as temp directory
    m_bookStream.tempDir = extractPath;
    Serial.println("Using extracted EPUB directory: " + m_bookStream.tempDir);

    // Build chapter references
    m_bookStream.chapterRefs.clear();
    m_currentBookInfo.chapters.clear();
    m_currentBookInfo.chapterSizes.clear();

    for (size_t i = 0; i < spineItems.size(); i++)
    {
        const EpubSpineItem &spineItem = spineItems[i];

        // Find corresponding manifest item
        String href = "";
        String title = "Chapter " + String(i + 1);

        for (const EpubManifestItem &manifestItem : manifestItems)
        {
            if (manifestItem.id == spineItem.idref)
            {
                href = manifestItem.href;
                break;
            }
        }

        if (href.isEmpty())
        {
            Serial.println("Warning: Could not find href for spine item: " + spineItem.idref);
            continue;
        }

        // Create chapter reference
        EpubChapterRef chapterRef;
        chapterRef.id = spineItem.idref;
        chapterRef.href = href;
        chapterRef.title = title;
        chapterRef.order = i;
        chapterRef.startPage = 0; // Will be calculated when chapter is loaded
        chapterRef.pageCount = 0;
        chapterRef.tempFilePath = "";
        chapterRef.isExtracted = false;

        m_bookStream.chapterRefs.push_back(chapterRef);
        m_currentBookInfo.chapters.push_back(title);
        m_currentBookInfo.chapterSizes.push_back(0); // Will be updated when extracted
    }

    m_currentBookInfo.totalChapters = m_bookStream.chapterRefs.size();
    m_bookStream.currentChapterIndex = 0;
    m_bookStream.globalPageOffset = 0;

    Serial.println("Found " + String(m_bookStream.chapterRefs.size()) + " chapters in EPUB");
    Serial.println("Title: " + m_currentBookInfo.title);
    Serial.println("Author: " + m_currentBookInfo.author);
    Serial.println("Temp directory: " + m_bookStream.tempDir);

    updateProgressDialog(0.9f, "Building page index...");

    // Build initial page index for first chapter
    if (!buildEpubPageIndex())
    {
        hideProgressDialog();
        Serial.println("Failed to build initial EPUB page index");
        cleanupTempFiles();
        return false;
    }

    // Final progress update
    updateProgressDialog(1.0f, "EPUB loaded successfully!");

    // Small delay to show completion, then hide dialog
    delay(500);
    hideProgressDialog();

    Serial.println("EPUB stream initialized successfully");
    return true;
}

void BookScreen::closeBookStream()
{
    // Clean up EPUB temp files if this is an EPUB stream
    if (m_bookStream.isEpubStream)
    {
        cleanupTempFiles();
    }

    m_bookStream.pagePositions.clear();
    std::vector<size_t>().swap(m_bookStream.pagePositions);
    m_bookStream.currentChunk.content = "";
    m_bookStream.currentChunk.isLoaded = false;
    m_bookStream.isOpen = false;

    // Clear EPUB-specific fields
    m_bookStream.isEpubStream = false;
    m_bookStream.tempDir = "";
    m_bookStream.chapterRefs.clear();
    m_bookStream.currentChapterIndex = 0;
    m_bookStream.globalPageOffset = 0;

    // Close current temp file if open
    if (m_bookStream.currentTempFile)
    {
        m_bookStream.currentTempFile.close();
        m_bookStream.currentTempFile = File();
    }

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

bool BookScreen::buildEpubPageIndex()
{
    if (!m_bookStream.isEpubStream || m_bookStream.chapterRefs.empty())
    {
        return false;
    }

    Serial.println("Building EPUB page index");

    // Extract and process first chapter to build initial page index
    EpubChapterRef &firstChapter = m_bookStream.chapterRefs[0];

    if (!extractChapterToTemp(0))
    {
        Serial.println("Failed to extract first chapter for page indexing");
        return false;
    }

    // Load the first chapter for streaming
    if (!loadChapterForStreaming(0))
    {
        Serial.println("Failed to load first chapter for streaming");
        return false;
    }

    // Calculate initial page count based on first chapter
    int wordsPerPage = calculateWordsPerPage();
    int charsPerPage = wordsPerPage * 5; // Estimate 5 chars per word

    // Get file size of first chapter temp file
    File tempFile = SD.open(firstChapter.tempFilePath);
    if (!tempFile)
    {
        Serial.println("Failed to open temp file for page calculation");
        return false;
    }

    size_t chapterSize = tempFile.size();
    tempFile.close();

    // Calculate pages for first chapter
    firstChapter.pageCount = (chapterSize + charsPerPage - 1) / charsPerPage;
    firstChapter.startPage = 0;

    // Estimate total pages (will be refined as more chapters are loaded)
    size_t estimatedTotalChars = chapterSize * m_bookStream.chapterRefs.size();
    m_pageInfo.totalPages = (estimatedTotalChars + charsPerPage - 1) / charsPerPage;
    m_pageInfo.currentPage = 0;

    Serial.println("First chapter: " + String(firstChapter.pageCount) + " pages");
    Serial.println("Estimated total pages: " + String(m_pageInfo.totalPages));

    return true;
}

bool BookScreen::extractChapterToTemp(int chapterIndex)
{
    if (chapterIndex < 0 || chapterIndex >= m_bookStream.chapterRefs.size())
    {
        return false;
    }

    EpubChapterRef &chapterRef = m_bookStream.chapterRefs[chapterIndex];

    if (chapterRef.isExtracted)
    {
        return true; // Already extracted
    }

    // Create temp file path
    chapterRef.tempFilePath = createTempFilePath(chapterIndex);

    // Create EPUB parser instance for extraction
    EpubParser epubParser;

    // Parse from extracted EPUB files
    if (!epubParser.parseExtractedEpub(m_bookStream.tempDir))
    {
        Serial.println("Failed to parse extracted EPUB for chapter extraction");
        return false;
    }

    // Extract chapter to temp file (this will read from extracted files and process to plain text)
    if (!epubParser.extractChapterToFile(chapterIndex, chapterRef.tempFilePath))
    {
        Serial.println("Failed to extract chapter to temp file: " + chapterRef.id);
        return false;
    }

    chapterRef.isExtracted = true;

    // Update chapter size in book info
    File tempFile = SD.open(chapterRef.tempFilePath);
    if (tempFile)
    {
        size_t chapterSize = tempFile.size();
        tempFile.close();

        if (chapterIndex < m_currentBookInfo.chapterSizes.size())
        {
            m_currentBookInfo.chapterSizes[chapterIndex] = chapterSize;
        }
    }

    Serial.println("Chapter extracted: " + chapterRef.title + " -> " + chapterRef.tempFilePath);
    return true;
}

bool BookScreen::loadChapterForStreaming(int chapterIndex)
{
    if (chapterIndex < 0 || chapterIndex >= m_bookStream.chapterRefs.size())
    {
        return false;
    }

    EpubChapterRef &chapterRef = m_bookStream.chapterRefs[chapterIndex];

    // Ensure chapter is extracted
    if (!ensureChapterExtracted(chapterIndex))
    {
        return false;
    }

    // Close current temp file if different
    if (m_bookStream.currentTempFile && m_bookStream.currentChapterIndex != chapterIndex)
    {
        m_bookStream.currentTempFile.close();
        m_bookStream.currentTempFile = File();
    }

    // Open the temp file for this chapter
    m_bookStream.currentTempFile = SD.open(chapterRef.tempFilePath);
    if (!m_bookStream.currentTempFile)
    {
        Serial.println("Failed to open temp file: " + chapterRef.tempFilePath);
        return false;
    }

    m_bookStream.currentChapterIndex = chapterIndex;
    m_bookStream.fileSize = m_bookStream.currentTempFile.size();

    Serial.println("Loaded chapter for streaming: " + chapterRef.title);
    return true;
}

String BookScreen::createTempFilePath(int chapterIndex)
{
    return m_bookStream.tempDir + "/chapter_" + String(chapterIndex) + ".txt";
}

bool BookScreen::ensureChapterExtracted(int chapterIndex)
{
    if (chapterIndex < 0 || chapterIndex >= m_bookStream.chapterRefs.size())
    {
        return false;
    }

    EpubChapterRef &chapterRef = m_bookStream.chapterRefs[chapterIndex];

    if (!chapterRef.isExtracted)
    {
        return extractChapterToTemp(chapterIndex);
    }

    return true;
}

void BookScreen::cleanupTempFiles()
{
    if (m_bookStream.tempDir.isEmpty())
    {
        return;
    }

    Serial.println("Cleaning up temp files in: " + m_bookStream.tempDir);

    // Close current temp file
    if (m_bookStream.currentTempFile)
    {
        m_bookStream.currentTempFile.close();
        m_bookStream.currentTempFile = File();
    }

    // Delete all temp files
    for (const auto &chapterRef : m_bookStream.chapterRefs)
    {
        if (!chapterRef.tempFilePath.isEmpty() && SD.exists(chapterRef.tempFilePath))
        {
            SD.remove(chapterRef.tempFilePath);
        }
    }

    // Remove temp directory
    SD.rmdir(m_bookStream.tempDir);

    Serial.println("Temp files cleaned up");
}

String BookScreen::getPageContentLoaded(int pageNumber)
{
    if (pageNumber < 0 || pageNumber >= m_pageInfo.totalPages || m_bookContent.isEmpty())
    {
        return "";
    }

    // Calculate page boundaries based on character count
    int wordsPerPage = calculateWordsPerPage();
    int charsPerPage = wordsPerPage * 5; // Estimate 5 chars per word

    size_t pageStart = pageNumber * charsPerPage;
    size_t pageEnd = std::min(pageStart + charsPerPage, (size_t)m_bookContent.length());

    if (pageStart >= m_bookContent.length())
    {
        return "";
    }

    // Extract the page content
    String pageContent = m_bookContent.substring(pageStart, pageEnd);

    // Debug logging for page boundaries
    Serial.print("ePub Page ");
    Serial.print(pageNumber);
    Serial.print(": start=");
    Serial.print(pageStart);
    Serial.print(", end=");
    Serial.print(pageEnd);
    Serial.print(", length=");
    Serial.println(pageContent.length());

    return pageContent;
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
    if (pageNumber < 0)
    {
        return "";
    }

    // Handle EPUB streaming differently
    if (m_bookStream.isEpubStream)
    {
        return getEpubPageContent(pageNumber);
    }

    // Original TXT streaming logic
    if (pageNumber >= m_bookStream.pagePositions.size())
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

String BookScreen::getEpubPageContent(int pageNumber)
{
    if (pageNumber < 0 || pageNumber >= m_pageInfo.totalPages)
    {
        return "";
    }

    // Find which chapter this page belongs to
    int chapterIndex = findChapterForPage(pageNumber);
    if (chapterIndex < 0)
    {
        return "";
    }

    // Ensure the chapter is loaded for streaming
    if (!loadChapterForStreaming(chapterIndex))
    {
        return "";
    }

    // Calculate page position within the chapter
    EpubChapterRef &chapterRef = m_bookStream.chapterRefs[chapterIndex];
    int pageInChapter = pageNumber - chapterRef.startPage;

    // Calculate character positions for this page
    int wordsPerPage = calculateWordsPerPage();
    int charsPerPage = wordsPerPage * 5;

    size_t pageStart = pageInChapter * charsPerPage;
    size_t pageEnd = pageStart + charsPerPage;

    // Read content from the temp file
    if (!m_bookStream.currentTempFile)
    {
        return "";
    }

    if (!m_bookStream.currentTempFile.seek(pageStart))
    {
        return "";
    }

    String content = "";
    content.reserve(charsPerPage + 1);

    char buffer[128];
    size_t remaining = std::min((size_t)charsPerPage, m_bookStream.fileSize - pageStart);

    while (remaining > 0 && m_bookStream.currentTempFile.available())
    {
        size_t toRead = std::min(remaining, (size_t)127);
        size_t bytesRead = m_bookStream.currentTempFile.readBytes(buffer, toRead);
        buffer[bytesRead] = '\0';
        content += buffer;
        remaining -= bytesRead;

        if (bytesRead < toRead)
        {
            break; // End of file
        }

        yield();
    }

    Serial.println("EPUB Page " + String(pageNumber) + " (Chapter " + String(chapterIndex) + ", Page " + String(pageInChapter) + "): " + String(content.length()) + " chars");

    return content;
}

int BookScreen::findChapterForPage(int pageNumber)
{
    for (int i = 0; i < m_bookStream.chapterRefs.size(); i++)
    {
        EpubChapterRef &chapterRef = m_bookStream.chapterRefs[i];

        if (pageNumber >= chapterRef.startPage &&
            pageNumber < chapterRef.startPage + chapterRef.pageCount)
        {
            return i;
        }
    }

    // If not found, might need to expand page index
    return 0; // Default to first chapter
}

// Progress Dialog Implementation
void BookScreen::showProgressDialog(const String &title, const String &message, bool canAbort)
{
    m_progressDialog.isVisible = true;
    m_progressDialog.title = title;
    m_progressDialog.message = message;
    m_progressDialog.progress = 0.0f;
    m_progressDialog.canAbort = canAbort;
    m_progressDialog.abortRequested = false;
    m_progressDialog.lastUpdateTime = millis();

    // Draw the dialog immediately
    extern EinkDisplayManager display;
    display.startDrawing();
    drawProgressDialog();
    display.endDrawing();
}

void BookScreen::updateProgressDialog(float progress, const String &message)
{
    if (!m_progressDialog.isVisible)
        return;

    m_progressDialog.progress = constrain(progress, 0.0f, 1.0f);
    if (!message.isEmpty())
    {
        m_progressDialog.message = message;
    }

    // Only update display every 500ms to avoid too frequent updates
    unsigned long currentTime = millis();
    if (currentTime - m_progressDialog.lastUpdateTime >= 500)
    {
        m_progressDialog.lastUpdateTime = currentTime;

        extern EinkDisplayManager display;
        display.startDrawing();
        drawProgressDialog();
        display.endDrawing();
    }
}

void BookScreen::hideProgressDialog()
{
    if (!m_progressDialog.isVisible)
        return;

    m_progressDialog.isVisible = false;

    // Redraw the screen to remove the dialog
    extern EinkDisplayManager display;
    draw(EinkDisplayManager::UPDATE_PARTIAL);
}

bool BookScreen::isProgressDialogAborted() const
{
    return m_progressDialog.abortRequested;
}

void BookScreen::drawProgressDialog()
{
    if (!m_progressDialog.isVisible)
        return;

    extern EinkDisplayManager display;

    // Dialog dimensions
    int dialogWidth = 280;
    int dialogHeight = 120;
    int dialogX = (display.m_display.width() - dialogWidth) / 2;
    int dialogY = (display.m_display.height() - dialogHeight) / 2;

    // Draw dialog background and border
    display.m_display.fillRect(dialogX, dialogY, dialogWidth, dialogHeight, GxEPD_WHITE);
    display.m_display.drawRect(dialogX, dialogY, dialogWidth, dialogHeight, GxEPD_BLACK);
    display.m_display.drawRect(dialogX + 1, dialogY + 1, dialogWidth - 2, dialogHeight - 2, GxEPD_BLACK);

    // Draw title
    display.m_display.setFont(&FreeMonoBold12pt7b);
    int titleY = dialogY + 25;
    display.drawCenteredText(m_progressDialog.title.c_str(), titleY, &FreeMonoBold12pt7b);

    // Draw separator line
    display.m_display.drawLine(dialogX + 10, titleY + 10, dialogX + dialogWidth - 10, titleY + 10, GxEPD_BLACK);

    // Draw message
    display.m_display.setFont(&FreeMono9pt7b);
    int messageY = titleY + 30;
    display.m_display.setCursor(dialogX + 10, messageY);
    display.m_display.print(m_progressDialog.message);

    // Draw progress bar
    int progressBarY = messageY + 20;
    int progressBarWidth = dialogWidth - 20;
    int progressBarHeight = 10;
    int progressBarX = dialogX + 10;

    // Progress bar background
    display.m_display.drawRect(progressBarX, progressBarY, progressBarWidth, progressBarHeight, GxEPD_BLACK);

    // Progress bar fill
    int fillWidth = (int)(progressBarWidth * m_progressDialog.progress);
    if (fillWidth > 0)
    {
        display.m_display.fillRect(progressBarX + 1, progressBarY + 1, fillWidth - 2, progressBarHeight - 2, GxEPD_BLACK);
    }

    // Draw percentage
    int percentage = (int)(m_progressDialog.progress * 100);
    String percentText = String(percentage) + "%";
    int percentY = progressBarY + progressBarHeight + 15;
    display.drawCenteredText(percentText.c_str(), percentY, &FreeMono9pt7b);

    // Draw abort instruction if applicable
    if (m_progressDialog.canAbort)
    {
        int instructionY = percentY + 15;
        display.m_display.setFont(&FreeMono9pt7b);
        display.drawCenteredText("Press any button to abort", instructionY, &FreeMono9pt7b);
    }

    // Reset text color
    display.m_display.setTextColor(GxEPD_BLACK);
}