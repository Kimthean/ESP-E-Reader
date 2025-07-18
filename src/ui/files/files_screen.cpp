#include "files_screen.h"
#include "../../../include/storage.h"
#include "../../../include/power.h"
#include "../../../include/display.h"
#include <SD.h>
#include <algorithm>

// Forward declarations for storage functions
extern bool powerOnSDCard();
extern bool isSDCardPowered();

FilesScreen::FilesScreen()
{
    selectedItemIndex = 0;
    currentPath = "/";
    isLoading = false;
    isInitialized = false;

    // Initialize global menu
    initializeGlobalMenu();

    // Don't load directory in constructor - defer until first access
}

void FilesScreen::draw(EinkDisplayManager::DisplayUpdateMode mode)
{
    extern EinkDisplayManager display;

    // Lazy initialization on first access
    if (!isInitialized)
    {
        isInitialized = true;
        refreshCurrentDirectory();
    }

    display.startDrawing();
    drawHeader();
    drawPathBreadcrumb();

    if (isLoading)
    {
        drawLoadingIndicator();
    }
    else
    {
        drawFileList();
    }

    // Draw global menu dialog if visible
    if (globalMenu.isVisible)
    {
        drawGlobalMenuDialog();
    }

    display.endDrawing();
    display.update(mode);
}

void FilesScreen::handleSelectAction()
{
    // If global menu is visible, handle menu selection
    if (globalMenu.isVisible)
    {
        handleGlobalMenuSelect();
        return;
    }

    // If no items, show global menu
    if (currentItems.empty())
    {
        showGlobalMenu();
        return;
    }

    // Handle file/directory selection
    if (selectedItemIndex >= 0 && selectedItemIndex < currentItems.size())
    {
        const FileItem &item = currentItems[selectedItemIndex];

        // Skip action for error placeholder items
        if (item.fullPath.isEmpty())
        {
            showGlobalMenu();
            return;
        }

        if (item.isDirectory)
        {
            navigateToDirectory(item.fullPath);
        }
        else
        {
            // For files, show global menu with file-specific options
            showGlobalMenu();
        }
    }
}

void FilesScreen::handleDownAction()
{
    // If global menu is visible, navigate menu down
    if (globalMenu.isVisible)
    {
        globalMenu.selectedOption = (globalMenu.selectedOption + 1) % globalMenu.options.size();
        draw(EinkDisplayManager::UPDATE_PARTIAL);
        return;
    }

    // Navigate file list down
    if (!currentItems.empty())
    {
        selectedItemIndex = (selectedItemIndex + 1) % currentItems.size();
        draw(EinkDisplayManager::UPDATE_PARTIAL);
    }
}

void FilesScreen::handleQuickDownAction(int steps)
{
    // If global menu is visible, ignore quick navigation
    if (globalMenu.isVisible)
    {
        return;
    }

    // Navigate file list down by multiple steps
    if (!currentItems.empty())
    {
        selectedItemIndex = (selectedItemIndex + steps) % currentItems.size();
        draw(EinkDisplayManager::UPDATE_PARTIAL);
    }
}

void FilesScreen::handleQuickUpAction(int steps)
{
    // If global menu is visible, ignore quick navigation
    if (globalMenu.isVisible)
    {
        return;
    }

    // Navigate file list up by multiple steps
    if (!currentItems.empty())
    {
        selectedItemIndex = (selectedItemIndex - steps + currentItems.size()) % currentItems.size();
        draw(EinkDisplayManager::UPDATE_PARTIAL);
    }
}

void FilesScreen::handleUpAction()
{
    // If global menu is visible, navigate menu up or close it
    if (globalMenu.isVisible)
    {
        if (globalMenu.selectedOption > 0)
        {
            globalMenu.selectedOption--;
        }
        else
        {
            hideGlobalMenu();
        }
        draw(EinkDisplayManager::UPDATE_PARTIAL);
        return;
    }

    // Navigate file list up
    if (!currentItems.empty() && selectedItemIndex > 0)
    {
        selectedItemIndex--;
        draw(EinkDisplayManager::UPDATE_PARTIAL);
    }
    else if (!isAtRoot())
    {
        // Go back to parent directory when at top of list
        navigateBack();
    }
    // If at root and at top of list, do nothing (long press opens global menu)
}

void FilesScreen::navigateToDirectory(const String &path)
{
    if (!isValidPath(path))
    {
        Serial.println("[Files] Invalid path: " + path);
        return;
    }

    // Add current path to history
    pathHistory.push_back(currentPath);

    // Navigate to new directory
    currentPath = path;
    selectedItemIndex = 0;

    loadDirectory(currentPath);
    draw(EinkDisplayManager::UPDATE_FAST);
}

void FilesScreen::navigateBack()
{
    if (!pathHistory.empty())
    {
        currentPath = pathHistory.back();
        pathHistory.pop_back();
        selectedItemIndex = 0;

        loadDirectory(currentPath);
        draw(EinkDisplayManager::UPDATE_FAST);
    }
}

void FilesScreen::deleteSelectedFile()
{
    if (selectedItemIndex >= 0 && selectedItemIndex < currentItems.size())
    {
        const FileItem &item = currentItems[selectedItemIndex];

        // Skip deletion for error placeholder items
        if (item.fullPath.isEmpty())
        {
            Serial.println("[Files] Cannot delete error placeholder item");
            return;
        }

        // Ensure SD card is powered
        if (!isSDCardPowered())
        {
            if (!powerOnSDCard())
            {
                Serial.println("[Files] Failed to power on SD card for deletion");
                return;
            }
        }

        bool success = false;
        if (item.isDirectory)
        {
            success = deleteDirectory(item.fullPath);
        }
        else
        {
            success = deleteFile(item.fullPath);
        }

        if (success)
        {
            Serial.println("[Files] Deleted: " + item.fullPath);
            refreshCurrentDirectory();
        }
        else
        {
            Serial.println("[Files] Failed to delete: " + item.fullPath);
        }
    }
}

void FilesScreen::refreshCurrentDirectory()
{
    loadDirectory(currentPath);
    draw(EinkDisplayManager::UPDATE_PARTIAL);
}

String FilesScreen::getCurrentPath() const
{
    return currentPath;
}

bool FilesScreen::isAtRoot() const
{
    return currentPath == "/" || currentPath.isEmpty();
}

void FilesScreen::showGlobalMenu()
{
    globalMenu.isVisible = true;
    globalMenu.selectedOption = 0;

    // Update menu options based on context
    globalMenu.options.clear();

    if (!currentItems.empty() && selectedItemIndex >= 0 && selectedItemIndex < currentItems.size())
    {
        const FileItem &item = currentItems[selectedItemIndex];
        if (item.isDirectory)
        {
            globalMenu.title = "Folder Options";
            globalMenu.options.push_back("Open Folder");
            globalMenu.options.push_back("Delete Folder");
        }
        else
        {
            globalMenu.title = "File Options";
            globalMenu.options.push_back("View File Info");
            globalMenu.options.push_back("Delete File");
        }
    }
    else
    {
        globalMenu.title = "Files Menu";
        globalMenu.options.push_back("Refresh");
        globalMenu.options.push_back("Go to Root");
    }

    globalMenu.options.push_back("Back to Main Menu");
    globalMenu.options.push_back("Cancel");

    draw(EinkDisplayManager::UPDATE_PARTIAL);
}

void FilesScreen::hideGlobalMenu()
{
    globalMenu.isVisible = false;
    draw(EinkDisplayManager::UPDATE_PARTIAL);
}

void FilesScreen::handleGlobalMenuSelect()
{
    if (globalMenu.selectedOption >= 0 && globalMenu.selectedOption < globalMenu.options.size())
    {
        String selectedOption = globalMenu.options[globalMenu.selectedOption];

        if (selectedOption == "Open Folder")
        {
            hideGlobalMenu();
            if (selectedItemIndex >= 0 && selectedItemIndex < currentItems.size())
            {
                navigateToDirectory(currentItems[selectedItemIndex].fullPath);
            }
        }
        else if (selectedOption == "Delete Folder" || selectedOption == "Delete File")
        {
            hideGlobalMenu();
            deleteSelectedFile();
        }
        else if (selectedOption == "View File Info")
        {
            hideGlobalMenu();
            // TODO: Implement file info display
            Serial.println("[Files] File info not implemented yet");
        }
        else if (selectedOption == "Refresh")
        {
            hideGlobalMenu();
            refreshCurrentDirectory();
        }
        else if (selectedOption == "Go to Root")
        {
            hideGlobalMenu();
            pathHistory.clear();
            navigateToDirectory("/");
        }
        else if (selectedOption == "Back to Main Menu")
        {
            hideGlobalMenu();
            // Signal to return to main menu - this will be handled by the main UI
            Serial.println("[Files] Returning to main menu");
            // The actual screen change will be handled by the UP button logic in main UI
        }
        else if (selectedOption == "Cancel")
        {
            hideGlobalMenu();
        }
    }
}

void FilesScreen::drawHeader()
{
    extern EinkDisplayManager display;

    // Draw status bar first
    extern void drawStatusBar();
    drawStatusBar();

    // Draw screen title
    display.m_display.setFont(&FreeMonoBold18pt7b);
    display.drawCenteredText("Files", 50, &FreeMonoBold18pt7b);
}

void FilesScreen::drawFileList()
{
    extern EinkDisplayManager display;

    int startY = 100;
    int lineHeight = 18;                                            // Reduced line height for more items
    int availableHeight = display.m_display.height() - startY - 20; // Use full height minus margins
    int maxVisible = availableHeight / lineHeight;                  // Calculate max items dynamically

    // Calculate scroll offset
    int scrollOffset = 0;
    if (selectedItemIndex >= maxVisible)
    {
        scrollOffset = selectedItemIndex - maxVisible + 1;
    }

    for (int i = 0; i < min((int)currentItems.size(), maxVisible); i++)
    {
        int itemIndex = i + scrollOffset;
        if (itemIndex >= currentItems.size())
            break;

        const FileItem &item = currentItems[itemIndex];
        int y = startY + (i * lineHeight);

        // Highlight selected item
        if (itemIndex == selectedItemIndex)
        {
            display.m_display.fillRect(2, y - 13, display.m_display.width() - 4, lineHeight, GxEPD_BLACK);
            display.m_display.setTextColor(GxEPD_WHITE);
        }
        else
        {
            display.m_display.setTextColor(GxEPD_BLACK);
        }

        // Draw file icon and name with text clipping
        display.m_display.setFont(&FreeMono9pt7b);
        String icon = getFileIcon(item);

        // Calculate available width for filename (accounting for icon, size, and margins)
        int iconWidth = 50;                                                              // Approximate width for icon + space
        int sizeWidth = item.isDirectory ? 0 : 60;                                       // Space reserved for file size
        int availableNameWidth = display.m_display.width() - iconWidth - sizeWidth - 20; // Total margins

        // Truncate filename to exactly 14 characters if too long, preserving extension
        String displayName = item.name;

        // Limit filename to 14 characters with ellipsis if needed, keeping extension
        if (displayName.length() > 13)
        {
            // Find the last dot for extension
            int lastDot = displayName.lastIndexOf('.');
            if (lastDot > 0 && lastDot < displayName.length() - 1)
            {
                // Has extension - preserve it
                String extension = displayName.substring(lastDot);
                String baseName = displayName.substring(0, lastDot);
                int maxBaseLength = 13 - extension.length() - 3; // 3 for "..."
                if (maxBaseLength > 0)
                {
                    displayName = baseName.substring(0, maxBaseLength) + "" + extension;
                }
                else
                {
                    // Extension too long, just truncate normally
                    displayName = displayName.substring(0, 13) + "";
                }
            }
            else
            {
                // No extension, just truncate
                displayName = displayName.substring(0, 13) + "";
            }
        }

        display.m_display.setCursor(5, y);
        display.m_display.print(icon + " " + displayName);

        // Draw file size for files (right-aligned)
        if (!item.isDirectory)
        {
            String sizeStr = formatFileSize(item.size);
            int16_t x1, y1;
            uint16_t w, h;
            display.m_display.getTextBounds(sizeStr.c_str(), 0, 0, &x1, &y1, &w, &h);
            display.m_display.setCursor(display.m_display.width() - w - 5, y);
            display.m_display.print(sizeStr);
        }

        // Reset text color
        display.m_display.setTextColor(GxEPD_BLACK);
    }

    // Draw scroll indicator if needed
    if (currentItems.size() > maxVisible)
    {
        int scrollBarHeight = availableHeight - 10; // Use available height for scroll bar
        int scrollBarY = startY + 5;
        int scrollBarX = display.m_display.width() - 6;

        // Draw scroll track
        display.m_display.drawRect(scrollBarX, scrollBarY, 3, scrollBarHeight, GxEPD_BLACK);

        // Draw scroll thumb
        int thumbHeight = max(8, (int)(scrollBarHeight * maxVisible / currentItems.size()));
        int maxScrollOffset = currentItems.size() - maxVisible;
        int thumbY = scrollBarY + (maxScrollOffset > 0 ? (int)(scrollBarHeight * scrollOffset / maxScrollOffset) : 0);

        // Ensure thumb doesn't go beyond track
        if (thumbY + thumbHeight > scrollBarY + scrollBarHeight)
        {
            thumbY = scrollBarY + scrollBarHeight - thumbHeight;
        }

        display.m_display.fillRect(scrollBarX + 1, thumbY, 1, thumbHeight, GxEPD_BLACK);
    }
}

void FilesScreen::drawLoadingIndicator()
{
    extern EinkDisplayManager display;

    display.m_display.setFont(&FreeMono12pt7b);
    display.drawCenteredText("Loading...", 150, &FreeMono12pt7b);
}

void FilesScreen::drawGlobalMenuDialog()
{
    extern EinkDisplayManager display;

    // Dialog dimensions
    int dialogWidth = 200;
    int dialogHeight = 150;
    int dialogX = (display.m_display.width() - dialogWidth) / 2;
    int dialogY = (display.m_display.height() - dialogHeight) / 2;

    // Draw dialog background and border
    display.m_display.fillRect(dialogX, dialogY, dialogWidth, dialogHeight, GxEPD_WHITE);
    display.m_display.drawRect(dialogX, dialogY, dialogWidth, dialogHeight, GxEPD_BLACK);
    display.m_display.drawRect(dialogX + 1, dialogY + 1, dialogWidth - 2, dialogHeight - 2, GxEPD_BLACK);

    // Draw title
    display.m_display.setFont(&FreeMonoBold12pt7b);
    int titleY = dialogY + 25;
    display.drawCenteredText(globalMenu.title.c_str(), titleY, &FreeMonoBold12pt7b);

    // Draw separator line
    display.m_display.drawLine(dialogX + 10, titleY + 10, dialogX + dialogWidth - 10, titleY + 10, GxEPD_BLACK);

    // Draw menu options
    display.m_display.setFont(&FreeMono9pt7b);
    int optionY = titleY + 25;
    int lineHeight = 16;

    for (int i = 0; i < globalMenu.options.size(); i++)
    {
        int y = optionY + (i * lineHeight);

        // Highlight selected option
        if (i == globalMenu.selectedOption)
        {
            display.m_display.fillRect(dialogX + 5, y - 12, dialogWidth - 10, lineHeight, GxEPD_BLACK);
            display.m_display.setTextColor(GxEPD_WHITE);
        }
        else
        {
            display.m_display.setTextColor(GxEPD_BLACK);
        }

        display.m_display.setCursor(dialogX + 10, y);
        display.m_display.print(globalMenu.options[i].c_str());
    }

    // Reset text color
    display.m_display.setTextColor(GxEPD_BLACK);
}

void FilesScreen::drawPathBreadcrumb()
{
    extern EinkDisplayManager display;

    display.m_display.setFont(&FreeMono9pt7b);
    String pathDisplay = currentPath;
    if (pathDisplay.length() > 30)
    {
        pathDisplay = "..." + pathDisplay.substring(pathDisplay.length() - 27);
    }

    display.m_display.setCursor(10, 80);
    display.m_display.print("Path: " + pathDisplay);
}

void FilesScreen::loadDirectory(const String &path)
{
    isLoading = true;
    currentItems.clear();

    // Ensure SD card is powered
    if (!isSDCardPowered())
    {
        if (!powerOnSDCard())
        {
            Serial.println("[Files] Failed to power on SD card");
            isLoading = false;
            // Add a placeholder item to show SD card error
            FileItem errorItem;
            errorItem.name = "SD Card Not Available";
            errorItem.fullPath = "";
            errorItem.isDirectory = false;
            errorItem.size = 0;
            currentItems.push_back(errorItem);
            return;
        }
    }

    // Add small delay to ensure SD card is ready
    delay(100);

    File dir = SD.open(path);
    if (!dir)
    {
        Serial.println("[Files] Failed to open directory: " + path);
        isLoading = false;
        // Add a placeholder item to show directory error
        FileItem errorItem;
        errorItem.name = "Directory Not Found";
        errorItem.fullPath = "";
        errorItem.isDirectory = false;
        errorItem.size = 0;
        currentItems.push_back(errorItem);
        return;
    }

    if (!dir.isDirectory())
    {
        Serial.println("[Files] Path is not a directory: " + path);
        dir.close();
        isLoading = false;
        return;
    }

    // Read directory contents
    File file = dir.openNextFile();
    while (file)
    {
        String fileName = String(file.name());

        // Skip config, log, and temp folders
        if (file.isDirectory() &&
            (fileName.equalsIgnoreCase("config") ||
             fileName.equalsIgnoreCase("log") ||
             fileName.equalsIgnoreCase("logs") ||
             fileName.equalsIgnoreCase("temp") ||
             fileName.equalsIgnoreCase("tmp") ||
             fileName.equalsIgnoreCase(".") ||
             fileName.equalsIgnoreCase("System Volume Information")))
        {
            file = dir.openNextFile();
            continue;
        }

        FileItem item;
        item.name = fileName;
        item.fullPath = path;
        if (!item.fullPath.endsWith("/"))
            item.fullPath += "/";
        item.fullPath += item.name;
        item.isDirectory = file.isDirectory();
        item.size = file.size();
        item.lastModified = ""; // TODO: Implement if needed

        currentItems.push_back(item);
        file = dir.openNextFile();
    }

    dir.close();

    // Sort items: directories first, then files, both alphabetically
    std::sort(currentItems.begin(), currentItems.end(), [](const FileItem &a, const FileItem &b)
              {
        if (a.isDirectory != b.isDirectory)
        {
            return a.isDirectory > b.isDirectory;
        }
        return a.name.compareTo(b.name) < 0; });

    ensureValidSelection();
    isLoading = false;
}

String FilesScreen::formatFileSize(size_t bytes)
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

String FilesScreen::getFileIcon(const FileItem &item)
{
    if (item.isDirectory)
    {
        return "[DIR]";
    }

    String name = item.name;
    name.toLowerCase();

    if (name.endsWith(".txt") || name.endsWith(".log"))
    {
        return "[TXT]";
    }
    else if (name.endsWith(".json"))
    {
        return "[JSON]";
    }
    else if (name.endsWith(".pdf"))
    {
        return "[PDF]";
    }
    else if (name.endsWith(".jpg") || name.endsWith(".png") || name.endsWith(".bmp"))
    {
        return "[IMG]";
    }
    else
    {
        return "[FILE]";
    }
}

bool FilesScreen::isValidPath(const String &path)
{
    return !path.isEmpty() && path.startsWith("/");
}

void FilesScreen::ensureValidSelection()
{
    if (currentItems.empty())
    {
        selectedItemIndex = 0;
    }
    else if (selectedItemIndex >= currentItems.size())
    {
        selectedItemIndex = currentItems.size() - 1;
    }
    else if (selectedItemIndex < 0)
    {
        selectedItemIndex = 0;
    }
}

void FilesScreen::scrollToSelection()
{
    // This is handled in drawFileList() with scroll offset calculation
}

void FilesScreen::initializeGlobalMenu()
{
    globalMenu.isVisible = false;
    globalMenu.selectedOption = 0;
    globalMenu.title = "Files Menu";
    globalMenu.options.clear();
}