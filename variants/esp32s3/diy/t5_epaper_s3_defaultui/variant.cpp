#include "variant.h"
#include "Arduino.h"
#include "TouchDrvGT911.hpp"
#include "input/TouchScreenImpl1.h"
#include <Wire.h>
#include <XPowersLib.h>

extern XPowersPPM *PPM;

#define BOARD_BL_EN 11

static TouchDrvGT911 touch;
static uint8_t backlightLevel = 0; // 0=off, 1=low, 2=mid, 3=high

static void cycleBacklight(void *user_data)
{
    backlightLevel = (backlightLevel + 1) & 0x03;
    static const uint8_t brightness[] = {0, 50, 100, 230};
    analogWrite(BOARD_BL_EN, brightness[backlightLevel]);
    LOG_INFO("Backlight level: %d", backlightLevel);
}

// Callback for TouchScreenImpl1 - reads touch coordinates from GT911
static bool readTouch(int16_t *x, int16_t *y)
{
    int16_t x_array[1], y_array[1];
    if (touch.isPressed()) {
        uint8_t touched = touch.getPoint(x_array, y_array, 1);
        if (touched > 0) {
            *x = x_array[0];
            *y = y_array[0];
            return true;
        }
    }
    return false;
}

void earlyInitVariant()
{
    // LORA and SD use the same SPI bus.
    // To avoid mutual interference, all SPI CS signals should be pulled high (unselected state) early on.
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);

#ifdef SDCARD_CS
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);
#endif
}

void lateInitVariant()
{
    // BQ25896: Configure charging parameters for T5S3 PRO
    if (PPM) {
        PPM->setSysPowerDownVoltage(3300);
        PPM->setPrechargeCurr(64);
        PPM->setChargeTargetVoltage(4208);
        PPM->disableOTG();
        LOG_INFO("BQ25896 power path configured for T5S3 PRO");
    }

    // Initialize GT911 touchscreen via I2C (shared bus with epdiy PMICs)
    touch.setPins(SCREEN_TOUCH_RST, SCREEN_TOUCH_INT);
    // Initialize backlight pin (default off)
    pinMode(BOARD_BL_EN, OUTPUT);
    analogWrite(BOARD_BL_EN, 0);

    if (touch.begin(Wire, TOUCH_SLAVE_ADDRESS, I2C_SDA, I2C_SCL)) {
        touch.setInterruptMode(0x03); // LOW_LEVEL_QUERY
        LOG_INFO("GT911 touchscreen initialized");

        // Home button on touch screen cycles backlight: off -> low -> mid -> high -> off
        touch.setHomeButtonCallback(cycleBacklight, NULL);

        // Create TouchScreenImpl1 with readTouch callback
        // This automatically registers with InputBroker for default UI input
        touchScreenImpl1 = new TouchScreenImpl1(EINK_WIDTH, EINK_HEIGHT, readTouch);
        touchScreenImpl1->init();
    } else {
        LOG_ERROR("GT911 touchscreen init failed");
    }
}
