#include "book_screen.h"
#include "../../../include/storage.h"
#include "../../../include/power.h"
#include "../../../include/display.h"
#include <SD.h>
#include <algorithm>

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

    // Initialize book menu
    initializeBookMenu();

    // Don't load book list in constructor - defer until first access
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
            display.update(EinkDisplayManager::UPDATE_FULL);

            if (loadBook(book.filename))
            {
                setMode(MODE_BOOK_READER);
                display.wipeScreen();
                draw(EinkDisplayManager::UPDATE_FULL);
            }
            else
            {
                // Show error message
                display.wipeScreen();
                display.m_display.setFont(&FreeMono9pt7b);
                display.drawCenteredText("Failed to load book", 150, &FreeMono9pt7b);
                display.m_display.setFont(&FreeMono9pt7b);
                display.drawCenteredText("Press any button to continue", 170, &FreeMono9pt7b);
                display.update(EinkDisplayManager::UPDATE_FULL);
                delay(2000);
                draw(EinkDisplayManager::UPDATE_FULL);
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
                draw(EinkDisplayManager::UPDATE_FULL);
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
                draw(EinkDisplayManager::UPDATE_FULL);
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
        draw(EinkDisplayManager::UPDATE_FULL);
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

    if (freeHeap < 30000) // Need at least 30KB free
    {
        Serial.println("Insufficient memory to load book");
        return false;
    }

    m_currentBookInfo.filename = filepath;
    m_currentBookInfo.format = detectBookFormat(filepath);
    m_currentBookInfo.fileSize = getFileSize(filepath);

    // Check if file is too large for available memory
    if (m_currentBookInfo.fileSize > freeHeap / 3)
    {
        Serial.println("File too large for available memory: " + String(m_currentBookInfo.fileSize) + " bytes");
        return false;
    }

    bool success = false;
    switch (m_currentBookInfo.format)
    {
    case FORMAT_TXT:
        success = loadTxtBook(filepath);
        break;
    case FORMAT_EPUB:
        success = loadEpubBook(filepath);
        break;
    default:
        Serial.println("Unsupported book format");
        return false;
    }

    if (success)
    {
        m_bookLoaded = true;
        m_pageInfo.currentPage = 0;

        // Show pagination progress
        display.wipeScreen();
        display.m_display.setFont(&FreeMono9pt7b);
        display.drawCenteredText("Processing book...", 150, &FreeMono9pt7b);
        display.m_display.setFont(&FreeMono9pt7b);
        display.drawCenteredText("Creating pages", 170, &FreeMono9pt7b);
        display.update(EinkDisplayManager::UPDATE_FULL);

        paginateContent();

        // Check memory after pagination
        size_t freeHeapAfter = ESP.getFreeHeap();
        Serial.println("Free heap after pagination: " + String(freeHeapAfter) + " bytes");

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

        Serial.println("Book loaded successfully: " + m_currentBookInfo.title);
        Serial.println("Total pages: " + String(m_pageInfo.totalPages));
        Serial.println("Memory used for book: " + String(freeHeap - freeHeapAfter) + " bytes");
    }

    return success;
}

void BookScreen::closeBook()
{
    m_bookLoaded = false;

    // Clear content and free memory
    m_bookContent = "";

    m_pages.clear();
    // Free unused memory by swapping with empty vector
    std::vector<String>().swap(m_pages);

    m_pageInfo.currentPage = 0;
    m_pageInfo.totalPages = 0;
    m_currentBookInfo = BookInfo();

    Serial.println("Book closed and memory freed");
}

bool BookScreen::isBookLoaded() const
{
    return m_bookLoaded;
}

bool BookScreen::nextPage()
{
    if (!m_bookLoaded || m_pageInfo.currentPage >= m_pageInfo.totalPages - 1)
    {
        return false;
    }

    m_pageInfo.currentPage++;
    return true;
}

bool BookScreen::previousPage()
{
    if (!m_bookLoaded || m_pageInfo.currentPage <= 0)
    {
        return false;
    }

    m_pageInfo.currentPage--;
    return true;
}

bool BookScreen::goToPage(int pageNumber)
{
    if (!m_bookLoaded || pageNumber < 0 || pageNumber >= m_pageInfo.totalPages)
    {
        return false;
    }

    m_pageInfo.currentPage = pageNumber;
    return true;
}

void BookScreen::increaseFontSize()
{
    if (m_textSettings.font == &FreeMono9pt7b)
    {
        m_textSettings.font = &FreeMono12pt7b;
        m_textSettings.fontSize = 12;
        m_textSettings.lineHeight = 18;
    }
    else if (m_textSettings.font == &FreeMono12pt7b)
    {
        m_textSettings.font = &FreeMono18pt7b;
        m_textSettings.fontSize = 18;
        m_textSettings.lineHeight = 24;
    }

    m_textSettings.wordsPerPage = calculateWordsPerPage();
    if (m_bookLoaded)
    {
        paginateContent(); // Re-paginate with new font size
    }
}

void BookScreen::decreaseFontSize()
{
    if (m_textSettings.font == &FreeMono18pt7b)
    {
        m_textSettings.font = &FreeMono12pt7b;
        m_textSettings.fontSize = 12;
        m_textSettings.lineHeight = 18;
    }
    else if (m_textSettings.font == &FreeMono12pt7b)
    {
        m_textSettings.font = &FreeMono9pt7b;
        m_textSettings.fontSize = 9;
        m_textSettings.lineHeight = 14;
    }

    m_textSettings.wordsPerPage = calculateWordsPerPage();
    if (m_bookLoaded)
    {
        paginateContent(); // Re-paginate with new font size
    }
}

void BookScreen::setFont(const GFXfont *font)
{
    m_textSettings.font = font;
    m_textSettings.wordsPerPage = calculateWordsPerPage();
    if (m_bookLoaded)
    {
        paginateContent();
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
    this->m_totalBookPages = (allBooks.size() + this->m_booksPerPage - 1) / this->m_booksPerPage;
    if (this->m_totalBookPages == 0)
        this->m_totalBookPages = 1;

    // Ensure current page is valid
    if (this->m_currentBookPage >= this->m_totalBookPages)
    {
        this->m_currentBookPage = this->m_totalBookPages - 1;
    }
    if (this->m_currentBookPage < 0)
    {
        this->m_currentBookPage = 0;
    }

    // Get books for current page
    int startIndex = this->m_currentBookPage * this->m_booksPerPage;
    int endIndex = std::min(startIndex + this->m_booksPerPage, (int)allBooks.size());

    for (int i = startIndex; i < endIndex; i++)
    {
        books.push_back(allBooks[i]);
        Serial.println("Loaded book: " + allBooks[i].title + " (" + String(allBooks[i].fileSize) + " bytes)");
    }

    Serial.println("Page " + String(this->m_currentBookPage + 1) + "/" + String(this->m_totalBookPages) + ", showing " + String(books.size()) + " of " + String(allBooks.size()) + " books");
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
    display.m_display.setFont(&FreeMono12pt7b);
    switch (m_currentMode)
    {
    case MODE_BOOK_LIST:
        if (this->m_totalBookPages > 1)
        {
            String title = "Books (" + String(this->m_currentBookPage + 1) + "/" + String(this->m_totalBookPages) + ")";
            display.drawCenteredText(title.c_str(), 50, &FreeMono9pt7b);
        }
        else
        {
            display.drawCenteredText("Books", 50, &FreeMono12pt7b);
        }
        break;
    case MODE_BOOK_READER:
        // Title is drawn in drawBookReaderContent
        break;
    case MODE_BOOK_MENU:
        display.drawCenteredText("Reading Menu", 50, &FreeMono12pt7b);
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
        if (this->m_selectedBookIndex >= maxVisible)
        {
            scrollOffset = this->m_selectedBookIndex - maxVisible + 1;
        }

        for (int i = 0; i < std::min((int)this->m_availableBooks.size(), maxVisible); i++)
        {
            int bookIndex = i + scrollOffset;
            if (bookIndex >= this->m_availableBooks.size())
                break;

            const BookInfo &book = this->m_availableBooks[bookIndex];
            int y = startY + (i * lineHeight);

            // Highlight selected book
            if (bookIndex == this->m_selectedBookIndex)
            {
                display.m_display.fillRect(5, y - 18, display.m_display.width() - 10, lineHeight, GxEPD_BLACK);
                display.m_display.setTextColor(GxEPD_WHITE);
            }
            else
            {
                display.m_display.setTextColor(GxEPD_BLACK);
            }

            // Draw book title without icon
            display.m_display.setFont(&FreeMono9pt7b);

            // Truncate title if too long
            String displayTitle = book.title;
            if (displayTitle.length() > 25)
            {
                displayTitle = displayTitle.substring(0, 22) + "...";
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

        // Draw pagination info at bottom if multiple pages
        if (this->m_totalBookPages > 1)
        {
            display.m_display.setFont(&FreeMono9pt7b);
            String pageInfo = "Page " + String(this->m_currentBookPage + 1) + " of " + String(this->m_totalBookPages);
            String navInfo = "Double-click UP/DOWN to change pages";

            int bottomY = display.m_display.height() - 30;
            display.drawCenteredText(pageInfo.c_str(), bottomY, &FreeMono9pt7b);
            display.drawCenteredText(navInfo.c_str(), bottomY + 15, &FreeMono9pt7b);
        }
    }
}

void BookScreen::drawBookReaderContent()
{
    if (!this->m_bookLoaded || this->m_pages.empty())
    {
        display.m_display.setFont(&FreeMono12pt7b);
        display.drawCenteredText("No book loaded", 200, &FreeMono12pt7b);
        return;
    }

    // Draw book title and page info in header area
    display.m_display.setFont(&FreeMono9pt7b);

    // Book title (truncated if too long)
    String title = this->m_currentBookInfo.title;
    if (title.length() > 20)
    {
        title = title.substring(0, 17) + "...";
    }
    display.m_display.setCursor(5, 16);
    display.m_display.print(title);

    // Page info
    String pageInfo = String(this->m_pageInfo.currentPage + 1) + "/" + String(this->m_pageInfo.totalPages);
    int16_t x1, y1;
    uint16_t w, h;
    display.m_display.getTextBounds(pageInfo.c_str(), 0, 0, &x1, &y1, &w, &h);
    display.m_display.setCursor(display.m_display.width() - w - 5, 16);
    display.m_display.print(pageInfo);

    // Separator line
    display.m_display.drawLine(0, 25, display.m_display.width(), 25, GxEPD_BLACK);

    // Draw book content
    display.m_display.setFont(m_textSettings.font);

    if (m_pageInfo.currentPage < m_pages.size())
    {
        String currentPageContent = m_pages[m_pageInfo.currentPage];

        // Optimized text rendering with pre-calculated line width
        int yPos = 45;
        int xPos = m_textSettings.margin;
        int maxLineWidth = display.m_display.width() - (m_textSettings.margin * 2);
        int maxY = display.m_display.height() - 30;

        // Estimate characters per line for faster word wrapping
        int approxCharsPerLine = maxLineWidth / 8; // Approximate character width

        String currentLine = "";
        currentLine.reserve(approxCharsPerLine + 20); // Pre-allocate

        int contentLength = currentPageContent.length();
        int i = 0;

        while (i < contentLength && yPos <= maxY)
        {
            // Find next word boundary
            int wordStart = i;
            while (i < contentLength && currentPageContent.charAt(i) != ' ' && currentPageContent.charAt(i) != '\n')
            {
                i++;
            }

            String word = currentPageContent.substring(wordStart, i);
            bool isNewline = (i < contentLength && currentPageContent.charAt(i) == '\n');

            // Quick length check before expensive getTextBounds
            String testLine = currentLine + (currentLine.isEmpty() ? "" : " ") + word;
            bool needNewLine = isNewline || testLine.length() > approxCharsPerLine;

            // Only use getTextBounds if quick check suggests we might exceed width
            if (needNewLine && testLine.length() > approxCharsPerLine * 0.8)
            {
                int16_t x1, y1;
                uint16_t w, h;
                display.m_display.getTextBounds(testLine.c_str(), 0, 0, &x1, &y1, &w, &h);
                needNewLine = (w > maxLineWidth) || isNewline;
            }

            if (needNewLine)
            {
                // Draw current line
                if (!currentLine.isEmpty())
                {
                    display.m_display.setCursor(xPos, yPos);
                    display.m_display.print(currentLine);
                    yPos += m_textSettings.lineHeight;
                    currentLine = "";
                }

                if (!isNewline && yPos <= maxY)
                {
                    currentLine = word;
                }
            }
            else
            {
                currentLine = testLine;
            }

            // Skip the space or newline character
            if (i < contentLength)
            {
                i++;
            }
        }

        // Draw remaining line
        if (!currentLine.isEmpty() && yPos <= maxY)
        {
            display.m_display.setCursor(xPos, yPos);
            display.m_display.print(currentLine);
        }
    }
}

void BookScreen::drawBookMenuDialog()
{
    // Draw semi-transparent background
    display.m_display.fillRect(20, 80, display.m_display.width() - 40, 200, GxEPD_WHITE);
    display.m_display.drawRect(20, 80, display.m_display.width() - 40, 200, GxEPD_BLACK);

    display.m_display.setFont(&FreeMono12pt7b);

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
    display.m_display.setFont(&FreeMono12pt7b);
    display.drawCenteredText("Loading books...", 150, &FreeMono12pt7b);
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

    File file = SD.open(filepath);
    if (!file)
    {
        Serial.println("Failed to open EPUB file: " + filepath);
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
        Serial.println("Failed to read EPUB file");
        return false;
    }

    // Basic HTML tag removal for EPUB content (optimized)
    m_bookContent.replace("<p>", "\n\n");
    m_bookContent.replace("</p>", "");
    m_bookContent.replace("<br>", "\n");
    m_bookContent.replace("<br/>", "\n");
    m_bookContent.replace("<br />", "\n");

    // Remove other common HTML tags (optimized)
    int tagStart = 0;
    while ((tagStart = m_bookContent.indexOf('<', tagStart)) >= 0)
    {
        int tagEnd = m_bookContent.indexOf('>', tagStart);
        if (tagEnd > tagStart)
        {
            m_bookContent.remove(tagStart, tagEnd - tagStart + 1);
        }
        else
        {
            tagStart++; // Move past this character if no closing >
        }

        // Yield periodically during tag removal
        if (tagStart % 1000 == 0)
        {
            yield();
        }
    }

    m_currentBookInfo.isValid = true;
    return true;
}

void BookScreen::paginateContent()
{
    if (m_bookContent.isEmpty())
    {
        return;
    }

    m_pages.clear();

    // Reserve some space but not too much to avoid memory issues
    int estimatedPages = (m_bookContent.length() / 1000) + 10;
    if (estimatedPages < 100) // Only reserve for reasonable number of pages
    {
        m_pages.reserve(estimatedPages);
    }

    // More conservative pagination for memory efficiency
    int displayHeight = display.m_display.height() - 75; // Account for header and footer
    int linesPerPage = displayHeight / m_textSettings.lineHeight;
    int approxCharsPerLine = (display.m_display.width() - (m_textSettings.margin * 2)) / 8;
    int charsPerPage = linesPerPage * approxCharsPerLine * 0.6; // More conservative estimate

    int contentLength = m_bookContent.length();
    int currentPos = 0;
    int pageCount = 0;

    while (currentPos < contentLength)
    {
        int endPos = std::min(currentPos + charsPerPage, contentLength);

        // Find word boundary
        if (endPos < contentLength)
        {
            // Look for space, newline, or punctuation
            int bestBreak = endPos;
            for (int j = endPos; j > currentPos + (charsPerPage * 0.7) && j > currentPos; j--)
            {
                char c = m_bookContent.charAt(j);
                if (c == ' ' || c == '\n' || c == '.' || c == '!' || c == '?')
                {
                    bestBreak = j;
                    break;
                }
            }
            endPos = bestBreak;
        }

        String page = m_bookContent.substring(currentPos, endPos);
        page.trim(); // Remove leading/trailing whitespace

        if (!page.isEmpty())
        {
            m_pages.push_back(page);
            pageCount++;
        }

        currentPos = endPos;

        // Yield every few pages to prevent watchdog timeout
        if (pageCount % 5 == 0)
        {
            yield();
            Serial.print("p"); // Progress indicator for pagination
        }

        // Safety limit on number of pages
        if (pageCount > 500)
        {
            Serial.println("\nToo many pages, limiting to 500");
            break;
        }

        // Skip whitespace at the beginning of next page
        while (currentPos < contentLength &&
               (m_bookContent.charAt(currentPos) == ' ' || m_bookContent.charAt(currentPos) == '\n'))
        {
            currentPos++;
        }

        // Yield periodically during pagination
        if (m_pages.size() % 10 == 0)
        {
            yield();
        }
    }

    m_pageInfo.totalPages = m_pages.size();
    if (m_pageInfo.totalPages > 0)
    {
        m_pageInfo.currentPage = 0;
    }

    Serial.println("Pagination complete: " + String(m_pageInfo.totalPages) + " pages");
}

void BookScreen::calculatePages()
{
    // This is handled by paginateContent()
    paginateContent();
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
    // Calculate based on display size and font
    int displayWidth = display.m_display.width() - (m_textSettings.margin * 2);
    int displayHeight = display.m_display.height() - 75; // Account for header and footer

    // More accurate character width estimation based on font
    int charWidth = 8; // Default for FreeMono12pt7b
    if (m_textSettings.font == &FreeMono9pt7b)
    {
        charWidth = 6;
    }
    else if (m_textSettings.font == &FreeMono18pt7b)
    {
        charWidth = 12;
    }

    int charsPerLine = displayWidth / charWidth;
    int linesPerPage = displayHeight / m_textSettings.lineHeight;

    // Estimate words (average 5 characters per word)
    return (charsPerLine * linesPerPage) / 5;
}

void BookScreen::initializeTextSettings()
{
    m_textSettings.font = &FreeMono12pt7b;
    m_textSettings.fontSize = 12;
    m_textSettings.lineHeight = 18;
    m_textSettings.margin = 10;
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