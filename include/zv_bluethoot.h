#ifndef ZV_BLUETHOOT_H
#define ZV_BLUETHOOT_H

#include "esp_gap_ble_api.h"

typedef struct {
    uint16_t company_id;
    const char *name;
} ble_signature_t;

// https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Assigned_Numbers/out/en/Assigned_Numbers.pdf
extern const ble_signature_t zv_signatures[];

const char *get_device_type(esp_bt_dev_type_t device_type);
void get_manufacturer_name(struct ble_scan_result_evt_param scan_result, char *out_name, int out_length);
void get_device_name(struct ble_scan_result_evt_param scan_result, char *out_name, int out_length);

#endif /* ZV_BLUETHOOT_H */
