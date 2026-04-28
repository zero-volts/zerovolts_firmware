#include "zv_uart.h"

#include "zv_bluetooth_device.h"
#include "zv_bt_gap.h"
#include "zv_bt_gattc.h"
#include "zv_uart_commands.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h> 

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"

#define ZV_UART_PORT UART_NUM_2
#define ZV_UART_TX   GPIO_NUM_17
#define ZV_UART_RX   GPIO_NUM_16
#define ZV_UART_BUF_SIZE 256

static const char *TAG = "ZV_UART";
static bool uart_ready = false;

static void handle_test(char *options);
static void handle_bt_scan(char *options);
static void handle_bt_connection(char *options);
static void handle_bt_disconnect(char *options);

typedef void (*command_handler_t)(char *options);
typedef struct {
    const char *cmd;
    command_handler_t handler;
} command_entry_t;

static const command_entry_t commands[] = {
    { UART_COMMAND_REQ_TEST, handle_test },
    { BT_COMMAND_REQ_SCANN, handle_bt_scan },
    { BT_COMMAND_REQ_CONNECT, handle_bt_connection },
    { BT_COMMAND_REQ_DISCONNECT, handle_bt_disconnect },
};

static void handle_test(char *options)
{
    zv_uart_send_line(UART_COMMAND_RES_TEST);
}

static void handle_bt_scan(char *options)
{
    zv_bt_clear_devices();
    zv_bt_start_scanning(15);
    zv_uart_send_line(BT_COMMAND_RES_SCANN_START);
}

static void handle_bt_connection(char *options)
{
     if (options == NULL) {
        zv_uart_send_line(BT_COMMAND_RES_CONNECT_ERROR);
        return;
    }

    char *mac_str = strtok(options, "|");
    char *addr_type_str = strtok(NULL, "|");
     
    if (!mac_str || !addr_type_str) {
        zv_uart_send_line(BT_COMMAND_RES_CONNECT_ERROR);
        return;
    }

    int addr_type = atoi(addr_type_str);

    uint8_t mac[6];
    if (sscanf(mac_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &mac[0], &mac[1], &mac[2],
               &mac[3], &mac[4], &mac[5]) != 6) {

        zv_uart_send_line(BT_COMMAND_RES_CONNECT_ERROR);
        return;
    }

    zv_uart_send_line(BT_COMMAND_RES_CONNECT_START);
    zv_bt_gatt_open_connection(mac, addr_type);
}

static void handle_bt_disconnect(char *options)
{
    (void)options;
    zv_bt_gatt_close_connection();
}

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

void zv_uart_parse_commands(void)
{
    static char line_buf[ZV_UART_BUF_SIZE];
    static size_t line_len = 0;

    uint8_t rx_byte = 0;
    int len = uart_read_bytes(ZV_UART_PORT, &rx_byte, 1, pdMS_TO_TICKS(1000));
    if (len <= 0) {
        return;
    }

    if (rx_byte == '\r') {
        return;
    }

    if (rx_byte != '\n') 
    {
        if (line_len < sizeof(line_buf) - 1)
        {
            line_buf[line_len++] = (char)rx_byte;
        }
        else
        {
            line_len = 0;
            ESP_LOGW(TAG, "UART command too long, dropping line");
            zv_uart_send_line("ERR");
        }

        return;
    }

    line_buf[line_len] = '\0';
    if (line_len == 0) {
        return;
    }

    ESP_LOGI(TAG, "RX CMD: %s", line_buf);

    char *cmd = line_buf;
    char *args = strchr(line_buf, '|');

    if (args != NULL) {
        *args = '\0';
        args++;
    }

    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
    {
        if (strcmp(cmd, commands[i].cmd) == 0) 
        {
            commands[i].handler(args);
            line_len = 0;
            return;
        }
    }

    zv_uart_send_line("ERR");
    line_len = 0;
}
