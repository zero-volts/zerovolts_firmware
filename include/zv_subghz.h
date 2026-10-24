#ifndef ZV_SUB_GHZ_H
#define ZV_SUB_GHZ_H

#include <stdint.h>
#include "esp_err.h"
#include "zv_cc1101.h"
#include "zv_receiver_event.h"

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

#define ZV_SUB_GHZ_PIN_CSN      5

typedef struct {
    long signal;
    int time_frame;
} capture_task_data_t;

void zv_subghz_set_event_receiver(zv_event_handler_t handler);

esp_err_t zv_subghz_init(void);
void zv_subghz_deinit(void);

// Reads the current RSSI in dBm. Valid once the chip is in RX (después de init).
esp_err_t zv_subghz_get_rssi_dbm(int8_t *rssi_dbm);
void subghz_listener_task(void *arg);
esp_err_t zv_subghz_transmit(const zv_cc1101_capture_t *capture, uint16_t repetitions);

#endif /* ZV_SUB_GHZ_H */
