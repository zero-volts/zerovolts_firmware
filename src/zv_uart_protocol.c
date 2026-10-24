#include "zv_uart_protocol.h"

#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include <errno.h>

#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "parser.h"
#include "zv_uart.h"
#include "zv_bt_gap.h"
#include "zv_subghz.h"
#include "zv_bt_gattc.h"
#include "zv_uart_commands.h"
#include "zv_bluetooth_device.h"

#define SUBGHZ_SEND_CHUNK_VALUES            16
#define SUBGHZ_SEND_MAX_TIMINGS             256
#define SUBGHZ_SEND_MAX_PULSE_US            32767
#define SUBGHZ_MAX_REPETITIONS              50

#define CAPTURE_FIELD_BUFFER_SIZE 20
#define DATA_CHUNK_MAX_VALUES 32
#define DATA_CHUNK_LINE_SIZE 512

static const char *TAG = "ZV_UART_PROTOCOL";

typedef struct {
    const char *cmd;
    command_handler_t handler;
} command_entry_t;

static void handle_test(char *options);
static void handle_bt_scan(char *options);
static void handle_bt_connection(char *options);
static void handle_bt_disconnect(char *options);
static void handle_subghz_capture(char *options);
static void handle_subghz_send_begin(char *options);
static void handle_subghz_transmission_chunk(char *options);
static void handle_send_end(char *options);

static const command_entry_t commands[] = {
    { UART_COMMAND_REQ_TEST, handle_test },
    { BT_COMMAND_REQ_SCAN, handle_bt_scan },
    { BT_COMMAND_REQ_CONNECT, handle_bt_connection },
    { BT_COMMAND_REQ_DISCONNECT, handle_bt_disconnect },
    { SUBGHZ_COMMAND_REQ_CAPTURE, handle_subghz_capture },
    { SUBGHZ_COMMAND_REQ_SEND_BEGIN, handle_subghz_send_begin },
    { SUBGHZ_COMMAND_REQ_SEND_DATA, handle_subghz_transmission_chunk },
    { SUBGHZ_COMMAND_REQ_SEND_END, handle_send_end },
};

static struct {
    bool active;
    uint32_t id;
    uint16_t repeats;
    uint16_t expected_count;
    uint16_t chunks;
    uint16_t next_seq;
    TickType_t updated_at;
    zv_cc1101_capture_t capture;
} transmit_context;

static void handle_test(char *options)
{
    zv_uart_send_line(UART_COMMAND_RES_TEST);
}

static void handle_bt_scan(char *options)
{
    zv_bt_clear_devices();
    zv_bt_start_scanning(15);
    zv_uart_send_line(BT_COMMAND_RES_SCAN_START);
}

static void handle_bt_connection(char *options)
{
    if (options == NULL)
    {
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

static void handle_subghz_capture(char *options)
{
    if (options == NULL)
    {
        zv_uart_send_line(SUBGHZ_COMMAND_RES_CAPTURE_ERROR);
        return;
    }

    ESP_LOGI(TAG, "handle_subghz_capture %s", options);

    char *freq_str = strtok(options, "|");
    char freq_buffer[CAPTURE_FIELD_BUFFER_SIZE] = {0};
    if (!get_string_field_value(freq_str, "freq_hz", freq_buffer, CAPTURE_FIELD_BUFFER_SIZE))
    {
        ESP_LOGE(TAG, "Can't find the frequency selected!");
        return;
    }

    char timeout_buffer[CAPTURE_FIELD_BUFFER_SIZE] = {0};
    char *timeout_str = strtok(NULL, "|");
    if (!get_string_field_value(timeout_str, "timeout_ms", timeout_buffer, CAPTURE_FIELD_BUFFER_SIZE))
    {
        ESP_LOGE(TAG, "Can't find the time frame to wait!");
        return;
    }

    capture_task_data_t *data = pvPortMalloc(sizeof(*data));
    if (data == NULL)
    {
        zv_uart_send_formatted_line("%s|reason=no_memory", SUBGHZ_COMMAND_RES_CAPTURE_ERROR);
        return;
    }

    data->signal = strtoul(freq_buffer, NULL, 10);
    data->time_frame = strtoul(timeout_buffer, NULL, 10);

    BaseType_t task_created = xTaskCreate(subghz_listener_task, "subghz_listener", 4096, data, 5, NULL );
    if (task_created != pdPASS)
    {
        vPortFree(data);
        ESP_LOGE(TAG, "CC1101 is ready, but the Sub-GHz listener task could not be created");
        zv_uart_send_formatted_line("%s|reason=task_creation", SUBGHZ_COMMAND_RES_CAPTURE_ERROR);
    }
}


/**
 * Convert the pulses sending line by line as a chunks
 * example:
 *  SUBGHZ_CAPTURE:DATA|seq=0|chunks=8|count=32|timings=-10375,+254,...
 */
static bool send_capture_chunks(const zv_cc1101_capture_t *cap, uint64_t capture_id)
{
    if (cap == NULL || cap->count == 0)
        return false;

    const uint16_t chunks_amount = (cap->count + DATA_CHUNK_MAX_VALUES - 1) / DATA_CHUNK_MAX_VALUES;
    for (uint16_t seq = 0; seq < chunks_amount; seq++)
    {
        const uint16_t start = seq * DATA_CHUNK_MAX_VALUES;
        uint16_t end = start + DATA_CHUNK_MAX_VALUES;
        if (end > cap->count)
            end = cap->count;

        const uint16_t values_in_chunk = end - start;
        char line[DATA_CHUNK_LINE_SIZE];

        int written = snprintf(line, sizeof(line),
                               "%s|capture_id=%" PRIu64 "|seq=%u|chunks=%u|count=%u|timings=",
                               SUBGHZ_COMMAND_RES_CAPTURE_DATA,
                               capture_id, seq, chunks_amount, values_in_chunk);

        if (written < 0 || (size_t)written >= sizeof(line))
            return false;

        size_t offset = (size_t)written;
        for (uint16_t index = start; index < end; index++)
        {
            const char *separator = (index == start) ? "" : ",";

            written = snprintf(line + offset, sizeof(line) - offset,
                               "%s%+" PRId32,
                               separator, cap->durations_us[index]);

            if (written < 0 || (size_t)written >= sizeof(line) - offset)
                return false;

            offset += (size_t)written;
        }

        zv_uart_send_line(line);
    }

    return true;
}

static void send_transmit_failure(uint32_t id, const char *reason)
{
    transmit_context.active = false;
    zv_uart_send_formatted_line("%s|id=%" PRIu32 "|reason=%s",
                               SUBGHZ_COMMAND_RES_SEND_FAIL, id, reason);
}

static void handle_subghz_send_begin(char *options)
{
    uint32_t id = 0, freq, repeats, count, chunks;
    bool valid = get_uint32_field_value(options, "id", &id) && id != 0;
    valid = valid && get_uint32_field_value(options, "freq", &freq);
    valid = valid && get_uint32_field_value(options, "repeats", &repeats) && repeats > 0 && repeats <= SUBGHZ_MAX_REPETITIONS;
    valid = valid && get_uint32_field_value(options, "count", &count) && count != 0 && count <= CC1101_CAPTURE_MAX_DURATIONS;
    valid = valid && get_uint32_field_value(options, "chunks", &chunks) &&
            chunks == (count + SUBGHZ_SEND_CHUNK_VALUES - 1) / SUBGHZ_SEND_CHUNK_VALUES;

    if (!valid)
    {
        send_transmit_failure(id, "invalid_metadata");
        return;
    }

    /* A new BEGIN discards any incomplete signal data from the previous transfer. */
    memset(&transmit_context, 0, sizeof(transmit_context));
    transmit_context.active = true;
    transmit_context.id = id;
    transmit_context.repeats = repeats;
    transmit_context.expected_count = count;
    transmit_context.chunks = chunks;
    transmit_context.capture.freq_hz = freq;
    transmit_context.updated_at = xTaskGetTickCount();
    zv_uart_send_formatted_line("%s|id=%" PRIu32, SUBGHZ_COMMAND_RES_SEND_BEGIN_OK, id);
}

static void handle_subghz_transmission_chunk(char *options)
{
    uint32_t id = 0, seq, count;
    char timings[SUBGHZ_SEND_CHUNK_VALUES * 12]; /* Signed int32_t values and separators. */
    bool valid = get_uint32_field_value(options, "id", &id) && transmit_context.active && id == transmit_context.id;
    valid = valid && get_uint32_field_value(options, "seq", &seq) &&
            seq == transmit_context.next_seq && seq < transmit_context.chunks;
    valid = valid && get_uint32_field_value(options, "count", &count) && count != 0 && count <= SUBGHZ_SEND_CHUNK_VALUES &&
            count == (transmit_context.expected_count - transmit_context.capture.count < SUBGHZ_SEND_CHUNK_VALUES ?
                      transmit_context.expected_count - transmit_context.capture.count : SUBGHZ_SEND_CHUNK_VALUES);
    valid = valid && get_string_field_value(options, "timings", timings, sizeof(timings));

    if (!valid)
    {
        send_transmit_failure(id, "invalid_chunk");
        return;
    }

    const char *cursor = timings;
    for (uint32_t i = 0; i < count; i++)
    {
        char *end;
        errno = 0;
        long value = strtol(cursor, &end, 10);
        if (errno == ERANGE)
        {
            send_transmit_failure(id, "invalid conversion");
            return;
        }

        if (end == cursor || value == 0 || value < -SUBGHZ_SEND_MAX_PULSE_US || value > SUBGHZ_SEND_MAX_PULSE_US ||
            (i + 1 < count ? *end != ',' : *end != '\0'))
        {
            send_transmit_failure(id, "invalid_timings");
            return;
        }

        transmit_context.capture.durations_us[transmit_context.capture.count++] = (int32_t)value;
        cursor = end + (i + 1 < count);
    }

    transmit_context.next_seq++;
    transmit_context.updated_at = xTaskGetTickCount();
    zv_uart_send_formatted_line("%s|id=%" PRIu32 "|seq=%" PRIu32,
                               SUBGHZ_COMMAND_RES_SEND_ACK, id, seq);
}

static void handle_send_end(char *options)
{
    uint32_t id = 0;
    if (!get_uint32_field_value(options, "id", &id) || !transmit_context.active || id != transmit_context.id ||
        transmit_context.capture.count != transmit_context.expected_count || transmit_context.next_seq != transmit_context.chunks)
    {
        send_transmit_failure(id, "incomplete transmission");
        return;
    }

    /* Clear the transfer before TX so a duplicate END cannot transmit twice. */
    transmit_context.active = false;
    esp_err_t result = zv_subghz_transmit(&transmit_context.capture, transmit_context.repeats);
    if (result != ESP_OK)
        send_transmit_failure(id, esp_err_to_name(result));
    else
        zv_uart_send_formatted_line("%s|id=%" PRIu32, SUBGHZ_COMMAND_RES_SEND_DONE, id);
}

static void event_handler(const zv_event_t *event, void *context)
{
    (void)context;

    switch(event->type)
    {
        case ZV_EVENT_BT_CONNECTED:
            zv_uart_send_formatted_line("%s|mac=%s|mtu=%d",
                         BT_COMMAND_RES_CONNECT_OK, event->data.connection.mac, event->data.connection.mtu);
            break;
        case ZV_EVENT_BT_CONNECTION_FAILED:
            zv_uart_send_formatted_line("%s|mac=%s|reason=%d", 
                BT_COMMAND_RES_CONNECT_FAIL, event->data.connection.mac,
                event->data.connection.reason);

            break;
        case ZV_EVENT_BT_DISCOVER_STARTED:
            zv_uart_send_line(BT_COMMAND_RES_DISCOVER_START);
            break;
        case ZV_EVENT_BT_DISCOVER_FAILED:
            zv_uart_send_formatted_line("%s|reason=%d",
                BT_COMMAND_RES_DISCOVER_FAIL, event->data.discovery_result.reason);
            break;
        case ZV_EVENT_BT_DISCOVER_SERVICE:
            zv_uart_send_formatted_line("%s|svc=%d|uuid=%s",
                BT_COMMAND_RES_DISCOVER_SERVICE,
                event->data.discovery_item.service_index,
                event->data.discovery_item.uuid);
            break;
        case ZV_EVENT_BT_DISCOVER_CHARACTERISTIC:
            zv_uart_send_formatted_line("%s|svc=%d|char=%d|uuid=%s|props=0x%02x|handle=%d",
                BT_COMMAND_RES_DISCOVER_CHAR,
                event->data.discovery_item.service_index,
                event->data.discovery_item.characteristic_index,
                event->data.discovery_item.uuid,
                event->data.discovery_item.properties,
                event->data.discovery_item.handle);
            break;
        case ZV_EVENT_BT_DISCOVER_DONE:
            zv_uart_send_formatted_line("%s|services=%d|chars=%d",
                BT_COMMAND_RES_DISCOVER_DONE,
                event->data.discovery_result.services,
                event->data.discovery_result.characteristics);
            break;
        case ZV_EVENT_BT_DISCONNECTED:
            zv_uart_send_formatted_line("%s|mac=%s",
                BT_COMMAND_RES_DISCONNECT_OK, event->data.connection.mac);
            break;
        case ZV_EVENT_BT_CONNECTION_LOST:
            zv_uart_send_formatted_line("%s|mac=%s|reason=%d",
                BT_COMMAND_RES_CONNECT_LOST, event->data.connection.mac,
                event->data.connection.reason);
            break;
        case ZV_EVENT_SUBGHZ_CAPTURE_STARTED:
            zv_uart_send_line(SUBGHZ_COMMAND_RES_CAPTURE_START);
            break;
        case ZV_EVENT_SUBGHZ_CAPTURE_FAILED:
            zv_uart_send_formatted_line("%s|reason=%s",
                SUBGHZ_COMMAND_RES_CAPTURE_FAIL, event->data.subghz_capture.reason);
            break;
        case ZV_EVENT_SUBGHZ_CAPTURE_ERROR:
            zv_uart_send_formatted_line("%s|reason=%s",
                SUBGHZ_COMMAND_RES_CAPTURE_ERROR, event->data.subghz_capture.reason);
            break;
        case ZV_EVENT_SUBGHZ_CAPTURE_DONE:
        {
            const zv_cc1101_capture_t *cap = event->data.subghz_capture.capture;
            uint64_t capture_id = event->data.subghz_capture.capture_id;
            if (!send_capture_chunks(cap, capture_id))
            {
                ESP_LOGE(TAG, "Sub-GHz capture could not be serialized into UART chunks");
                zv_uart_send_formatted_line("%s|reason=serialization", SUBGHZ_COMMAND_RES_CAPTURE_FAIL);
                break;
            }

            const uint16_t chunks_amount = (cap->count + DATA_CHUNK_MAX_VALUES - 1) / DATA_CHUNK_MAX_VALUES;
            zv_uart_send_formatted_line("%s|capture_id=%" PRIu64 "|freq=%lu|count=%u|chunks=%u|buffer_full=%u",
                SUBGHZ_COMMAND_RES_CAPTURE_DONE, capture_id, (unsigned long)cap->freq_hz,
                cap->count, chunks_amount, cap->count == CC1101_CAPTURE_MAX_DURATIONS);
            break;
        }
        default:
            break;
    }
}

void zv_uart_protocol_init(void)
{
    zv_bt_gattc_set_event_receiver(event_handler);
    zv_subghz_set_event_receiver(event_handler);
}

static void process_rx_byte(uint8_t rx_byte)
{
    static char line_buf[ZV_UART_BUF_SIZE];
    static size_t line_len = 0;
    static bool discarding = false;

    if (discarding)
    {
        if (rx_byte == '\n') discarding = false;
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
            discarding = true;
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
            // ESP_LOGI(TAG, "%s", cmd);
            commands[i].handler(args);
            line_len = 0;
            return;
        }
    }

    zv_uart_send_line("ERR");
    line_len = 0;
}

void zv_uart_parse_commands(void)
{
    if (transmit_context.active && xTaskGetTickCount() - transmit_context.updated_at > pdMS_TO_TICKS(5000))
        send_transmit_failure(transmit_context.id, "upload_timeout");

    /* Drain a block each iteration; reading one byte per 10 ms cannot keep up at 115200 baud. */
    uint8_t bytes[ZV_UART_BUF_SIZE];
    int count = uart_read_bytes(ZV_UART_PORT, bytes, sizeof(bytes), pdMS_TO_TICKS(10));
    for (int i = 0; i < count; i++)
        process_rx_byte(bytes[i]);
}
