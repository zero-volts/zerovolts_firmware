#ifndef ZV_UART_PROTOCOL_H
#define ZV_UART_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

typedef void (*command_handler_t)(char *options);

void zv_uart_protocol_init(void);
void zv_uart_parse_commands(void);

#ifdef __cplusplus
}
#endif

#endif /* ZV_UART_PROTOCOL_H */
