#ifndef UART_COMMANDS_H
#define UART_COMMANDS_H

#define UART_COMMAND_REQ_TEST           "PING"
#define UART_COMMAND_RES_TEST           "PONG"

//-- UART COMMANDS -- //

#define BT_COMMAND_REQ_SCAN        "SCAN"                  // User requests to start scanning
#define BT_COMMAND_RES_SCAN_START  "SCAN:START"            // ESP32 notifies that scanning has started
#define BT_COMMAND_RES_SCAN_DONE   "SCAN:DONE"             // ESP32 notifies that scanning has finished
#define BT_COMMAND_RES_SCAN_DEVICE "SCAN:DEVICE"           // ESP32 sends a discovered device result
#define BT_COMMAND_RES_SCAN_UPDATE "SCAN:UPDATE"           // ESP32 sends an update for scanned devices

//-- CONNECT -- //

#define BT_COMMAND_REQ_CONNECT          "CONNECT"           // User requests connection: "CONNECT:<MAC>"
#define BT_COMMAND_RES_CONNECT_START    "CONNECT:START"     // ESP32 notifies connection attempt started
#define BT_COMMAND_RES_CONNECT_OK       "CONNECT:OK"        // Connection established: "CONNECT:OK:<MAC>"
#define BT_COMMAND_RES_CONNECT_FAIL     "CONNECT:FAIL"      // Connection failed: "CONNECT:FAIL:<MAC>:<reason>"
#define BT_COMMAND_RES_CONNECT_LOST     "CONNECT:LOST"      // Unexpected disconnection occurred
#define BT_COMMAND_RES_CONNECT_ERROR    "CONNECT:ERROR"     // Generic connection error

#define BT_COMMAND_REQ_DISCONNECT       "DISCONNECT"        // User requests to disconnect
#define BT_COMMAND_RES_DISCONNECT_OK    "DISCONNECT:OK"     // Connection successfully closed

//-- DISCOVER (services and characteristics) -- //

#define BT_COMMAND_REQ_DISCOVER         "DISCOVER"          // Request service/characteristic discovery
#define BT_COMMAND_RES_DISCOVER_START   "DISCOVER:START"    // ESP32 notifies discovery started
#define BT_COMMAND_RES_DISCOVER_SERVICE "DISCOVER:SERVICE"  // Service info: "DISCOVER:SERVICE:<idx>:<uuid>"
#define BT_COMMAND_RES_DISCOVER_CHAR    "DISCOVER:CHAR"     // Characteristic info: "DISCOVER:CHAR:<svc_idx>:<char_idx>:<uuid>:<props>"
#define BT_COMMAND_RES_DISCOVER_DESC    "DISCOVER:DESC"     // (Optional) descriptor info: "DISCOVER:DESC:<svc_idx>:<char_idx>:<uuid>"
#define BT_COMMAND_RES_DISCOVER_DONE    "DISCOVER:DONE"     // Discovery finished
#define BT_COMMAND_RES_DISCOVER_FAIL    "DISCOVER:FAIL"     // Discovery error occurred

#endif /* UART_COMMANDS_H */