#ifndef ZV_COM_PROTOCOL_H
#define ZV_COM_PROTOCOL_H

/* Public wire tokens: SUBSYSTEM_OPERATION[:EVENT], fields in snake_case.
 * General commands (PING/PONG, INFO, CAPS and HELP) have no subsystem.
 * A token being defined does not imply that a command is executable.
 */
#define ZV_COM_REQ_PING                     "PING"
#define ZV_COM_RES_PONG                     "PONG"
#define ZV_COM_RES_READY                    "READY"

#define ZV_COM_REQ_INFO                    "INFO"                       // Gets the current version of the firmware
#define ZV_COM_RES_INFO_OK                 "INFO:OK"
#define ZV_COM_RES_INFO_ERROR              "INFO:ERROR"

#define ZV_COM_REQ_CAPS                     "CAPS"                      // List commands registered in this firmware build
#define ZV_COM_RES_CAPS_COMMAND             "CAPS:COMMAND"
#define ZV_COM_RES_CAPS_DONE                "CAPS:DONE"

#define ZV_COM_REQ_HELP                     "HELP"
#define ZV_COM_RES_HELP_COMMAND             "HELP:COMMAND"
#define ZV_COM_RES_HELP_PARAM               "HELP:PARAM"
#define ZV_COM_RES_HELP_RESPONSE            "HELP:RESPONSE"
#define ZV_COM_RES_HELP_MESSAGE             "HELP:MESSAGE"
#define ZV_COM_RES_HELP_FIELD               "HELP:FIELD"
#define ZV_COM_RES_HELP_NOTE                "HELP:NOTE"
#define ZV_COM_RES_HELP_DONE                "HELP:DONE"
#define ZV_COM_RES_HELP_ERROR               "HELP:ERROR"
#define ZV_COM_RES_ERROR                    "ERR"

/* Operational wire limits. Catalog consistency is checked by tests. */
#define ZV_COM_SEND_CHUNK_VALUES            16
#define ZV_COM_SEND_MAX_TIMINGS             256
#define ZV_COM_SEND_MAX_PULSE_US            32767
#define ZV_COM_SEND_MAX_REPETITIONS         50
#define ZV_COM_CAPTURE_CHUNK_VALUES         32

//-- BLE SCAN -- //

#define ZV_COM_REQ_BLE_SCAN                 "BLE_SCAN"                   // User requests to start scanning
#define ZV_COM_RES_BLE_SCAN_START           "BLE_SCAN:START"             // ESP32 notifies that scanning has started
#define ZV_COM_RES_BLE_SCAN_DONE            "BLE_SCAN:DONE"              // ESP32 notifies that scanning has finished
#define ZV_COM_RES_BLE_SCAN_DEVICE          "BLE_SCAN:DEVICE"            // ESP32 sends a discovered device result
#define ZV_COM_RES_BLE_SCAN_UPDATE          "BLE_SCAN:UPDATE"            // ESP32 sends an update for scanned devices

//-- BLE_CONNECT -- //

#define ZV_COM_REQ_BLE_CONNECT              "BLE_CONNECT"                // Request: "BLE_CONNECT|<MAC>|<addr_type>"
#define ZV_COM_RES_BLE_CONNECT_START        "BLE_CONNECT:START"          // ESP32 notifies connection attempt started
#define ZV_COM_RES_BLE_CONNECT_OK           "BLE_CONNECT:OK"             // Response: "BLE_CONNECT:OK|mac=...|mtu=..."
#define ZV_COM_RES_BLE_CONNECT_FAIL         "BLE_CONNECT:FAIL"           // Response: "BLE_CONNECT:FAIL|mac=...|reason=..."
#define ZV_COM_RES_BLE_CONNECT_LOST         "BLE_CONNECT:LOST"           // Unexpected disconnection occurred
#define ZV_COM_RES_BLE_CONNECT_ERROR        "BLE_CONNECT:ERROR"          // Generic connection error

#define ZV_COM_REQ_BLE_DISCONNECT           "BLE_DISCONNECT"             // User requests to disconnect
#define ZV_COM_RES_BLE_DISCONNECT_OK        "BLE_DISCONNECT:OK"          // Connection successfully closed

//-- BLE_DISCOVER (services and characteristics) -- //

#define ZV_COM_REQ_BLE_DISCOVER             "BLE_DISCOVER"               // Reserved name; discovery runs automatically after BLE_CONNECT
#define ZV_COM_RES_BLE_DISCOVER_START       "BLE_DISCOVER:START"         // ESP32 notifies discovery started
#define ZV_COM_RES_BLE_DISCOVER_SERVICE     "BLE_DISCOVER:SERVICE"       // Response: "BLE_DISCOVER:SERVICE|svc=...|uuid=..."
#define ZV_COM_RES_BLE_DISCOVER_CHAR        "BLE_DISCOVER:CHAR"          // Response includes svc, char, uuid, props and handle
#define ZV_COM_RES_BLE_DISCOVER_DESC        "BLE_DISCOVER:DESC"          // Reserved response; not currently emitted
#define ZV_COM_RES_BLE_DISCOVER_DONE        "BLE_DISCOVER:DONE"          // Discovery finished
#define ZV_COM_RES_BLE_DISCOVER_FAIL        "BLE_DISCOVER:FAIL"          // Discovery error occurred

//-- SUB-GHZ -- //

#define ZV_COM_REQ_SUBGHZ_CHECK_MODULE      "SUBGHZ_CHECK_MODULE"
#define ZV_COM_RES_SUBGHZ_CHECK_MODULE_OK   "SUBGHZ_CHECK_MODULE:OK"
#define ZV_COM_RES_SUBGHZ_CHECK_MODULE_FAIL "SUBGHZ_CHECK_MODULE:FAIL"
#define ZV_COM_REQ_SUBGHZ_CAPTURE           "SUBGHZ_CAPTURE"
#define ZV_COM_RES_SUBGHZ_CAPTURE_START     "SUBGHZ_CAPTURE:START"
#define ZV_COM_RES_SUBGHZ_CAPTURE_DATA      "SUBGHZ_CAPTURE:DATA"
#define ZV_COM_RES_SUBGHZ_CAPTURE_DONE      "SUBGHZ_CAPTURE:DONE"
#define ZV_COM_RES_SUBGHZ_CAPTURE_FAIL      "SUBGHZ_CAPTURE:FAIL"
#define ZV_COM_RES_SUBGHZ_CAPTURE_ERROR     "SUBGHZ_CAPTURE:ERROR"

#define ZV_COM_REQ_SUBGHZ_SEND              "SUBGHZ_SEND"
#define ZV_COM_REQ_SUBGHZ_SEND_BEGIN        "SUBGHZ_SEND:BEGIN"
#define ZV_COM_REQ_SUBGHZ_SEND_DATA         "SUBGHZ_SEND:DATA"
#define ZV_COM_REQ_SUBGHZ_SEND_END          "SUBGHZ_SEND:END"
#define ZV_COM_RES_SUBGHZ_SEND_BEGIN_OK     "SUBGHZ_SEND:BEGIN_OK"
#define ZV_COM_RES_SUBGHZ_SEND_ACK          "SUBGHZ_SEND:ACK"
#define ZV_COM_RES_SUBGHZ_SEND_DONE         "SUBGHZ_SEND:DONE"
#define ZV_COM_RES_SUBGHZ_SEND_FAIL         "SUBGHZ_SEND:FAIL"


#endif /* ZV_COM_PROTOCOL_H */
