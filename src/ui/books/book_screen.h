#ifndef BOOK_SCREEN_H
#define BOOK_SCREEN_H

#include "../../../include/display.h"
#include "../../../include/storage.h"
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
    BookFormat format;
    size_t fileSize;
    bool isValid;
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

// Book menu dialog structure
struct BookMenuDialog
{
    bool isVisible;
    int selectedOption;
    std::vector<String> options;
    String title;
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
    String m_bookContent;
    std::vector<String> m_pages;
    bool m_bookLoaded;
    
    // Book menu dialog
    BookMenuDialog m_bookMenu;
    
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
    void paginateContent();
    void calculatePages();
    String extractTextFromPage(const String &content, int startPos, int maxChars);
    int calculateWordsPerPage();
    void initializeTextSettings();
    
    // Navigation helpers
    void ensureValidBookSelection();
    void scrollToSelection();
    
    // Menu helpers
    void initializeBookMenu();
    
    // Utility helpers
    String formatFileSize(size_t bytes);
    String getBookIcon(const BookInfo &book);
};

#endif // BOOK_SCREEN_H