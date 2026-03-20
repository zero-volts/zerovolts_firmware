#include "bluethoot_device.h"
#include <stdio.h>
#include "esp_log.h"

#define MAX_DEVICES 35
#define DEFAULT_NAME "Unknown"

static const char *TAG = "BLUETHOOT_DEVICE";

static bl_device_t devices[MAX_DEVICES];
static int device_count = 0;

static bl_device_t *find_device(const char *mac)
{
    for (int index = 0; index < device_count; index++)
    {
        if ( strcmp(devices[index].mac, mac) == 0)
            return &devices[index];
    }

    return NULL;
}

void add_device(const char *name, const char *mac, int rssi, const char *manufacturer)
{
    if (!mac || !mac[0])
        return;

    bl_device_t *device = find_device(mac);
    if (device) 
    {
        device->rssi = rssi;
        if (name && strcmp(device->name, DEFAULT_NAME) == 0)
            snprintf(device->name, sizeof(device->name), "%s", name);

        if (manufacturer && strcmp(device->manufacturer, DEFAULT_NAME) == 0)
            snprintf(device->manufacturer, sizeof(device->manufacturer), "%s", manufacturer);

        return;
    }

    if (device_count >= MAX_DEVICES) 
    {
        ESP_LOGI(TAG, "Can't save more devices, is in the limit of %d", MAX_DEVICES);
        return;
    }

    device = &devices[device_count++];

    device->rssi = rssi;
    snprintf(device->name, sizeof(device->name), "%s", name ? name : DEFAULT_NAME);
    snprintf(device->mac, sizeof(device->mac), "%s", mac);
    snprintf(device->manufacturer, sizeof(device->manufacturer), "%s",  manufacturer ? manufacturer : DEFAULT_NAME);
}

void print_devices()
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "-------------------------------");
    for (int idx = 0; idx < device_count; idx++)
    {
        ESP_LOGI(TAG, "[%d] name: %s, mac: %s, Manufacturer: %s, rssi: %d", 
                idx, devices[idx].name, devices[idx].mac, devices[idx].manufacturer, devices[idx].rssi);
    }
    ESP_LOGI(TAG, "-------------------------------");
}