#ifndef ZV_SUB_GHZ_H
#define ZV_SUB_GHZ_H

#include "driver/spi_common.h"

// CC11001 ---------- ESP32

// 1   -> GND          GND
// 2   -> VCC          3v3
// 3   -> GDO0         D27
// 4   -> CSN          D5
// 5   -> SCK          D14
// 6   -> MOSI         D13
// 7   -> MISO/GDO1    D25
// 8   -> GDO2         D26

#define ZV_SUB_GHZ_PIN_GDO0     27
#define ZV_SUB_GHZ_PIN_GDO2     26

// SPI communication
#define ZV_SUB_GHZ_PIN_CSN      5
#define ZV_SUB_GHZ_PIN_SCK      14
#define ZV_SUB_GHZ_PIN_MOSI     13
#define ZV_SUB_GHZ_PIN_MISO     25

#define ZV_SUB_GHZ_SPI_HOST     SPI3_HOST

esp_err_t zv_subghz_init(void);
void zv_subghz_deinit(void);

#endif /* ZV_SUB_GHZ_H */