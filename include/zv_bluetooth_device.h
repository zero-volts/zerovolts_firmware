#ifndef ZV_BLUETOOTH_DEVICE_H
#define ZV_BLUETOOTH_DEVICE_H

#define MAX_STRING_SIZE 128

typedef struct {
    char name[MAX_STRING_SIZE];
    char mac[18];
    int rssi;
    char manufacturer[MAX_STRING_SIZE];
    char service[MAX_STRING_SIZE];
    char appearance[MAX_STRING_SIZE];
} zv_bt_device_t;

void zv_bt_add_device(const char *name, const char *mac, int rssi, const char *manufacturer, const char *service, const char *appearance);
void zv_bt_print_devices();

#endif /* ZV_BLUETOOTH_DEVICE_H */
