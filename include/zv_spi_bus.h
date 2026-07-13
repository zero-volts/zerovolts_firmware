#ifndef ZV_SPI_BUS_H
#define ZV_SPI_BUS_H

#include "driver/spi_master.h"

/*
 * Shared SPI bus.
 *
 * The SPI bus is NOT owned by any single radio module: the CC1101 (sub-GHz),
 * and others all share the same.
 * SCK/MOSI/MISO lines and differ only in their CS pin. So the bus lives here,
 * and each chip attaches itself as a device with its own CS/mode/clock.
 */
#define ZV_SPI_BUS_HOST     SPI3_HOST   /* VSPI */

// Shared bus lines — every device on this bus uses these. The per-chip CS pin
// is NOT here: it belongs to each module (e.g. ZV_SUB_GHZ_PIN_CSN).
#define ZV_SPI_BUS_PIN_SCK  14
#define ZV_SPI_BUS_PIN_MOSI 13
#define ZV_SPI_BUS_PIN_MISO 25

/**
 * Initializes the shared bus. Idempotent and safe to call from several modules:
 *  if another module already initialized it, this still returns ESP_OK.
 */
esp_err_t zv_spi_bus_init(void);

/**
 * Attaches a chip to the shared bus. The CS pin, SPI mode and clock speed live
 * in dev_cfg (those ARE per-chip). Returns the device handle in out_handle.
 **/ 
esp_err_t zv_spi_bus_add_device(const spi_device_interface_config_t *dev_cfg, spi_device_handle_t *out_handle);
esp_err_t zv_spi_bus_remove_device(spi_device_handle_t handle);
esp_err_t zv_spi_bus_free(void);

#endif /* ZV_SPI_BUS_H */
