# Tutorial 2: Enumeración de servicios GATT

> **Prerequisito:** Tutorial 01 (BLE Scanner) y las funciones `zv_bt_*` del firmware ZeroVolts.
> Aquí el ESP32 pasa de ser observador a ser **GATT Client**: se conecta a un dispositivo y enumera sus servicios y características.

---

## 1. Objetivo

Vamos a:

1. Escanear dispositivos BLE (reutilizando el scanner del Tutorial 01)
2. **Seleccionar automáticamente** el dispositivo con mejor señal (RSSI más alto)
3. Conectarnos como **GATT Client**
4. Negociar MTU
5. Descubrir todos los servicios y características
6. **Analizar las propiedades** de cada característica para saber qué se puede leer, escribir o suscribirse

Usamos **GAP + GATT Client** — dos callbacks en vez de uno.

---

## 2. ¿Qué es GATT y por qué necesitamos conectarnos?

En el Tutorial 01 usamos solo **GAP** — el ESP32 escuchaba paquetes de advertising sin conectarse a nadie. Con eso obteníamos nombre, MAC, RSSI y manufacturer. Pero esa información es solo la "tarjeta de presentación" del dispositivo.

**GATT** (Generic Attribute Profile) es la capa que organiza los **datos reales** del dispositivo: sensores, batería, configuración, comandos, etc. Para acceder a GATT **es obligatorio establecer una conexión** con el dispositivo.

Referencia: [ESP-IDF GATT Client API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/bluetooth/esp_gattc.html)

```
┌──────────────────────────────────────────────────────────────────────┐
│                                                                      │
│   SIN CONEXIÓN (solo GAP)          CON CONEXIÓN (GAP + GATT)        │
│                                                                      │
│   Lo que puedes ver:               Lo que puedes ver:                │
│   ├── Nombre                       ├── Todo lo de GAP                │
│   ├── MAC address                  ├── Servicios (Battery, Heart...) │
│   ├── RSSI (señal)                 ├── Características (nivel, bpm)  │
│   ├── Manufacturer (Apple, etc)    ├── Propiedades (READ, WRITE...)  │
│   └── Flags, UUIDs parciales       ├── Descriptores (CCCD, etc)      │
│                                    └── Valores reales de los datos   │
│   Es como leer el nombre           Es como entrar al edificio y     │
│   en la puerta de un edificio.     revisar cada oficina.             │
│                                                                      │
│   ⚠ IMPORTANTE: Que un dispositivo tenga una característica         │
│   con propiedad READ no significa que puedas leerla.                 │
│   Puede requerir autenticación (pairing) o cifrado.                  │
│   Si intentas leer sin permiso, recibirás un error                   │
│   ESP_GATT_INSUF_AUTHENTICATION o ESP_GATT_INSUF_ENCRYPTION.        │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

### ¿Qué significan las propiedades de una característica?

```
┌──────────────────────────────────────────────────────────────────────┐
│   PROPIEDADES DE UNA CARACTERÍSTICA                                  │
│                                                                      │
│   Propiedad    │ Qué significa          │ Riesgo si no hay auth      │
│   ─────────────┼────────────────────────┼────────────────────────────│
│   READ         │ Puedes pedir el valor  │ Exposición de datos        │
│   WRITE        │ Puedes cambiar el valor│ Inyección de comandos      │
│   WRITE_NR     │ Write sin respuesta    │ Inyección sin confirmación │
│   NOTIFY       │ Te avisa de cambios    │ Monitoreo pasivo           │
│   INDICATE     │ Notify con confirmación│ Monitoreo con ACK          │
│                                                                      │
│   Niveles de protección:                                             │
│                                                                      │
│   ┌────────────────────────────────────────────────────────┐         │
│   │ NINGUNA AUTH → Cualquiera puede leer/escribir          │ ← VULN  │
│   │ PAIRING REQUERIDO → Necesitas pairing previo           │         │
│   │ CIFRADO REQUERIDO → Necesitas conexión cifrada         │         │
│   │ MITM PROTECTION → Necesitas pairing seguro             │ ← OK    │
│   └────────────────────────────────────────────────────────┘         │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

---

## 3. Qué necesitas

- ESP32 con cable USB
- Un dispositivo BLE objetivo (puede ser otro ESP32 corriendo una pulsera, etc.)
- App **nRF Connect** en el teléfono (si quieres que el teléfono sea el peripheral)

---

## 4. Flujo: Del scanner al GATT Client

En vez de buscar un dispositivo por nombre (hardcodeado), vamos a reutilizar el scanner del Tutorial 01 y conectarnos **automáticamente al dispositivo con mejor señal RSSI**.

```
┌──────────────────────────────────────────────────────────────────────┐
│   FLUJO: SCANNER → SELECCIÓN POR RSSI → GATT ENUMERATION            │
│                                                                      │
│   Fase 1: Escaneo (15 segundos — reutiliza Tutorial 01)             │
│   ┌──────────────────────────────────────────────────┐              │
│   │  zv_bt_add_device() guarda cada dispositivo      │              │
│   │  con nombre, MAC, BDA, RSSI, manufacturer        │              │
│   └──────────────────────────┬───────────────────────┘              │
│                              │                                       │
│   Fase 2: Selección automática                                       │
│   ┌──────────────────────────▼───────────────────────┐              │
│   │  zv_bt_get_closest_device()                      │              │
│   │  → Recorre todos los dispositivos encontrados    │              │
│   │  → Retorna el que tiene RSSI más alto (más cerca)│              │
│   │                                                  │              │
│   │  Ejemplo:                                        │              │
│   │  [0] Samsung TV    RSSI: -98  ← lejos            │              │
│   │  [1] Apple device  RSSI: -69                     │              │
│   │  [2] Mi Band       RSSI: -42  ← MÁS CERCA ✓    │              │
│   │  [3] Unknown       RSSI: -85                     │              │
│   └──────────────────────────┬───────────────────────┘              │
│                              │                                       │
│   Fase 3: Conexión GATT                                              │
│   ┌──────────────────────────▼───────────────────────┐              │
│   │  esp_ble_gattc_open(bda del más cercano)         │              │
│   │  → Conectar                                      │              │
│   │  → Negociar MTU                                  │              │
│   │  → Descubrir servicios                           │              │
│   │  → Enumerar características + propiedades        │              │
│   │  → Intentar lectura de prueba                    │              │
│   └──────────────────────────────────────────────────┘              │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

---

## 5. Diagrama: Flujo completo de conexión GATT

```
┌─────────────────────────────────────────────────────────────────────┐
│                                                                     │
│  ESP32 (GATT Client)                    Periférico (GATT Server)    │
│                                                                     │
│  1. esp_ble_gattc_app_register()                                    │
│     │                                                               │
│     ▼  ESP_GATTC_REG_EVT                                            │
│     "App registrada, guardo gattc_if"                               │
│     │                                                               │
│     ▼  Configuro scan y empiezo a escanear                          │
│     │                                                               │
│     ▼  Encuentro dispositivo objetivo por nombre                    │
│     │                                                               │
│  2. esp_ble_gap_stop_scanning()                                     │
│  3. esp_ble_gattc_open()  ─────────────── CONNECT_REQ ───────────►  │
│     │                                                               │
│     ▼  ESP_GATTC_OPEN_EVT              ◄── CONNECT_RSP              │
│     "Conexión abierta, guardo conn_id"                              │
│     │                                                               │
│  4. esp_ble_gattc_send_mtu_req() ──── MTU_REQ ───────────────────►  │
│     │                                                               │
│     ▼  ESP_GATTC_CFG_MTU_EVT          ◄── MTU_RSP                   │
│     "MTU negociado (ej: 500)"                                       │
│     │                                                               │
│  5. esp_ble_gattc_search_service()── DISCOVER_SERVICES ──────────►  │
│     │                                                               │
│     ▼  ESP_GATTC_SEARCH_RES_EVT (×N)  ◄── SERVICE_FOUND             │
│     "Servicio encontrado: UUID, handles"                            │
│     │                                                               │
│     ▼  ESP_GATTC_SEARCH_CMPL_EVT      ◄── SEARCH_DONE               │
│     "Busqueda terminada, ahora enumero características"             │
│     │                                                               │
│  6. esp_ble_gattc_get_attr_count()                                  │
│     esp_ble_gattc_get_all_char()                                    │
│     │                                                               │
│     ▼  Imprimo servicios y características                          │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 5. Diagrama: Jerarquía GATT real

Esto es lo que vamos a descubrir al conectarnos:

```
    Dispositivo: "Mi Band 7"
    │
    ├─── Servicio: Generic Access (0x1800)
    │    ├── Característica: Device Name (0x2A00) [READ]
    │    └── Característica: Appearance (0x2A01) [READ]
    │
    ├─── Servicio: Generic Attribute (0x1801)
    │    └── Característica: Service Changed (0x2A05) [INDICATE]
    │         └── Descriptor: CCCD (0x2902)
    │
    ├─── Servicio: Battery (0x180F)
    │    └── Característica: Battery Level (0x2A19) [READ, NOTIFY]
    │         └── Descriptor: CCCD (0x2902)
    │
    └─── Servicio: Custom (128-bit UUID)
         ├── Característica: Custom 1 [READ, WRITE]
         └── Característica: Custom 2 [NOTIFY]
              └── Descriptor: CCCD (0x2902)
```

---

## 6. Diagrama: Client vs Server

```
┌──────────────────────────────────────────────────────────────────────┐
│                                                                      │
│  GATT CLIENT (ESP32)                  GATT SERVER (periférico)       │
│                                                                      │
│  ┌──────────────┐                    ┌──────────────┐                │
│  │              │                    │              │                │
│  │  "¿Qué       │ ── search ───────► │  Servicios:  │                │
│  │   servicios  │                    │  - Battery   │                │
│  │   tienes?"   │ ◄── respuesta ──── │  - Heart Rate│                │
│  │              │                    │  - Custom    │                │
│  │  "Dame el    │ ── read ─────────► │              │                │
│  │   nivel de   │                    │              │                │
│  │   bateria"   │ ◄── valor: 87% ─── │              │                │
│  │              │                    │              │                │
│  │  "Avisame    │ ── write CCCD ───► │              │                │
│  │   cuando     │                    │              │                │
│  │   cambie"    │ ◄── notify: 86% ── │              │                │
│  │              │ ◄── notify: 85% ── │              │                │
│  └──────────────┘                    └──────────────┘                │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

---

## 7. Paso 1: Configuración del proyecto

Mismo `platformio.ini` y `sdkconfig.defaults` del Tutorial 01. Reutilizamos los archivos `zv_bluetooth.h/c` y `zv_bluetooth_device.h/c` del firmware ZeroVolts.

---

## 8. Paso 2: Selección automática del objetivo

En vez de hardcodear un nombre de dispositivo, primero escaneamos (como en el Tutorial 01) y luego usamos `zv_bt_get_closest_device()` para seleccionar automáticamente el dispositivo con mejor señal:

```c
// Cuando el scan termina (ESP_GAP_SEARCH_INQ_CMPL_EVT):
const zv_bt_device_t *target = zv_bt_get_closest_device();
if (target) {
    ESP_LOGI(TAG, "Objetivo seleccionado: %s (MAC: %s, RSSI: %d)",
             target->name, target->mac, target->rssi);
    // Conectar usando el BDA del dispositivo
    esp_ble_gattc_open(gl_profile.gattc_if, (uint8_t *)target->bda,
                       BLE_ADDR_TYPE_PUBLIC, true);
}
```

Esto es más útil que buscar por nombre porque:
- No necesitas saber de antemano qué dispositivos hay
- El más cercano es el más probable candidato para auditoría
- Puedes filtrar después por nombre/manufacturer si quieres un target específico

---

## 9. Paso 3: Los dos callbacks

Ahora necesitas **dos** callbacks:

```c
// GAP: escaneo, conexión física
esp_ble_gap_register_callback(gap_event_handler);

// GATT Client: servicios, características, lecturas
esp_ble_gattc_register_callback(gattc_event_handler);

// Registrar la aplicación (dispara ESP_GATTC_REG_EVT)
esp_ble_gattc_app_register(0);
```

---

## 10. Paso 4: Qué significa app_id y gattc_if

- **app_id**: número que tú eliges para identificar tu app cliente (0, 1, 2...)
- **gattc_if**: interfaz que Bluedroid te asigna cuando registras la app

```
    Tu código                     Bluedroid

    app_register(0) ──────────►  "Ok, tu interfaz es gattc_if=3"
                                  │
                    ◄──────────── ESP_GATTC_REG_EVT
                                  param->reg.gattc_if = 3

    Desde ahora usas gattc_if=3 para todas las operaciones GATT.
```

---

## 11. Código completo: main.c

```c
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"

#include "zv_bluetooth.h"
#include "zv_bluetooth_device.h"

static const char *TAG = "GATT_CLIENT";

// ─── Configuración ────────────────────────────────────────────
#define PROFILE_APP_ID      0
#define SCAN_DURATION       15   // segundos (mismo que Tutorial 01)
#define INVALID_HANDLE      0

// ─── Estado del perfil ────────────────────────────────────────
static struct {
    uint16_t            gattc_if;
    uint16_t            conn_id;
    esp_bd_addr_t       remote_bda;
    bool                connected;
} gl_profile = {
    .gattc_if = ESP_GATT_IF_NONE,
    .connected = false,
};

// ─── Parámetros de escaneo ────────────────────────────────────
static esp_ble_scan_params_t ble_scan_params = {
    .scan_type          = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval      = 50,
    .scan_window        = 30,
    .scan_duplicate     = BLE_SCAN_DUPLICATE_DISABLE
};

// ─── Helper: imprimir UUID ────────────────────────────────────
static void print_uuid(esp_bt_uuid_t *uuid)
{
    if (uuid->len == ESP_UUID_LEN_16) {
        ESP_LOGI(TAG, "    UUID: 0x%04X", uuid->uuid.uuid16);
    } else if (uuid->len == ESP_UUID_LEN_32) {
        ESP_LOGI(TAG, "    UUID: 0x%08lX", (unsigned long)uuid->uuid.uuid32);
    } else if (uuid->len == ESP_UUID_LEN_128) {
        ESP_LOGI(TAG, "    UUID: %02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                 uuid->uuid.uuid128[15], uuid->uuid.uuid128[14],
                 uuid->uuid.uuid128[13], uuid->uuid.uuid128[12],
                 uuid->uuid.uuid128[11], uuid->uuid.uuid128[10],
                 uuid->uuid.uuid128[9],  uuid->uuid.uuid128[8],
                 uuid->uuid.uuid128[7],  uuid->uuid.uuid128[6],
                 uuid->uuid.uuid128[5],  uuid->uuid.uuid128[4],
                 uuid->uuid.uuid128[3],  uuid->uuid.uuid128[2],
                 uuid->uuid.uuid128[1],  uuid->uuid.uuid128[0]);
    }
}

// ─── Almacenar servicios descubiertos ─────────────────────────
#define MAX_SERVICES 20
static struct {
    uint16_t start_handle;
    uint16_t end_handle;
    esp_bt_uuid_t uuid;
} discovered_services[MAX_SERVICES];
static int service_count = 0;

// ─── Helper: analizar nivel de acceso de una característica ──
static const char *analyze_access_level(uint8_t props)
{
    bool can_read  = props & ESP_GATT_CHAR_PROP_BIT_READ;
    bool can_write = props & (ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR);
    bool can_notify = props & (ESP_GATT_CHAR_PROP_BIT_NOTIFY | ESP_GATT_CHAR_PROP_BIT_INDICATE);

    if (can_read && can_write) return "LECTURA + ESCRITURA (posible inyección)";
    if (can_read && can_notify) return "LECTURA + SUSCRIPCION (monitoreo posible)";
    if (can_read)  return "SOLO LECTURA (posible exposición de datos)";
    if (can_write) return "SOLO ESCRITURA (posible inyección de comandos)";
    if (can_notify) return "SOLO NOTIFY/INDICATE (monitoreo pasivo)";
    return "SIN ACCESO APARENTE";
}

// ─── Enumerar características de todos los servicios ──────────
static void enumerate_characteristics(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║           GATT DATABASE DEL DISPOSITIVO                 ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");

    int total_readable = 0;
    int total_writable = 0;
    int total_notify = 0;

    for (int s = 0; s < service_count; s++) {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "┌─── Servicio %d (handles %d-%d) ───",
                 s + 1,
                 discovered_services[s].start_handle,
                 discovered_services[s].end_handle);
        print_uuid(&discovered_services[s].uuid);

        // Contar características
        uint16_t char_count = 0;
        esp_gatt_status_t status = esp_ble_gattc_get_attr_count(
            gl_profile.gattc_if,
            gl_profile.conn_id,
            ESP_GATT_DB_CHARACTERISTIC,
            discovered_services[s].start_handle,
            discovered_services[s].end_handle,
            INVALID_HANDLE,
            &char_count
        );

        if (status != ESP_GATT_OK || char_count == 0) {
            ESP_LOGI(TAG, "│   (sin características)");
            continue;
        }

        ESP_LOGI(TAG, "│   Características: %d", char_count);

        // Obtener todas las características
        esp_gattc_char_elem_t *char_elems = malloc(sizeof(esp_gattc_char_elem_t) * char_count);
        if (char_elems == NULL) {
            ESP_LOGE(TAG, "│   Error: sin memoria para características");
            continue;
        }

        uint16_t count = char_count;
        status = esp_ble_gattc_get_all_char(
            gl_profile.gattc_if,
            gl_profile.conn_id,
            discovered_services[s].start_handle,
            discovered_services[s].end_handle,
            char_elems,
            &count,
            0
        );

        if (status == ESP_GATT_OK) {
            for (int c = 0; c < count; c++) {
                uint8_t props = char_elems[c].properties;

                ESP_LOGI(TAG, "│");
                ESP_LOGI(TAG, "│   ├── Caracteristica %d (handle: %d)",
                         c + 1, char_elems[c].char_handle);
                print_uuid(&char_elems[c].uuid);

                // Decodificar propiedades
                ESP_LOGI(TAG, "│   │   Propiedades: %s%s%s%s%s",
                         (props & ESP_GATT_CHAR_PROP_BIT_READ)     ? "READ "     : "",
                         (props & ESP_GATT_CHAR_PROP_BIT_WRITE)    ? "WRITE "    : "",
                         (props & ESP_GATT_CHAR_PROP_BIT_WRITE_NR) ? "WRITE_NR " : "",
                         (props & ESP_GATT_CHAR_PROP_BIT_NOTIFY)   ? "NOTIFY "   : "",
                         (props & ESP_GATT_CHAR_PROP_BIT_INDICATE) ? "INDICATE " : "");

                // Análisis de acceso
                ESP_LOGI(TAG, "│   │   Acceso: %s", analyze_access_level(props));

                // Contadores para resumen
                if (props & ESP_GATT_CHAR_PROP_BIT_READ) total_readable++;
                if (props & (ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR)) total_writable++;
                if (props & (ESP_GATT_CHAR_PROP_BIT_NOTIFY | ESP_GATT_CHAR_PROP_BIT_INDICATE)) total_notify++;
            }
        }

        free(char_elems);
    }

    // Resumen de superficie de ataque
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║           RESUMEN DE SUPERFICIE DE ATAQUE               ║");
    ESP_LOGI(TAG, "╠══════════════════════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║  Servicios:       %d                                    ", service_count);
    ESP_LOGI(TAG, "║  Legibles (READ): %d  ← posible exposición de datos    ", total_readable);
    ESP_LOGI(TAG, "║  Escribibles:     %d  ← posible inyección de comandos  ", total_writable);
    ESP_LOGI(TAG, "║  Notify/Indicate: %d  ← posible monitoreo pasivo       ", total_notify);
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");

    if (total_readable > 0) {
        ESP_LOGW(TAG, "  ⚠ Hay %d características legibles — intentar leer sin auth", total_readable);
        ESP_LOGW(TAG, "    Si el read funciona sin pairing → HALLAZGO de seguridad");
        ESP_LOGW(TAG, "    Si retorna INSUF_AUTHENTICATION → el dispositivo exige auth");
    }
    if (total_writable > 0) {
        ESP_LOGW(TAG, "  ⚠ Hay %d características escribibles — posible inyección", total_writable);
    }
}

// ─── Helper: formatear MAC ────────────────────────────────────
static void format_mac_address(const uint8_t *mac, char *out)
{
    sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// ─── Callback GAP ─────────────────────────────────────────────
// Reutiliza el flujo del Tutorial 01: escanea, guarda todos los
// dispositivos con zv_bt_add_device(), y al terminar el scan
// selecciona el más cercano (mejor RSSI) para conectar.
static void gap_event_handler(esp_gap_ble_cb_event_t event,
                               esp_ble_gap_cb_param_t *param)
{
    switch (event) {

    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        if (param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Escaneando dispositivos BLE (%d segundos)...", SCAN_DURATION);
            esp_ble_gap_start_scanning(SCAN_DURATION);
        }
        break;

    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        struct ble_scan_result_evt_param scan_result = param->scan_rst;

        switch (scan_result.search_evt) {

        case ESP_GAP_SEARCH_INQ_RES_EVT: {
            // Reutilizar las funciones zv_bt_* del Tutorial 01
            char name_buf[128];
            zv_bt_get_device_name(scan_result, name_buf, sizeof(name_buf));

            char mac_str[18];
            format_mac_address(scan_result.bda, mac_str);

            char manufacturer[128];
            zv_bt_get_manufacturer_name(scan_result, manufacturer, sizeof(manufacturer));

            zv_bt_add_device(name_buf, mac_str, scan_result.bda,
                             scan_result.rssi, manufacturer);
            break;
        }

        case ESP_GAP_SEARCH_INQ_CMPL_EVT: {
            ESP_LOGI(TAG, "Escaneo completo.");
            zv_bt_print_devices();

            // Seleccionar automáticamente el dispositivo más cercano
            const zv_bt_device_t *target = zv_bt_get_closest_device();
            if (target == NULL) {
                ESP_LOGW(TAG, "No se encontraron dispositivos BLE");
                break;
            }

            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, ">>> Objetivo seleccionado (mejor RSSI):");
            ESP_LOGI(TAG, "    Nombre: %s", target->name);
            ESP_LOGI(TAG, "    MAC:    %s", target->mac);
            ESP_LOGI(TAG, "    RSSI:   %d dBm", target->rssi);
            ESP_LOGI(TAG, "    Manuf:  %s", target->manufacturer);
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "Conectando como GATT Client...");

            // Conectar al dispositivo más cercano
            esp_ble_gattc_open(
                gl_profile.gattc_if,
                (uint8_t *)target->bda,
                BLE_ADDR_TYPE_PUBLIC,
                true
            );
            break;
        }

        default:
            break;
        }
        break;
    }

    default:
        break;
    }
}

// ─── Callback GATT Client ─────────────────────────────────────
static void gattc_event_handler(esp_gattc_cb_event_t event,
                                 esp_gatt_if_t gattc_if,
                                 esp_ble_gattc_cb_param_t *param)
{
    switch (event) {

    // 1. App registrada → guardar interfaz y empezar scan
    case ESP_GATTC_REG_EVT:
        if (param->reg.status == ESP_GATT_OK) {
            gl_profile.gattc_if = gattc_if;
            ESP_LOGI(TAG, "App GATT Client registrada (gattc_if=%d)", gattc_if);
            esp_ble_gap_set_scan_params(&ble_scan_params);
        } else {
            ESP_LOGE(TAG, "Error registrando app: %d", param->reg.status);
        }
        break;

    // 2. Conexión abierta → negociar MTU
    case ESP_GATTC_OPEN_EVT:
        if (param->open.status == ESP_GATT_OK) {
            gl_profile.conn_id = param->open.conn_id;
            gl_profile.connected = true;
            memcpy(gl_profile.remote_bda, param->open.remote_bda, 6);

            ESP_LOGI(TAG, "Conectado (conn_id=%d). Negociando MTU...",
                     param->open.conn_id);
            esp_ble_gattc_send_mtu_req(gattc_if, param->open.conn_id);
        } else {
            ESP_LOGE(TAG, "Error de conexion: %d", param->open.status);
            gl_profile.connected = false;
        }
        break;

    // 3. MTU negociado → buscar servicios
    case ESP_GATTC_CFG_MTU_EVT:
        if (param->cfg_mtu.status == ESP_GATT_OK) {
            ESP_LOGI(TAG, "MTU negociado: %d bytes. Buscando servicios...",
                     param->cfg_mtu.mtu);
            esp_ble_gattc_search_service(gattc_if, gl_profile.conn_id, NULL);
        } else {
            ESP_LOGW(TAG, "MTU fallo (status=%d), busco servicios igual...",
                     param->cfg_mtu.status);
            esp_ble_gattc_search_service(gattc_if, gl_profile.conn_id, NULL);
        }
        break;

    // 4. Se encontró un servicio
    case ESP_GATTC_SEARCH_RES_EVT:
        if (service_count < MAX_SERVICES) {
            discovered_services[service_count].start_handle = param->search_res.start_handle;
            discovered_services[service_count].end_handle = param->search_res.end_handle;
            discovered_services[service_count].uuid = param->search_res.srvc_id.uuid;
            service_count++;
        }
        break;

    // 5. Búsqueda de servicios terminada → enumerar características
    case ESP_GATTC_SEARCH_CMPL_EVT:
        if (param->search_cmpl.status == ESP_GATT_OK) {
            ESP_LOGI(TAG, "Servicios encontrados: %d", service_count);
            enumerate_characteristics();
        } else {
            ESP_LOGE(TAG, "Error buscando servicios: %d", param->search_cmpl.status);
        }
        break;

    // 6. Desconexión
    case ESP_GATTC_DISCONNECT_EVT:
        ESP_LOGW(TAG, "Desconectado (reason=%d)", param->disconnect.reason);
        gl_profile.connected = false;
        service_count = 0;
        break;

    default:
        break;
    }
}

// ─── Inicialización ───────────────────────────────────────────
static void init_ble(void)
{
    esp_err_t ret;

    // NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Controlador BT
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

    // Bluedroid
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    // Callbacks: GAP + GATT Client
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));
    ESP_ERROR_CHECK(esp_ble_gattc_register_callback(gattc_event_handler));

    // Registrar app (dispara ESP_GATTC_REG_EVT)
    ESP_ERROR_CHECK(esp_ble_gattc_app_register(PROFILE_APP_ID));

    // MTU local
    ESP_ERROR_CHECK(esp_ble_gatt_set_local_mtu(500));

    ESP_LOGI(TAG, "GATT Client inicializado");
}

// ─── Main ─────────────────────────────────────────────────────
void app_main(void)
{
    ESP_LOGI(TAG, "=== GATT Client - Enumeración de servicios ===");
    ESP_LOGI(TAG, "Fase 1: Escanear dispositivos BLE");
    ESP_LOGI(TAG, "Fase 2: Conectar al más cercano (mejor RSSI)");
    ESP_LOGI(TAG, "Fase 3: Enumerar servicios y características");
    ESP_LOGI(TAG, "");
    init_ble();
}
```

---

## 12. Qué vas a ver en el monitor serie

```
I (325)  GATT_CLIENT: === GATT Client - Enumeración de servicios ===
I (325)  GATT_CLIENT: Fase 1: Escanear dispositivos BLE
I (325)  GATT_CLIENT: Fase 2: Conectar al más cercano (mejor RSSI)
I (325)  GATT_CLIENT: Fase 3: Enumerar servicios y características
I (335)  GATT_CLIENT: GATT Client inicializado
I (345)  GATT_CLIENT: App GATT Client registrada (gattc_if=3)
I (355)  GATT_CLIENT: Escaneando dispositivos BLE (15 segundos)...

  ... (15 segundos escaneando, acumulando dispositivos) ...

I (15355) ZV_BLUETOOTH_DEVICE: -------------------------------
I (15355) ZV_BLUETOOTH_DEVICE: [0] name: Unknown, mac: B0:E4:5C:7E:F0:EF, Manufacturer: Samsung, rssi: -81
I (15365) ZV_BLUETOOTH_DEVICE: [1] name: Mi Band 7, mac: 48:DA:55:C8:95:E0, Manufacturer: Xiaomi, rssi: -42
I (15375) ZV_BLUETOOTH_DEVICE: [2] name: [TV] Samsung 7, mac: 64:E7:D8:91:7E:9B, Manufacturer: Samsung, rssi: -98
I (15385) ZV_BLUETOOTH_DEVICE: [3] name: Unknown, mac: EA:F6:29:E2:38:22, Manufacturer: Apple, rssi: -69
I (15395) ZV_BLUETOOTH_DEVICE: -------------------------------

I (15400) GATT_CLIENT:
I (15400) GATT_CLIENT: >>> Objetivo seleccionado (mejor RSSI):
I (15400) GATT_CLIENT:     Nombre: Mi Band 7
I (15400) GATT_CLIENT:     MAC:    48:DA:55:C8:95:E0
I (15400) GATT_CLIENT:     RSSI:   -42 dBm
I (15400) GATT_CLIENT:     Manuf:  Xiaomi
I (15400) GATT_CLIENT:
I (15400) GATT_CLIENT: Conectando como GATT Client...
I (15850) GATT_CLIENT: Conectado (conn_id=0). Negociando MTU...
I (16100) GATT_CLIENT: MTU negociado: 500 bytes. Buscando servicios...
I (16350) GATT_CLIENT: Servicios encontrados: 3

╔══════════════════════════════════════════════════════════╗
║           GATT DATABASE DEL DISPOSITIVO                 ║
╚══════════════════════════════════════════════════════════╝

┌─── Servicio 1 (handles 1-5) ───
    UUID: 0x1800
│   Características: 2
│
│   ├── Caracteristica 1 (handle: 3)
    UUID: 0x2A00
│   │   Propiedades: READ
│   │   Acceso: SOLO LECTURA (posible exposición de datos)
│
│   ├── Caracteristica 2 (handle: 5)
    UUID: 0x2A01
│   │   Propiedades: READ
│   │   Acceso: SOLO LECTURA (posible exposición de datos)

┌─── Servicio 2 (handles 6-9) ───
    UUID: 0x1801
│   Características: 1
│
│   ├── Caracteristica 1 (handle: 8)
    UUID: 0x2A05
│   │   Propiedades: INDICATE
│   │   Acceso: SOLO NOTIFY/INDICATE (monitoreo pasivo)

┌─── Servicio 3 (handles 10-20) ───
    UUID: 0x181A
│   Características: 2
│
│   ├── Caracteristica 1 (handle: 12)
    UUID: 0x2A6E
│   │   Propiedades: READ NOTIFY
│   │   Acceso: LECTURA + SUSCRIPCION (monitoreo posible)
│
│   ├── Caracteristica 2 (handle: 16)
    UUID: 0x2A6F
│   │   Propiedades: READ WRITE
│   │   Acceso: LECTURA + ESCRITURA (posible inyección)

╔══════════════════════════════════════════════════════════╗
║           RESUMEN DE SUPERFICIE DE ATAQUE               ║
╠══════════════════════════════════════════════════════════╣
║  Servicios:       3
║  Legibles (READ): 4  ← posible exposición de datos
║  Escribibles:     1  ← posible inyección de comandos
║  Notify/Indicate: 2  ← posible monitoreo pasivo
╚══════════════════════════════════════════════════════════╝
W (16500) GATT_CLIENT:   ⚠ Hay 4 características legibles — intentar leer sin auth
W (16500) GATT_CLIENT:     Si el read funciona sin pairing → HALLAZGO de seguridad
W (16500) GATT_CLIENT:     Si retorna INSUF_AUTHENTICATION → el dispositivo exige auth
W (16500) GATT_CLIENT:   ⚠ Hay 1 características escribibles — posible inyección
```

> **Nota**: El resumen de "superficie de ataque" indica **qué propiedades declara** el dispositivo, no garantiza que puedas acceder. Una característica con READ puede requerir pairing/cifrado. En el Tutorial 03 intentaremos leer y manejar los errores de autenticación.

---

## 13. Diagrama: Secuencia de mensajes

```
    ESP32 (Client)                          Periférico (Server)
         │                                        │
         │──── SCAN_REQ ────────────────────────► │
         │◄─── SCAN_RSP (nombre, UUIDs) ────────  │
         │                                        │
         │──── CONNECT_REQ ─────────────────────► │
         │◄─── CONNECT_RSP ─────────────────────  │
         │                                        │
         │──── MTU_REQ (500) ───────────────────► │
         │◄─── MTU_RSP (500) ───────────────────  │
         │                                        │
         │──── DISCOVER_ALL_PRIMARY_SERVICES ───► │
         │◄─── Service 1: 0x1800 (handles 1-5) ─  │
         │◄─── Service 2: 0x1801 (handles 6-9) ─  │
         │◄─── Service 3: 0x181A (handles 10-20)  │
         │◄─── SEARCH_COMPLETE ────────────────── │
         │                                        │
         │──── READ_BY_TYPE (chars de svc 1) ──►  │
         │◄─── Char: 0x2A00 handle=3 READ ──────  │
         │◄─── Char: 0x2A01 handle=5 READ ──────  │
         │                                        │
         │  (repite para cada servicio)           │
         │                                        │
```

---

## 14. Errores comunes

| Error | Causa | Solución |
|-------|-------|----------|
| "Escaneo terminado sin encontrar..." | Nombre no coincide exactamente | Verificar con scanner del Tutorial 01 |
| `OPEN_EVT` con error | Dispositivo se fue o ya está conectado | Reiniciar ambos dispositivos |
| 0 servicios encontrados | MTU falló o conexión inestable | Buscar servicios incluso si MTU falla |
| Crash en `enumerate_characteristics` | `malloc` falla por muchas características | Verificar retorno de `malloc` |
| UUID se ve como 128-bit cuando esperaba 16-bit | El servicio usa UUID custom | Implementar `print_uuid` para ambos formatos |
| `gattc_if` inválido | No guardaste el valor de `ESP_GATTC_REG_EVT` | Guardar en `gl_profile.gattc_if` |

---

## 15. Referencias

- ESP-IDF GATT Client API: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/bluetooth/esp_gattc.html
- GATT Client walkthrough: https://github.com/espressif/esp-idf/blob/master/examples/bluetooth/bluedroid/ble/gatt_client/tutorial/Gatt_Client_Example_Walkthrough.md
- ESP-IDF GAP BLE API: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/bluetooth/esp_gap_ble.html
