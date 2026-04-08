#include "zv_bt_gattc.h"
#include "zv_format_utils.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

#define MAX_SERVICES 20
static const char *TAG = "ZV_BT_GATTC";

static zv_bt_service_t discovered_services[MAX_SERVICES];
static int services_count = 0;

// internal context to manager gatcc id and connection id between calls
static zv_bt_gatt_ctx_t gattc_context = {
    .gattc_if = ESP_GATT_IF_NONE,
    .connection_id = 0,
    .connected = false
};

static zv_bt_service_t *zv_gatt_find_service(const esp_bt_uuid_t uuid)
{
    for (int index = 0; index < services_count; index++)
    {
        esp_bt_uuid_t stored = discovered_services[index].uuid;
        if (stored.len != uuid.len)
            continue;

        if (memcmp(stored.uuid.uuid128, uuid.uuid.uuid128, uuid.len) == 0)
            return &discovered_services[index];
    }

    return NULL;
}

void zv_bt_gatt_add_service(zv_bt_service_t new_service)
{
    zv_bt_service_t *service = zv_gatt_find_service(new_service.uuid);
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

    char uuid_str[37];
    zv_format_uuid(new_service.uuid.uuid.uuid128, new_service.uuid.len, uuid_str, sizeof(uuid_str));
}

zv_bt_gatt_ctx_t *zv_bt_gattc_get_context()
{
    return &gattc_context;
}

void zv_bt_gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    switch(event)
    {
        case ESP_GATTC_REG_EVT:

            if (param->reg.status == ESP_GATT_OK)
            {
                gattc_context.gattc_if = gattc_if;
                ESP_LOGI(TAG, "ESP_GATTC_REG_EVT gatt_if: %d", gattc_if);
            }
            else {
                ESP_LOGE(TAG, "Error registering the app: %d", param->reg.status);
            }

            break;
        case ESP_GATTC_CONNECT_EVT:

            gattc_context.connection_id = param->connect.conn_id;
            gattc_context.connected = true;
            ESP_LOGI(TAG, "ESP_GATTC_CONNECT_EVT connection ID: %d", param->connect.conn_id);
            break;

        case ESP_GATTC_OPEN_EVT:
            if (param->open.status == ESP_GATT_OK)
            {
                ESP_LOGI(TAG, "ESP_GATTC_OPEN_EVT connection ID: %d", param->open.conn_id);
                esp_ble_gattc_send_mtu_req(gattc_if, param->open.conn_id);
            }
            else {
                ESP_LOGE(TAG, "ESP_GATTC_OPEN_EVT failed, status: %d", param->open.status);
            }

            break;
        case ESP_GATTC_CFG_MTU_EVT:
            if (param->cfg_mtu.status != ESP_GATT_OK)
            {
                ESP_LOGW(TAG, "MTU negotiation failed (status=%d), searching services anyway...", param->cfg_mtu.status);
            }
            esp_ble_gattc_search_service(gattc_if, param->cfg_mtu.conn_id, NULL);
            break;
        case ESP_GATTC_SEARCH_RES_EVT:
        {
            zv_bt_service_t new_service;
            new_service.uuid = param->search_res.srvc_id.uuid;
            new_service.start_handle = param->search_res.start_handle;
            new_service.end_handle = param->search_res.end_handle;

            zv_bt_gatt_add_service(new_service);

            break;
        }
        case ESP_GATTC_SEARCH_CMPL_EVT:
        {
            ESP_LOGI(TAG, "ESP_GATTC_SEARCH_CMPL_EVT");
            ESP_LOGI(TAG, "services found: %d", services_count);

            if (services_count <= 0)
                break;

            int total_readable = 0;
            int total_writable = 0;
            int total_notify = 0;

            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "======================================================");
            ESP_LOGI(TAG, "           GATT Device database                       ");
            ESP_LOGI(TAG, "======================================================");

            zv_bt_service_t *services = discovered_services;
            for (int index = 0; index < services_count; index++)
            {
                zv_bt_service_t service = services[index];

                char svc_uuid_str[37];
                zv_format_uuid(service.uuid.uuid.uuid128, service.uuid.len, svc_uuid_str, sizeof(svc_uuid_str));
                ESP_LOGI(TAG, "");
                ESP_LOGI(TAG, "+--- Service %d (handles %d-%d) ---",
                         index + 1, service.start_handle, service.end_handle);
                ESP_LOGI(TAG, "|    UUID: %s", svc_uuid_str);

                uint16_t count = 0;
                esp_ble_gattc_get_attr_count(gattc_if, gattc_context.connection_id,
                    ESP_GATT_DB_CHARACTERISTIC,
                    service.start_handle, service.end_handle,
                    0,
                    &count
                );

                if (count == 0)
                {
                    ESP_LOGI(TAG, "|   (sin caracteristicas)");
                    continue;
                }

                esp_gattc_char_elem_t *characteristic = malloc(sizeof(esp_gattc_char_elem_t) * count);
                if (characteristic == NULL)
                {
                    ESP_LOGE(TAG, "|   malloc failed for %d characteristics", count);
                    continue;
                }

                esp_gatt_status_t result = esp_ble_gattc_get_all_char(gattc_if,
                    param->search_cmpl.conn_id,
                    service.start_handle,
                    service.end_handle,
                    characteristic,
                    &count,
                    0
                );

                if (result != ESP_OK)
                {
                    ESP_LOGE(TAG, "|   get_all_char failed: %d", result);
                    free(characteristic);
                    continue;
                }

                ESP_LOGI(TAG, "|   Caracteristicas: %d", count);
                for (int idx = 0; idx < count; idx++)
                {
                    char uuid_str[37];
                    zv_format_uuid(characteristic[idx].uuid.uuid.uuid128, characteristic[idx].uuid.len, uuid_str, sizeof(uuid_str));

                    uint8_t props = characteristic[idx].properties;
                    ESP_LOGI(TAG, "|");
                    ESP_LOGI(TAG, "|   +-- Characteristic %d (handle: %d)", idx + 1, characteristic[idx].char_handle);
                    ESP_LOGI(TAG, "|   |   UUID: %s", uuid_str);
                    ESP_LOGI(TAG, "|   |   Properties: %s%s%s%s%s",
                             (props & ESP_GATT_CHAR_PROP_BIT_READ)     ? "READ "     : "",
                             (props & ESP_GATT_CHAR_PROP_BIT_WRITE)    ? "WRITE "    : "",
                             (props & ESP_GATT_CHAR_PROP_BIT_WRITE_NR) ? "WRITE_NR " : "",
                             (props & ESP_GATT_CHAR_PROP_BIT_NOTIFY)   ? "NOTIFY "   : "",
                             (props & ESP_GATT_CHAR_PROP_BIT_INDICATE) ? "INDICATE " : "");

                    bool can_read = props & ESP_GATT_CHAR_PROP_BIT_READ;
                    bool can_write = props & (ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR);
                    bool can_notify = props & (ESP_GATT_CHAR_PROP_BIT_NOTIFY | ESP_GATT_CHAR_PROP_BIT_INDICATE);

                    const char *access_level;
                    if (can_read && can_write)       access_level = "READ + WRITE";
                    else if (can_read && can_notify)  access_level = "READ + SUBSCRIPTION";
                    else if (can_read)                access_level = "ONLY READ";
                    else if (can_write)               access_level = "ONLY WRITE";
                    else if (can_notify)              access_level = "ONLY NOTIFY/INDICATE";
                    else                              access_level = "WITHOUT ACCESS";

                    ESP_LOGI(TAG, "|   |   Access: %s", access_level);

                    if (can_read) total_readable++;
                    if (can_write) total_writable++;
                    if (can_notify) total_notify++;
                }

                free(characteristic);
            }

            break;
        }
        case ESP_GATTC_DISCONNECT_EVT:
            ESP_LOGW(TAG, "DISCONNECT reason: %d", param->disconnect.reason);
            gattc_context.connection_id = 0;
            gattc_context.connected = false;
            services_count = 0;
            break;
        default:
            ESP_LOGI(TAG, "event not recognized: %d", event);
            break;
    }
}
