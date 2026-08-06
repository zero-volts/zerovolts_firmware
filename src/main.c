#include "zv_bluetooth.h"
#include "zv_uart.h"
#include "zv_subghz.h"

#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"

#include "zv_cc1101.h"


static const char *TAG = "ZEROVOLTS_FIRMWARE";

static bool capture_looks_like_noise(
    const zv_cc1101_capture_t *cap)
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

static void subghz_listener_task(void *arg)
{
    zv_cc1101_capture_t cap;
    while (true)
    {
        esp_err_t result = zv_cc1101_capture(&cap, 10000);
        if (result == ESP_OK && cap.count > 0)
        {
            if (capture_looks_like_noise(&cap))
            {
                ESP_LOGD(TAG, "Captura descartada como ruido: %u flancos",cap.count);
                continue;
            }

            ESP_LOGI(TAG, "Señal candidata: %u flancos en %lu Hz", cap.count, (unsigned long)cap.freq_hz);
            for (uint16_t i = 0; i < cap.count; i++)
            {
                printf("%+" PRId32 " ",cap.durations_us[i]);
            }

            printf("\n");
        }
        else if (result == ESP_ERR_TIMEOUT)
        {
            ESP_LOGD(TAG, "Sin señal; reiniciando escucha");
        }
        else
        {
            if (result != ESP_OK)
                ESP_LOGE(TAG, "Error capturando: %s", esp_err_to_name(result));
            else
                ESP_LOGD(TAG, "Carrier vacio, nada que escuchar");

            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NOT_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    // Initializing Bluetooth controller
    esp_bt_controller_config_t bt_config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_config));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

    // Initializing Bluedroid host

    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    zv_init_ble_handlers();
    zv_uart_init();

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "==== ZeroVolts Firmware initialized ====");
    ESP_LOGI(TAG, "========================================");

    if (zv_uart_is_ready())
        zv_uart_send_line("READY");

    if (zv_subghz_init() == ESP_OK)
    {
        ESP_LOGI(TAG, "CC1101 READY");
        BaseType_t task_created = xTaskCreate(
            subghz_listener_task,
            "subghz_listener",
            4096,
            NULL,
            5,
            NULL
        );

        if (task_created != pdPASS)
            ESP_LOGE(TAG, "No se pudo crear la tarea Sub-GHz");
    }
    else
    {
        ESP_LOGE(TAG, "CC1101 NOT_FOUND — revisar cableado SPI");
    }

    while (1) {
        zv_uart_parse_commands();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
