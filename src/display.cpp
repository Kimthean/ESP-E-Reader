#include "display.h"
#include <Arduino.h>

EinkDisplayManager::EinkDisplayManager() : m_display(GxEPD2_370_GDEY037T03(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY))
{
    m_state = {.initialized = false, .sleeping = false, .dirty = false, .last_full_refresh = 0, .partial_update_count = 0};
}

void EinkDisplayManager::begin()
{
    Serial.println("Initializing display...");
    // Use false for second parameter to prevent full refresh on each boot
    m_display.init(115200, false, 10, false);
    m_display.setRotation(0);
    m_display.setTextColor(GxEPD_BLACK);
    m_state.initialized = true;
    m_state.sleeping = false;
}

void EinkDisplayManager::sleep()
{
    if (!m_state.initialized)
        return;
    Serial.println("[DISPLAY] EinkDisplayManager::sleep() - Putting display to hibernate mode");
    m_display.hibernate();
    m_state.sleeping = true;
    Serial.println("[DISPLAY] Display hibernation complete");
}

void EinkDisplayManager::wake()
{
    if (m_state.sleeping)
    {
        Serial.println("[DISPLAY] EinkDisplayManager::wake() - Waking display from hibernate");
        m_display.init(115200, true, 2, false);
        m_state.sleeping = false;
        // Mark as dirty to ensure content gets redrawn
        m_state.dirty = true;
        Serial.println("[DISPLAY] Display wake complete, marked as dirty for redraw");
    }
    else
    {
        Serial.println("[DISPLAY] EinkDisplayManager::wake() called but display was not sleeping");
    }
}

void EinkDisplayManager::startDrawing()
{
    if (!m_state.initialized)
        return;
    m_display.setFullWindow();
    m_display.fillScreen(GxEPD_WHITE);
}

void EinkDisplayManager::endDrawing()
{
    m_state.dirty = true;
}

void EinkDisplayManager::update(DisplayUpdateMode mode)
{
    if (!m_state.initialized || !m_state.dirty)
        return;

    bool partial_update = (mode == UPDATE_PARTIAL);

    if (mode == UPDATE_FAST)
    {
        partial_update = true;
    }

    // Check if we need to wipe screen due to too many partial updates
    const int MAX_PARTIAL_UPDATES = 10;
    if (partial_update && m_state.partial_update_count >= MAX_PARTIAL_UPDATES)
    {
        Serial.println("Auto-wiping screen after multiple partial updates");
        wipeScreen();
        m_state.partial_update_count = 0;
    }

    // Use standard display update for all modes
    m_display.display(partial_update);

    if (!partial_update)
    {
        m_state.last_full_refresh = millis();
        m_state.partial_update_count = 0;
    }
    else
    {
        m_state.partial_update_count++;
    }

    m_state.dirty = false;
}

void EinkDisplayManager::drawCenteredText(const char *text, int y, const GFXfont *font)
{
    if (!m_state.initialized)
        return;
    m_display.setFont(font);
    int16_t x1, y1;
    uint16_t w, h;
    m_display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
    m_display.setCursor((m_display.width() - w) / 2, y);
    m_display.print(text);
}

void EinkDisplayManager::drawBatteryIcon(int x, int y, float battery_voltage, bool charging)
{
    if (!m_state.initialized)
        return;

    float percentage = ((battery_voltage - 3.2) / (4.2 - 3.2)) * 100.0;
    if (percentage < 0)
        percentage = 0;
    if (percentage > 100)
        percentage = 100;

    // Improved battery icon dimensions
    int icon_width = 16;
    int icon_height = 8;
    int terminal_width = 2;
    int terminal_height = 4;

    // Draw battery outline
    m_display.drawRect(x, y, icon_width, icon_height, GxEPD_BLACK);

    // Draw battery terminal (positive end)
    m_display.fillRect(x + icon_width, y + (icon_height - terminal_height) / 2, terminal_width, terminal_height, GxEPD_BLACK);

    // Calculate and draw battery fill based on percentage
    int fill_width = (int)((percentage / 100.0) * (icon_width - 2.0));
    if (fill_width > 0)
    {
        // Different fill patterns based on battery level
        if (percentage > 20)
        {
            // Normal fill for healthy battery
            m_display.fillRect(x + 1, y + 1, fill_width, icon_height - 2, GxEPD_BLACK);
        }
        else
        {
            // Striped pattern for low battery warning
            for (int i = 0; i < fill_width; i += 2)
            {
                m_display.drawLine(x + 1 + i, y + 1, x + 1 + i, y + icon_height - 2, GxEPD_BLACK);
            }
        }
    }

    // Draw charging indicator if charging
    if (charging)
    {
        // Draw small lightning bolt in center
        int bolt_x = x + icon_width / 2 - 1;
        int bolt_y = y + 2;

        // Lightning bolt shape (simplified)
        m_display.drawLine(bolt_x, bolt_y, bolt_x + 2, bolt_y + 2, GxEPD_WHITE);
        m_display.drawLine(bolt_x + 2, bolt_y + 2, bolt_x, bolt_y + 4, GxEPD_WHITE);
        m_display.drawPixel(bolt_x + 1, bolt_y + 2, GxEPD_WHITE);
    }
}

void EinkDisplayManager::drawWifiIcon(int x, int y, bool connected)
{
    if (!m_state.initialized)
        return;

    if (connected)
    {
        // Draw WiFi signal strength bars (3 bars)
        // Bar 1 (shortest)
        m_display.fillRect(x, y + 8, 2, 2, GxEPD_BLACK);

        // Bar 2 (medium)
        m_display.fillRect(x + 3, y + 6, 2, 4, GxEPD_BLACK);

        // Bar 3 (tallest)
        m_display.fillRect(x + 6, y + 4, 2, 6, GxEPD_BLACK);
    }
    else
    {
        // Draw disconnected WiFi icon (X over bars)
        // Draw faded bars
        m_display.drawRect(x, y + 8, 2, 2, GxEPD_BLACK);
        m_display.drawRect(x + 3, y + 6, 2, 4, GxEPD_BLACK);
        m_display.drawRect(x + 6, y + 4, 2, 6, GxEPD_BLACK);

        // Draw X to indicate disconnection
        m_display.drawLine(x, y, x + 10, y + 10, GxEPD_BLACK);
        m_display.drawLine(x + 10, y, x, y + 10, GxEPD_BLACK);
    }
}

void EinkDisplayManager::wipeScreen()
{
    if (!m_state.initialized)
        return;

    m_display.setPartialWindow(0, 0, m_display.width(), m_display.height());

    m_display.firstPage();
    do
    {
        m_display.fillRect(0, 0, m_display.width(), m_display.height(), GxEPD_BLACK);
    } while (m_display.nextPage());

    delay(10);

    m_display.firstPage();
    do
    {
        m_display.fillRect(0, 0, m_display.width(), m_display.height(), GxEPD_WHITE);
    } while (m_display.nextPage());

    m_display.setFullWindow();
}

void EinkDisplayManager::resetPartialUpdateCount()
{
    m_state.partial_update_count = 0;
}