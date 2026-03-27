#include "zv_format_utils.h"

void zv_format_mac_address(const uint8_t* mac_addr, char* output_str) 
{
    sprintf(output_str, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac_addr[0], mac_addr[1], mac_addr[2],
            mac_addr[3], mac_addr[4], mac_addr[5]);
}