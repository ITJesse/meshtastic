#include "variant.h"
#include "Arduino.h"
#include "TouchDrvGT911.hpp"
#include "graphics/EInkEpdiyDisplay.h"
#include "graphics/Screen.h"
#include "input/TouchScreenImpl1.h"
#include "sleep.h"
#include <Wire.h>
#include <XPowersLib.h>
#include <esp_timer.h>

extern "C" {
#include <board/pca9555.h>
}

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

// --- PCA9535 physical button: interrupt-driven full refresh ---
// GPIO 38 (PCA9535 INT) is shared with epdiy power management.
// The ISR fires for ANY PCA9535 input change (including poweron/poweroff).
// We debounce + verify button state via I2C, then trigger a full refresh
// of the current screen content (no page switch).

static TaskHandle_t btnTaskHandle = nullptr;

static void IRAM_ATTR pca9535ButtonISR()
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(btnTaskHandle, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void pca9535ButtonTask(void *param)
{
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // sleep until ISR fires

        // epdiy refresh is synchronous: by the time epd_poweroff() triggers
        // this PCA9535 interrupt, the refresh has already completed.
        // Short debounce to filter spurious edges from PMIC I2C activity.
        delay(100);

        // Read BOTH ports to clear PCA9535 INT line.
        // INT stays LOW until all input registers are read.
        pca9555_read_input(I2C_NUM_0, 0);
        uint8_t val = pca9555_read_input(I2C_NUM_0, 1);
        bool pressed = !(val & PCA9535_BUTTON_MASK);
        LOG_INFO("PCA9535 INT fired, port1=0x%02x, button %s", val, pressed ? "PRESSED" : "not pressed");

        // Verify the physical button is actually pressed (not a spurious interrupt)
#ifdef USE_EINK_EPDIY
        if (pressed && screen) {
            auto *eink = static_cast<EInkEpdiyDisplay *>(screen->getDisplayDevice());
            if (eink) {
                eink->cleanRefresh(); // two-pass: clear white + redraw with GC16
            }
        }
#endif
    }
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

    // PCA9535 button: create task first, then attach interrupt
    xTaskCreate(pca9535ButtonTask, "pca_btn", 3 * 1024, NULL, 1, &btnTaskHandle);
    pinMode(PCA9535_INT_PIN, INPUT_PULLUP);
    attachInterrupt(PCA9535_INT_PIN, pca9535ButtonISR, FALLING);
    LOG_INFO("PCA9535 button initialized (GPIO %d)", PCA9535_INT_PIN);
}
