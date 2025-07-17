#include "display.h"
#include "icons.h"
#include <Arduino.h>

EinkDisplayManager::EinkDisplayManager() : m_display(GxEPD2_370_GDEY037T03(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY))
{
    m_state = {.initialized = false, .sleeping = false, .dirty = false, .last_full_refresh = 0, .partial_update_count = 0};
}

void EinkDisplayManager::begin()
{
    Serial.println("Initializing display...");
    m_display.init(115200, true, 2, false);
    m_display.setRotation(0);
    m_display.setTextColor(GxEPD_BLACK);
    m_state.initialized = true;
    m_state.sleeping = false;
}

void EinkDisplayManager::sleep()
{
    if (!m_state.initialized)
        return;
    m_display.hibernate();
    m_state.sleeping = true;
}

void EinkDisplayManager::wake()
{
    if (m_state.sleeping)
    {
        m_display.init(115200, true, 2, false);
        m_state.sleeping = false;
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
        partial_update = false;
    }

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

    // Smaller icon dimensions
    int icon_width = 20;
    int icon_height = 10;
    int terminal_width = 2;
    int terminal_height = 5;

    m_display.drawRect(x, y, icon_width, icon_height, GxEPD_BLACK);
    m_display.drawRect(x + icon_width, y + (icon_height - terminal_height) / 2, terminal_width, terminal_height, GxEPD_BLACK);
    int fill_width = (int)((percentage / 100.0) * (icon_width - 2.0));
    if (fill_width > 0)
    {
        m_display.fillRect(x + 1, y + 1, fill_width, icon_height - 2, GxEPD_BLACK);
    }
}

void EinkDisplayManager::drawWifiIcon(int x, int y, bool connected)
{
    if (!m_state.initialized)
        return;

    // TODO: Replace with a proper disconnected icon
    const unsigned char *icon = connected ? wifi_icon : wifi_icon;
    m_display.drawXBitmap(x, y, icon, 48, 48, GxEPD_BLACK);
}