#ifndef UART_COMMANDS_H
#define UART_COMMANDS_H

#define UART_COMMAND_REQ_TEST           "PING"
#define UART_COMMAND_RES_TEST           "PONG"
//-- UART COMMANDS -- //

#define BT_COMMAND_REQ_SCANN        "SCAN"          // el usuario comienza el scaneo
#define BT_COMMAND_RES_SCANN_START  "SCAN:START"    // el esp32 avisa que comenzo a scanear dispositivos
#define BT_COMMAND_RES_SCANN_DONE   "SCAN:DONE"     // el esp32 avisa que termino de hacer el scaner
#define BT_COMMAND_RES_SCANN_DEVICE "SCAN:DEVICE"   // el es32 esta enviando la informacion de u ndispositivo, resultado del scan
#define BT_COMMAND_RES_SCANN_UPDATE "SCAN:UPDATE"

//-- CONNECT -- //
#define BT_COMMAND_REQ_CONNECT          "CONNECT"           // el usuario pide conectar: "CONNECT:<MAC>"
#define BT_COMMAND_RES_CONNECT_START    "CONNECT:START"     // el esp32 avisa que inicio el intento de conexion
#define BT_COMMAND_RES_CONNECT_OK       "CONNECT:OK"        // conexion establecida: "CONNECT:OK:<MAC>"
#define BT_COMMAND_RES_CONNECT_FAIL     "CONNECT:FAIL"      // fallo: "CONNECT:FAIL:<MAC>:<reason>"
#define BT_COMMAND_RES_CONNECT_LOST     "CONNECT:LOST"      // desconexion inesperada
#define BT_COMMAND_RES_CONNECT_ERROR    "CONNECT:ERROR"     // 

#define BT_COMMAND_REQ_DISCONNECT       "DISCONNECT"        // el usuario pide cerrar conexion
#define BT_COMMAND_RES_DISCONNECT_OK    "DISCONNECT:OK"     // conexion cerrada

//-- DISCOVER (servicios y caracteristicas) -- //
#define BT_COMMAND_REQ_DISCOVER         "DISCOVER"          // pedir descubrimiento de servicios/chars
#define BT_COMMAND_RES_DISCOVER_START   "DISCOVER:START"    // el esp32 avisa que comenzo el discovery
#define BT_COMMAND_RES_DISCOVER_SERVICE "DISCOVER:SERVICE"  // info de un servicio: "DISCOVER:SERVICE:<idx>:<uuid>"
#define BT_COMMAND_RES_DISCOVER_CHAR    "DISCOVER:CHAR"     // info de una caracteristica: "DISCOVER:CHAR:<svc_idx>:<char_idx>:<uuid>:<props>"
#define BT_COMMAND_RES_DISCOVER_DESC    "DISCOVER:DESC"     // (opcional) descriptor: "DISCOVER:DESC:<svc_idx>:<char_idx>:<uuid>"
#define BT_COMMAND_RES_DISCOVER_DONE    "DISCOVER:DONE"     // termino el discovery
#define BT_COMMAND_RES_DISCOVER_FAIL    "DISCOVER:FAIL"     // error en el discovery

#endif /* UART_COMMANDS_H */