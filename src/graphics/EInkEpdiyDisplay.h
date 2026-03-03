#pragma once

#ifdef USE_EINK_EPDIY

#include <OLEDDisplay.h>

// epdiy C API
extern "C" {
#include <epd_highlevel.h>
#include <epdiy.h>
#include "epd_board_t5s3_pro.h"
}

/**
 * An adapter class that allows using the epdiy library as if it was an OLEDDisplay implementation.
 *
 * This is used for the LilyGO T5S3 4.7" E-Paper PRO (ED047TC1) which uses a parallel bus
 * driven by epdiy, rather than the SPI-based displays driven by GxEPD2.
 *
 * This class is a parallel to EInkDisplay (GxEPD2 backend) - both inherit from OLEDDisplay.
 */
class EInkEpdiyDisplay : public OLEDDisplay
{
    /// How often should we update the display
    /// thereafter we do once per 5 minutes
    uint32_t slowUpdateMsec = 5 * 60 * 1000;

  public:
    // Constructor
    EInkEpdiyDisplay(uint8_t, int, int, OLEDDISPLAY_GEOMETRY, HW_I2C);

    // Write the buffer to the display memory (for eink we only do this occasionally)
    virtual void display(void) override;

    /**
     * Force a display update if we haven't drawn within the specified msecLimit
     *
     * @return true if we did draw the screen
     */
    bool forceDisplay(uint32_t msecLimit = 1000);

    /**
     * Run any code needed to complete an update, after the physical refresh has completed.
     */
    void endUpdate();

    /**
     * Shim to make the abstraction happy
     */
    void setDetected(uint8_t detected);

  protected:
    // the header size of the buffer used, e.g. for the SPI command header
    virtual int getBufferOffset(void) override { return 0; }

    // Send a command to the display (low level function)
    virtual void sendCommand(uint8_t com) override;

    // Connect to the display
    virtual bool connect() override;

  private:
    EpdiyHighlevelState hl;
    uint32_t lastDrawMsec = 0;
};

#endif // USE_EINK_EPDIY
