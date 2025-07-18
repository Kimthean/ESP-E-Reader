#ifndef FILES_SCREEN_H
#define FILES_SCREEN_H

#include "../../../include/display.h"
#include "../../../include/storage.h"
#include <vector>
#include <FS.h>
#include <SD.h>

// File/Directory structure
struct FileItem
{
    String name;
    String fullPath;
    bool isDirectory;
    size_t size;
    String lastModified;
};

// Global menu dialog structure
struct GlobalMenuDialog
{
    bool isVisible;
    int selectedOption;
    std::vector<String> options;
    String title;
};

class FilesScreen
{
public:
    FilesScreen();

    // Screen drawing
    void draw(EinkDisplayManager::DisplayUpdateMode mode = EinkDisplayManager::UPDATE_PARTIAL);

    // Button handlers
    void handleSelectAction(); // Enter directory or select file
    void handleDownAction();   // Navigate list down
    void handleUpAction();     // Navigate list up or back
    void handleQuickDownAction(int steps = 5); // Quick scroll down
    void handleQuickUpAction(int steps = 5);   // Quick scroll up
    
    // Menu control
    void showGlobalMenu();     // Show context menu (accessible from button handler)
    void hideGlobalMenu();     // Hide context menu

    // File operations
    void navigateToDirectory(const String &path);
    void navigateBack();
    void deleteSelectedFile();
    void refreshCurrentDirectory();

    // State management
    String getCurrentPath() const;
    bool isAtRoot() const;
    
    // Global menu
    void handleGlobalMenuSelect();

private:
    // UI state
    int selectedItemIndex;
    String currentPath;
    std::vector<String> pathHistory;
    bool isLoading;
    bool isInitialized;
    
    // File data
    std::vector<FileItem> currentItems;
    
    // Global menu dialog
    GlobalMenuDialog globalMenu;
    
    // Drawing helpers
    void drawHeader();
    void drawFileList();
    void drawLoadingIndicator();
    void drawGlobalMenuDialog();
    void drawPathBreadcrumb();
    
    // File system helpers
    void loadDirectory(const String &path);
    String formatFileSize(size_t bytes);
    String getFileIcon(const FileItem &item);
    bool isValidPath(const String &path);
    
    // Navigation helpers
    void ensureValidSelection();
    void scrollToSelection();
    
    // Global menu helpers
    void initializeGlobalMenu();
};

#endif // FILES_SCREEN_H