/*

NicheGraphics setup for LilyGO T5S3 4.7" E-Paper PRO

This file configures InkHUD for the T5S3 E-Paper PRO device.
The display (ED047TC1) uses a parallel interface driven by epdiy v7,
which is different from the SPI-based displays used by other InkHUD variants.

*/

#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

// InkHUD-specific components
// ---------------------------
#include "graphics/niche/InkHUD/InkHUD.h"

// Applets
#include "graphics/niche/InkHUD/Applets/User/AllMessage/AllMessageApplet.h"
#include "graphics/niche/InkHUD/Applets/User/DM/DMApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Heard/HeardApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Positions/PositionsApplet.h"
#include "graphics/niche/InkHUD/Applets/User/RecentsList/RecentsListApplet.h"
#include "graphics/niche/InkHUD/Applets/User/ThreadedMessage/ThreadedMessageApplet.h"

// Shared NicheGraphics components
// --------------------------------
#include "graphics/niche/Drivers/EInk/ED047TC1_Epdiy.h"
#include "graphics/niche/Inputs/TwoButton.h"

void setupNicheGraphics()
{
    using namespace NicheGraphics;

    // E-Ink Driver (parallel bus via epdiy, not SPI)
    // -----------------------------------------------

    Drivers::ED047TC1_Epdiy *driver = new Drivers::ED047TC1_Epdiy;
    driver->begin(); // Initializes epdiy v7, sets VCOM, clears display

    // InkHUD
    // ----------------------------

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();

    // Set the E-Ink driver
    inkhud->setDriver(driver);

    // Set how many FAST updates per FULL update
    // ED047TC1 parallel displays can handle more fast updates before ghosting
    inkhud->setDisplayResilience(10, 1.5);

    // Select fonts - all 12pt (largest available) for this 960x540 display
    InkHUD::Applet::fontLarge = FREESANS_12PT_WIN1252;
    InkHUD::Applet::fontMedium = FREESANS_12PT_WIN1252;
    InkHUD::Applet::fontSmall = FREESANS_12PT_WIN1252;

    // Customize default settings
    // 960x540 is large enough for multiple tiles
    inkhud->persistence->settings.userTiles.maxCount = 4;
    inkhud->persistence->settings.rotation = 3;        // 270 degrees clockwise (portrait, USB at bottom)
    inkhud->persistence->settings.userTiles.count = 2; // Two tiles by default

    // Pick applets
    // Note: order of applets determines priority of "auto-show" feature
    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, true, true); // Activated, autoshown
    inkhud->addApplet("DMs", new InkHUD::DMApplet);                              // -
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0));        // -
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1));        // -
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true);           // Activated
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet);            // -
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false, 0);         // Activated, not autoshown, default on tile 0

    // Start running InkHUD
    inkhud->begin();

    // Buttons
    // --------------------------

    Inputs::TwoButton *buttons = Inputs::TwoButton::getInstance();

    // #0: BOOT button as main user button
    buttons->setWiring(0, 0); // GPIO 0 = BOOT button
    buttons->setHandlerShortPress(0, [inkhud]() { inkhud->shortpress(); });
    buttons->setHandlerLongPress(0, [inkhud]() { inkhud->longpress(); });

    // Begin handling button events
    buttons->start();
}

#endif
