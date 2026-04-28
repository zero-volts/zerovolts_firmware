#include "zv_bt_gap.h"
#include "zv_bt_gattc.h"
#include "zv_bluetooth_device.h"
#include "zv_format_utils.h"
#include "zv_uart.h"
#include "zv_uart_commands.h"

#include "esp_log.h"
#include "esp_gattc_api.h"
#include <string.h>
#include <stdio.h>

#define SCAN_DURATION_IN_SEC    15

static const char *TAG = "ZV_BT_GAP";

const zv_bt_signature_t zv_bt_signatures[] = {
    // Smartphones
    {0x004C, "Apple"},
    {0x0075, "Samsung"},
    {0x0001, "Nokia"},
    {0x0008, "Motorola"},
    {0x04EC, "Motorola Solutions"},
    {0x027D, "Huawei"},
    {0x038F, "Xiaomi"},
    {0x072F, "OnePlus"},
    {0x079A, "Oppo"},
    {0x0837, "Vivo"},
    {0x08A4, "Realme"},
    {0x08CA, "Nubia"},
    {0x09C6, "Honor"},
    {0x02ED, "HTC"},
    {0x0056, "Sony Ericsson"},
    {0x03AB, "Meizu"},
    {0x0958, "ZTE"},

    // TVs
    {0x00C4, "LG Electronics"},
    {0x012D, "Sony"},
    {0x0058, "Vizio"},
    {0x033D, "TPV Technology"},
    {0x003A, "Panasonic"},
    {0x0381, "Sharp"},
    {0x07A2, "Roku"},
    {0x0471, "TiVo"},

    // Wearables
    {0x00E0, "Google"},
    {0x018E, "Google LLC"},
    {0x0087, "Garmin"},
    {0x03FF, "Withings"},
    {0x006B, "Polar"},
    {0x009F, "Suunto"},
    {0x00D6, "Timex"},

    // Audio
    {0x009E, "Bose"},
    {0x0057, "Harman"},
    {0x075A, "Harman (JBL)"},
    {0x0494, "Sennheiser"},
    {0x0103, "Bang & Olufsen"},
    {0x00CC, "Beats"},
    {0x0618, "Audio-Technica"},
    {0x04AD, "Shure"},
    {0x07FA, "Klipsch"},
    {0x05A7, "Sonos"},
    {0x062F, "Onkyo"},
    {0x0150, "Pioneer"},
    {0x07E0, "Edifier"},

    // Chips
    {0x0002, "Intel"},
    {0x000A, "Qualcomm (QTIL)"},
    {0x001D, "Qualcomm"},
    {0x00D7, "Qualcomm Technologies"},
    {0x000F, "Broadcom"},
    {0x0046, "MediaTek"},
    {0x000D, "Texas Instruments"},
    {0x0059, "Nordic Semiconductor"},
    {0x005D, "Realtek"},
    {0x010F, "HiSilicon"},
    {0x02FF, "Silicon Laboratories"},

    // Otros
    {0x0006, "Microsoft"},
    {0x0171, "Amazon"},
    {0x01AB, "Meta (Facebook)"},
    {0x01DA, "Logitech"},
    {0x041E, "Dell"},
    {0x0065, "HP"},
    {0x01DD, "Philips"},
    {0x022E, "Siemens"},
    {0x022B, "Tesla"},
    {0x0553, "Nintendo"},
    {0x08AA, "DJI"},
    {0x0A2A, "Yamaha"},
    {0x0003, "IBM"},
    {0x0004, "Toshiba"},
    {0x0014, "Mitsubishi Electric"},
    {0x0022, "NEC"},
    {0x0029, "Hitachi"},
    {0x0040, "Seiko Epson"},
    {0x003C, "BlackBerry"},
    {0x0030, "ST Microelectronics"},
    {0x0025, "NXP"},
    {0x01A9, "Canon"},
    {0x0399, "Nikon"},
    {0x04D8, "Fujifilm"},
    {0x02EA, "Fujitsu"},
    {0x068E, "Razer"},
    {0x0078, "Nike"},
    {0x083F, "D-Link"},
    {0x0446, "Netgear"},
    {0x021B, "Cisco"},
    {0x0826, "Hyundai Motor"},
    {0x0977, "Toyota"},
    {0x0915, "Honda Motor"},
    {0x0600, "iRobot"},
    {0x0A12, "Dyson"},
};

static const char *appearance_to_string(uint16_t appearance)
{
    switch (appearance >> 6)
    {
        case 0x01: return "Phone";
        case 0x02: return "Computer";
        case 0x03: return "Watch";
        case 0x04: return "Clock";
        case 0x05: return "Display";
        case 0x06: return "Remote Control";
        case 0x0F: return "HID (Keyboard/Mouse)";
        case 0x11: return "Pulse Oximeter";
        case 0x13: return "Running Sensor";
        case 0x15: return "Thermometer";
        case 0x31: return "Blood Pressure";
        default:   return NULL;
    }
}

static const char *uuid16_to_service(uint16_t uuid)
{
    switch (uuid)
    {
        case 0x180A: return "Device Information";
        case 0x180F: return "Battery Service";
        case 0x180D: return "Heart Rate";
        case 0x1812: return "HID (Teclado/Mouse)";
        case 0x1808: return "Glucose";
        case 0x1809: return "Health Thermometer";
        case 0xFE9F: return "Google Fast Pair";
        case 0xFD6F: return "COVID Exposure Notif.";
        default:     return NULL;
    }
}

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
        snprintf(out_name, out_length, "%s", "Unknown");
        return;
    }

    uint16_t company_id = adv_manufactured[0] | (adv_manufactured[1] << 8);

    const char *name = "Unknown";
    for (int i = 0; i < sizeof(zv_bt_signatures) / sizeof(zv_bt_signatures[0]); i++)
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

    if (!adv_name || name_length == 0) {
        snprintf(out_name, out_length, "%s", "Unknown");
        return;
    }

    int copy_len = (name_length < out_length - 1) ? name_length : out_length - 1;
    memcpy(out_name, adv_name, copy_len);
    out_name[copy_len] = '\0';
}

void zv_get_device_appearance(struct ble_scan_result_evt_param scan_result, char *out_name, int out_length)
{
    uint8_t appearance_len = 0;
    uint8_t *appearance = esp_ble_resolve_adv_data_by_type(
        scan_result.ble_adv,
        scan_result.adv_data_len,
        ESP_BLE_AD_TYPE_APPEARANCE,
        &appearance_len
    );

    snprintf(out_name, out_length, "%s", "Unknown");

    if (appearance && appearance_len >= 2)
    {
        uint16_t app = appearance[0] | (appearance[1] << 8);
        const char *appe = appearance_to_string(app);
        if (appe)
            snprintf(out_name, out_length, "%s", appe);
    }
}

void zv_get_device_service(struct ble_scan_result_evt_param scan_result, char *out_name, int out_length)
{
    uint8_t uuid_len = 0;
    uint8_t *uuid16 = esp_ble_resolve_adv_data_by_type(
        scan_result.ble_adv,
        scan_result.adv_data_len + scan_result.scan_rsp_len,
        ESP_BLE_AD_TYPE_16SRV_CMPL,
        &uuid_len
    );

    if (!uuid16)
    {
        uuid16 = esp_ble_resolve_adv_data_by_type(
            scan_result.ble_adv,
            scan_result.adv_data_len + scan_result.scan_rsp_len,
            ESP_BLE_AD_TYPE_16SRV_PART,
            &uuid_len
        );
    }

    snprintf(out_name, out_length, "%s", "Unknown");
    if (uuid16 && uuid_len >= 2)
    {
        for (int i = 0; i + 1 < uuid_len; i += 2)
        {
            uint16_t uuid = uuid16[i] | (uuid16[i + 1] << 8);
            const char *service = uuid16_to_service(uuid);

            if (service)
                snprintf(out_name, out_length, "%s", service);
        }
    }
}

void zv_bt_gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch(event)
    {
        case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
            ESP_LOGI(TAG, "scan params ready");
            break;
        case ESP_GAP_BLE_SCAN_RESULT_EVT:
        {
            struct ble_scan_result_evt_param scan_result = param->scan_rst;
            switch(scan_result.search_evt)
            {
                case ESP_GAP_SEARCH_INQ_RES_EVT:
                {
                    zv_bt_device_t new_device;

                    zv_bt_get_device_name(scan_result, new_device.name, sizeof(new_device.name));
                    zv_format_mac_address(scan_result.bda, new_device.mac);
                    zv_bt_get_manufacturer_name(scan_result, new_device.manufacturer, sizeof(new_device.manufacturer));
                    zv_get_device_service(scan_result, new_device.service, sizeof(new_device.service));
                    zv_get_device_appearance(scan_result, new_device.appearance, sizeof(new_device.appearance));

                    new_device.connectable = scan_result.ble_evt_type == ESP_BLE_EVT_CONN_ADV || scan_result.ble_evt_type == ESP_BLE_EVT_CONN_DIR_ADV;
                    new_device.rssi = scan_result.rssi;
                    new_device.addr_type = scan_result.ble_addr_type;
                    memcpy(new_device.mac_address, scan_result.bda, sizeof(new_device.mac_address));

                    zv_bt_add_device(new_device);
                    
                    break;
                }
                case ESP_GAP_SEARCH_INQ_CMPL_EVT:
                {
                    ESP_LOGI(TAG, "scan complete");
                    zv_uart_send_line(BT_COMMAND_RES_SCANN_DONE);
                    break;
                }
                default:
                    break;
            }

            break;
        }
        default:
            break;
    }
}

void zv_bt_start_scanning(int duration)
{
    esp_ble_gap_start_scanning(duration > 0 ? duration : SCAN_DURATION_IN_SEC);
}

void zv_bt_send_scan_result_uart(const zv_bt_device_t *device)
{
    if (!device)
        return;

    zv_uart_send_formatted_line( "SCAN:DEVICE|name=%.48s|mac=%.17s|rssi=%d|manufacturer=%.48s|service=%.32s|appearance=%.32s|connectable=%d|addr_type=%d",
        device->name,
        device->mac,
        device->rssi,
        device->manufacturer,
        device->service,
        device->appearance,
        device->connectable ? 1 : 0,
        (int)device->addr_type);
}
