#include "variant.h"
#include "Arduino.h"
#include <XPowersLib.h>

extern XPowersPPM *PPM;

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
    // BQ25896: Configure power path for battery-only operation.
    // Without these settings, the e-paper display (TPS65185) may not receive
    // sufficient power when USB is disconnected, because the default ILIM pin
    // restricts system current delivery.
    if (PPM) {
        PPM->disableCurrentLimitPin();
        PPM->setSysPowerDownVoltage(3300);
        PPM->setInputCurrentLimit(3250);
        PPM->setPrechargeCurr(64);
        PPM->setChargeTargetVoltage(4208);
        PPM->disableOTG();
        LOG_INFO("BQ25896 power path configured for T5S3 PRO");
    }
}
