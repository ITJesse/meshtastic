/**
 * Custom epdiy board definition for LilyGo T5S3 4.7" E-Paper PRO.
 *
 * Based on epd_board_v7 from vroland/epdiy (upstream/main), with:
 *   1. I2C init tolerance (no ESP_ERROR_CHECK on i2c_param_config / i2c_driver_install)
 *      — Meshtastic initializes I2C first for GT911 touch, BQ27220, etc.
 *   2. GPIO ISR service tolerance (no ESP_ERROR_CHECK on gpio_install_isr_service)
 *   3. T5S3 Pro-specific PCA9555 IO expander configuration
 */

#include <stdint.h>
#include "epd_board_t5s3_pro.h"
#include <epdiy.h>

#include <output_lcd/lcd_driver.h>
#include <output_common/render_method.h>
#include <esp_log.h>
#include <board/pca9555.h>
#include <board/tps65185.h>

#include <driver/gpio.h>
#include <driver/i2c.h>
#include <sdkconfig.h>

/* I2C */
#define CFG_SCL GPIO_NUM_40
#define CFG_SDA GPIO_NUM_39
#define CFG_INTR GPIO_NUM_38
#define T5S3_I2C_PORT I2C_NUM_0

/* PCA9555 pin mapping (port 1, high byte) */
#define CFG_PIN_OE (PCA_PIN_PC10 >> 8)
#define CFG_PIN_MODE (PCA_PIN_PC11 >> 8)
#define __CFG_PIN_STV (PCA_PIN_PC12 >> 8)
#define CFG_PIN_PWRUP (PCA_PIN_PC13 >> 8)
#define CFG_PIN_VCOM_CTRL (PCA_PIN_PC14 >> 8)
#define CFG_PIN_WAKEUP (PCA_PIN_PC15 >> 8)
#define CFG_PIN_PWRGOOD (PCA_PIN_PC16 >> 8)
#define CFG_PIN_INT (PCA_PIN_PC17 >> 8)

/* 16-bit data bus */
#define D15 GPIO_NUM_47
#define D14 GPIO_NUM_21
#define D13 GPIO_NUM_14
#define D12 GPIO_NUM_13
#define D11 GPIO_NUM_12
#define D10 GPIO_NUM_11
#define D9 GPIO_NUM_10
#define D8 GPIO_NUM_9

#define D7 GPIO_NUM_8
#define D6 GPIO_NUM_18
#define D5 GPIO_NUM_17
#define D4 GPIO_NUM_16
#define D3 GPIO_NUM_15
#define D2 GPIO_NUM_7
#define D1 GPIO_NUM_6
#define D0 GPIO_NUM_5

/* Control Lines */
#define CKV GPIO_NUM_48
#define STH GPIO_NUM_41
#define LEH GPIO_NUM_42
#define STV GPIO_NUM_45

/* Edges */
#define CKH GPIO_NUM_4

typedef struct {
    i2c_port_t port;
    bool pwrup;
    bool vcom_ctrl;
    bool wakeup;
    bool others[8];
} t5s3_config_register_t;

static int t5s3_vcom = 1600;
static t5s3_config_register_t t5s3_config_reg;
static bool t5s3_interrupt_done = false;

static void IRAM_ATTR t5s3_interrupt_handler(void *arg) {
    t5s3_interrupt_done = true;
}

static lcd_bus_config_t t5s3_lcd_config = {
    .clock = CKH,
    .ckv = CKV,
    .leh = LEH,
    .start_pulse = STH,
    .stv = STV,
    .data[0] = D0,
    .data[1] = D1,
    .data[2] = D2,
    .data[3] = D3,
    .data[4] = D4,
    .data[5] = D5,
    .data[6] = D6,
    .data[7] = D7,
    .data[8] = D8,
    .data[9] = D9,
    .data[10] = D10,
    .data[11] = D11,
    .data[12] = D12,
    .data[13] = D13,
    .data[14] = D14,
    .data[15] = D15,
};

static void t5s3_board_init(uint32_t epd_row_width) {
    gpio_hold_dis(CKH); // free CKH after wakeup

    // T5S3 Pro: I2C bus is already initialized by Meshtastic Wire library on the same port.
    // Do NOT install our own I2C driver — just reuse the existing one.
    t5s3_config_reg.port = T5S3_I2C_PORT;
    t5s3_config_reg.pwrup = false;
    t5s3_config_reg.vcom_ctrl = false;
    t5s3_config_reg.wakeup = false;
    for (int i = 0; i < 8; i++) {
        t5s3_config_reg.others[i] = false;
    }

    gpio_set_direction(CFG_INTR, GPIO_MODE_INPUT);
    gpio_set_intr_type(CFG_INTR, GPIO_INTR_NEGEDGE);

    // T5S3 Pro: ISR service may already be installed by Meshtastic
    gpio_install_isr_service(ESP_INTR_FLAG_EDGE);

    ESP_ERROR_CHECK(gpio_isr_handler_add(CFG_INTR, t5s3_interrupt_handler, (void *)CFG_INTR));

    // T5S3 Pro: Set all epdiy lines to output except TPS interrupt + PWR good + STV
    ESP_ERROR_CHECK(pca9555_set_config(t5s3_config_reg.port, CFG_PIN_PWRGOOD | CFG_PIN_INT | __CFG_PIN_STV, 1));

    // T5S3 Pro: Additional PCA9555 port 0 configuration
    pca9555_set_inversion(t5s3_config_reg.port, 0x00, 0);
    pca9555_set_inversion(t5s3_config_reg.port, 0x00, 1);
    pca9555_set_config(t5s3_config_reg.port, 0x00, 0);
    pca9555_set_value(t5s3_config_reg.port, 0xff, 0);

    const EpdDisplay_t *display = epd_get_display();

    LcdEpdConfig_t config = {
        .pixel_clock = display->bus_speed * 1000 * 1000,
        .ckv_high_time = 60,
        .line_front_porch = 4,
        .le_high_time = 4,
        .bus_width = display->bus_width,
        .bus = t5s3_lcd_config,
    };
    epd_lcd_init(&config, display->width, display->height);
}

static void t5s3_board_deinit() {
    epd_lcd_deinit();

    ESP_ERROR_CHECK(pca9555_set_config(
        t5s3_config_reg.port, CFG_PIN_PWRGOOD | CFG_PIN_INT | CFG_PIN_VCOM_CTRL | CFG_PIN_PWRUP, 1));

    int tries = 0;
    while (!((pca9555_read_input(t5s3_config_reg.port, 1) & 0xC0) == 0x80)) {
        if (tries >= 50) {
            ESP_LOGE("epdiy", "failed to shut down TPS65185!");
            break;
        }
        tries++;
        vTaskDelay(1);
    }

    // TPS65185 generates a delayed interrupt that needs to be cleared
    vTaskDelay(50);
    pca9555_read_input(t5s3_config_reg.port, 0);
    pca9555_read_input(t5s3_config_reg.port, 1);
    // Don't delete I2C driver or uninstall ISR service —
    // Wire library owns I2C_NUM_0, Meshtastic manages its lifecycle.
}

static void t5s3_board_set_ctrl(epd_ctrl_state_t *state, const epd_ctrl_state_t *const mask) {
    uint8_t value = 0x00;
    if (mask->ep_output_enable || mask->ep_mode || mask->ep_stv) {
        if (state->ep_output_enable)
            value |= CFG_PIN_OE;
        if (state->ep_mode)
            value |= CFG_PIN_MODE;
        // if (state->ep_stv) value |= CFG_PIN_STV;
        if (t5s3_config_reg.pwrup)
            value |= CFG_PIN_PWRUP;
        if (t5s3_config_reg.vcom_ctrl)
            value |= CFG_PIN_VCOM_CTRL;
        if (t5s3_config_reg.wakeup)
            value |= CFG_PIN_WAKEUP;

        ESP_ERROR_CHECK(pca9555_set_value(t5s3_config_reg.port, value, 1));
    }
}

static void t5s3_board_poweron(epd_ctrl_state_t *state) {
    epd_ctrl_state_t mask = {
        .ep_output_enable = true,
        .ep_mode = true,
        .ep_stv = true,
    };
    state->ep_stv = true;
    state->ep_mode = false;
    state->ep_output_enable = true;
    t5s3_config_reg.wakeup = true;
    t5s3_board_set_ctrl(state, &mask);

    // Check if DISPLAY_UPSEQ_MC2 is set
    const EpdDisplay_t *display = epd_get_display();
    if (display->display_type & DISPLAY_UPSEQ_MC2) {
        vTaskDelay(3);
        tps_set_upseq_carta1300();
    }
    t5s3_config_reg.pwrup = true;
    t5s3_board_set_ctrl(state, &mask);
    t5s3_config_reg.vcom_ctrl = true;
    t5s3_board_set_ctrl(state, &mask);

    // give the IC time to powerup and set lines
    vTaskDelay(1);

    while (!(pca9555_read_input(t5s3_config_reg.port, 1) & CFG_PIN_PWRGOOD)) {
    }

    ESP_ERROR_CHECK(tps_write_register(t5s3_config_reg.port, TPS_REG_ENABLE, 0x3F));

    tps_set_vcom(t5s3_config_reg.port, t5s3_vcom);

    state->ep_sth = true;
    mask = (const epd_ctrl_state_t){
        .ep_sth = true,
    };
    t5s3_board_set_ctrl(state, &mask);

    int tries = 0;
    while (!((tps_read_register(t5s3_config_reg.port, TPS_REG_PG) & 0xFA) == 0xFA)) {
        if (tries >= 500) {
            ESP_LOGE("epdiy", "Power enable failed! PG status: %X",
                     tps_read_register(t5s3_config_reg.port, TPS_REG_PG));
            return;
        }
        tries++;
        vTaskDelay(1);
    }
}

static void t5s3_board_measure_vcom(epd_ctrl_state_t *state) {
    epd_ctrl_state_t mask = {
        .ep_output_enable = true,
        .ep_mode = true,
        .ep_stv = true,
    };
    state->ep_stv = true;
    state->ep_mode = false;
    state->ep_output_enable = true;
    t5s3_config_reg.wakeup = true;
    t5s3_board_set_ctrl(state, &mask);
    t5s3_config_reg.pwrup = true;
    t5s3_board_set_ctrl(state, &mask);

    // give the IC time to powerup and set lines
    vTaskDelay(1);
    state->ep_sth = true;
    mask = (const epd_ctrl_state_t){
        .ep_sth = true,
    };
    t5s3_board_set_ctrl(state, &mask);

    while (!(pca9555_read_input(t5s3_config_reg.port, 1) & CFG_PIN_PWRGOOD)) {
    }
    ESP_LOGI("epdiy", "Power rails enabled");

    state->ep_sth = true;
    mask = (const epd_ctrl_state_t){
        .ep_sth = true,
    };
    t5s3_board_set_ctrl(state, &mask);

    int tries = 0;
    while (!((tps_read_register(t5s3_config_reg.port, TPS_REG_PG) & 0xFA) == 0xFA)) {
        if (tries >= 500) {
            ESP_LOGE("epdiy", "Power enable failed! PG status: %X",
                     tps_read_register(t5s3_config_reg.port, TPS_REG_PG));
            return;
        }
        tries++;
        vTaskDelay(1);
    }
}

static void t5s3_board_poweroff(epd_ctrl_state_t *state) {
    epd_ctrl_state_t mask = {
        .ep_stv = true,
        .ep_output_enable = true,
        .ep_mode = true,
    };
    t5s3_config_reg.vcom_ctrl = false;
    t5s3_config_reg.pwrup = false;
    state->ep_stv = false;
    state->ep_output_enable = false;
    state->ep_mode = false;
    t5s3_board_set_ctrl(state, &mask);
    vTaskDelay(1);
    t5s3_config_reg.wakeup = false;
    t5s3_board_set_ctrl(state, &mask);
}

static float t5s3_board_ambient_temperature() {
    float temp = tps_read_thermistor(T5S3_I2C_PORT);
    if (temp <= -20.0f || temp >= 80.0f) {
        return 20.0f;
    }
    return temp;
}

static void t5s3_set_vcom(int value) {
    t5s3_vcom = value;
}

const EpdBoardDefinition epd_board_t5s3_pro = {
    .init = t5s3_board_init,
    .deinit = t5s3_board_deinit,
    .set_ctrl = t5s3_board_set_ctrl,
    .poweron = t5s3_board_poweron,
    .poweroff = t5s3_board_poweroff,

    .measure_vcom = t5s3_board_measure_vcom,
    .get_temperature = t5s3_board_ambient_temperature,
    .set_vcom = t5s3_set_vcom,

    .gpio_set_direction = NULL,
    .gpio_read = NULL,
    .gpio_write = NULL,
};
