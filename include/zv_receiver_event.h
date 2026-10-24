#ifndef ZV_RECEIVER_EVENT_H
#define ZV_RECEIVER_EVENT_H

#include <stdint.h>
#include "zv_cc1101.h"

typedef enum {
    ZV_EVENT_BT_CONNECTED,
    ZV_EVENT_BT_CONNECTION_FAILED,
    ZV_EVENT_SUBGHZ_CAPTURE_DONE,
    ZV_EVENT_BT_DISCOVER_STARTED,
    ZV_EVENT_BT_DISCOVER_FAILED,
    ZV_EVENT_BT_DISCOVER_SERVICE,
    ZV_EVENT_BT_DISCOVER_CHARACTERISTIC,
    ZV_EVENT_BT_DISCOVER_DONE,
    ZV_EVENT_BT_DISCONNECTED,
    ZV_EVENT_BT_CONNECTION_LOST,
    ZV_EVENT_SUBGHZ_CAPTURE_STARTED,
    ZV_EVENT_SUBGHZ_CAPTURE_FAILED,
    ZV_EVENT_SUBGHZ_CAPTURE_ERROR,
} zv_event_type_t;

typedef struct {
    zv_event_type_t type;

    union {
        struct {
            // Borrowed data: consume or copy before the callback returns.
            const zv_cc1101_capture_t *capture;
            uint64_t capture_id;
            const char *reason;
        } subghz_capture;

        // CONNECTED, CONNECTION_FAILED, DISCONNECTED, CONNECTION_LOST.
        struct {
            char mac[18];
            uint16_t mtu;
            int reason;
        } connection;

        // DISCOVER_SERVICE, DISCOVER_CHARACTERISTIC.
        struct {
            int service_index;
            int characteristic_index;
            char uuid[37];
            uint8_t properties;
            uint16_t handle;
        } discovery_item;

        // DISCOVER_FAILED, DISCOVER_DONE.
        struct {
            int reason;
            int services;
            int characteristics;
        } discovery_result;
    } data;
} zv_event_t;

typedef void (*zv_event_handler_t)(const zv_event_t *event, void *context);

#endif /* ZV_RECEIVER_EVENT_H */
