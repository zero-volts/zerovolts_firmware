#include "zv_format_utils.h"
#include <stdio.h>

void zv_format_mac_address(const uint8_t *mac_addr, char *output_str)
{
    sprintf(output_str, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac_addr[0], mac_addr[1], mac_addr[2],
            mac_addr[3], mac_addr[4], mac_addr[5]);
}

void zv_format_uuid(const uint8_t *uuid_bytes, uint8_t len, char *out, int out_length)
{
    if (uuid_bytes == NULL || out == NULL || out_length < 5)
    {
        if (out && out_length > 0)
            out[0] = '\0';
        return;
    }

    switch (len)
    {
        case 2: // UUID-16: servicios estándar BLE (ej: 0x180F = Battery Service)
            snprintf(out, out_length, "0x%02X%02X", uuid_bytes[1], uuid_bytes[0]);
            break;
        case 4: // UUID-32: extensión de UUID-16, poco común en la práctica
            snprintf(out, out_length, "0x%02X%02X%02X%02X",
                     uuid_bytes[3], uuid_bytes[2], uuid_bytes[1], uuid_bytes[0]);
            break;
        case 16: // UUID-128: servicios custom de fabricantes (ej: Xiaomi, Fitbit)
            snprintf(out, out_length,
                     "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                     uuid_bytes[15], uuid_bytes[14], uuid_bytes[13], uuid_bytes[12],
                     uuid_bytes[11], uuid_bytes[10],
                     uuid_bytes[9],  uuid_bytes[8],
                     uuid_bytes[7],  uuid_bytes[6],
                     uuid_bytes[5],  uuid_bytes[4],  uuid_bytes[3],
                     uuid_bytes[2],  uuid_bytes[1],  uuid_bytes[0]);
            break;
        default:
            snprintf(out, out_length, "UUID?(%d)", len);
            break;
    }
}