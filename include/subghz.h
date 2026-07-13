#ifndef ZV_SUB_GHZ_H
#define ZV_SUB_GHZ_H

#include <stdint.h>
#include "esp_err.h"

// CC1101 ---------- ESP32
//
// 1   -> GND          GND
// 2   -> VCC          3v3
// 3   -> GDO0         D27
// 4   -> CSN          D5
// 5   -> SCK          D14   (bus SPI shared → zv_spi_bus.h)
// 6   -> MOSI         D13   (bus SPI shared → zv_spi_bus.h)
// 7   -> MISO/GDO1    D25   (bus SPI shared → zv_spi_bus.h)
// 8   -> GDO2         D26

#define ZV_SUB_GHZ_PIN_GDO0     27
#define ZV_SUB_GHZ_PIN_GDO2     26
#define ZV_SUB_GHZ_PIN_CSN      5

esp_err_t zv_subghz_init(void);
void zv_subghz_deinit(void);

// Reads the current RSSI in dBm. Valid once the chip is in RX (después de init).
int8_t zv_subghz_get_rssi_dbm(void);

#endif /* ZV_SUB_GHZ_H */