// https://docs.espressif.com/projects/esp-idf/en/v4.2/esp32s2/api-reference/peripherals/spi_master.html#_CPPv416spi_bus_config_t
#include "subghz.h"

#include "driver/spi_master.h"

#define SPI_CLOCK_VELOCITY 4 * 1000 * 1000 // 4 MHz
// Command strobes
#define CC_SRES         0x30    // Reset command
#define CC_PARTNUM      0x30
#define CC_VERSION      0x31
#define CC_READ_BURST   0xC0

static spi_device_handle_t device_handle = NULL;

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
     // setup the pinout to communication throught GPIO matrix
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

esp_err_t zv_subghz_init(void)
{
    esp_err_t res = zv_initialize_bus();
    if (res != ESP_OK)
        return res;

    res = spi_attach_device();
    if (res != ESP_OK)
        return res;

    spi_transaction_t transaction = {
        .cmd = CC_SRES,
        .length = 8
    };

    printf("antes de reset en el chip\n");
    //reset the chip
    spi_device_polling_transmit(device_handle, &transaction);

    printf("antes de obtener el partnum\n");
    uint8_t partnum = zv_subghz_read_partnum();
    uint8_t version = zv_subghz_read_version();
    printf("version: 0x%02X - parnum: 0x%02X\n", version, partnum);

    return ESP_OK;
}

uint8_t zv_subghz_read_partnum(void)
{
    // All transactions on the SPI interface start with
    // a header byte containing a R/W¯ bit, a burst
    // access bit (B), and a 6-bit address (A5 – A0)
    // 1st bit = read/write
    // 2nd bit = burst
    // 3rd/7th = addreses to read/write
    
    // CC_PARTNUM/0x30      = 0 0 1 1 0 0 0 0
    // CC_READ_BURST/0xC0   = 1 1 0 0 0 0 0 0

    // partnum byte buffer  = 1 1 1 1 0 0 0 0 

    uint8_t rx[2] = { 0 };
    uint8_t tx[2] = { (uint8_t)(CC_PARTNUM | CC_READ_BURST), 0x00 };

    spi_transaction_t transaction = {
        .length = 16,
        .tx_buffer = tx,
        .rx_buffer = rx
    };
    
    spi_device_polling_transmit(device_handle, &transaction);
    return rx[1];
}

uint8_t zv_subghz_read_version(void)
{
    uint8_t rx[2] = { 0 };
    uint8_t tx[2] = { (uint8_t)(CC_VERSION | CC_READ_BURST), 0x00 };

    spi_transaction_t transaction = {
        .length = 16,
        .tx_buffer = tx,
        .rx_buffer = rx
    };
    
    spi_device_polling_transmit(device_handle, &transaction);
    return rx[1];
}

void zv_subghz_deinit(void)
{

}