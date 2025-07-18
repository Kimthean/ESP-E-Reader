#ifndef DISPLAY_H
#define DISPLAY_H

#include <GxEPD2_BW.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMono12pt7b.h>
#include <Fonts/FreeMono18pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include "pins.h"

class EinkDisplayManager
{
public:
    EinkDisplayManager();

    // Core functions
    void begin();
    void sleep();
    void wake();
    void startDrawing();
    void endDrawing();

    enum DisplayUpdateMode
    {
        UPDATE_FULL,
        UPDATE_PARTIAL,
        UPDATE_FAST
    };
    void update(DisplayUpdateMode mode = UPDATE_PARTIAL);

    // --- Public access to the display object and helpers ---
    GxEPD2_BW<GxEPD2_370_GDEY037T03, GxEPD2_370_GDEY037T03::HEIGHT> m_display;

    // Helper functions
    void drawCenteredText(const char *text, int y, const GFXfont *font);
    void drawBatteryIcon(int x, int y, float battery_voltage, bool charging);
    void drawWifiIcon(int x, int y, bool connected);
    
    // Screen clearing function to eliminate ghosting without flicker
    void wipeScreen();
    
    // Reset partial update counter (useful during active navigation)
    void resetPartialUpdateCount();

private:
    struct DisplayState
    {
        bool initialized;
        bool sleeping;
        bool dirty;
        unsigned long last_full_refresh;
        int partial_update_count;
    };
    DisplayState m_state;
};

#endif // DISPLAY_H