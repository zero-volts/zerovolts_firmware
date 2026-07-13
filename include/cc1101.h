#ifndef ZV_CC1101_H
#define ZV_CC1101_H

#include <stdint.h>
#include "driver/spi_master.h"

/* SPI configuration */
#define CC1101_SPI_CLOCK_HZ             (4 * 1000 * 1000) // 4 MHz

/* SPI access bits */
#define CC1101_READ_SINGLE              0x80
#define CC1101_READ_BURST               0xC0

/* Command strobes */
#define CC1101_STROBE_SRES              0x30
#define CC1101_STROBE_SRX               0x34
#define CC1101_STROBE_SIDLE             0x36
#define CC1101_STROBE_SFRX              0x3A

/* Status registers */
#define CC1101_STATUS_PARTNUM           0x30
#define CC1101_STATUS_VERSION           0x31
#define CC1101_STATUS_RSSI             0x34
#define CC1101_STATUS_MARCSTATE        0x35

/* MARCSTATE values */
#define CC1101_MARCSTATE_RX             0x0D

esp_err_t zv_cc1101_init(spi_device_handle_t handle);

/**
* Sends a command strobe, controls the radio state
* e.g SRES, SRC, SIDLE
*/
esp_err_t zv_cc1101_send_strobe(uint8_t command);

/**
 * Reads a CC1101 status register into *output_value. Returns ESP_OK on success.
 * Status registers provide runtime info like PARTNUM, VERSION, RSSI.
 */
esp_err_t zv_cc1101_read_status_register(uint8_t address, uint8_t *output_value);

/**
 * Reads a configuration register into *output_value. Returns ESP_OK on success.
 * Config registers define the operating parameters of the radio (frequency, modulation, etc.).
 */
esp_err_t zv_cc1101_read_config_register(uint8_t address, uint8_t *output_value);

/**
 * Writes a value to the configuration register
 */
esp_err_t zv_cc1101_write_config_register(uint8_t address, uint8_t value);
esp_err_t zv_cc1101_config_and_verify();
esp_err_t zv_cc1101_wait_for_rx();

uint8_t zv_cc1101_read_partnum(void);
uint8_t zv_cc1101_read_version(void);
int8_t zv_cc1101_get_rssi_dbm(void);

#endif /* ZV_CC1101_H */