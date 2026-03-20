#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bluethoot_device.h"
#include "zv_bluethoot.h"

#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"


#define SCAN_DURATION_SEC 15

static const char *ZEROVOLTS_MAIN_TAG = "ZEROVOLTS_FIRMWARE";

static void format_mac_address(const uint8_t* mac_addr, char* output_str) 
{
    sprintf(output_str, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac_addr[0], mac_addr[1], mac_addr[2],
            mac_addr[3], mac_addr[4], mac_addr[5]);
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{    
    switch(event)
    {
        case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
            ESP_LOGI(ZEROVOLTS_MAIN_TAG, "gap_event_handler::set params ready");
            esp_ble_gap_start_scanning(SCAN_DURATION_SEC);
            break;
        case ESP_GAP_BLE_SCAN_RESULT_EVT:

                struct ble_scan_result_evt_param scan_result = param->scan_rst;
                switch(scan_result.search_evt)
                {
                    case ESP_GAP_SEARCH_INQ_RES_EVT:
                        
                        char name_buf[128];
                        get_device_name(scan_result, name_buf, 128);
                       
                        char mac_str[18];
                        format_mac_address(scan_result.bda, mac_str);

                        char manufacturer_name[128];
                        get_manufacturer_name(scan_result, manufacturer_name, 128);

                        add_device(name_buf, mac_str, scan_result.rssi, manufacturer_name);

                        break;
                    case ESP_GAP_SEARCH_INQ_CMPL_EVT:
                        ESP_LOGI(ZEROVOLTS_MAIN_TAG, "The data retrieve is complete");
                        print_devices();
                        break;
                    default:
                        ESP_LOGI(ZEROVOLTS_MAIN_TAG, "not reading search_evet: %d", scan_result.search_evt);
                        break;
                }
                
            break;
            case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
                ESP_LOGI(ZEROVOLTS_MAIN_TAG, "gap_event_handler::ESP_GAP_BLE_SCAN_START_COMPLETE_EVT");
                break;
            case ESP_GAP_SEARCH_INQ_CMPL_EVT:
                ESP_LOGI(ZEROVOLTS_MAIN_TAG, "════ Escaneo completado ════");
                break;
        default:
            ESP_LOGI(ZEROVOLTS_MAIN_TAG, "gap_event_handler::Event not recognized");
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

    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    // Initializing Bluethoot controller
    esp_bt_controller_config_t bluethoot_config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bluethoot_config);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);

    // Initializing Bluedroid host
    //TODO: validar los resultados :S
    esp_bluedroid_init();
    esp_bluedroid_enable();

    esp_ble_gap_register_callback(gap_event_handler);

    // https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/ble/get-started/ble-device-discovery.html
    esp_ble_scan_params_t scan_params = {
        .scan_type = BLE_SCAN_TYPE_ACTIVE,              // Asking for more information 
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_window = 30,
        .scan_interval = 50,
        .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE    
    };

    esp_ble_gap_set_scan_params(&scan_params);
}