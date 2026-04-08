#include "zv_bluetooth.h"
#include "zv_bt_gap.h"
#include "zv_bt_gattc.h"

#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"

#define GATT_APPLICATION_ID     0

static const char *TAG = "ZV_BLUETOOTH";

// ============================================================================
// Complete flow to scan and enumerate services and characteristics from devices
// ============================================================================
//
//  Phase 1: Initialization (main.c)
//  ================================
//
//  app_main()
//      |
//      +-- nvs_flash_init()
//      +-- esp_bt_controller_mem_release(CLASSIC_BT)
//      +-- esp_bt_controller_init() + enable(BLE)
//      +-- esp_bluedroid_init() + enable()
//      +-- esp_ble_gatt_set_local_mtu(500)
//      |
//      +-- zv_init_ble_handlers()  ----->  (esta funcion, ver abajo)
//
//
//  Phase 2: Callback registration (zv_bluetooth.c)
//  ===============================================
//
//  zv_init_ble_handlers()
//      |
//      +-- esp_ble_gap_register_callback()
//      +-- esp_ble_gap_set_scan_params()
//      +-- esp_ble_gattc_register_callback()
//      +-- esp_ble_gattc_app_register(0)
//             |
//             +--[evento]--> ESP_GATTC_REG_EVT
//                            store gattc_if to future calls
//
//
//  Phase 3: BLE Scann (zv_bt_gap.c)
//  ===================================
//
//  [evento] ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT
//      |
//      +-- esp_ble_gap_start_scanning(15 seg)
//             |
//             |  (this repeat each time a new devices is detected)
//             |
//             +--[evento]--> ESP_GAP_SEARCH_INQ_RES_EVT
//             |              parse name, MAC, manufacturer, RSSI
//             |              zv_bt_add_device()
//             |
//             |  (15 seconds later...)
//             |
//             +--[evento]--> ESP_GAP_SEARCH_INQ_CMPL_EVT
//                            |
//                            +-- zv_bt_print_devices()
//                            +-- zv_bt_get_closest_device()  // better RSSI [CONN]
//                            |
//                            +-- (if exists any target)
//                                   |
//                                   +-- esp_ble_gattc_open(target)
//                                          |
//                                          v
//                                   Phase 4: connection
//
//
//  Phase 4: GATT Connection (zv_bt_gattc.c)
//  =======================================
//
//  [evento] ESP_GATTC_CONNECT_EVT
//      |    store connection_id, connected = true
//      v
//  [evento] ESP_GATTC_OPEN_EVT
//      |
//      +-- (status OK?)
//      |      |
//      |     YES --> esp_ble_gattc_send_mtu_req()
//      |      |             |
//      |      |             v
//      |      |      [evento] ESP_GATTC_CFG_MTU_EVT
//      |      |             |
//      |      |             +-- esp_ble_gattc_search_service(NULL)
//      |      |                        |
//      |      |                        v
//      |      |                 Phase 5: Enumeration
//      |      |
//      |     NO --> log error, end
//      |
//      v
//
//
//  Phase 5: Services enumeration (zv_bt_gattc.c)
//  ==================================================
//
//  [evento] ESP_GATTC_SEARCH_RES_EVT  (x N, each per service)
//      |    zv_bt_gatt_add_service() --> store UUID + handles
//      v
//  [evento] ESP_GATTC_SEARCH_CMPL_EVT
//      |
//      +-- Per each service:
//      |      +-- esp_ble_gattc_get_attr_count()    // how many charactersitics exists
//      |      +-- esp_ble_gattc_get_all_char()      // getting data
//      |      +-- Per each characteristic:
//      |             +-- Print UUID, handle, properties
//
//  DESCONEXION
//  ===========
//
//  [evento] ESP_GATTC_DISCONNECT_EVT
//      +-- connected = false
//      +-- zv_bt_gattc_reset_services()
//
// ============================================================================
void zv_init_ble_handlers()
{
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(zv_bt_gap_event_handler));

    esp_ble_scan_params_t scan_params = {
        .scan_type = BLE_SCAN_TYPE_ACTIVE,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_window = 30,
        .scan_interval = 50,
        .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE
    };

    esp_ble_gap_set_scan_params(&scan_params);

    ESP_ERROR_CHECK(esp_ble_gatt_set_local_mtu(500));
    ESP_ERROR_CHECK(esp_ble_gattc_register_callback(zv_bt_gattc_event_handler));
    esp_ble_gattc_app_register(GATT_APPLICATION_ID);

    ESP_LOGI(TAG, "BLE handlers registered");
}
