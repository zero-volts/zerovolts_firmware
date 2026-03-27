#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "zv_bluetooth_device.h"
#include "zv_bluetooth.h"
#include "zv_format_utils.h"

#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"

#include "esp_gattc_api.h"


#define SCAN_DURATION_IN_SEC 15

static const char *TAG = "ZEROVOLTS_FIRMWARE";

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{    
    switch(event)
    {
        case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
            ESP_LOGI(TAG, "gap_event_handler::set params ready");
            esp_ble_gap_start_scanning(SCAN_DURATION_IN_SEC);
            break;
        case ESP_GAP_BLE_SCAN_RESULT_EVT:

                struct ble_scan_result_evt_param scan_result = param->scan_rst;
                switch(scan_result.search_evt)
                {
                    case ESP_GAP_SEARCH_INQ_RES_EVT:
                        
                        char name_buf[MAX_STRING_SIZE];
                        zv_bt_get_device_name(scan_result, name_buf, MAX_STRING_SIZE);
                       
                        char mac_str[18];
                        zv_format_mac_address(scan_result.bda, mac_str);

                        char manufacturer_name[MAX_STRING_SIZE];
                        zv_bt_get_manufacturer_name(scan_result, manufacturer_name, MAX_STRING_SIZE);

                        char service[MAX_STRING_SIZE];
                        zv_get_device_service(scan_result, service, MAX_STRING_SIZE);

                        char appereance[MAX_STRING_SIZE];
                        zv_get_device_appearance(scan_result, appereance, MAX_STRING_SIZE);

                        zv_bt_add_device(name_buf, mac_str, scan_result.rssi, manufacturer_name, service, appereance);

                        break;
                    case ESP_GAP_SEARCH_INQ_CMPL_EVT:
                        ESP_LOGI(TAG, "The data retrieve is complete");
                        zv_bt_print_devices();
                        break;
                    default:
                        ESP_LOGI(TAG, "not reading search_evet: %d", scan_result.search_evt);
                        break;
                }
                
                break;
            case ESP_GAP_SEARCH_INQ_CMPL_EVT:
                ESP_LOGI(TAG, "════ Escaneo completado ════");
                break;
        default:
            ESP_LOGI(TAG, "gap_event_handler::Event not recognized");
            break;
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

    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));

    esp_ble_scan_params_t scan_params = {
        .scan_type = BLE_SCAN_TYPE_ACTIVE,              // Asking for more information 
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_window = 30,
        .scan_interval = 50,
        .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE
    };

    esp_ble_gap_set_scan_params(&scan_params);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "==== ZeroVolts Firmware initialized ====");
    ESP_LOGI(TAG, "========================================");
}