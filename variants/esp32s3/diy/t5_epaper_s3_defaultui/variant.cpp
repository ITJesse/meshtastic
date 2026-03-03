#include "variant.h"
#include "Arduino.h"
#include "TouchDrvGT911.hpp"
#include "input/TouchScreenImpl1.h"
#include "sleep.h"
#include <Wire.h>
#include <XPowersLib.h>
#include <esp_timer.h>

extern XPowersPPM *PPM;

static constexpr uint8_t BL_PIN = 11;
static constexpr uint32_t BL_TIMEOUT_MS = 15000; // 15s auto-off

static TouchDrvGT911 touch;
static bool backlightOn = false;
static esp_timer_handle_t blTimer = nullptr;

static void turnOffBacklight()
{
    backlightOn = false;
    if (blTimer)
        esp_timer_stop(blTimer); // safe to call even if not running
    analogWrite(BL_PIN, 0);
}

static void blTimerCallback(void *arg)
{
    turnOffBacklight();
}

static void toggleBacklight(void *user_data)
{
    if (backlightOn) {
        turnOffBacklight();
    } else {
        backlightOn = true;
        analogWrite(BL_PIN, 50); // lowest brightness
        if (blTimer)
            esp_timer_start_once(blTimer, BL_TIMEOUT_MS * 1000ULL); // microseconds
    }
}

// Observer to turn off backlight before sleep
class BacklightSleepObserver : public Observer<void *>
{
  protected:
    int onNotify(void *arg) override
    {
        turnOffBacklight();
        return 0;
    }
};
static BacklightSleepObserver backlightDeepSleepObserver;
static BacklightSleepObserver backlightLightSleepObserver;

// Callback for TouchScreenImpl1 - reads touch coordinates from GT911
// GT911 reports physical pixel coords (960x540), but the UI framework
// (OLEDDisplay / virtual keyboard) works in logical coords (~475x265).
// We must scale here so touch hits match drawn UI elements.

static bool readTouch(int16_t *x, int16_t *y)
{
    int16_t x_array[1], y_array[1];
    if (touch.isPressed()) {
        uint8_t touched = touch.getPoint(x_array, y_array, 1);
        if (touched > 0) {
            // Convert physical → logical: subtract safe-area offset, then scale down
            *x = (x_array[0] - EINK_SAFE_AREA_LEFT) / EINK_SCALE;
            *y = (y_array[0] - EINK_SAFE_AREA_TOP) / EINK_SCALE;
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

    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);
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

    // Subscribe to sleep notifications to auto-off backlight
    backlightDeepSleepObserver.observe(&notifyDeepSleep);
    backlightLightSleepObserver.observe(&notifyLightSleep);

    // Create backlight auto-off timer (one-shot)
    esp_timer_create_args_t timerArgs = {};
    timerArgs.callback = blTimerCallback;
    timerArgs.name = "bl_off";
    esp_timer_create(&timerArgs, &blTimer);

    // Initialize backlight pin (default off)
    pinMode(BL_PIN, OUTPUT);
    analogWrite(BL_PIN, 0);

    // Initialize GT911 touchscreen via I2C (shared bus with epdiy PMICs)
    touch.setPins(SCREEN_TOUCH_RST, SCREEN_TOUCH_INT);

    if (touch.begin(Wire, TOUCH_SLAVE_ADDRESS, I2C_SDA, I2C_SCL)) {
        touch.setInterruptMode(0x03); // LOW_LEVEL_QUERY
        // GT911 reports portrait coords (540x960); swap X/Y for landscape (960x540)
        touch.setSwapXY(true);
        // After swap: X range=0..960, Y range=0..540. Must set max for mirror to work.
        touch.setMaxCoordinates(960, 540);
        touch.setMirrorXY(false, true);
        LOG_INFO("GT911 touchscreen initialized");

        // Home button on touch screen toggles backlight (15s auto-off)
        touch.setHomeButtonCallback(toggleBacklight, NULL);

        // Create TouchScreenImpl1 with readTouch callback (logical coords)
        // readTouch already converts physical→logical, so pass logical dimensions
        int logicalW = (EINK_WIDTH - EINK_SAFE_AREA_LEFT - EINK_SAFE_AREA_RIGHT) / EINK_SCALE;
        int logicalH = (EINK_HEIGHT - EINK_SAFE_AREA_TOP - EINK_SAFE_AREA_BOTTOM) / EINK_SCALE;
        touchScreenImpl1 = new TouchScreenImpl1(logicalW, logicalH, readTouch);
        touchScreenImpl1->init();
    } else {
        LOG_ERROR("GT911 touchscreen init failed");
    }
}
