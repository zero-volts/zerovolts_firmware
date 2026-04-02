#ifndef ZV_BT_GAP_H
#define ZV_BT_GAP_H

#include "esp_gap_ble_api.h"

typedef struct {
    uint16_t company_id;
    const char *name;
} zv_bt_signature_t;

// https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Assigned_Numbers/out/en/Assigned_Numbers.pdf
extern const zv_bt_signature_t zv_bt_signatures[];

const char *zv_bt_get_device_type(esp_bt_dev_type_t device_type);
void zv_bt_get_manufacturer_name(struct ble_scan_result_evt_param scan_result, char *out_name, int out_length);
void zv_bt_get_device_name(struct ble_scan_result_evt_param scan_result, char *out_name, int out_length);
void zv_get_device_appearance(struct ble_scan_result_evt_param scan_result, char *out_name, int out_length);
void zv_get_device_service(struct ble_scan_result_evt_param scan_result, char *out_name, int out_length);

void zv_bt_gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);

#endif /* ZV_BT_GAP_H */
