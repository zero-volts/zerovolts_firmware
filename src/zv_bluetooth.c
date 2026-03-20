#include "zv_bluetooth.h"
#include <stdio.h>

#include "esp_log.h"

static const char *TAG = "ZV_BLUETOOTH";
const zv_bt_signature_t zv_bt_signatures[] = {
    { 0x004C, "Apple" },
    { 0x0075, "Samsung" },
    { 0x038F, "Xiaomi" },
    { 0x027D, "Huawei"}
};

const char *zv_bt_get_device_type(esp_bt_dev_type_t device_type)
{
    switch(device_type) {
        case ESP_BT_DEVICE_TYPE_BREDR:
            return "Classic Bluetooth";
        case ESP_BT_DEVICE_TYPE_BLE:
            return "BLE";
        case ESP_BT_DEVICE_TYPE_DUMO:
            return "Classic & BLE";
    }
    return "Unknown";
}

void zv_bt_get_manufacturer_name(struct ble_scan_result_evt_param scan_result, char *out_name, int out_length)
{
    uint8_t adv_name_length = 0;
    uint8_t *adv_manufactured = esp_ble_resolve_adv_data_by_type(scan_result.ble_adv,
        scan_result.adv_data_len + scan_result.scan_rsp_len,
        ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE,
        &adv_name_length);

    if (!adv_manufactured || adv_name_length < 2)
    {
        sprintf(out_name, "%s", "Unkwnown");
        ESP_LOGI(TAG, "No manufacturer name found");
        return;
    }

    //(low_byte << 8) | high_byte;
    uint16_t company_id = adv_manufactured[0] | (adv_manufactured[1] << 8);

    const char *name = "Unknown";
    for (int i = 0; i < 5; i++)
    {
        if (zv_bt_signatures[i].company_id == company_id)
        {
            name = zv_bt_signatures[i].name;
            break;
        }
    }

    snprintf(out_name, out_length, "%s", name);
}

void zv_bt_get_device_name(struct ble_scan_result_evt_param scan_result, char *out_name, int out_length)
{
    uint8_t name_length = 0;
    uint8_t *adv_name = esp_ble_resolve_adv_data_by_type(
        scan_result.ble_adv,
        scan_result.adv_data_len + scan_result.scan_rsp_len,
        ESP_BLE_AD_TYPE_NAME_CMPL,
        &name_length
    );

    if (adv_name == NULL) {
        adv_name = esp_ble_resolve_adv_data_by_type(
            scan_result.ble_adv,
            scan_result.adv_data_len + scan_result.scan_rsp_len,
            ESP_BLE_AD_TYPE_NAME_SHORT,
            &name_length
        );
    }

    char *name = "Unknown";
    if (!adv_name || name_length == 0) {
        snprintf(out_name, out_length, "%s", name);
        return;
    }

    snprintf(out_name, out_length, "%s", adv_name);
}
