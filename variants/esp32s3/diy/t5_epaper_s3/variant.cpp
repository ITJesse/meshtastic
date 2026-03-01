#include "variant.h"
#include "Arduino.h"

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
