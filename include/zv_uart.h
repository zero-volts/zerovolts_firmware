#ifndef ZV_UART_H
#define ZV_UART_H

#include <stdbool.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZV_UART_BUF_SIZE 256
#define ZV_UART_PORT UART_NUM_2

void zv_uart_init(void);
void zv_uart_send_line(const char *line);
void zv_uart_send_formatted_line(const char *message, ...);
bool zv_uart_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* ZV_UART_H */
