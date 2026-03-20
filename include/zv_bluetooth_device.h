#ifndef ZV_BLUETOOTH_DEVICE_H
#define ZV_BLUETOOTH_DEVICE_H

typedef struct {
    char name[128];
    char mac[18];
    int rssi;
    char manufacturer[128];
} zv_bt_device_t;

void zv_bt_add_device(const char *name, const char *mac, int rssi, const char *manufacturer);
void zv_bt_print_devices();

#endif /* ZV_BLUETOOTH_DEVICE_H */
