# BLE Scanner con Bluedroid


## 1. Objetivos

Pprogramar un **escáner BLE** en ESP32 que:

1. Escucha paquetes de advertising durante 30 segundos
2. Extrae nombre, MAC y RSSI de cada dispositivo
3. Evita duplicados comparando por MAC
4. Al terminar, imprime una tabla con todos los dispositivos encontrados

Solo usamos la capa **GAP** — todavía no hay conexión ni GATT.

---

## 2. Qué se necesita

- ESP32 (cualquier variante con BLE)
- Cable USB
- VS Code con PlatformIO instalado
- Al menos un dispositivo BLE encendido cerca (teléfono, pulsera, parlante, etc.)

---

## 3. Diagrama: Cómo funciona el escaneo BLE

```
    Dispositivos BLE cercanos                       Tu ESP32

    ┌──────────────┐
    │  Mi Band 7   │ ── "Soy Mi Band 7" ────────┐
    └──────────────┘                            │
                                                │
    ┌──────────────┐                            ▼
    │  TV Samsung  │ ── "Soy TV Samsung" ──►  ┌──────────────┐
    └──────────────┘                          │              │
                                              │  ESP32       │
    ┌──────────────┐                          │  SCANNER     │
    │  AirPods     │ ── "Soy AirPods" ────►   │              │
    └──────────────┘                          │  Escucha     │
                                              │  todo y      │
    ┌──────────────┐                          │  guarda en   │
    │  (sin nombre)│ ── advertising ────────► │  una tabla   │
    └──────────────┘                          │              │
                                              └──────────────┘

    Cada dispositivo BLE grita periódicamente quién es.
    Nuestro ESP32 solo escucha y toma nota.
```

---

## 4. Diagrama: Flujo de eventos del scanner

```
┌─────────────────────────────────────────────────────────────────────┐
│                    FLUJO DE EVENTOS                                 │
│                                                                     │
│  app_main()                                                         │
│     │                                                               │
│     ▼                                                               │
│  init_ble()                                                         │
│     │                                                               │
│     ├─► nvs_flash_init()                                            │
│     ├─► esp_bt_controller_mem_release(CLASSIC_BT)                   │
│     ├─► esp_bt_controller_init() + enable(BLE)                      │
│     ├─► esp_bluedroid_init() + enable()                             │
│     ├─► esp_ble_gap_register_callback(gap_event_handler)            │
│     └─► esp_ble_gap_set_scan_params(&scan_params)                   │
│            │                                                        │
│            ▼  (callback automático)                                 │
│  ┌─── ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT                       │
│  │    "Los parámetros de scan fueron aceptados"                     │
│  │         │                                                        │
│  │         ▼                                                        │
│  │    esp_ble_gap_start_scanning(30)  ◄── inicia 30 segundos        │
│  │         │                                                        │
│  │         ▼  (se repite por cada dispositivo detectado)            │
│  │    ┌─── ESP_GAP_BLE_SCAN_RESULT_EVT                              │
│  │    │    search_evt = ESP_GAP_SEARCH_INQ_RES_EVT                  │
│  │    │    │                                                        │
│  │    │    ├─► Extraer nombre (completo o corto)                    │
│  │    │    ├─► Formatear MAC                                        │
│  │    │    ├─► Leer RSSI                                            │
│  │    │    ├─► ¿Ya existe esta MAC? → Si: ignorar                   │
│  │    │    └─► No: agregar a la tabla                               │
│  │    │                                                             │
│  │    │    (se repite muchas veces durante 30 segundos)             │
│  │    │                                                             │
│  │    └─── ESP_GAP_SEARCH_INQ_CMPL_EVT                              │
│  │         "Escaneo terminado"                                      │
│  │         │                                                        │
│  │         ▼                                                        │
│  │    print_devices()  ◄── imprime tabla resumen                    │
│  └──────────────────────────────────────────────────────────────────│
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 5. Paso 1: Crear el proyecto

### platformio.ini

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = espidf
monitor_speed = 115200

board_build.cmake_extra_args =
    -DSDKCONFIG_DEFAULTS="sdkconfig.defaults"
```

### sdkconfig.defaults

```
# Habilitar Bluetooth
CONFIG_BT_ENABLED=y

# Usar Bluedroid (no NimBLE)
CONFIG_BT_BLUEDROID_ENABLED=y
CONFIG_BT_BLE_ENABLED=y

# Deshabilitar Bluetooth Clásico para ahorrar memoria
CONFIG_BT_CLASSIC_ENABLED=n

# Modo solo BLE en el controlador
CONFIG_BTDM_CTRL_MODE_BLE_ONLY=y

# Log level
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
```

> **Importante:** Si ya existe un `sdkconfig.esp32dev`, borrarlo y correr `pio run -t clean` para que tome los nuevos defaults.

---

## 6. Paso 2: Entender la inicialización

Cada llamada de init tiene un propósito específico:

```c
// 1. NVS — memoria no volátil. Bluetooth la necesita internamente
nvs_flash_init();

// 2. Liberar RAM de BT Clásico (no lo usamos, ganamos ~30KB)
esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

// 3. Inicializar el controlador BT (capa hardware)
esp_bt_controller_init(&bt_cfg);
esp_bt_controller_enable(ESP_BT_MODE_BLE);

// 4. Inicializar Bluedroid (capa software: GAP, GATT, seguridad)
esp_bluedroid_init();
esp_bluedroid_enable();

// 5. Registrar TU función que recibirá eventos GAP
esp_ble_gap_register_callback(gap_event_handler);

// 6. Configurar parámetros de escaneo (NO inicia el scan todavía)
//    Cuando termina, dispara ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT
esp_ble_gap_set_scan_params(&ble_scan_params);
```

---

## 7. Paso 3: Parámetros de escaneo

```c
static esp_ble_scan_params_t ble_scan_params = {
    .scan_type          = BLE_SCAN_TYPE_ACTIVE,       // Pide scan response
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,       // Dirección pública
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,  // Acepta todo
    .scan_interval      = 0x50,                       // 50 ms entre ventanas
    .scan_window        = 0x30,                       // 30 ms escuchando
    .scan_duplicate     = BLE_SCAN_DUPLICATE_DISABLE  // Reporta repetidos
};
```

| Campo | Qué hace |
|-------|----------|
| `scan_type` | ACTIVE pide más datos (scan response). PASSIVE solo escucha |
| `own_addr_type` | Tipo de dirección propia. PUBLIC es la más simple |
| `scan_filter_policy` | ALLOW_ALL acepta paquetes de cualquier dispositivo |
| `scan_interval` | Cada cuánto se abre la ventana de escucha |
| `scan_window` | Cuánto tiempo escucha dentro de cada intervalo |
| `scan_duplicate` | DISABLE = reporta todo, incluso repetidos (útil para ver RSSI en tiempo real) |

---

## 8. Paso 4: El callback GAP

El callback recibe **todos** los eventos GAP. Para un scanner nos interesan dos:

### ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT
Los parámetros de scan fueron configurados → arrancamos el escaneo.

### ESP_GAP_BLE_SCAN_RESULT_EVT
Llegó un resultado. Dentro tiene un sub-evento:
- `ESP_GAP_SEARCH_INQ_RES_EVT` → un dispositivo fue detectado
- `ESP_GAP_SEARCH_INQ_CMPL_EVT` → terminó el tiempo de escaneo

### Cómo extraer el nombre del advertising

```c
// Intentar nombre completo
uint8_t *adv_name = esp_ble_resolve_adv_data(
    param->scan_rst.ble_adv,
    ESP_BLE_AD_TYPE_NAME_CMPL,
    &adv_name_len
);

// Si no hay, intentar nombre corto
if (adv_name == NULL) {
    adv_name = esp_ble_resolve_adv_data(
        param->scan_rst.ble_adv,
        ESP_BLE_AD_TYPE_NAME_SHORT,
        &adv_name_len
    );
}
```

---

## 9. Paso 5: Almacenar dispositivos sin duplicados

Comparamos por **MAC address** porque:
- El nombre puede repetirse (varios "ESP32")
- El nombre puede estar vacío
- La MAC es única por dispositivo

```c
// Buscar si ya tenemos este dispositivo
static bool device_exists(const char *mac) {
    for (int i = 0; i < device_count; i++) {
        if (strcmp(devices[i].mac, mac) == 0) {
            return true;
        }
    }
    return false;
}
```

---

## 10. Diagrama: Estructura del paquete advertising

```
┌────────────────────────────────────────────────────────────────────┐
│           PAQUETE DE ADVERTISING (máximo 31 bytes)                 │
│                                                                    │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │ Campo 1        │ Campo 2         │ Campo 3                   │  │
│  │ [len][tipo]    │ [len][tipo]     │ [len][tipo]               │  │
│  │ [datos...]     │ [datos...]      │ [datos...]                │  │
│  └──────────────────────────────────────────────────────────────┘  │
│                                                                    │
│  Ejemplo real:                                                     │
│                                                                    │
│  02 01 06                                                          │
│  │  │  └── dato: General Discoverable + BR/EDR Not Supported       │
│  │  └───── tipo: 0x01 = Flags                                      │
│  └──────── longitud: 2 bytes siguen                                │
│                                                                    │
│  09 09 4D 69 42 61 6E 64 37                                        │
│  │  │  └── datos: "MiBand7" (ASCII)                                │
│  │  └───── tipo: 0x09 = Complete Local Name                        │
│  └──────── longitud: 9 bytes siguen                                │
│                                                                    │
│  Tipos AD más comunes:                                             │
│  0x01 = Flags                                                      │
│  0x09 = Complete Local Name                                        │
│  0x08 = Shortened Local Name                                       │
│  0x03 = Complete List of 16-bit Service UUIDs                      │
│  0xFF = Manufacturer Specific Data                                 │
│  0x16 = Service Data                                               │
└────────────────────────────────────────────────────────────────────┘
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

static const char *TAG = "BLE_SCANNER";

// ─── Configuración ────────────────────────────────────────────
#define MAX_DEVICES    50
#define SCAN_DURATION  30   // segundos

// ─── Estructura para almacenar dispositivos ───────────────────
typedef struct {
    char name[64];
    char mac[18];    // "AA:BB:CC:DD:EE:FF" = 17 chars + \0
    int  rssi;
} bl_device_t;

static bl_device_t devices[MAX_DEVICES];
static int device_count = 0;

// ─── Parámetros de escaneo ────────────────────────────────────
static esp_ble_scan_params_t ble_scan_params = {
    .scan_type          = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval      = 0x50,   // 50 ms
    .scan_window        = 0x30,   // 30 ms
    .scan_duplicate     = BLE_SCAN_DUPLICATE_DISABLE
};

// ─── Verificar si un dispositivo ya existe (por MAC) ──────────
static bool device_exists(const char *mac)
{
    for (int i = 0; i < device_count; i++) {
        if (strcmp(devices[i].mac, mac) == 0) {
            return true;
        }
    }
    return false;
}

// ─── Agregar dispositivo a la lista ───────────────────────────
static void add_device(const char *name, const char *mac, int rssi)
{
    if (device_count >= MAX_DEVICES) return;
    if (device_exists(mac)) return;

    bl_device_t *dev = &devices[device_count];
    strncpy(dev->name, name, sizeof(dev->name) - 1);
    dev->name[sizeof(dev->name) - 1] = '\0';
    strncpy(dev->mac, mac, sizeof(dev->mac) - 1);
    dev->mac[sizeof(dev->mac) - 1] = '\0';
    dev->rssi = rssi;

    device_count++;
    ESP_LOGI(TAG, "[%d] Nuevo: %-20s | %s | %d dBm", device_count, name, mac, rssi);
}

// ─── Imprimir tabla de dispositivos ───────────────────────────
static void print_devices(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║              DISPOSITIVOS BLE ENCONTRADOS: %-3d                  ║", device_count);
    ESP_LOGI(TAG, "╠════╦══════════════════════╦═══════════════════╦═══════════════════╣");
    ESP_LOGI(TAG, "║  # ║ Nombre               ║ MAC               ║ RSSI             ║");
    ESP_LOGI(TAG, "╠════╬══════════════════════╬═══════════════════╬═══════════════════╣");

    for (int i = 0; i < device_count; i++) {
        ESP_LOGI(TAG, "║ %2d ║ %-20s ║ %s ║ %4d dBm         ║",
                 i + 1,
                 devices[i].name,
                 devices[i].mac,
                 devices[i].rssi);
    }

    ESP_LOGI(TAG, "╚════╩══════════════════════╩═══════════════════╩═══════════════════╝");
}

// ─── Callback GAP ─────────────────────────────────────────────
static void gap_event_handler(esp_gap_ble_cb_event_t event,
                               esp_ble_gap_cb_param_t *param)
{
    switch (event) {

    // Parámetros de scan configurados → iniciar escaneo
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        if (param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Parametros configurados. Escaneando %d segundos...", SCAN_DURATION);
            esp_ble_gap_start_scanning(SCAN_DURATION);
        } else {
            ESP_LOGE(TAG, "Error configurando parametros de escaneo");
        }
        break;

    // Resultado del escaneo
    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        esp_ble_gap_cb_param_t *scan_result = param;

        switch (scan_result->scan_rst.search_evt) {

        // ── Se detectó un dispositivo ──
        case ESP_GAP_SEARCH_INQ_RES_EVT: {

            // 1. Extraer nombre (completo o corto)
            uint8_t adv_name_len = 0;
            uint8_t *adv_name = esp_ble_resolve_adv_data(
                scan_result->scan_rst.ble_adv,
                ESP_BLE_AD_TYPE_NAME_CMPL,
                &adv_name_len
            );

            if (adv_name == NULL) {
                adv_name = esp_ble_resolve_adv_data(
                    scan_result->scan_rst.ble_adv,
                    ESP_BLE_AD_TYPE_NAME_SHORT,
                    &adv_name_len
                );
            }

            // 2. Copiar nombre a string
            char name_str[64] = "(desconocido)";
            if (adv_name != NULL && adv_name_len > 0) {
                int copy_len = (adv_name_len < sizeof(name_str) - 1)
                               ? adv_name_len : sizeof(name_str) - 1;
                memcpy(name_str, adv_name, copy_len);
                name_str[copy_len] = '\0';
            }

            // 3. Formatear MAC
            char mac_str[18];
            snprintf(mac_str, sizeof(mac_str),
                     "%02X:%02X:%02X:%02X:%02X:%02X",
                     scan_result->scan_rst.bda[0],
                     scan_result->scan_rst.bda[1],
                     scan_result->scan_rst.bda[2],
                     scan_result->scan_rst.bda[3],
                     scan_result->scan_rst.bda[4],
                     scan_result->scan_rst.bda[5]);

            // 4. Agregar si no existe
            add_device(name_str, mac_str, scan_result->scan_rst.rssi);

            break;
        }

        // ── Escaneo terminado ──
        case ESP_GAP_SEARCH_INQ_CMPL_EVT:
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "════ Escaneo completado ════");
            print_devices();
            break;

        default:
            break;
        }
        break;
    }

    default:
        break;
    }
}

// ─── Inicialización BLE ───────────────────────────────────────
static void init_ble(void)
{
    esp_err_t ret;

    // 1. NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Liberar memoria de BT clásico
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    // 3. Controlador BT
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

    // 4. Bluedroid
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    // 5. Callback GAP
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));

    // 6. Configurar scan (dispara callback cuando esté listo)
    ESP_ERROR_CHECK(esp_ble_gap_set_scan_params(&ble_scan_params));

    ESP_LOGI(TAG, "BLE Scanner inicializado");
}

// ─── Main ─────────────────────────────────────────────────────
void app_main(void)
{
    ESP_LOGI(TAG, "=== BLE Scanner ESP32 ===");
    init_ble();
}
```

---

## 12. Qué se va a ver en el monitor serie

```
I (325) BLE_SCANNER: === BLE Scanner ESP32 ===
I (335) BLE_SCANNER: BLE Scanner inicializado
I (345) BLE_SCANNER: Parametros configurados. Escaneando 30 segundos...
I (412) BLE_SCANNER: [1] Nuevo: Mi Band 7            | AA:BB:CC:DD:EE:FF | -45 dBm
I (415) BLE_SCANNER: [2] Nuevo: (desconocido)        | 11:22:33:44:55:66 | -72 dBm
I (523) BLE_SCANNER: [3] Nuevo: TV Samsung           | 77:88:99:AA:BB:CC | -68 dBm
I (891) BLE_SCANNER: [4] Nuevo: ESP32_GATTS_DEMO     | DE:AD:BE:EF:00:01 | -55 dBm
I (1205) BLE_SCANNER: [5] Nuevo: (desconocido)       | 12:34:56:78:9A:BC | -83 dBm
...
I (30350) BLE_SCANNER:
I (30350) BLE_SCANNER: ════ Escaneo completado ════
I (30350) BLE_SCANNER:
I (30350) BLE_SCANNER: ╔══════════════════════════════════════════════════════════════════╗
I (30350) BLE_SCANNER: ║              DISPOSITIVOS BLE ENCONTRADOS: 5                    ║
I (30350) BLE_SCANNER: ╠════╦══════════════════════╦═══════════════════╦═══════════════════╣
I (30350) BLE_SCANNER: ║  # ║ Nombre               ║ MAC               ║ RSSI             ║
I (30350) BLE_SCANNER: ╠════╬══════════════════════╬═══════════════════╬═══════════════════╣
I (30350) BLE_SCANNER: ║  1 ║ Mi Band 7            ║ AA:BB:CC:DD:EE:FF ║  -45 dBm         ║
I (30350) BLE_SCANNER: ║  2 ║ (desconocido)        ║ 11:22:33:44:55:66 ║  -72 dBm         ║
I (30350) BLE_SCANNER: ║  3 ║ TV Samsung           ║ 77:88:99:AA:BB:CC ║  -68 dBm         ║
I (30350) BLE_SCANNER: ║  4 ║ ESP32_GATTS_DEMO     ║ DE:AD:BE:EF:00:01 ║  -55 dBm         ║
I (30350) BLE_SCANNER: ║  5 ║ (desconocido)        ║ 12:34:56:78:9A:BC ║  -83 dBm         ║
I (30350) BLE_SCANNER: ╚════╩══════════════════════╩═══════════════════╩═══════════════════╝
```

---

## 13. Errores comunes

| Error | Causa | Solución |
|-------|-------|----------|
| `esp_bt.h: No such file` | Bluetooth no habilitado en sdkconfig | Borrar `sdkconfig.esp32dev`, hacer `pio run -t clean` |
| No encuentra dispositivos | `scan_window` o `scan_interval` muy bajos | Usar valores por defecto (0x50/0x30) |
| Todos aparecen como "(desconocido)" | Scan pasivo o dispositivos no publican nombre | Usar `BLE_SCAN_TYPE_ACTIVE` |
| No compila con `framework = arduino` | Este tutorial es para ESP-IDF | Usar `framework = espidf` en platformio.ini |

---


## 14. Referencias

- ESP-IDF GAP BLE API: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/bluetooth/esp_gap_ble.html
- BLE device discovery guide: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/ble/get-started/ble-device-discovery.html
- Ejemplo iBeacon (incluye scanner): https://github.com/espressif/esp-idf/tree/master/examples/bluetooth/bluedroid/ble/ble_ibeacon
