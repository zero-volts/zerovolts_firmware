#ifndef BLUETHOOT_DEVICE_H
#define BLUETHOOT_DEVICE_H

typedef struct {
    char name[128];
    char mac[18];
    int rssi;
    char manufacturer[128];
} bl_device_t;

void add_device(const char *name, const char *mac, int rssi, const char *manufacturer);
void print_devices();

#endif /* BLUETHOOT_DEVICE_H */
