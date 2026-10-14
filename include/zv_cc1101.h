#ifndef ZV_CC1101_H
#define ZV_CC1101_H

#include <stdint.h>
#include "driver/spi_master.h"

#define CC1101_CAPTURE_MAX_DURATIONS 256

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

#define ZV_SUB_GHZ_PIN_GDO0     27
#define ZV_SUB_GHZ_PIN_GDO2     26

typedef struct {

    int32_t durations_us[CC1101_CAPTURE_MAX_DURATIONS];
    uint16_t count;
    uint32_t freq_hz;
} zv_cc1101_capture_t;

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
esp_err_t zv_cc1101_set_frequency(uint32_t frequency);

esp_err_t zv_cc1101_read_partnum(uint8_t *output_value);
esp_err_t zv_cc1101_read_version(uint8_t *output_value);
esp_err_t zv_cc1101_get_rssi_dbm(int8_t *rssi_dbm);

esp_err_t zv_cc1101_capture(zv_cc1101_capture_t *out, uint32_t timeout_ms);
esp_err_t zv_cc1101_transmit(zv_cc1101_capture_t *capture);
void zv_cc1101_deinit(void);

#endif /* ZV_CC1101_H */