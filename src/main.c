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

#include "zv_uart_commands.h"
#include "zv_uart_protocol.h"

static const char *TAG = "ZEROVOLTS_FIRMWARE";

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
    zv_uart_protocol_init();

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "==== ZeroVolts Firmware initialized ====");
    ESP_LOGI(TAG, "========================================");

    if (zv_uart_is_ready())
        zv_uart_send_line("READY");

    esp_err_t subghz_result = zv_subghz_init();
    if (subghz_result == ESP_OK)
    {
       zv_uart_send_formatted_line("%s:SUCCESS", SUBGHZ_COMMAND_RES_CHECK_MODULE);
    }
    else
    {
        zv_uart_send_formatted_line("%s:FAILS", SUBGHZ_COMMAND_RES_CHECK_MODULE);
        ESP_LOGE(TAG, "CC1101 initialization failed: %s. Check the SPI wiring and radio power",
                 esp_err_to_name(subghz_result));
    }

    while (1) {
        zv_uart_parse_commands();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
