#ifndef ZV_FORMAT_UTILS_H
#define ZV_FORMAT_UTILS_H

#include <stdint.h>

void zv_format_mac_address(const uint8_t *mac_addr, char *output_str);

// Formats a UUID of 2, 4, or 16 bytes into a human-readable string.
// out must be at least 37 bytes for 128-bit UUIDs (32 hex + 4 dashes + \0).
void zv_format_uuid(const uint8_t *uuid_bytes, uint8_t len, char *out, int out_length);

#endif /* ZV_FORMAT_UTILS_H */