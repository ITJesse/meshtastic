#include "configuration.h"

#ifdef USE_EINK_EPDIY
#include "EInkEpdiyDisplay.h"
#include "main.h"

#ifndef EINK_SCALE
#define EINK_SCALE 2
#endif

#ifndef EINK_SAFE_AREA_LEFT
#define EINK_SAFE_AREA_LEFT 0
#endif
#ifndef EINK_SAFE_AREA_RIGHT
#define EINK_SAFE_AREA_RIGHT 0
#endif
#ifndef EINK_SAFE_AREA_TOP
#define EINK_SAFE_AREA_TOP 0
#endif
#ifndef EINK_SAFE_AREA_BOTTOM
#define EINK_SAFE_AREA_BOTTOM 0
#endif

EInkEpdiyDisplay::EInkEpdiyDisplay(uint8_t address, int sda, int scl, OLEDDISPLAY_GEOMETRY geometry, HW_I2C i2cBus)
{
    this->geometry = GEOMETRY_RAWMODE;
    this->displayWidth = (EINK_WIDTH - EINK_SAFE_AREA_LEFT - EINK_SAFE_AREA_RIGHT) / EINK_SCALE;
    this->displayHeight = (EINK_HEIGHT - EINK_SAFE_AREA_TOP - EINK_SAFE_AREA_BOTTOM) / EINK_SCALE;

    // Round shortest side up to nearest byte to prevent undersized buffer
    uint16_t shortSide = min(displayWidth, displayHeight);
    uint16_t longSide = max(displayWidth, displayHeight);
    if (shortSide % 8 != 0)
        shortSide = (shortSide | 7) + 1;

    this->displayBufferSize = longSide * (shortSide / 8);
}

void EInkEpdiyDisplay::renderToFramebuffer(uint8_t *fb)
{
    const uint32_t nativeW = epd_width();
    const uint32_t nativeH = epd_height();
    const uint32_t fbStride = nativeW / 2; // 4bpp

    memset(fb, 0xFF, nativeW * nativeH / 2);

    const bool flipped = config.display.flip_screen;

    for (uint32_t y = 0; y < displayHeight; y++) {
        for (uint32_t x = 0; x < displayWidth; x++) {
            auto b = buffer[x + (y / 8) * displayWidth];
            auto isset = b & (1 << (y & 7));

            if (isset) {
                // OLEDDisplay bit=1 means BLACK on e-paper
                uint32_t baseX, baseY;
                if (flipped) {
                    baseX = (displayWidth - 1 - x) * EINK_SCALE + EINK_SAFE_AREA_LEFT;
                    baseY = (displayHeight - 1 - y) * EINK_SCALE + EINK_SAFE_AREA_TOP;
                } else {
                    baseX = x * EINK_SCALE + EINK_SAFE_AREA_LEFT;
                    baseY = y * EINK_SCALE + EINK_SAFE_AREA_TOP;
                }

                for (uint32_t sy = 0; sy < EINK_SCALE; sy++) {
                    for (uint32_t sx = 0; sx < EINK_SCALE; sx++) {
                        uint32_t px = baseX + sx;
                        uint32_t py = baseY + sy;
                        if (px < nativeW && py < nativeH) {
                            uint32_t fbIndex = py * fbStride + px / 2;
                            if (px % 2 == 0)
                                fb[fbIndex] &= 0xF0;
                            else
                                fb[fbIndex] &= 0x0F;
                        }
                    }
                }
            }
        }
    }
}

bool EInkEpdiyDisplay::forceDisplay(uint32_t msecLimit)
{
    uint32_t now = millis();
    uint32_t sinceLast = now - lastDrawMsec;

    if (sinceLast > msecLimit || lastDrawMsec == 0)
        lastDrawMsec = now;
    else
        return false;

    if (xSemaphoreTake(epdiyMutex, 0) != pdTRUE) {
        LOG_DEBUG("epdiy busy (cleanRefresh in progress), skipping frame");
        lastDrawMsec = 0;
        return false;
    }

    bool doFullRefresh = pendingFullRefresh || (fastRefreshCount >= fastRefreshLimit);

    uint8_t *fb = epd_hl_get_framebuffer(&hl);

    // Full: reset diff state so every pixel is redrawn
    // Fast: only drive changed pixels
    if (doFullRefresh) {
        epd_hl_set_all_white(&hl);
    }

    renderToFramebuffer(fb);

    enum EpdDrawMode mode = doFullRefresh ? MODE_GL16 : MODE_DU;

    LOG_DEBUG("Update epdiy E-Paper (%s)", doFullRefresh ? "FULL" : "FAST");
    epd_poweron();
    enum EpdDrawError err = epd_hl_update_screen(&hl, mode, epd_ambient_temperature());
    epd_poweroff();

    if (err != EPD_DRAW_SUCCESS) {
        LOG_ERROR("epdiy draw error: 0x%X", err);
    }

    if (doFullRefresh) {
        fastRefreshCount = 0;
        pendingFullRefresh = false;
    } else {
        fastRefreshCount++;
    }

    xSemaphoreGive(epdiyMutex);
    LOG_DEBUG("done");
    return true;
}

/**
 * Two-pass clean refresh:
 *   Pass 1: epd_clear() to physically clear the display
 *   Pass 2: re-render content with MODE_GC16
 */
void EInkEpdiyDisplay::cleanRefresh()
{
    xSemaphoreTake(epdiyMutex, portMAX_DELAY);

    LOG_INFO("Clean refresh: physical clear");
    epd_poweron();
    epd_clear();

    // Reset both front_fb and back_fb so diff drives ALL content pixels
    epd_hl_set_all_white(&hl);
    int fb_size = epd_width() / 2 * epd_height();
    memset(hl.back_fb, 0xFF, fb_size);

    renderToFramebuffer(epd_hl_get_framebuffer(&hl));

    LOG_INFO("Clean refresh: redraw with GC16");
    enum EpdDrawError err = epd_hl_update_screen(&hl, MODE_GC16, epd_ambient_temperature());
    epd_poweroff();

    if (err != EPD_DRAW_SUCCESS) {
        LOG_ERROR("epdiy clean refresh error: 0x%X", err);
    }

    fastRefreshCount = 0;
    pendingFullRefresh = false;
    lastDrawMsec = millis();

    xSemaphoreGive(epdiyMutex);
    LOG_INFO("Clean refresh done");
}

void EInkEpdiyDisplay::display(void)
{
    if (lastDrawMsec) {
        forceDisplay(slowUpdateMsec);
    }
}

void EInkEpdiyDisplay::sendCommand(uint8_t com)
{
    (void)com;
}

void EInkEpdiyDisplay::setDetected(uint8_t detected)
{
    (void)detected;
}

bool EInkEpdiyDisplay::connect()
{
    LOG_INFO("Do EInk epdiy init");

    epdiyMutex = xSemaphoreCreateMutex();

    epd_init(&epd_board_t5s3_pro, &ED047TC2, EPD_LUT_64K);
    epd_set_vcom(1560);
    hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
    epd_set_rotation(EPD_ROT_LANDSCAPE);

#ifdef EINK_LCD_PIXEL_CLOCK_MHZ
    epd_set_lcd_pixel_clock_MHz(EINK_LCD_PIXEL_CLOCK_MHZ);
#endif

    LOG_INFO("epdiy panel: native %d x %d, logical %d x %d (scale %d)", epd_width(), epd_height(), displayWidth, displayHeight,
             EINK_SCALE);

    epd_poweron();
    epd_clear();
    epd_poweroff();

    LOG_INFO("epdiy display initialized");

    return true;
}

#endif // USE_EINK_EPDIY
