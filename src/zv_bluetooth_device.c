#include "zv_bluetooth_device.h"
#include "zv_uart.h"
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "zv_bt_gap.h"

#define MAX_DEVICES 35
#define DEFAULT_NAME "Unknown"

static const char *TAG = "ZV_BLUETOOTH_DEVICE";

static zv_bt_device_t devices[MAX_DEVICES];
static int device_count = 0;

static zv_bt_device_t *find_device(const char *mac)
{
    for (int index = 0; index < device_count; index++)
    {
        if ( strcmp(devices[index].mac, mac) == 0)
            return &devices[index];
    }

    return NULL;
}

zv_bt_device_t *zv_bt_get_closest_device()
{
    if (device_count == 0)
    {
        ESP_LOGW(TAG, "zv_bt_get_closest_device:: no devices stored yet ...");
        return NULL;
    }

    int last_rssi = -500;
    int device_found_index = -1;
    for (int index = 0; index < device_count; index++)
    {
        zv_bt_device_t device = devices[index];
        if (!device.connectable) {
            continue;
        }

        if (device.rssi > last_rssi)
        {
            device_found_index = index;
            last_rssi = device.rssi;
        }
    }

    if (device_found_index < 0)
    {
        ESP_LOGW(TAG, "zv_bt_get_closest_device:: device not found");
        return NULL;
    }

    return &devices[device_found_index];
}

zv_bt_device_t *zv_bt_get_device_by_name(const char *name)
{
    if (name == NULL || !name[0])
    {
        ESP_LOGW(TAG, "zv_bt_get_device_by_name:: invalid name");
        return NULL;
    }

    for (int index = 0; index < device_count; index++)
    {
        zv_bt_device_t device = devices[index];
        if (!device.connectable) {
            continue;
        }

        if (strcmp(device.name, name) == 0)
        {
            ESP_LOGD(TAG, "zv_bt_get_device_by_name:: name found index %d", index);
            return &devices[index];
        }
    }

    return NULL;
}

void zv_bt_add_device(zv_bt_device_t new_device)
{
    if (!new_device.mac[0])
        return;

    zv_bt_device_t *device = find_device(new_device.mac);
    if (device)
    {
        device->rssi = new_device.rssi;
        if (new_device.name[0] && strcmp(device->name, DEFAULT_NAME) == 0)
            snprintf(device->name, sizeof(device->name), "%s", new_device.name);

        if (new_device.manufacturer[0] && strcmp(device->manufacturer, DEFAULT_NAME) == 0)
            snprintf(device->manufacturer, sizeof(device->manufacturer), "%s", new_device.manufacturer);

        return;
    }

    if (device_count >= MAX_DEVICES)
    {
        ESP_LOGI(TAG, "Can't save more devices, is in the limit of %d", MAX_DEVICES);
        return;
    }

    device = &devices[device_count++];

    device->rssi = new_device.rssi;
    device->connectable = new_device.connectable;
    device->addr_type = new_device.addr_type;
    memcpy(device->mac_address, new_device.mac_address, sizeof(device->mac_address));

    snprintf(device->name, sizeof(device->name), "%s", new_device.name[0] ? new_device.name : DEFAULT_NAME);
    snprintf(device->mac, sizeof(device->mac), "%s", new_device.mac);
    snprintf(device->manufacturer, sizeof(device->manufacturer), "%s",  new_device.manufacturer[0] ? new_device.manufacturer : DEFAULT_NAME);
    snprintf(device->service, sizeof(device->service), "%s",  new_device.service[0] ? new_device.service : DEFAULT_NAME);
    snprintf(device->appearance, sizeof(device->appearance), "%s",  new_device.appearance[0] ? new_device.appearance : DEFAULT_NAME);

    zv_bt_send_scan_result_uart(device);
}

void zv_bt_print_devices()
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "-------------------------------");
    for (int idx = 0; idx < device_count; idx++)
    {
        ESP_LOGI(TAG, "[%d] %s name: %s, mac: %s, Manufacturer: %s, service: %s, appearance: %s, rssi: %d",
                idx,
                devices[idx].connectable ? "[CONN]" : "[----]",
                devices[idx].name, devices[idx].mac, devices[idx].manufacturer,
                devices[idx].service, devices[idx].appearance, devices[idx].rssi);
    }
    ESP_LOGI(TAG, "-------------------------------");
}

void zv_bt_print_device(const zv_bt_device_t *device)
{
    if (device == NULL)
        return;

    ESP_LOGI(TAG, "name: %s, mac: %s, Manufacturer: %s, service: %s, appearance: %s, rssi: %d",
                device->name, device->mac, device->manufacturer, device->service, device->appearance, device->rssi);
}
