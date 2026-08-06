// https://docs.espressif.com/projects/esp-idf/en/v4.2/esp32s2/api-reference/peripherals/spi_master.html#_CPPv416spi_bus_config_t
// https://www.coralradio.com/cc1101-433mhz-module-guide.html
// https://gist.github.com/Taylor-eOS/0882416612accbeee27b064519ef4d35
#include "zv_subghz.h"
#include "zv_cc1101.h"
#include "zv_spi_bus.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"   // gpio_config(), gpio_set_level()
#include "esp_rom_sys.h"   // esp_rom_delay_us()
#include "esp_log.h"

static const char *TAG = "ZV_SUBGHZ";

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

    return zv_spi_bus_add_device(&dev, &device_handle);
}

esp_err_t zv_subghz_init(void)
{
    zv_manual_reset();

    // SPI bus + device. From spi_attach_device() on, the driver owns CS.
    esp_err_t res = zv_spi_bus_init();
    if (res != ESP_OK)
    {
        ESP_LOGE(TAG, "Can't initialize SPI bus, error: %d", res);
        return res;
    }
        
    res = spi_attach_device();
    if (res != ESP_OK)
    {
        ESP_LOGE(TAG, "Can't attache the device to the bus, error: %d", res);
        return res;
    }
        
    res = zv_cc1101_init(device_handle);
    if (res != ESP_OK)
    {
        ESP_LOGE(TAG, "Can't initialize the CC1101 logic, error: %d", res);
        return res;
    }

    // Software reset (SRES strobe). The byte goes through tx_buffer
    res = zv_cc1101_send_strobe(CC1101_STROBE_SRES);
    if (res != ESP_OK)
    {
        ESP_LOGE(TAG, "Can't reset the module to start a clean work: %d", res);
        return res;
    }

    // Without this wait, the first register reads could back as garbage.
    esp_rom_delay_us(1000);

    uint8_t partnum = zv_cc1101_read_partnum();
    uint8_t version = zv_cc1101_read_version();
    ESP_LOGI(TAG, "version: 0x%02X - parnum: 0x%02X\n", version, partnum);

    // PARTNUM is always 0x00 from the factory; VERSION is 0x14 (or 0x04 on old chip).
    // Anything else means the chip is not responding / SPI is wrong.
    if (partnum != 0x00 || (version != 0x14 && version != 0x04))
        return ESP_ERR_NOT_FOUND;

    res = zv_cc1101_config_and_verify();
    if (res != ESP_OK)
    {
        ESP_LOGE(TAG, "Can't configure the default state of the CC1101 module: %d", res);
        return res;
    }
    
    res = zv_cc1101_send_strobe(CC1101_STROBE_SRX);
    if (res != ESP_OK)
    {
        return res;
    }

    res = zv_cc1101_wait_for_rx();
    if (res != ESP_OK)
    {
        ESP_LOGE(TAG, "Error listening the current state of the CC1101 module: %d", res);
        return res;
    }
    
    esp_rom_delay_us(3000);

    return ESP_OK;
}

esp_err_t zv_subghz_get_rssi_dbm(int8_t *rssi_dbm)
{
    return zv_cc1101_get_rssi_dbm(rssi_dbm);
}

void zv_subghz_deinit(void)
{
    if (device_handle != NULL)
    {
        zv_spi_bus_remove_device(device_handle);
        device_handle = NULL;
    }

    zv_spi_bus_free();
}