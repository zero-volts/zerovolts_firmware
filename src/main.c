#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "zv_bluetooth.h"
#include "zv_bt_gap.h"
#include "zv_uart.h"

#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"

#include "driver/uart.h"

static const char *TAG = "ZEROVOLTS_FIRMWARE";

#define UART_PORT UART_NUM_2
#define BUF_SIZE  256

void app_main(void)
{
    zv_uart_init();
    zv_uart_send_line("READY");

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
    

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "==== ZeroVolts Firmware initialized ====");
    ESP_LOGI(TAG, "========================================");

    uint8_t rx_byte = 0;
    char line_buf[BUF_SIZE];
    size_t line_len = 0;

    while (1) {
        int len = uart_read_bytes(UART_PORT, &rx_byte, 1, pdMS_TO_TICKS(1000));
        if (len <= 0) {
            continue;
        }

        if (rx_byte == '\r') {
            continue;
        }

        if (rx_byte != '\n') {
            if (line_len < sizeof(line_buf) - 1) {
                line_buf[line_len++] = (char)rx_byte;
            } else {
                line_len = 0;
                ESP_LOGW(TAG, "UART command too long, dropping line");
                zv_uart_send_line("ERR");
            }
            continue;
        }

        line_buf[line_len] = '\0';

        if (line_len == 0) {
            continue;
        }

        ESP_LOGI(TAG, "RX CMD: %s", line_buf);

        if (strcmp(line_buf, "PING") == 0) {
            zv_uart_send_line("PONG");
        } 
        else if (strcmp(line_buf, "SCAN") == 0 || strcmp(line_buf, "SCANN") == 0) {
            zv_bt_start_scanning(10);
            zv_uart_send_line("SCAN:START");
        } else {
            zv_uart_send_line("ERR");
        }

        line_len = 0;
    }
}
