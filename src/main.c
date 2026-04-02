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

#define GATT_APPLICATION_ID     0
#define SCAN_DURATION_IN_SEC    15
#define DEFAULT_NAME            "Unknown"
#define MAX_SERVICES            20

static const char *TAG = "ZEROVOLTS_FIRMWARE";
static esp_gatt_if_t gatt_if = ESP_GATT_IF_NONE;
static zv_bt_service_t discovered_services[MAX_SERVICES];
static int services_count = 0;

static zv_bt_service_t *find_service(const esp_bt_uuid_t uuid)
{
    for (int index = 0; index < services_count; index++)
    {
        if ( discovered_services[index].uuid.uuid.uuid32 == uuid.uuid.uuid32)
            return &discovered_services[index];
    }

    return NULL;
}

static void zv_bt_add_services(zv_bt_service_t new_service)
{
    zv_bt_service_t *service = find_service(new_service.uuid);
    if (service != NULL)
        return;
    
    if (services_count >= MAX_SERVICES)
    {
        ESP_LOGI(TAG, "Can't save more services, is in the limit of %d", MAX_SERVICES);
        return;
    }

    service = &discovered_services[services_count++];

    service->uuid = new_service.uuid;
    service->start_handle = new_service.start_handle;
    service->end_handle = new_service.end_handle;

    ESP_LOGI(TAG, "service stored ID: %d", new_service.uuid.uuid.uuid32);
}

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
                        
                        zv_bt_device_t new_device;

                        zv_bt_get_device_name(scan_result, new_device.name, sizeof(new_device.name));
                        zv_format_mac_address(scan_result.bda, new_device.mac);
                        zv_bt_get_manufacturer_name(scan_result, new_device.manufacturer, sizeof(new_device.manufacturer));
                        zv_get_device_service(scan_result, new_device.service, sizeof(new_device.service));
                        zv_get_device_appearance(scan_result, new_device.appearance, sizeof(new_device.appearance));

                        new_device.connectable = scan_result.ble_evt_type == ESP_BLE_EVT_CONN_ADV || scan_result.ble_evt_type == ESP_BLE_EVT_CONN_DIR_ADV;
                        new_device.rssi = scan_result.rssi;
                        new_device.addr_type = scan_result.ble_addr_type;
                        memcpy(new_device.mac_address, scan_result.bda, sizeof(new_device.mac_address));
                        
                        zv_bt_add_device(new_device);

                        break;
                    case ESP_GAP_SEARCH_INQ_CMPL_EVT:
                        ESP_LOGI(TAG, "The data retrieve is complete");
                        // zv_bt_print_devices();

                        const zv_bt_device_t *near_device = zv_bt_get_closest_device();
                        zv_bt_print_device(near_device);

                        if (near_device != NULL)
                        {
                            ESP_LOGI(TAG, "gap_event_handler:: before call esp_ble_gattc_open");

                            esp_err_t open_result = esp_ble_gattc_open(gatt_if, (uint8_t *)near_device->mac_address, near_device->addr_type, true);
                            const char *error_str = esp_err_to_name(open_result);
                            ESP_LOGE(TAG, "esp_ble_gattc_open result : %d, error: %s", open_result, error_str);
                        }

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
            ESP_LOGI(TAG, "gap_event_handler::Event not recognized %d", event);
            break;
    }
}

static void gatt_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    switch(event)
    {
        case ESP_GATTC_REG_EVT:

            // 1.- Application id registered and storing the interface GATT id 
            if (param->reg.status == ESP_GATT_OK) 
            {    
                gatt_if = gattc_if;
                ESP_LOGI(TAG, "gatt_event_handler::ESP_GATTC_REG_EVT gatt_if: %d", gatt_if);
            }
            else {
                ESP_LOGE(TAG, "gatt_event_handler:: Error registering the app: %d", param->reg.status);
            }
            
            break;
        case ESP_GATTC_CONNECT_EVT:
            
            ESP_LOGI(TAG, "gatt_event_handler::ESP_GATTC_CONNECT_EVT connection ID: %d", param->connect.conn_id);
            break;
       
        case ESP_GATTC_OPEN_EVT:
            // 2.- Virtual connection is open
            ESP_LOGI(TAG, "gatt_event_handler::ESP_GATTC_OPEN_EVT ");
            ESP_LOGI(TAG, "gatt_event_handler::ESP_GATTC_OPEN_EVT status: %d", param->open.status);
            if (param->open.status == ESP_GATT_OK)
            {
                ESP_LOGI(TAG, "gatt_event_handler::ESP_GATTC_OPEN_EVT connection ID: %d", param->open.conn_id);
                
                // Making the transmission buffer bigger.
                esp_ble_gattc_send_mtu_req(gatt_if, param->open.conn_id);  
            }
            
            break;
        case ESP_GATTC_CFG_MTU_EVT:
            ESP_LOGI(TAG, "gatt_event_handler::ESP_GATTC_CFG_MTU_EVT ");
            ESP_LOGI(TAG, "gatt_event_handler::ESP_GATTC_CFG_MTU_EVT status: %d", param->cfg_mtu.status);
            
            esp_ble_gattc_search_service(gatt_if, param->cfg_mtu.conn_id, NULL);
            break;
        case ESP_GATTC_SEARCH_RES_EVT:
            // service(s) found

            zv_bt_service_t new_service;
            new_service.uuid = param->search_res.srvc_id.uuid;
            new_service.start_handle = param->search_res.start_handle;
            new_service.end_handle = param->search_res.end_handle;

            zv_bt_add_services(new_service);
            
            break;
        case ESP_GATTC_SEARCH_CMPL_EVT:
            ESP_LOGI(TAG, "gatt_event_handler::ESP_GATTC_SEARCH_CMPL_EVT");
            // No more services found getting the characteristic from the services.

            ESP_LOGI(TAG, "gatt_event_handler::services found amount : %d", services_count);
            for (int index = 0; index < services_count; index++)
            {
                esp_gattc_char_elem_t *characteristic = malloc (sizeof (esp_gattc_char_elem_t));
                uint16_t count = 1;
                zv_bt_service_t service = discovered_services[index];
                esp_gatt_status_t result = esp_ble_gattc_get_all_char(gatt_if, 
                    param->search_cmpl.conn_id, 
                    service.start_handle, 
                    service.end_handle, 
                    characteristic, 
                    &count, 
                    0
                );

                ESP_LOGI(TAG, "gatt_event_handler::result : %d", result);
                if (result == ESP_OK)
                {
                    ESP_LOGI(TAG, "gatt_event_handler::services found : %d", characteristic->char_handle);
                    ESP_LOGI(TAG, "gatt_event_handler::read property : %d", characteristic->properties & ESP_GATT_CHAR_PROP_BIT_READ);
                    ESP_LOGI(TAG, "gatt_event_handler::write nr property : %d", characteristic->properties & ESP_GATT_CHAR_PROP_BIT_WRITE_NR);
                    ESP_LOGI(TAG, "gatt_event_handler::write property : %d", characteristic->properties & ESP_GATT_CHAR_PROP_BIT_WRITE);
                    ESP_LOGI(TAG, "gatt_event_handler::notify property : %d", characteristic->properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY);
                    ESP_LOGI(TAG, "gatt_event_handler::notify indicate : %d", characteristic->properties & ESP_GATT_CHAR_PROP_BIT_INDICATE);
                }
               
                free(characteristic);
                break;
            }

            break;
        default:
            ESP_LOGI(TAG, "gatt_event_handler:: event not recognized: %d", event);
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

    // TODO: mejorar diagrama, crear un diagrama de flujo para entender mejor y estar seguro de cadada llamada
    // 1.- Configuracion del host y controller
    // 2.- Registro del handler del GAP (esp_ble_gap_register_callback)
    // 3.- Seteando los paramatros para el escaneo (esp_ble_gap_set_scan_params)
    //     |
    //     |------> ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT
    //     |        |
    //     |        |-------> Se da inicio para buscar o escuchar dispositivos esp_ble_gap_start_scanning
    //     |
    //     |------> .....
    //     |------> Termina el scan y se llama el evento ESP_GAP_SEARCH_INQ_CMPL_EVT
    //             |
    //             |--------> Se obtiene el dispositivo mas cercano y se abre la comunicacion con el GATT
    //                        llamando a esp_ble_gattc_open
    //                        |
    //                        |--------> ESP_GATTC_CONNECT_EVT
    //                        |--------> ESP_GATTC_OPEN_EVT
    //                                   |
    //                                   |---------> esp_ble_gattc_send_mtu_req
    //                                               |
    //                                               |----> ESP_GATTC_CFG_MTU_EVT
    //                                                      |
    //                                                      |------>  Se comienza a buscar servicios del dispositivo usando 
    //                                                                esp_ble_gattc_search_service
    //                                                                |
    //                                                                |---> ESP_GATTC_SEARCH_RES_EVT
    //                                                                |
    //                                                                |----> ESP_GATTC_SEARCH_CMPL_EVT
    //
    //    
    // 4.- Registro del handler para GATTC (esp_ble_gattc_register_callback)
    // 5.- Registro de identificacion de la aplicacion (esp_ble_gattc_app_register)
    //     |
    //     |------> ESP_GATTC_REG_EVT
    //             |
    //             |--- Guarda gattc_if para futuras llamadas a la libreria para obtener informacion del GATT
                



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

    ESP_ERROR_CHECK(esp_ble_gattc_register_callback(gatt_event_handler));
    esp_ble_gattc_app_register(GATT_APPLICATION_ID);    
    // esp_ble_gatt_set_local_mtu()

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "==== ZeroVolts Firmware initialized ====");
    ESP_LOGI(TAG, "========================================");
}