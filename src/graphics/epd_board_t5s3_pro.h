#pragma once

/**
 * Custom epdiy board definition for LilyGo T5S3 4.7" E-Paper PRO.
 *
 * Based on epd_board_v7 from vroland/epdiy, with modifications for
 * shared I2C bus compatibility (Meshtastic initializes I2C before epdiy)
 * and T5S3 Pro-specific PCA9555 IO expander configuration.
 *
 * This allows using the official epdiy library without maintaining a fork.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <epd_board.h>

extern const EpdBoardDefinition epd_board_t5s3_pro;

#ifdef __cplusplus
}
#endif
