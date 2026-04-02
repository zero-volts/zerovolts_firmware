#include "zv_bluetooth.h"
#include "zv_bt_gap.h"
#include "zv_bt_gattc.h"

#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_log.h"

#define GATT_APPLICATION_ID     0

static const char *TAG = "ZV_BLUETOOTH";

// 1.- Configuracion del host y controller (main.c)
// 2.- Registro del handler del GAP (esp_ble_gap_register_callback)
// 3.- Seteando los parametros para el escaneo (esp_ble_gap_set_scan_params)
//     |
//     |------> ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT
//     |        |
//     |        |-------> esp_ble_gap_start_scanning
//     |
//     |------> Termina el scan -> ESP_GAP_SEARCH_INQ_CMPL_EVT
//             |
//             |--------> Se obtiene el dispositivo mas cercano y se abre la comunicacion
//                        llamando a esp_ble_gattc_open
//                        |
//                        |--------> ESP_GATTC_CONNECT_EVT
//                        |--------> ESP_GATTC_OPEN_EVT
//                                   |
//                                   |---------> esp_ble_gattc_send_mtu_req
//                                               |
//                                               |----> ESP_GATTC_CFG_MTU_EVT
//                                                      |
//                                                      |------> esp_ble_gattc_search_service
//                                                                |
//                                                                |---> ESP_GATTC_SEARCH_RES_EVT (x N)
//                                                                |
//                                                                |----> ESP_GATTC_SEARCH_CMPL_EVT
//                                                                       |
//                                                                       |----> enumerate characteristics
//
// 4.- Registro del handler para GATTC (esp_ble_gattc_register_callback)
// 5.- Registro de identificacion de la aplicacion (esp_ble_gattc_app_register)
//     |
//     |------> ESP_GATTC_REG_EVT
//             |
//             |--- Guarda gattc_if para futuras llamadas

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

    ESP_ERROR_CHECK(esp_ble_gattc_register_callback(zv_bt_gattc_event_handler));
    esp_ble_gattc_app_register(GATT_APPLICATION_ID);

    ESP_LOGI(TAG, "BLE handlers registered");
}
