// https://docs.espressif.com/projects/esp-idf/en/v4.2/esp32s2/api-reference/peripherals/spi_master.html#_CPPv416spi_bus_config_t
// https://www.coralradio.com/cc1101-433mhz-module-guide.html
// https://gist.github.com/Taylor-eOS/0882416612accbeee27b064519ef4d35
#include "zv_subghz.h"

#include "esp_log.h"
#include "esp_rom_sys.h"   // esp_rom_delay_us()
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"   // gpio_config(), gpio_set_level()
#include "driver/spi_master.h"

#include "zv_spi_bus.h"


static const char *TAG = "ZV_SUBGHZ";
typedef struct {
    spi_device_handle_t device_handle;
    bool is_capturing;
} subghz_context;

static subghz_context local_ctx;
static zv_event_handler_t event_receiver;

void zv_subghz_set_event_receiver(zv_event_handler_t handler)
{
    event_receiver = handler;
}

static void notify_capture_event(zv_event_type_t type, const char *reason, const zv_cc1101_capture_t *capture, uint64_t capture_id)
{
    if (event_receiver == NULL)
        return;
    
    const zv_event_t event = {
        .type = type,
        .data.subghz_capture = {
            .capture = capture,
            .capture_id = capture_id,
            .reason = reason,
        },
    };

    event_receiver(&event, NULL);
}

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

    return zv_spi_bus_add_device(&dev, &local_ctx.device_handle);
}

esp_err_t zv_subghz_init(void)
{
    if (local_ctx.device_handle != NULL)
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

    res = zv_cc1101_init(local_ctx.device_handle);
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

    res = zv_cc1101_wait_for_marc_state(CC1101_MARCSTATE_RX);
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

static void relase_capture_task()
{
    local_ctx.is_capturing = false;
    vTaskDelete(NULL);
}

void subghz_listener_task(void *arg)
{
    if (local_ctx.is_capturing)
    {
        vPortFree(arg);
        notify_capture_event(ZV_EVENT_SUBGHZ_CAPTURE_FAILED, "busy", NULL, 0);
        vTaskDelete(NULL);
        return; 
    }

    capture_task_data_t *task_data = (capture_task_data_t *)arg;
    if (task_data == NULL)
    {
        relase_capture_task();
        return;
    }

    capture_task_data_t data = *task_data;
    vPortFree(task_data);
    task_data = NULL;

    zv_cc1101_capture_t cap = { 0 };
    const TickType_t capture_window = pdMS_TO_TICKS(data.time_frame);
    const TickType_t started_at = xTaskGetTickCount();

    ESP_LOGI(TAG, "Waiting up to %d ms for a valid Sub-GHz signal...", data.time_frame);
    notify_capture_event(ZV_EVENT_SUBGHZ_CAPTURE_STARTED, NULL, NULL, 0);

    esp_err_t result = zv_cc1101_set_frequency(data.signal);
    if (result != ESP_OK) 
    {
        notify_capture_event(ZV_EVENT_SUBGHZ_CAPTURE_ERROR, "not supported frequency", NULL, 0);
        relase_capture_task();
        return; 
    }

    local_ctx.is_capturing = true;
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
            notify_capture_event(ZV_EVENT_SUBGHZ_CAPTURE_FAILED, esp_err_to_name(result), NULL, 0);

            relase_capture_task();
            return;
        }

        if (cap.count == 0 || capture_looks_like_noise(&cap))
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        ESP_LOGI(TAG, "Sub-GHz signal captured: %u pulse durations at %lu Hz", cap.count, (unsigned long)cap.freq_hz);
        uint64_t capture_id = (uint64_t)esp_timer_get_time();
        notify_capture_event(ZV_EVENT_SUBGHZ_CAPTURE_DONE, NULL, &cap, capture_id);

        relase_capture_task();
        return;
    }

    ESP_LOGI(TAG, "Sub-GHz capture timed out: no valid signal was received");
    notify_capture_event(ZV_EVENT_SUBGHZ_CAPTURE_FAILED, "timeout", NULL, 0);
    relase_capture_task();
}

esp_err_t zv_subghz_transmit(const zv_cc1101_capture_t *capture, uint16_t repetitions)
{
    if (capture == NULL || repetitions == 0)
        return ESP_ERR_INVALID_ARG;

    esp_err_t result = zv_cc1101_set_frequency(capture->freq_hz);
    if (result == ESP_OK)
        result = zv_cc1101_transmit(capture, repetitions);

    if (result != ESP_OK)
        ESP_LOGE(TAG, "Sub-GHz transmission failed: %s", esp_err_to_name(result));

    return result;
}

void zv_subghz_deinit(void)
{
    zv_cc1101_deinit();

    if (local_ctx.device_handle != NULL)
    {
        zv_spi_bus_remove_device(local_ctx.device_handle);
        local_ctx.device_handle = NULL;
    }

    local_ctx.is_capturing = false;

    zv_spi_bus_free();
}
