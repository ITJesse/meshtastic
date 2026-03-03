#include "configuration.h"

#ifdef USE_EINK_EPDIY
#include "EInkEpdiyDisplay.h"
#include "main.h"

// Pixel scaling factor: OLEDDisplay renders at (EINK_WIDTH / EINK_SCALE) x (EINK_HEIGHT / EINK_SCALE)
// then the image is upscaled by EINK_SCALE when writing to the epdiy framebuffer.
// This makes UI elements and fonts appear larger on the high-res e-paper display.
#ifndef EINK_SCALE
#define EINK_SCALE 2
#endif

// Constructor
EInkEpdiyDisplay::EInkEpdiyDisplay(uint8_t address, int sda, int scl, OLEDDISPLAY_GEOMETRY geometry, HW_I2C i2cBus)
{
    // Set logical dimensions in OLEDDisplay base class (what the UI renders to)
    this->geometry = GEOMETRY_RAWMODE;
    this->displayWidth = EINK_WIDTH / EINK_SCALE;   // Logical width (e.g. 480 for 960/2)
    this->displayHeight = EINK_HEIGHT / EINK_SCALE; // Logical height (e.g. 270 for 540/2)

    // Round shortest side up to nearest byte, to prevent truncation causing an undersized buffer
    uint16_t shortSide = min(displayWidth, displayHeight);
    uint16_t longSide = max(displayWidth, displayHeight);
    if (shortSide % 8 != 0)
        shortSide = (shortSide | 7) + 1;

    this->displayBufferSize = longSide * (shortSide / 8);
}

/**
 * Force a display update if we haven't drawn within the specified msecLimit
 */
bool EInkEpdiyDisplay::forceDisplay(uint32_t msecLimit)
{
    uint32_t now = millis();
    uint32_t sinceLast = now - lastDrawMsec;

    if (sinceLast > msecLimit || lastDrawMsec == 0)
        lastDrawMsec = now;
    else
        return false;

    // Get the epdiy 4bpp framebuffer
    uint8_t *fb = epd_hl_get_framebuffer(&hl);

    // Reset diff state for full redraw
    epd_hl_set_all_white(&hl);

    // epdiy framebuffer is ALWAYS in native panel dimensions (960x540 for ED047TC1)
    // regardless of epd_set_rotation(). Row stride = native_width / 2 bytes.
    const uint32_t nativeW = epd_width();  // 960 (physical panel width)
    const uint32_t nativeH = epd_height(); // 540 (physical panel height)
    const uint32_t fbStride = nativeW / 2; // 480 bytes per row in 4bpp

    // Clear framebuffer to white (0xFF = white in 4bpp)
    memset(fb, 0xFF, nativeW * nativeH / 2);

    // Convert OLEDDisplay 1bpp buffer to epdiy 4bpp framebuffer with upscaling
    //
    // OLEDDisplay buffer is displayWidth x displayHeight (e.g. 480x270)
    // epdiy framebuffer is nativeW x nativeH (960x540)
    // Each logical pixel maps to a EINK_SCALE x EINK_SCALE block of physical pixels
    const bool flipped = config.display.flip_screen;

    for (uint32_t y = 0; y < displayHeight; y++) {
        for (uint32_t x = 0; x < displayWidth; x++) {
            // Read from OLEDDisplay column-major buffer
            auto b = buffer[x + (y / 8) * displayWidth];
            auto isset = b & (1 << (y & 7));

            if (isset) {
                // WHITE(1) in OLEDDisplay buffer = BLACK on e-paper
                uint32_t baseX, baseY;
                if (flipped) {
                    baseX = (displayWidth - 1 - x) * EINK_SCALE;
                    baseY = (displayHeight - 1 - y) * EINK_SCALE;
                } else {
                    baseX = x * EINK_SCALE;
                    baseY = y * EINK_SCALE;
                }

                // Fill a EINK_SCALE x EINK_SCALE block of black pixels
                for (uint32_t sy = 0; sy < EINK_SCALE; sy++) {
                    for (uint32_t sx = 0; sx < EINK_SCALE; sx++) {
                        uint32_t px = baseX + sx;
                        uint32_t py = baseY + sy;
                        if (px < nativeW && py < nativeH) {
                            uint32_t fbIndex = py * fbStride + px / 2;
                            if (px % 2 == 0) {
                                fb[fbIndex] &= 0xF0; // Even pixel → low nibble → black
                            } else {
                                fb[fbIndex] &= 0x0F; // Odd pixel → high nibble → black
                            }
                        }
                    }
                }
            }
        }
    }

    // Power on → synchronous refresh → power off
    LOG_DEBUG("Update epdiy E-Paper");
    epd_poweron();
    enum EpdDrawError err = epd_hl_update_screen(&hl, MODE_GC16, epd_ambient_temperature());
    epd_poweroff();

    if (err != EPD_DRAW_SUCCESS) {
        LOG_ERROR("epdiy draw error: 0x%X", err);
    }

    // End the update process
    endUpdate();

    LOG_DEBUG("done");
    return true;
}

// End the update process
void EInkEpdiyDisplay::endUpdate()
{
    // epdiy power is already managed in forceDisplay()
    // Nothing additional needed here
}

// Write the buffer to the display memory
void EInkEpdiyDisplay::display(void)
{
    // We don't allow regular 'dumb' display() calls to draw on eink until we've shown
    // at least one forceDisplay() keyframe. This prevents flashing when we show the critical
    // bootscreen (that we want to look nice)
    if (lastDrawMsec) {
        forceDisplay(slowUpdateMsec); // Show the first screen a few seconds after boot, then slower
    }
}

// Send a command to the display (low level function)
void EInkEpdiyDisplay::sendCommand(uint8_t com)
{
    (void)com;
    // Drop all commands to device (we just update the buffer)
}

void EInkEpdiyDisplay::setDetected(uint8_t detected)
{
    (void)detected;
}

// Connect to the display - epdiy specific initialization
bool EInkEpdiyDisplay::connect()
{
    LOG_INFO("Do EInk epdiy init");

    // Initialize epdiy with v7 board and ED047TC2 waveform
    // ED047TC2 produces cleaner font rendering with fewer artifacts on the T5S3 PRO
    epd_init(&epd_board_t5s3_pro, &ED047TC2, EPD_LUT_64K);

    // Set VCOM voltage via TPS65185 (I2C)
    epd_set_vcom(1560);

    // Initialize high-level API (allocates front_fb + back_fb + difference_fb in PSRAM)
    hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);

    // Use landscape orientation — matches native panel dims (960x540)
    // so OLEDDisplay buffer coords map directly to framebuffer coords (with scaling)
    epd_set_rotation(EPD_ROT_LANDSCAPE);

    LOG_INFO("epdiy panel: native %d x %d, logical %d x %d (scale %d)", epd_width(), epd_height(), displayWidth, displayHeight,
             EINK_SCALE);

    // Initial full clear of the physical display
    epd_poweron();
    epd_clear();
    epd_poweroff();

    LOG_INFO("ED047TC1 epdiy display initialized for default UI");

    return true;
}

#endif // USE_EINK_EPDIY
