#ifndef BOOK_SCREEN_H
#define BOOK_SCREEN_H

#include "../../../include/display.h"
#include "../../../include/storage.h"
#include "../../../include/epub_parser.h"
#include <vector>
#include <FS.h>
#include <SD.h>

// Book file formats supported
enum BookFormat
{
    FORMAT_TXT,
    FORMAT_EPUB,
    FORMAT_UNKNOWN
};

// Book information structure
struct BookInfo
{
    String filename;
    String title;
    String author;
    String publisher;
    String language;
    BookFormat format;
    size_t fileSize;
    bool isValid;
    
    // ePub-specific fields
    std::vector<String> chapters;
    std::vector<size_t> chapterSizes;
    String coverImagePath;
    int totalChapters;
    
    BookInfo() : fileSize(0), isValid(false), totalChapters(0) {}
};

// Text rendering settings
struct TextSettings
{
    const GFXfont *font;
    int fontSize;
    int lineHeight;
    int margin;
    int wordsPerPage;
};

// Page information
struct PageInfo
{
    int currentPage;
    int totalPages;
    size_t startPosition;
    size_t endPosition;
    String content;
};

// Chunk-based reading structure
struct BookChunk
{
    size_t filePosition;
    size_t chunkSize;
    String content;
    bool isLoaded;
};

// Book streaming manager
struct BookStream
{
    String filepath;
    size_t fileSize;
    size_t chunkSize;
    size_t currentPosition;
    std::vector<size_t> pagePositions; // File positions for each page start
    BookChunk currentChunk;
    bool isOpen;
};

// Book menu dialog structure
struct BookMenuDialog
{
    bool isVisible;
    int selectedOption;
    std::vector<String> options;
    String title;
};

// Reading history structure
struct ReadingHistory
{
    String filepath;
    String filename;
    int lastPage;
    size_t lastPosition;
    String lastReadTime;
    float readingProgress; // Percentage (0.0 - 1.0)
};

class BookScreen
{
public:
    BookScreen();

    // Screen drawing
    void draw(EinkDisplayManager::DisplayUpdateMode mode = EinkDisplayManager::UPDATE_PARTIAL);
    void drawBookList(EinkDisplayManager::DisplayUpdateMode mode = EinkDisplayManager::UPDATE_PARTIAL);
    void drawBookReader(EinkDisplayManager::DisplayUpdateMode mode = EinkDisplayManager::UPDATE_PARTIAL);
    void drawBookMenu(EinkDisplayManager::DisplayUpdateMode mode = EinkDisplayManager::UPDATE_PARTIAL);

    // Button handlers
    void handleSelectAction(); // Select book or menu option
    void handleDownAction();   // Navigate list down or next page
    void handleUpAction();     // Navigate list up or previous page
    void handleBackAction();   // Go back to previous screen

    // Book management
    bool loadBook(const String &filepath);
    void closeBook();
    bool isBookLoaded() const;

    // Navigation
    bool nextPage();
    bool previousPage();
    bool goToPage(int pageNumber);

    // Text settings
    void increaseFontSize();
    void decreaseFontSize();
    void setFont(const GFXfont *font);

    // Menu control
    void showBookMenu();
    void hideBookMenu();
    void handleBookMenuSelect();

    // State management
    enum ScreenMode {
        MODE_BOOK_LIST,
        MODE_BOOK_READER,
        MODE_BOOK_MENU
    };
    
    ScreenMode getCurrentMode() const;
    void setMode(ScreenMode mode);
    
    // Book list management
    void refreshBookList();
    std::vector<BookInfo> getAvailableBooks() const;
    int getSelectedBookIndex() const;
    void setSelectedBookIndex(int index);
    
    // Book list pagination
    void nextBookPage();
    void previousBookPage();
    int getCurrentBookPage() const;
    int getTotalBookPages() const;

    // Getters
    BookInfo getBookInfo() const;
    PageInfo getPageInfo() const;
    TextSettings getTextSettings() const;

    // Static utility methods
    std::vector<BookInfo> scanBooksDirectory();
    static BookFormat detectBookFormat(const String &filename);
    
    // Reading history management
    bool saveReadingHistory();
    bool loadReadingHistory(const String &filepath);
    ReadingHistory getReadingHistory(const String &filepath);
    std::vector<ReadingHistory> getAllReadingHistory();
    bool clearReadingHistory(const String &filepath = "");
    float calculateReadingProgress() const;

private:
    // UI state
    ScreenMode m_currentMode;
    int m_selectedBookIndex;
    bool m_isLoading;
    bool m_isInitialized;
    
    // Book list pagination
    int m_currentBookPage;
    int m_booksPerPage;
    int m_totalBookPages;
    
    // Book data
    std::vector<BookInfo> m_availableBooks;
    BookInfo m_currentBookInfo;
    TextSettings m_textSettings;
    PageInfo m_pageInfo;
    String m_bookContent; // Legacy - will be phased out
    std::vector<String> m_pages; // Legacy - will be phased out
    bool m_bookLoaded;
    
    // Streaming book data
    BookStream m_bookStream;
    static const size_t CHUNK_SIZE = 2048; // 2KB chunks
    int m_estimatedTotalPages; // Estimated total pages for UI display
    
    // Book menu dialog
    BookMenuDialog m_bookMenu;
    
    // Reading history
    std::vector<ReadingHistory> m_readingHistory;
    static const String HISTORY_FILE;
    
    // Drawing helpers
    void drawHeader();
    void drawBookListContent();
    void drawBookReaderContent();
    void drawBookMenuDialog();
    void drawLoadingIndicator();
    void drawStatusBar();
    
    // Book management helpers
    bool loadTxtBook(const String &filepath);
    bool loadEpubBook(const String &filepath);
    // void paginateContent(); // Legacy - replaced by streaming approach
    void calculatePages();
    String extractTextFromPage(const String &content, int startPos, int maxChars);
    int calculateWordsPerPage();
    void initializeTextSettings();
    
    // Streaming book management
    bool initializeBookStream(const String &filepath);
    void closeBookStream();
    bool loadChunkAtPosition(size_t position);
    String getPageContentStreaming(int pageNumber);
    bool buildPageIndex();
    bool expandPageIndex(int targetPage); // Dynamically expand page index
    size_t findNextPageBreak(size_t startPosition);
    String readChunkFromFile(size_t position, size_t size);
    
    // Navigation helpers
    void ensureValidBookSelection();
    void scrollToSelection();
    
    // Menu helpers
    void initializeBookMenu();
    
    // Utility helpers
    String formatFileSize(size_t bytes);
    String extractTitleFromFilename(const String &filepath);
    String getBookIcon(const BookInfo &book);
};

#endif // BOOK_SCREEN_H