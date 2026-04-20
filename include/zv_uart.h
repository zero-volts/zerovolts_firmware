#ifndef ZV_UART_H
#define ZV_UART_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void zv_uart_init(void);
void zv_uart_send_line(const char *line);
bool zv_uart_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* ZV_UART_H */
