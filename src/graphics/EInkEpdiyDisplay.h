#pragma once

#ifdef USE_EINK_EPDIY

#include <OLEDDisplay.h>
#include <freertos/semphr.h>

extern "C" {
#include <epd_highlevel.h>
#include <epdiy.h>
#include "epd_board_t5s3_pro.h"
}

/**
 * Adapter class for epdiy-based E-Paper displays (LilyGO T5S3 4.7" PRO).
 * Parallel to EInkDisplay (GxEPD2 backend) - both inherit from OLEDDisplay.
 */
class EInkEpdiyDisplay : public OLEDDisplay
{
    uint32_t slowUpdateMsec = 30 * 1000;

  public:
    EInkEpdiyDisplay(uint8_t, int, int, OLEDDISPLAY_GEOMETRY, HW_I2C);

    virtual void display(void) override;
    bool forceDisplay(uint32_t msecLimit = 50);

    /// Force next update to use full refresh (MODE_GL16)
    void forceFullRefresh() { pendingFullRefresh = true; }

    /// Two-pass clean refresh: epd_clear() + MODE_GC16 redraw. Thread-safe.
    void cleanRefresh();

    void setDetected(uint8_t detected);

  protected:
    virtual int getBufferOffset(void) override { return 0; }
    virtual void sendCommand(uint8_t com) override;
    virtual bool connect() override;

  private:
    EpdiyHighlevelState hl;
    uint32_t lastDrawMsec = 0;
    SemaphoreHandle_t epdiyMutex = nullptr;

    /// Convert OLEDDisplay 1bpp buffer → epdiy 4bpp framebuffer with EINK_SCALE upscaling
    void renderToFramebuffer(uint8_t *fb);

    bool pendingFullRefresh = true;
    uint32_t fastRefreshCount = 0;
    static constexpr uint32_t fastRefreshLimit = 10;
};

#endif // USE_EINK_EPDIY
