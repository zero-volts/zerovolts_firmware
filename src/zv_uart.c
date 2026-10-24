#include "zv_uart.h"

#include <stdlib.h>
#include <stdio.h> 

#include "zv_uart_commands.h"

#include "esp_log.h"
#include "driver/uart.h"

#define ZV_UART_TX   GPIO_NUM_17
#define ZV_UART_RX   GPIO_NUM_16

static const char *TAG = "ZV_UART";
static bool uart_ready = false;

void zv_uart_init(void)
{
    const uart_config_t cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(ZV_UART_PORT, ZV_UART_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(ZV_UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(ZV_UART_PORT, ZV_UART_TX, ZV_UART_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    uart_ready = true;
    ESP_LOGI(TAG, "UART ready on port %d", ZV_UART_PORT);
}

void zv_uart_send_line(const char *line)
{
    if (!uart_ready || !line)
        return;

    uart_write_bytes(ZV_UART_PORT, line, strlen(line));
    uart_write_bytes(ZV_UART_PORT, "\r\n", 2);
}

void zv_uart_send_formatted_line(const char *message, ...)
{
    if (!message)
        return;

    va_list args;
    va_start(args, message);

    char msg[1024];
    vsnprintf(msg, sizeof(msg), message, args);

    va_end(args);

    zv_uart_send_line(msg);
}

bool zv_uart_is_ready(void)
{
    return uart_ready;
}
