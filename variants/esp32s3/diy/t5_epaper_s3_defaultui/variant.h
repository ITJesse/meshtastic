#pragma once
// LilyGo T5S3 4.7" e-paper PRO (Default UI + epdiy + GT911 Touch)

#define HAS_SDCARD
#define SDCARD_USE_SPI1
#define SDCARD_CS 12

// GPS - Optional module support
// #define GPS_DEFAULT_NOT_PRESENT 1
#define GPS_RX_PIN 44
#define GPS_TX_PIN 43

// I2C
#undef I2C_SDA
#undef I2C_SCL
#define I2C_SDA 39
#define I2C_SCL 40

// SPI (Shared for SD and LoRa)
#define SPI_MISO 21
#define SPI_MOSI 13
#define SPI_SCLK 14

// LORA (SX1262)
#define USE_SX1262

#define LORA_CS 46
#define LORA_DIO1 10
#define LORA_RESET 1
#define LORA_DIO2 47 // BUSY pin in Lilygo code

#define LORA_SCK SPI_SCLK
#define LORA_MISO SPI_MISO
#define LORA_MOSI SPI_MOSI

#ifdef USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif

// Power / Button
#define BUTTON_PIN 0 // BOOT button (GPIO 0)
// #define BUTTON_NEED_PULLUP

// Battery Manager BQ25896
#define HAS_PPM 1
#define XPOWERS_CHIP_BQ25896

// Battery Fuel Gauge BQ27220
#define HAS_BQ27220 1
#define BQ27220_USE_CHARGE_PERCENT 1
#define BQ27220_I2C_SDA I2C_SDA
#define BQ27220_I2C_SCL I2C_SCL
#define BQ27220_DESIGN_CAPACITY 1500

// Touch Screen (GT911)
#define HAS_TOUCHSCREEN 1
#define SCREEN_TOUCH_INT 3
#define SCREEN_TOUCH_RST 9
#define TOUCH_SLAVE_ADDRESS 0x5D
#define USE_VIRTUAL_KEYBOARD 1

// Screen scale factor (physical pixels per logical pixel)
#define EINK_SCALE 2

// Safe display area: physical pixels hidden by enclosure on each side.
// Uncomment and adjust values to inset UI content away from hidden edges.
#define EINK_SAFE_AREA_LEFT   5
#define EINK_SAFE_AREA_RIGHT  5
#define EINK_SAFE_AREA_TOP    0
#define EINK_SAFE_AREA_BOTTOM 10

// Fix PlatformIO generic board macro collisions
#undef LED_BUILTIN

// Alias for FS subsystem
#define SPI_SCK SPI_SCLK
