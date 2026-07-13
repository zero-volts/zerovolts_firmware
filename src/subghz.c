// https://docs.espressif.com/projects/esp-idf/en/v4.2/esp32s2/api-reference/peripherals/spi_master.html#_CPPv416spi_bus_config_t
// https://www.coralradio.com/cc1101-433mhz-module-guide.html
// https://gist.github.com/Taylor-eOS/0882416612accbeee27b064519ef4d35
#include "subghz.h"
#include "cc1101.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"   // gpio_config(), gpio_set_level()
#include "esp_rom_sys.h"   // esp_rom_delay_us()

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
        .clock_speed_hz = CC1101_SPI_CLOCK_HZ
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

//TODO: mover a cc1101
static esp_err_t zv_wait_for_rx()
{
    for (int i = 0; i < 100; i++)
    {
        uint8_t state = zv_cc1101_read_status_register(CC1101_STATUS_MARCSTATE);
        state &= 0x1F;

        if (state == CC1101_MARCSTATE_RX)
        {
            printf("state ready: 0x%02X\n", state);
            return ESP_OK;
        }
            
        esp_rom_delay_us(100);
    }

    return ESP_ERR_TIMEOUT;
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

    zv_cc1101_init(device_handle);

    // Software reset (SRES strobe). The byte goes through tx_buffer
    zv_cc1101_send_strobe(CC1101_STROBE_SRES);

    // Without this wait, the first register reads could back as garbage.
    esp_rom_delay_us(1000);

    uint8_t partnum = zv_cc1101_read_partnum();
    uint8_t version = zv_cc1101_read_version();
    printf("version: 0x%02X - parnum: 0x%02X\n", version, partnum);

    // PARTNUM is always 0x00 from the factory; VERSION is 0x14 (or 0x04 on old chip). 
    // Anything else means the chip is not responding / SPI is wrong.
    if (partnum != 0x00 || (version != 0x14 && version != 0x04))
        return ESP_ERR_NOT_FOUND;

    res = zv_cc1101_config_and_verify();
    if (res != ESP_OK)
        return res;
       
    zv_cc1101_send_strobe(CC1101_STROBE_SRX);
    zv_wait_for_rx();

    esp_rom_delay_us(3000);
    int8_t rssi = zv_cc1101_get_rssi_dbm();
    printf("rssi: %d\n", rssi);

    return ESP_OK;
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