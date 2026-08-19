// https://docs.espressif.com/projects/esp-idf/en/v4.2/esp32s2/api-reference/peripherals/spi_master.html#_CPPv416spi_bus_config_t
// https://www.coralradio.com/cc1101-433mhz-module-guide.html
// https://gist.github.com/Taylor-eOS/0882416612accbeee27b064519ef4d35
#include "zv_subghz.h"

#include "esp_log.h"
#include "esp_rom_sys.h"   // esp_rom_delay_us()

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"   // gpio_config(), gpio_set_level()
#include "driver/spi_master.h"

#include "zv_cc1101.h"
#include "zv_spi_bus.h"
#include "zv_uart_commands.h"

#include <inttypes.h>
#include <stdio.h>

#define DATA_CHUNK_MAX_VALUES 32
#define DATA_CHUNK_LINE_SIZE  512

static const char *TAG = "ZV_SUBGHZ";

static spi_device_handle_t device_handle = NULL;

static void zv_manual_reset(void)
{
    // This CS toggle brings the SPI interface out of an undefined power-up state.
    // Once the device is attached to the ESP-IDF SPI driver, we send SRES as a separate
    // software reset before writing any radio settings.
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
    if (device_handle != NULL)
    {
        ESP_LOGW(TAG, "Sub-GHz is already initialized; resetting the radio and its resources");
        zv_subghz_deinit();
    }

    zv_manual_reset();

    // SPI bus + device. From spi_attach_device() on, the driver owns CS.
    esp_err_t res = zv_spi_bus_init();
    if (res != ESP_OK)
    {
        ESP_LOGE(TAG, "Cannot initialize the shared SPI bus: %s", esp_err_to_name(res));
        return res;
    }

    res = spi_attach_device();
    if (res != ESP_OK)
    {
        ESP_LOGE(TAG, "Cannot attach the CC1101 to the SPI bus: %s", esp_err_to_name(res));

        zv_subghz_deinit();
        return res;
    }

    res = zv_cc1101_init(device_handle);
    if (res != ESP_OK)
    {
        ESP_LOGE(TAG, "Cannot initialize the CC1101 driver state: %s", esp_err_to_name(res));

        zv_subghz_deinit();
        return res;
    }

    // Software reset (SRES strobe). The byte goes through tx_buffer
    res = zv_cc1101_send_strobe(CC1101_STROBE_SRES);
    if (res != ESP_OK)
    {
        ESP_LOGE(TAG, "CC1101 software reset command failed: %s", esp_err_to_name(res));

        zv_subghz_deinit();
        return res;
    }

    // Without this wait, the first register reads could back as garbage.
    esp_rom_delay_us(1000);

    uint8_t partnum = 0;
    uint8_t version = 0;
    esp_err_t check_result = zv_cc1101_read_partnum(&partnum);
    if (check_result != ESP_OK)
    {
        ESP_LOGE(TAG, "Cannot read CC1101 PARTNUM over SPI: %s", esp_err_to_name(check_result));
        zv_subghz_deinit();
        return check_result;
    }

    check_result = zv_cc1101_read_version(&version);
    if (check_result != ESP_OK)
    {
        ESP_LOGE(TAG, "Cannot read CC1101 VERSION over SPI: %s", esp_err_to_name(check_result));
        zv_subghz_deinit();
        return check_result; 
    }

    ESP_LOGI(TAG, "CC1101 detected: PARTNUM=0x%02X, VERSION=0x%02X", partnum, version);

    // PARTNUM is always 0x00 from the factory; VERSION is 0x14 (or 0x04 on old chip).
    // Anything else means the chip is not responding / SPI is wrong.
    if (partnum != 0x00 || (version != 0x14 && version != 0x04))
    {
        ESP_LOGE(TAG, "Unexpected CC1101 identity: PARTNUM=0x%02X, VERSION=0x%02X",
                 partnum, version);
        zv_subghz_deinit();
        return ESP_ERR_NOT_FOUND;
    }
        
    res = zv_cc1101_config_and_verify();
    if (res != ESP_OK)
    {
        ESP_LOGE(TAG, "CC1101 register configuration or read-back verification failed: %s",
                 esp_err_to_name(res));

        zv_subghz_deinit();
        return res;
    }
    
    res = zv_cc1101_send_strobe(CC1101_STROBE_SRX);
    if (res != ESP_OK)
    {
        ESP_LOGE(TAG, "CC1101 could not start RX mode: %s", esp_err_to_name(res));
        zv_subghz_deinit();
        return res;
    }

    res = zv_cc1101_wait_for_rx();
    if (res != ESP_OK)
    {
        ESP_LOGE(TAG, "CC1101 did not reach RX state before the timeout: %s",
                 esp_err_to_name(res));

        zv_subghz_deinit();
        return res;
    }
    
    esp_rom_delay_us(3000);

    return ESP_OK;
}

esp_err_t zv_subghz_get_rssi_dbm(int8_t *rssi_dbm)
{
    return zv_cc1101_get_rssi_dbm(rssi_dbm);
}

static bool capture_looks_like_noise(const zv_cc1101_capture_t *cap)
{
    if (cap->count < 20)
        return true;

    uint16_t very_short = 0;
    for (uint16_t i = 0; i < cap->count; i++)
    {
        int32_t duration = cap->durations_us[i];

        if (duration < 0)
            duration = -duration;

        if (duration < 150)
            very_short++;
    }

    return very_short > (cap->count / 3);
}

/**
 * Convert the pulses sending line by line as a chunks
 * example:
 *  SUBGHZ_CAPTURE:DATA|seq=0|chunks=8|count=32|timings=-10375,+254,...
 */
static bool send_capture_chunks(const zv_cc1101_capture_t *cap)
{
    if (cap == NULL || cap->count == 0)
        return false;

    const uint16_t chunks_amount = (cap->count + DATA_CHUNK_MAX_VALUES - 1) / DATA_CHUNK_MAX_VALUES;
    for (uint16_t seq = 0; seq < chunks_amount; seq++)
    {
        const uint16_t start = seq * DATA_CHUNK_MAX_VALUES;
        uint16_t end = start + DATA_CHUNK_MAX_VALUES;
        if (end > cap->count)
            end = cap->count;

        const uint16_t values_in_chunk = end - start;
        char line[DATA_CHUNK_LINE_SIZE];

        int written = snprintf(line, sizeof(line),
                               "%s|seq=%u|chunks=%u|count=%u|timings=",
                               SUBGHZ_COMMAND_RES_CAPTURE_DATA,
                               seq, chunks_amount, values_in_chunk);

        if (written < 0 || (size_t)written >= sizeof(line))
            return false;

        size_t offset = (size_t)written;
        for (uint16_t index = start; index < end; index++)
        {
            const char *separator = (index == start) ? "" : ",";

            written = snprintf(line + offset, sizeof(line) - offset,
                               "%s%+" PRId32,
                               separator, cap->durations_us[index]);

            if (written < 0 || (size_t)written >= sizeof(line) - offset)
                return false;

            offset += (size_t)written;
        }

        zv_uart_send_line(line);
    }

    return true;
}

void subghz_listener_task(void *arg)
{
    capture_task_data_t *task_data = (capture_task_data_t *)arg;
    if (task_data == NULL)
    {
        vTaskDelete(NULL);
        return;
    }

    capture_task_data_t data = *task_data;
    vPortFree(task_data);
    task_data = NULL;

    zv_cc1101_capture_t cap = { 0 };
    const TickType_t capture_window = pdMS_TO_TICKS(data.time_frame);
    const TickType_t started_at = xTaskGetTickCount();

    ESP_LOGI(TAG, "Waiting up to %d ms for a valid Sub-GHz signal...", data.time_frame);
    zv_uart_send_line(SUBGHZ_COMMAND_RES_CAPTURE_START);

    zv_cc1101_set_frequency(data.signal);
    while ((xTaskGetTickCount() - started_at) < capture_window)
    {
        TickType_t elapsed = xTaskGetTickCount() - started_at;
        TickType_t remaining = capture_window - elapsed;
        uint32_t remaining_ms = pdTICKS_TO_MS(remaining);
        if (remaining_ms == 0)
            remaining_ms = 1;

        cap.count = 0;
        esp_err_t result = zv_cc1101_capture(&cap, remaining_ms);

        if (result == ESP_ERR_TIMEOUT)
            break;

        if (result != ESP_OK)
        {
            ESP_LOGE(TAG, "Sub-GHz capture failed: %s", esp_err_to_name(result));
            zv_uart_send_formatted_line("%s|reason=%s", SUBGHZ_COMMAND_RES_CAPTURE_FAIL, esp_err_to_name(result));
            vTaskDelete(NULL);
            return;
        }

        if (cap.count == 0 || capture_looks_like_noise(&cap))
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        ESP_LOGI(TAG, "Sub-GHz signal captured: %u pulse durations at %lu Hz", cap.count, (unsigned long)cap.freq_hz);
        if (!send_capture_chunks(&cap))
        {
            ESP_LOGE(TAG, "Sub-GHz capture could not be serialized into UART chunks");
            zv_uart_send_formatted_line("%s|reason=serialization", SUBGHZ_COMMAND_RES_CAPTURE_FAIL);
            vTaskDelete(NULL);
            return;
        }

        const uint16_t chunks_amount = (cap.count + DATA_CHUNK_MAX_VALUES - 1) / DATA_CHUNK_MAX_VALUES;
        zv_uart_send_formatted_line("%s|freq=%lu|count=%u|chunks=%u|buffer_full=%u",
                                    SUBGHZ_COMMAND_RES_CAPTURE_DONE,
                                    (unsigned long)cap.freq_hz,
                                    cap.count,
                                    chunks_amount,
                                    cap.count == CC1101_CAPTURE_MAX_DURATIONS);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Sub-GHz capture timed out: no valid signal was received");
    zv_uart_send_formatted_line("%s|reason=timeout", SUBGHZ_COMMAND_RES_CAPTURE_FAIL);
    vTaskDelete(NULL);
}

void zv_subghz_deinit(void)
{
    zv_cc1101_deinit();

    if (device_handle != NULL)
    {
        zv_spi_bus_remove_device(device_handle);
        device_handle = NULL;
    }

    zv_spi_bus_free();
}
