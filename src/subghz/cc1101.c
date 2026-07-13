#include "cc1101.h"
#include "esp_rom_sys.h"

static spi_device_handle_t cc_device_handle = NULL;

typedef struct {
    const char *name;
    uint8_t address;
    uint8_t value;
} cc1101_register_config_t;

static const cc1101_register_config_t cc1101_base_config[] = {
    { "IOCFG2",   0x00, 0x0D },
    { "IOCFG0",   0x02, 0x0D }, // data output
    { "FIFOTHR",  0x03, 0x47 },
    { "PKTCTRL0", 0x08, 0x32 },
    { "FREQ2",    0x0D, 0x10 }, // carry frecuency default 433.92
    { "FREQ1",    0x0E, 0xB0 }, // mid frecuency
    { "FREQ0",    0x0F, 0x71 }, // low frecuency
    { "MDMCFG4",  0x10, 0xF7 }, // RXBW = 58 Khz
    { "MDMCFG3",  0x11, 0x83 },
    { "MDMCFG2",  0x12, 0x30 }, // protocol tu use default is OOK
    { "MCSM0",    0x18, 0x18 },
    { "FOCCFG",   0x19, 0x16 },
    { "AGCCTRL2", 0x1B, 0x03 },
    { "AGCCTRL1", 0x1C, 0x40 },
    { "AGCCTRL0", 0x1D, 0x91 },
    { "FREND0",   0x22, 0x11 },
    { "TEST2",    0x2C, 0x81 },
    { "TEST1",    0x2D, 0x35 },
    { "TEST0",    0x2E, 0x09 },
};

esp_err_t zv_cc1101_send_strobe(uint8_t command)
{
    if (cc_device_handle == NULL)
        return ESP_ERR_INVALID_STATE;

    spi_transaction_t transaction = {
        .length    = 8,
        .tx_buffer = &command
    };

    return spi_device_polling_transmit(cc_device_handle, &transaction);
}

static esp_err_t zv_cc1101_read_register(uint8_t address, uint8_t burst, uint8_t *output_value)
{
    if (cc_device_handle == NULL)
        return ESP_ERR_INVALID_STATE;

    uint8_t rx[2] = { 0 };
    // tx[0]: header (read + burst); tx[1]: 0x00 padding so the chip can clock out the value into rx[1]
    uint8_t tx[2] = { (uint8_t)(address | burst), 0x00 };

    spi_transaction_t transaction = {
        .length = 16,
        .tx_buffer = tx,
        .rx_buffer = rx
    };

    esp_err_t err = spi_device_polling_transmit(cc_device_handle, &transaction);
    if (err != ESP_OK)
        return err;

    *output_value = rx[1];
    return ESP_OK;
}

esp_err_t zv_cc1101_read_status_register(uint8_t address, uint8_t *output_value)
{
    // All transactions on the SPI interface start with a header byte containing a R/W bit, a burst
    // access bit (B), and a 6-bit address (A5 – A0).
    // 1st bit = read/write
    // 2nd bit = burst
    // 3rd/7th = addresses to read/write

    // CC1101_STATUS_PARTNUM/0x30   = 0 0 1 1 0 0 0 0
    // CC1101_READ_BURST/0xC0       = 1 1 0 0 0 0 0 0
    //                             ------------------ OR
    // PARTNUM byte buffer          = 1 1 1 1 0 0 0 0
    return zv_cc1101_read_register(address, CC1101_READ_BURST, output_value);
}

esp_err_t zv_cc1101_read_config_register(uint8_t address, uint8_t *output_value)
{
    return zv_cc1101_read_register(address, CC1101_READ_SINGLE, output_value);
}

esp_err_t zv_cc1101_write_config_register(uint8_t address, uint8_t value)
{
    if (cc_device_handle == NULL)
        return ESP_ERR_INVALID_STATE;

    uint8_t tx[2] = { address, value };
    spi_transaction_t transaction = {
        .length = 16,
        .tx_buffer = tx,
    };

    return spi_device_polling_transmit(cc_device_handle, &transaction);
}

esp_err_t zv_cc1101_config_and_verify()
{
    size_t config_length = sizeof(cc1101_base_config) / sizeof((cc1101_base_config)[0]);
    for (size_t i= 0; i < config_length; i++)
    {
        cc1101_register_config_t reg_value = cc1101_base_config[i];

        esp_err_t success = zv_cc1101_write_config_register(reg_value.address, reg_value.value);
        if (success != ESP_OK)
            return success;

        uint8_t new_value;
        success = zv_cc1101_read_config_register(reg_value.address, &new_value);
        if (success !=ESP_OK || reg_value.value != new_value)
            return ESP_ERR_INVALID_RESPONSE;

    }

    return ESP_OK;
}

uint8_t zv_cc1101_read_partnum(void)
{
    uint8_t new_value;
    zv_cc1101_read_status_register(CC1101_STATUS_PARTNUM, &new_value);
    return new_value;
}

uint8_t zv_cc1101_read_version(void)
{
    uint8_t new_value;
    zv_cc1101_read_status_register(CC1101_STATUS_VERSION, &new_value);
    return new_value;
}

int8_t zv_cc1101_get_rssi_dbm(void)
{
    uint8_t raw;
    zv_cc1101_read_status_register(CC1101_STATUS_RSSI, &raw);
    int8_t r = (int8_t)((raw >= 128) ? (int)raw - 256 : raw);
    return (r / 2) - 74;   /* offset = 74 dBm para 433 MHz */
}

esp_err_t zv_cc1101_init(spi_device_handle_t handle)
{
    if (handle == NULL)
        return ESP_ERR_INVALID_ARG;

    cc_device_handle = handle;
    return ESP_OK;
}

esp_err_t zv_cc1101_wait_for_rx()
{
    for (int i = 0; i < 100; i++)
    {
        uint8_t state;
        zv_cc1101_read_status_register(CC1101_STATUS_MARCSTATE, &state);
        state &= 0x1F;

        if (state == CC1101_MARCSTATE_RX)
            return ESP_OK;

        esp_rom_delay_us(100);
    }

    return ESP_ERR_TIMEOUT;
}