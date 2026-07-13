#include "zv_spi_bus.h"

#include <stdbool.h>

static bool s_initialized = false;
static bool s_bus_owned = false;

esp_err_t zv_spi_bus_init(void)
{
    if (s_initialized)
        return ESP_OK;

    spi_bus_config_t bus = {
        .mosi_io_num     = ZV_SPI_BUS_PIN_MOSI,
        .miso_io_num     = ZV_SPI_BUS_PIN_MISO,
        .sclk_io_num     = ZV_SPI_BUS_PIN_SCK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 256,
    };

    esp_err_t err = spi_bus_initialize(ZV_SPI_BUS_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err == ESP_OK)
    {
        s_bus_owned   = true;   // we created it → we are responsible for freeing it
        s_initialized = true;
        return ESP_OK;
    }

    if (err == ESP_ERR_INVALID_STATE)
    {
        // Another module already initialized this bus. We share it, but we must
        // NOT free it later (we don't own it).
        s_bus_owned   = false;
        s_initialized = true;
        return ESP_OK;
    }

    return err;
}

esp_err_t zv_spi_bus_add_device(const spi_device_interface_config_t *dev_cfg, spi_device_handle_t *out_handle)
{
    if (dev_cfg == NULL || out_handle == NULL)
        return ESP_ERR_INVALID_ARG;

    return spi_bus_add_device(ZV_SPI_BUS_HOST, dev_cfg, out_handle);
}

esp_err_t zv_spi_bus_remove_device(spi_device_handle_t handle)
{
    if (handle == NULL)
        return ESP_ERR_INVALID_ARG;

    return spi_bus_remove_device(handle);
}

esp_err_t zv_spi_bus_free(void)
{
    // Only free if we own the bus. If another module created it, freeing here would break them.
    // TODO: con varios devices en el bus, contar referencias antes de liberar.
    if (!s_bus_owned)
        return ESP_OK;

    esp_err_t err = spi_bus_free(ZV_SPI_BUS_HOST);
    if (err == ESP_OK) 
    {
        s_bus_owned   = false;
        s_initialized = false;
    }

    return err;
}
