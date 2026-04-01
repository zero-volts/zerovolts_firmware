#ifndef ZV_BLUETOOTH_DEVICE_H
#define ZV_BLUETOOTH_DEVICE_H

#include "esp_gattc_api.h"

#define MAX_STRING_SIZE 128

typedef struct {
    char name[MAX_STRING_SIZE];
    char mac[18];
    uint8_t mac_address[6];
    esp_ble_addr_type_t addr_type;
    int rssi;
    char manufacturer[MAX_STRING_SIZE];
    char service[MAX_STRING_SIZE];
    char appearance[MAX_STRING_SIZE];
    bool connectable;
} zv_bt_device_t;

void zv_bt_add_device(zv_bt_device_t new_device);
zv_bt_device_t *zv_bt_get_closest_device();
zv_bt_device_t *zv_bt_get_device_by_name(const char *name);
void zv_bt_print_devices();
void zv_bt_print_device(const zv_bt_device_t *device);

#endif /* ZV_BLUETOOTH_DEVICE_H */
