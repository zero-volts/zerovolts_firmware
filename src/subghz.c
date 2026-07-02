// https://docs.espressif.com/projects/esp-idf/en/v4.2/esp32s2/api-reference/peripherals/spi_master.html#_CPPv416spi_bus_config_t
#include "subghz.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"   // gpio_config(), gpio_set_level()
#include "esp_rom_sys.h"   // esp_rom_delay_us()

#define SPI_CLOCK_VELOCITY (4 * 1000 * 1000) // 4 MHz
// Command strobes
#define CC_SRES         0x30    // Reset command
#define CC_PARTNUM      0x30
#define CC_VERSION      0x31
#define CC_READ_BURST   0xC0

static spi_device_handle_t device_handle = NULL;

static void zv_manual_reset(void)
{
    // Manual hardware reset (datasheet 19.1.2): brings the chip out of an
    // undefined power-up state with a CS toggle HIGH-LOW-HIGH.
    gpio_config_t cs = {
        .pin_bit_mask = (1ULL << ZV_SUB_GHZ_PIN_CSN),
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cs);

    gpio_set_level(ZV_SUB_GHZ_PIN_CSN, 1);   // HIGH
    esp_rom_delay_us(1000);                  // datasheet asks for >= 40 us; 1 ms is generous
    gpio_set_level(ZV_SUB_GHZ_PIN_CSN, 0);   // LOW
    esp_rom_delay_us(1000);
    gpio_set_level(ZV_SUB_GHZ_PIN_CSN, 1);   // HIGH
    esp_rom_delay_us(10000);                 // 10 ms to let the crystal settle
}

static esp_err_t spi_attach_device(void)
{
    spi_device_interface_config_t dev = {
        .spics_io_num = ZV_SUB_GHZ_PIN_CSN,
        .queue_size = 4,
        .mode = 0,
        .clock_speed_hz = SPI_CLOCK_VELOCITY
    };

    return spi_bus_add_device(ZV_SUB_GHZ_SPI_HOST, &dev, &device_handle);
}

static esp_err_t zv_initialize_bus(void)
{
     // setup the pinout to communication through GPIO matrix
    spi_bus_config_t bus = {
        .mosi_io_num = ZV_SUB_GHZ_PIN_MOSI,
        .miso_io_num = ZV_SUB_GHZ_PIN_MISO,
        .sclk_io_num = ZV_SUB_GHZ_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 256
    };

    esp_err_t res = spi_bus_initialize(ZV_SUB_GHZ_SPI_HOST, &bus, SPI_DMA_CH_AUTO);
    if (res != ESP_OK) {
        return res;
    }

    return ESP_OK;
}

static uint8_t zv_read_status_reg(uint8_t address)
{
    // All transactions on the SPI interface start with a header byte containing a R/W bit, a burst
    // access bit (B), and a 6-bit address (A5 – A0).
    // 1st bit = read/write
    // 2nd bit = burst
    // 3rd/7th = addresses to read/write

    // CC_PARTNUM/0x30      = 0 0 1 1 0 0 0 0
    // CC_READ_BURST/0xC0   = 1 1 0 0 0 0 0 0
    //                      ------------------ OR
    // partnum byte buffer  = 1 1 1 1 0 0 0 0

    uint8_t rx[2] = { 0 };
    // tx[0]: header (read + burst); tx[1]: 0x00 padding so the chip can clock out the value into rx[1]
    uint8_t tx[2] = { (uint8_t)(address | CC_READ_BURST), 0x00 };

    spi_transaction_t transaction = {
        .length = 16,
        .tx_buffer = tx,
        .rx_buffer = rx
    };

    spi_device_polling_transmit(device_handle, &transaction);
    return rx[1];
}

esp_err_t zv_subghz_init(void)
{
    zv_manual_reset();

    // SPI bus + device. From spi_attach_device() on, the driver owns CS.
    esp_err_t res = zv_initialize_bus();
    if (res != ESP_OK)
        return res;

    res = spi_attach_device();
    if (res != ESP_OK)
        return res;

    // Software reset (SRES strobe). The byte goes through tx_buffer
    uint8_t cmd = CC_SRES;
    spi_transaction_t transaction = {
        .length    = 8,
        .tx_buffer = &cmd
    };

    spi_device_polling_transmit(device_handle, &transaction);

    // Without this wait, the first register reads could back as garbage.
    esp_rom_delay_us(1000);

    uint8_t partnum = zv_subghz_read_partnum();
    uint8_t version = zv_subghz_read_version();
    printf("version: 0x%02X - parnum: 0x%02X\n", version, partnum);

    // PARTNUM is always 0x00 from the factory; VERSION is 0x14 (or 0x04 on old
    // chip). Anything else means the chip is not responding / SPI is wrong.
    if (partnum != 0x00 || (version != 0x14 && version != 0x04))
        return ESP_ERR_NOT_FOUND;

    return ESP_OK;
}

uint8_t zv_subghz_read_partnum(void)
{
    return zv_read_status_reg(CC_PARTNUM);
}

uint8_t zv_subghz_read_version(void)
{
    return zv_read_status_reg(CC_VERSION);
}

void zv_subghz_deinit(void)
{
    if (device_handle != NULL)
    {
        spi_bus_remove_device(device_handle);
        device_handle = NULL;
    }

    spi_bus_free(ZV_SUB_GHZ_SPI_HOST);
}