/*

E-Ink display driver for parallel epdiy-based displays
    - ED047TC1 (4.7 inch, 960x540)
    - Used on LilyGO T5S3 4.7" E-Paper PRO
    - Driven via epdiy v7 board (parallel bus, not SPI)

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./EInk.h"

// epdiy C API
extern "C" {
#include <epd_highlevel.h>
#include <epdiy.h>
}

namespace NicheGraphics::Drivers
{

class ED047TC1_Epdiy : public EInk
{
    // Display properties
  private:
    static constexpr uint32_t panelWidth = 960;
    static constexpr uint32_t panelHeight = 540;
    static constexpr UpdateTypes supported = (UpdateTypes)(FULL | FAST);

  public:
    ED047TC1_Epdiy() : EInk(panelWidth, panelHeight, supported) {}

    // epdiy uses parallel bus, not SPI. This override does nothing.
    void begin(SPIClass *spi, uint8_t pin_dc, uint8_t pin_cs, uint8_t pin_busy, uint8_t pin_rst = -1) override {}

    // Actual initialization for epdiy parallel display
    void begin();

    // Change the display image
    void update(uint8_t *imageData, UpdateTypes type) override;

  protected:
    bool isUpdateDone() override;
    void finalizeUpdate() override {}

  private:
    EpdiyHighlevelState hl;

    // Convert InkHUD 1bpp packed image to epdiy 4bpp framebuffer
    void convert1bppTo4bpp(const uint8_t *src1bpp, uint8_t *dst4bpp);
};

} // namespace NicheGraphics::Drivers

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
