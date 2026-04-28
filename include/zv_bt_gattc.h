#ifndef ZV_BT_GATTC_H
#define ZV_BT_GATTC_H

#include "esp_gattc_api.h"

typedef struct {
    esp_bt_uuid_t uuid;
    uint16_t start_handle;
    uint16_t end_handle;
} zv_bt_service_t;

typedef struct {
    esp_gatt_if_t gattc_if;
    uint16_t connection_id;
    bool connected;
} zv_bt_gatt_ctx_t;

void zv_bt_gatt_add_service(zv_bt_service_t new_service);
zv_bt_gatt_ctx_t *zv_bt_gattc_get_context(void);
void zv_bt_gatt_open_connection(uint8_t *device_mac, int addr_type);
void zv_bt_gatt_close_connection(void);

void zv_bt_gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);

#endif /* ZV_BT_GATTC_H */
