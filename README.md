# ZeroVolts Firmware

Firmware para ESP32 enfocado en aprender Bluetooth (BLE y Classic) desde cero, utilizando el stack Bluedroid de ESP-IDF. El proyecto avanza a traves de tutoriales progresivos: desde fundamentos teoricos hasta escaneo de dispositivos, enumeracion de servicios GATT, seguridad y emulacion.

## Requisitos

- ESP32 DevKit v1 (o compatible)
- Cable USB micro/tipo-C
- VS Code
- Extension PlatformIO IDE

## Configuracion del entorno

### 1. Instalar VS Code y PlatformIO

1. Instalar [VS Code](https://code.visualstudio.com/)
2. Abrir VS Code, ir a Extensions (`Ctrl+Shift+X`) y buscar **PlatformIO IDE**
3. Instalar la extension y reiniciar VS Code
4. Esperar a que PlatformIO termine su instalacion inicial (puede tomar unos minutos la primera vez)

### 2. Abrir el proyecto

1. `File > Open Folder` y seleccionar la carpeta `zerovolts_firmware`
2. PlatformIO detectara automaticamente el `platformio.ini` y configurara el entorno
3. La primera vez descargara el toolchain de ESP32 y el framework ESP-IDF (~1-2 GB)

### 3. Compilar y flashear

Desde la barra inferior de VS Code (barra de PlatformIO):

- **Build** (checkmark icon): Compila el proyecto
- **Upload** (flecha icon): Compila y flashea al ESP32
- **Monitor** (enchufe icon): Abre el serial monitor a 115200 baud

O desde la terminal:

```bash
# Compilar
pio run

# Compilar y flashear
pio run --target upload

# Monitor serial
pio device monitor
```

### 4. Estructura del proyecto

```
zerovolts_firmware/
├── include/                      # Headers publicos
│   ├── zv_bluetooth.h                # Registro de handlers BLE (GAP + GATTC)
│   ├── zv_bluetooth_device.h         # Struct y manejo de dispositivos descubiertos
│   ├── zv_bt_gap.h                   # Capa GAP: escaneo, parsing de advertising data
│   ├── zv_bt_gattc.h                 # Capa GATT Client: conexion y discovery de servicios
│   └── zv_format_utils.h            # Utilidades de formateo (MAC, UUID)
├── src/                          # Codigo fuente (lo unico que se compila)
│   ├── main.c                        # Punto de entrada: init NVS, controller, bluedroid
│   ├── zv_bluetooth.c                # Registro de callbacks y scan params (orquestador)
│   ├── zv_bluetooth_device.c         # Almacen de dispositivos (array estatico, dedup por MAC)
│   ├── zv_bt_gap.c                   # Handler GAP: escaneo, base de firmas de fabricantes
│   ├── zv_bt_gattc.c                 # Handler GATTC: MTU, services, characteristics
│   └── zv_format_utils.c            # Formateo de MAC (XX:XX:...) y UUID (16/32/128 bit)
├── docs/                         # Tutoriales (no se compilan)
├── platformio.ini                # Configuracion de PlatformIO
└── sdkconfig.default             # Configuracion de Bluetooth para ESP-IDF
```

### 5. Arquitectura del firmware

```
┌─────────────────────────────────────────────────────────┐
│                      main.c                             │
│         NVS + BT Controller + Bluedroid init            │
│                        │                                │
│                zv_init_ble_handlers()                    │
└────────────────────────┬────────────────────────────────┘
                         │
            ┌────────────┴────────────┐
            │    zv_bluetooth.c       │
            │  register callbacks     │
            │  set scan params        │
            │  set local MTU          │
            │  register GATTC app     │
            └─────┬─────────────┬─────┘
                  │             │
     ┌────────────┘             └────────────┐
     │                                       │
     ▼                                       ▼
┌─────────────────────┐         ┌────────────────────────┐
│   zv_bt_gap.c       │         │   zv_bt_gattc.c        │
│                     │         │                        │
│ GAP event handler   │         │ GATTC event handler    │
│ - start scan        │         │ - store gattc_if       │
│ - parse adv data    │         │ - MTU negotiation      │
│ - manufacturer DB   │         │ - service discovery    │
│ - appearance map    │         │ - char enumeration     │
│ - service UUID map  │         │ - properties parsing   │
│                     │  open   │                        │
│ scan complete ──────┼────────>│                        │
└────────┬────────────┘         └────────────────────────┘
         │
         ▼
┌─────────────────────┐         ┌────────────────────────┐
│zv_bluetooth_device.c│         │  zv_format_utils.c     │
│                     │         │                        │
│ device array (x35)  │         │ zv_format_mac_address()│
│ add / dedup by MAC  │         │ zv_format_uuid()       │
│ get closest (RSSI)  │         │  (16/32/128 bit)       │
│ get by name         │         │                        │
│ print device list   │         │                        │
└─────────────────────┘         └────────────────────────┘
```

## Tutoriales

| # | Tema | Documento |
|---|------|-----------|
| 00 | Fundamentos de Bluetooth y BLE | [00_fundamentos.md](docs/00_fundamentos.md) |
| 01 | BLE Scanner - Descubrimiento de dispositivos | [01_ble_Scanner.md](docs/01_ble_Scanner.md) |
| 02 | GATT Client - Enumeracion de servicios y caracteristicas | [02_gat_service_enumeration.md](docs/02_gat_service_enumeration.md) |
