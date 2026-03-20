# Fundamentos de Bluetooth y BLE


## 1. Objetivos

Antes de escribir una sola línea de código, necesitamos entender:

- Qué diferencia a **Bluetooth Classic** de **BLE**
- Qué hacen **GAP** y **GATT**
- Qué es un **servicio**, una **característica** y un **descriptor**
- Qué es un **beacon**
- Cómo se relaciona todo esto con **Bluedroid** en ESP32

---

## 2. Bluetooth Classic vs BLE

Son dos tecnologías **diferentes** dentro de la misma familia Bluetooth.
Referencia: [Bluetooth Core Specification](https://www.bluetooth.com/specifications/specs/core-specification/) | [ESP-IDF Bluetooth Architecture](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/bt-architecture/index.html)

```
┌──────────────────────────────────────────────────────────────────────┐
│                    BLUETOOTH CLASSIC vs BLE                          │
├──────────────────────┬───────────────────────────────────────────────┤
│                      │                                               │
│  BLUETOOTH CLASSIC   │   BLE (Bluetooth Low Energy)                  │
│                      │                                               │
│  Diseñado para:      │   Diseñado para:                              │
│  - Audio continuo    │   - Sensores                                  │
│  - Streaming         │   - Beacons                                   │
│  - Puerto serie      │   - Wearables                                 │
│  - Archivos          │   - IoT                                       │
│                      │                                               │
│  Consumo: ALTO       │   Consumo: MUY BAJO                           │
│                      │                                               │
│  Datos: continuos    │   Datos: ráfagas cortas                       │
│  y abundantes        │   y esporádicas                               │
│                      │                                               │
│  Ejemplos:           │   Ejemplos:                                   │
│  - Auriculares       │   - Pulsera fitness                           │
│  - Parlantes         │   - Sensor de temperatura                     │
│  - Manos libres      │   - Cerradura inteligente                     │
│  - Transferencia     │   - Baliza/beacon                             │
│    de archivos       │   - Control remoto simple                     │
│                      │                                               │
│  Perfiles: A2DP,     │   Protocolo: GATT + GAP                       │
│  SPP, HFP, AVRCP     │                                               │
│                      │                                               │
└──────────────────────┴───────────────────────────────────────────────┘
```

**Regla simple para recordar:**
- **Classic** = audio, streaming, datos continuos
- **BLE** = sensores, beacons, datos pequeños y esporádicos

---

## 3. El stack BLE: capas de la arquitectura

Cuando programas BLE en ESP32, el código habla con un **stack** (pila de software) que tiene varias capas.
Referencia: [ESP-IDF BLE Overview](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/ble/index.html) | [ESP-IDF Bluetooth Architecture](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/bt-architecture/index.html)

```
┌──────────────────────────────────────────────────────────────────────┐
│                                                                      │
│   ┌──────────────────────────────────────────────────────────────┐   │
│   │                    CÓDIGO (app_main)                         │   │
│   │                                                              │   │
│   │   - Decides qué hacer cuando llega un evento                 │   │
│   │   - Lees datos, escribes datos, escaneas, haces advertising  │   │
│   └──────────────────────────┬───────────────────────────────────┘   │
│                              │ callbacks                             │
│   ┌──────────────────────────▼───────────────────────────────────┐   │
│   │                    HOST (Bluedroid)                          │   │
│   │                                                              │   │
│   │   ┌────────────┐    ┌─────────────┐    ┌──────────────┐      │   │
│   │   │    GAP     │    │    GATT     │    │  Seguridad   │      │   │
│   │   │            │    │             │    │  (SMP)       │      │   │
│   │   │ Escaneo    │    │ Servicios   │    │ Pairing      │      │   │
│   │   │ Advertising│    │ Caract.     │    │ Bonding      │      │   │
│   │   │ Conexión   │    │ Descriptores│    │ Cifrado      │      │   │
│   │   └────────────┘    └─────────────┘    └──────────────┘      │   │
│   │                                                              │   │
│   └──────────────────────────┬───────────────────────────────────┘   │
│                              │ HCI                                   │
│   ┌──────────────────────────▼───────────────────────────────────┐   │
│   │                    CONTROLLER (hardware)                     │   │
│   │                                                              │   │
│   │   - Maneja la radio Bluetooth del chip ESP32                 │   │
│   │   - Link Layer, modulación, frecuencias                      │   │
│   │   - No lo tocas directamente desde tu código                 │   │
│   └──────────────────────────────────────────────────────────────┘   │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

**Lo que importa:**
- El **Controller** es hardware — no se toca directamente
- **Bluedroid** es el HOST — ahí viven GAP, GATT y seguridad
- **Tu código** reacciona a eventos que Bluedroid te envía por callbacks

---

## 4. ¿Qué es GAP? (Generic Access Profile)

GAP responde a la pregunta: **¿Cómo se encuentran y se conectan los dispositivos?**
Referencia: [ESP-IDF GAP API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/bluetooth/esp_gap_ble.html) | [BLE Device Discovery](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/ble/get-started/ble-device-discovery.html)

```
┌──────────────────────────────────────────────────────────────────────┐
│                           GAP                                        │
│                                                                      │
│   GAP controla:                                                      │
│                                                                      │
│   ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                  │
│   │ ADVERTISING │  │  SCANNING   │  │  CONEXIÓN   │                  │
│   │             │  │             │  │             │                  │
│   │ "¡Aquí      │  │ "¿Quién     │  │ "Conectar   │                  │
│   │  estoy!"    │  │  hay?"      │  │  con..."    │                  │
│   └──────┬──────┘  └──────┬──────┘  └──────┬──────┘                  │
│          │                │                │                         │
│          ▼                ▼                ▼                         │
│   ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                  │
│   │  PAIRING    │  │  BONDING    │  │   RSSI      │                  │
│   │             │  │             │  │             │                  │
│   │ Autenticar  │  │ Guardar     │  │ Intensidad  │                  │
│   │ dispositivo │  │ claves      │  │ de señal    │                  │
│   └─────────────┘  └─────────────┘  └─────────────┘                  │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

**Analogía:** GAP es como el **portero** de un edificio — controla quién entra, cómo se identifica y cómo se conecta.

---

## 5. Advertising y Scanning: cómo se encuentran

El mecanismo fundamental de BLE es el **advertising**.
Referencia: [ESP-IDF BLE Device Discovery](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/ble/get-started/ble-device-discovery.html) | [Assigned Numbers - AD Types](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Assigned_Numbers/out/en/Assigned_Numbers.pdf)

```
                    ADVERTISING Y SCANNING

    Periférico BLE                         Observadores
    (ej: sensor)                           (ej: ESP32, teléfono)

    ┌──────────┐          paquete adv       ┌──────────┐
    │          │  ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ►   │          │
    │  SENSOR  │     "Soy 'TempSensor'"     │  ESP32   │
    │   BLE    │     "MAC: AA:BB:CC:..."    │ SCANNER  │
    │          │     "RSSI: -45 dBm"        │          │
    │          │  ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ►   │          │
    └──────────┘                            └──────────┘
         │              paquete adv
         │  ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ►    ┌──────────┐
         │                                   │          │
         │                                   │ TELÉFONO │
         │  ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ►    │          │
         │                                   └──────────┘
         │              paquete adv
         │  ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ►    ┌──────────┐
         │                                   │  OTRO    │
         └ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─►    │  ESP32   │
                                             └──────────┘

    El periférico grita periódicamente:
    "¡EXISTO! Me llamo X, mi MAC es Y"

    Los observadores escuchan y deciden si les interesa.
```

Un **paquete de advertising** contiene:

```
┌────────────────────────────────────────────────────────────────┐
│              PAQUETE DE ADVERTISING (max 31 bytes)             │
├────────┬────────┬──────────────────┬───────────────────────────┤
│ Flags  │ Nombre │  UUIDs de        │  Manufacturer Data        │
│ (tipo) │ (si    │  servicios       │  (datos del fabricante)   │
│        │  cabe) │  (si los tiene)  │                           │
├────────┴────────┴──────────────────┴───────────────────────────┤
│                                                                │
│  Cada campo tiene:  [longitud] [tipo AD] [datos...]            │
│                                                                │
│  Ejemplo:  02 01 06  = Flags: General Discoverable             │
│            09 09 4D 69 42 61 6E 64 37 = Nombre: "MiBand7"      │
│                                                                │
└────────────────────────────────────────────────────────────────┘
```

---

## 6. ¿Qué es GATT? (Generic Attribute Profile)

Si GAP es "cómo se encuentran", GATT es **"cómo se organizan y se intercambian los datos"**.
Referencia: [ESP-IDF GATT Server API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/bluetooth/esp_gatts.html) | [ESP-IDF GATT Client API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/bluetooth/esp_gattc.html) | [GATT Services (Bluetooth SIG)](https://www.bluetooth.com/specifications/specs/?types=specs-docs&keyword=GATT&filter=)

GATT solo funciona **después de que hay una conexión** (o en algunos casos especiales con advertising extendido).

```
┌───────────────────────────────────────────────────────────────────┐
│                                                                   │
│   GATT organiza los datos como una JERARQUÍA:                     │
│                                                                   │
│   ┌─── Dispositivo BLE ─────────────────────────────────────────┐ │
│   │                                                             │ │
│   │   ┌─── Servicio: Batería (UUID: 0x180F) ───────────────┐    │ │
│   │   │                                                    │    │ │
│   │   │   ┌─── Característica: Nivel (UUID: 0x2A19) ────┐  │    │ │
│   │   │   │    Propiedades: READ, NOTIFY                │  │    │ │
│   │   │   │    Valor: 87 (porcentaje)                   │  │    │ │
│   │   │   │                                             │  │    │ │
│   │   │   │    └─── Descriptor: CCCD (UUID: 0x2902)     │  │    │ │
│   │   │   │         Valor: 0x0001 (notify activado)     │  │    │ │
│   │   │   └─────────────────────────────────────────────┘  │    │ │
│   │   └────────────────────────────────────────────────────┘    │ │
│   │                                                             │ │
│   │   ┌─── Servicio: Temp. Ambiental (UUID: 0x181A) ─────────┐  │ │
│   │   │                                                      │  │ │
│   │   │   ┌─── Característica: Temperatura (UUID: 0x2A6E)─┐  │  │ │
│   │   │   │    Propiedades: READ, NOTIFY                  │  │  │ │
│   │   │   │    Valor: 2350 (23.50 °C)                     │  │  │ │
│   │   │   │                                               │  │  │ │
│   │   │   │    └─── Descriptor: CCCD (UUID: 0x2902)       │  │  │ │
│   │   │   │         Valor: 0x0000 (notify desactivado)    │  │  │ │
│   │   │   └───────────────────────────────────────────────┘  │  │ │
│   │   │                                                      │  │ │
│   │   │   ┌─── Característica: Humedad (UUID: 0x2A6F) ────┐  │  │ │
│   │   │   │    Propiedades: READ                          │  │  │ │
│   │   │   │    Valor: 6500 (65.00 %)                      │  │  │ │
│   │   │   └───────────────────────────────────────────────┘  │  │ │
│   │   └──────────────────────────────────────────────────────┘  │ │
│   │                                                             │ │
│   └─────────────────────────────────────────────────────────────┘ │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
```

### Resumen rápido:

| Concepto | Qué es | Analogía |
|----------|--------|----------|
| **Servicio** | Agrupador de funcionalidad | Una **carpeta** |
| **Característica** | El dato o comportamiento principal | Un **archivo** dentro de la carpeta |
| **Descriptor** | Metadato o configuración extra | Las **propiedades** del archivo |

### El descriptor más importante: CCCD

El **CCCD** (Client Characteristic Configuration Descriptor, UUID `0x2902`) es el "interruptor" que un cliente escribe para activar o desactivar notificaciones.
Referencia: [Bluetooth SIG - Assigned UUIDs](https://www.bluetooth.com/specifications/assigned-numbers/)

```
    Cliente (teléfono)                     Servidor (ESP32)

    Escribe CCCD = 0x0001
    ─────────────────────────────────────►  "Ok, activo notify"

                                            Cada 2 segundos:
    ◄─────────────────────────────────────  Temp = 23.50°C
    ◄─────────────────────────────────────  Temp = 23.65°C
    ◄─────────────────────────────────────  Temp = 23.40°C

    Escribe CCCD = 0x0000
    ─────────────────────────────────────►  "Ok, desactivo notify"

                                            (silencio)
```

---

## 7. GATT Client vs GATT Server

Referencia: [GATT Server Walkthrough](https://github.com/espressif/esp-idf/blob/v6.0/examples/bluetooth/bluedroid/ble/gatt_server/tutorial/Gatt_Server_Example_Walkthrough.md) | [GATT Client Walkthrough](https://github.com/espressif/esp-idf/blob/v6.0/examples/bluetooth/bluedroid/ble/gatt_client/tutorial/Gatt_Client_Example_Walkthrough.md)

```
┌──────────────────────────────────────────────────────────────────────┐
│                                                                      │
│   GATT SERVER (expone datos)          GATT CLIENT (consume datos)    │
│                                                                      │
│   ┌──────────────┐                    ┌──────────────┐               │
│   │              │                    │              │               │
│   │   ESP32      │ ◄──── conexión ──► │   Teléfono   │               │
│   │   Sensor     │                    │   con app    │               │
│   │              │                    │   BLE        │               │
│   │ Tiene:       │                    │              │               │
│   │ - Servicios  │  ◄── READ ──────── │ Pide:        │               │
│   │ - Caract.    │  ── NOTIFY ─────►  │ - Leer datos │               │
│   │ - Valores    │  ◄── WRITE ──────  │ - Escribir   │               │
│   │              │                    │ - Suscribirse│               │
│   └──────────────┘                    └──────────────┘               │
│                                                                      │
│   Ejemplos de Server:                 Ejemplos de Client:            │
│   - Sensor BLE                        - App móvil (nRF Connect)      │
│   - Pulsera fitness                   - Otro ESP32 escaneando        │
│   - ESP32 emulando dispositivo        - Computadora con BLE          │
│                                                                      │
│   REGLA: El Server OFRECE la base     El Client la CONSULTA          │
│          de datos                      o la MODIFICA                 │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

---

## 8. ¿Qué es un Beacon?

Un beacon es un dispositivo BLE que **solo emite advertising** periódicamente, sin necesidad de conexión.
Referencia: [ESP-IDF iBeacon Example](https://github.com/espressif/esp-idf/tree/master/examples/bluetooth/bluedroid/ble/ble_ibeacon) | [iBeacon Spec (Apple)](https://developer.apple.com/ibeacon/) | [Eddystone Spec (Google)](https://github.com/google/eddystone)

```
┌──────────────────────────────────────────────────────────────────────┐
│                                                                      │
│   BEACON BLE                                                         │
│                                                                      │
│   ┌──────────┐          ))))))          ┌──────────┐                 │
│   │          │        ))))))))          │          │                 │
│   │  BEACON  │ ───► ))))))))))  ◄───── │ Receptor │                  │
│   │  ESP32   │        ))))))))          │ (pasivo) │                 │
│   │          │          ))))))          │          │                 │
│   └──────────┘                          └──────────┘                 │
│                                                                      │
│   El beacon repite constantemente:                                   │
│   "Soy beacon X, UUID: abc123, Major: 1, Minor: 42"                  │
│                                                                      │
│   NO hay conexión GATT.                                              │
│   NO hay servicios ni características.                               │
│   Solo advertising periódico.                                        │
│                                                                      │
│   ┌─────────────────────────────────────────────────────────────┐    │
│   │ Formatos comunes:                                           │    │
│   │                                                             │    │
│   │  iBeacon (Apple)          Eddystone (Google)                │    │
│   │  - UUID + Major + Minor   - UID: identificador              │    │
│   │  - TX Power               - URL: transmite una URL          │    │
│   │  - Manufacturer Data      - TLM: telemetría                 │    │
│   │                           - Service Data                    │    │
│   └─────────────────────────────────────────────────────────────┘    │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

### Beacon vs Periférico conectable

```
    BEACON                              PERIFÉRICO CONECTABLE

    Solo emite advertising              Emite advertising Y acepta conexión

    ████ ░░ ████ ░░ ████ ░░ ████       ████ ░░ ████ ── CONECTADO ──────►
    adv     adv     adv     adv        adv     adv    (GATT activo)

    No tiene GATT                       Tiene servicios GATT
    No acepta conexión                  Acepta conexión
    Ideal para: localización,           Ideal para: sensores, control,
    proximidad, identificación          lectura/escritura de datos
```

---

## 9. ¿Qué es RSSI?

**RSSI** = Received Signal Strength Indicator (intensidad de señal recibida).
Referencia: [Bluetooth Core Spec Vol 4, Part E, 7.5.4 - Read RSSI Command](https://www.bluetooth.com/specifications/specs/core-specification/)

```
    ┌─────────────────────────────────────────────────────────┐
    │                    ESCALA DE RSSI                        │
    │                                                         │
    │   -30 dBm ████████████████████████  Muy cerca (< 1m)   │
    │   -40 dBm ██████████████████████    Cerca               │
    │   -50 dBm ████████████████████      Buena señal         │
    │   -60 dBm ██████████████████        Normal              │
    │   -70 dBm ████████████████          Media               │
    │   -80 dBm ██████████████            Débil               │
    │   -90 dBm ████████████              Muy débil           │
    │  -100 dBm ██████████                Apenas detectable   │
    │                                                         │
    │   ⚠ NO es una medida exacta de distancia.              │
    │   Sirve para estimar proximidad y ordenar dispositivos. │
    └─────────────────────────────────────────────────────────┘
```

---

## 10. Bluedroid en ESP32: el papel del Host Stack

En ESP32, **Bluedroid** es el host stack que implementa GAP, GATT y seguridad.
Referencia: [ESP-IDF Bluetooth API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/bluetooth/index.html) | [esp_bt.h](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/bluetooth/controller_vhci.html) | [esp_bt_main.h](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/bluetooth/esp_bt_main.html)

```
┌──────────────────────────────────────────────────────────────────────┐
│                    INICIALIZACIÓN TÍPICA EN ESP-IDF                  │
│                                                                      │
│   Paso 1: nvs_flash_init()                                           │
│           └─► NVS guarda claves de bonding y config interna          │
│                                                                      │
│   Paso 2: esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT)      │
│           └─► Libera RAM del Bluetooth Clásico (solo usamos BLE)     │
│                                                                      │
│   Paso 3: esp_bt_controller_init(&bt_cfg)                            │
│           └─► Inicializa el controlador (capa baja / hardware)       │
│                                                                      │
│   Paso 4: esp_bt_controller_enable(ESP_BT_MODE_BLE)                  │
│           └─► Enciende el controlador en modo BLE                    │
│                                                                      │
│   Paso 5: esp_bluedroid_init()                                       │
│           └─► Inicializa Bluedroid (capa alta / host)                │
│                                                                      │
│   Paso 6: esp_bluedroid_enable()                                     │
│           └─► Activa Bluedroid para procesar eventos                 │
│                                                                      │
│   Paso 7: Registrar callbacks (GAP, GATTC o GATTS)                   │
│           └─► Tu código recibe eventos por estas funciones           │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

---

## 11. ¿Cómo funciona todo junto?

```
┌──────────────────────────────────────────────────────────────────────┐
│                    FLUJO COMPLETO BLE                                │
│                                                                      │
│                                                                      │
│   1. ADVERTISING              2. SCANNING                            │
│   ┌──────────┐                ┌──────────┐                           │
│   │ Periférico│ ── paquetes ──► │ Observer │                         │
│   │ BLE      │   advertising  │ (ESP32)  │                           │
│   └──────────┘                └────┬─────┘                           │
│        ▲                           │                                 │
│        │                    3. CONEXIÓN                              │
│        │                           │                                 │
│        └───────── enlace ◄─────────┘                                 │
│                                                                      │
│   4. GATT                                                            │
│   ┌──────────────────────────────────────────────┐                   │
│   │                                              │                   │
│   │  Client ──── descubrir servicios ────► Server│                   │
│   │  Client ──── leer característica ────► Server│                   │
│   │  Client ◄──── notificación ──────── Server   │                   │
│   │  Client ──── escribir valor ─────► Server    │                   │
│   │                                              │                   │
│   └──────────────────────────────────────────────┘                   │
│                                                                      │
│   5. SEGURIDAD (opcional)                                            │
│   ┌──────────────────────────────────────────────┐                   │
│   │  Pairing → intercambio de claves             │                   │
│   │  Bonding → guardar claves para reconectar    │                   │
│   │  Cifrado → comunicación encriptada           │                   │
│   └──────────────────────────────────────────────┘                   │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

---

## 13. Glosario rápido

Referencia general: [ESP-IDF BLE Getting Started](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/ble/get-started/ble-introduction.html) | [Bluetooth SIG Glossary](https://www.bluetooth.com/learn-about-bluetooth/key-attributes/range/)

| Término | Significado |
|---------|-------------|
| **GAP** | Generic Access Profile — descubrimiento, advertising, conexión |
| **GATT** | Generic Attribute Profile — organización e intercambio de datos |
| **Advertising** | Paquetes que un dispositivo BLE emite periódicamente para ser visible |
| **Scanning** | Proceso de escuchar paquetes de advertising |
| **Servicio** | Agrupador lógico de funcionalidad en GATT |
| **Característica** | Dato o comportamiento dentro de un servicio |
| **Descriptor** | Metadato de una característica (ej: CCCD para notify) |
| **CCCD** | Client Characteristic Configuration Descriptor — activa/desactiva notify |
| **UUID** | Identificador único (16 bits estándar o 128 bits custom) |
| **Handle** | Número interno que GATT usa para identificar cada atributo |
| **RSSI** | Intensidad de señal recibida (en dBm) |
| **Pairing** | Proceso de autenticación entre dos dispositivos |
| **Bonding** | Guardar claves de pairing para reconexión futura |
| **Beacon** | Dispositivo que solo emite advertising periódico |
| **Bluedroid** | Host stack BLE/BT Classic de ESP-IDF |
| **NimBLE** | Host stack alternativo (solo BLE, más ligero) |
| **NVS** | Non-Volatile Storage — memoria persistente del ESP32 |

---
