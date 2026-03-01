#include "./ED047TC1_Epdiy.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "main.h"

using namespace NicheGraphics::Drivers;

// Initialize the epdiy parallel e-ink display
// Must be called after Wire.begin() has been called for I2C
void ED047TC1_Epdiy::begin()
{
    // Initialize epdiy with v7 board and ED047TC1 display
    // Note: Using ED047TC2 waveform instead of ED047TC1.
    // ED047TC2 produces cleaner font rendering with fewer artifacts on the T5S3 PRO.
    epd_init(&epd_board_v7, &ED047TC2, EPD_LUT_64K);

    // Set VCOM voltage via TPS65185 (I2C)
    epd_set_vcom(1560);

    // Initialize high-level API (allocates front_fb + back_fb + difference_fb in PSRAM)
    hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);

    // Use landscape orientation - InkHUD manages its own rotation
    epd_set_rotation(EPD_ROT_LANDSCAPE);

    // Note: Do NOT call epd_set_lcd_pixel_clock_MHz() here.
    // The default epdiy bus speed is conservative but avoids "line buffer underrun"
    // errors that occur when PSRAM bandwidth is shared with Meshtastic tasks.

    // Initial full clear of the physical display
    epd_poweron();
    epd_clear();
    epd_poweroff();

    LOG_INFO("ED047TC1 epdiy display initialized (960x540, 4bpp)");
}

// Update the display with new image data from InkHUD
// imageData: 1bpp packed format (1=white, 0=black, MSB=leftmost pixel, 8 pixels per byte)
void ED047TC1_Epdiy::update(uint8_t *imageData, UpdateTypes type)
{
    // Get the epdiy 4bpp framebuffer
    uint8_t *fb = epd_hl_get_framebuffer(&hl);

    // For FULL updates: reset diff state to force full screen redraw
    // This ensures all pixels are re-driven for display health maintenance
    if (type == FULL) {
        epd_hl_set_all_white(&hl);
    }

    // Convert InkHUD 1bpp image to epdiy 4bpp framebuffer
    convert1bppTo4bpp(imageData, fb);

    // Select draw mode based on update type:
    // FULL: MODE_GC16 for high-quality full grayscale refresh (clears ghosting)
    // FAST: MODE_DU for fast monochrome differential update (only changed pixels)
    enum EpdDrawMode mode = (type == FULL) ? MODE_GC16 : MODE_DU;

    // Power on → synchronous refresh → power off
    epd_poweron();
    enum EpdDrawError err = epd_hl_update_screen(&hl, mode, epd_ambient_temperature());
    epd_poweroff();

    if (err != EPD_DRAW_SUCCESS) {
        LOG_ERROR("epdiy draw error: 0x%X", err);
    }

    // epdiy refresh is synchronous, so start polling with zero delay
    // The polling mechanism will immediately find isUpdateDone() == true
    beginPolling(10, 0);
}

// epdiy refresh is synchronous - always done when update() returns
bool ED047TC1_Epdiy::isUpdateDone()
{
    return true;
}

// Convert InkHUD 1bpp packed bitmap to epdiy 4bpp framebuffer
//
// InkHUD format: 1bpp, 8 pixels per byte, MSB = leftmost pixel
//   bit 1 = white, bit 0 = black
//   Row stride = ceil(width / 8) bytes
//
// epdiy format: 4bpp, 2 pixels per byte
//   In epdiy's epd_draw_pixel():
//     even x (left pixel)  → lower nibble  (color >> 4)
//     odd  x (right pixel) → upper nibble  (color & 0xF0)
//   0x00 = black, 0xFF = white
//   Row stride = width / 2 bytes
void ED047TC1_Epdiy::convert1bppTo4bpp(const uint8_t *src1bpp, uint8_t *dst4bpp)
{
    const uint32_t srcRowBytes = ((panelWidth - 1) / 8) + 1; // Same as Renderer::imageBufferWidth
    const uint32_t dstRowBytes = panelWidth / 2;

    for (uint32_t y = 0; y < panelHeight; y++) {
        const uint8_t *srcRow = src1bpp + y * srcRowBytes;
        uint8_t *dstRow = dst4bpp + y * dstRowBytes;

        for (uint32_t x = 0; x < panelWidth; x += 2) {
            // Extract two 1bpp pixels
            uint8_t srcByte0 = srcRow[x / 8];
            uint8_t bit0 = (srcByte0 >> (7 - (x % 8))) & 1;

            uint8_t srcByte1 = srcRow[(x + 1) / 8];
            uint8_t bit1 = (srcByte1 >> (7 - ((x + 1) % 8))) & 1;

            // Convert: 1bpp bit 1=white→0xF, 0=black→0x0
            // Pack per epdiy convention: even x → low nibble, odd x → high nibble
            uint8_t left  = bit0 ? 0x0F : 0x00;  // even x → low nibble
            uint8_t right = bit1 ? 0xF0 : 0x00;  // odd  x → high nibble
            dstRow[x / 2] = left | right;
        }
    }
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
