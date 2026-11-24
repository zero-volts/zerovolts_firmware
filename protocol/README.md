# Protocolo UART, catálogo JSON y header compartido

Esta es la referencia para entender la comunicación entre el firmware y sus clientes: Raspberry Pi, UI, consola u otra aplicación. Se trata de explicar el formato para poder comunicarse con cualquier otro tipo de cliente teniendo el codigo o sin el. 

- [Formato de comunicación UART](#formato-de-comunicación-uart)
- [Uso desde una terminal serie](#uso-desde-una-terminal-serie)
- [Pruebas desde la consola de la Raspberry Pi](#pruebas-desde-la-consola-de-la-raspberry-pi)
- [Comandos implementados](#comandos-implementados)
- [Ayuda consultable con HELP](#ayuda-consultable-con-help)
- [Resultados BLE](#resultados-ble)
- [Captura y carga de pulsos](#captura-y-carga-de-pulsos)
- [Catálogo JSON embebido y memoria](#catálogo-json-embebido-y-memoria)
- [Integración del header C compartido](#integración-del-header-c-compartido)

## Formato de comunicación UART

Los comandos son sensibles a mayúsculas. Cada comando ocupa una línea terminada en `\n`; se ignora `\r`. El primer `|` separa el comando de sus argumentos. Las respuestas terminan en `\r\n`.

El buffer de entrada admite **255 caracteres por línea**, sin contar los terminadores. Una línea demasiado larga se descarta hasta el siguiente `\n` y produce `ERR`. Los comandos desconocidos también producen `ERR`.

## Uso desde una terminal serie

1. Carga el firmware por USB.
2. Conecta el adaptador USB-UART a UART2 según la [tabla de conexiones](../README.md#uart-de-comandos): TX del adaptador a GPIO 16, RX a GPIO 17 y tierra común.
3. Abre el puerto del adaptador en una terminal serie a **115200 baud, 8N1 y sin control de flujo**. Configura el envío de líneas con `LF` o `CRLF`.
4. Envía `PING` seguido de Enter. El firmware responde `PONG`.
5. Envía `BLE_SCAN` para recibir los dispositivos BLE cercanos durante 15 segundos. Usa la MAC y el `addr_type` de un resultado para solicitar una conexión.

Por ejemplo, envía estas líneas individualmente, reemplazando la MAC y el tipo de dirección por los del dispositivo elegido:

```text
PING
BLE_SCAN
BLE_CONNECT|AA:BB:CC:DD:EE:FF|0
BLE_DISCONNECT
```

El firmware no envía un prompt. Las respuestas y los eventos asíncronos llegan como líneas por el mismo puerto. Una aplicación cliente debe procesarlos según sus prefijos y esperar las confirmaciones indicadas para las transferencias de pulsos.

## Pruebas desde la consola de la Raspberry Pi

Usa **dos terminales en la Raspberry**, locales o por SSH: una escucha las respuestas y otra envía los comandos. Los ejemplos usan `/dev/ttyAMA5`, el puerto configurado en `zerovolts-ui/app-config.json`. Si tu puerto es otro, reemplázalo en todos los comandos.

El ESP32 debe tener cargado el firmware actualizado y estar conectado por UART2 según la [tabla de conexiones](../README.md#uart-de-comandos). El monitor USB muestra logs; las órdenes de estos ejemplos se envían por los GPIO UART de la Raspberry.

### 1. Primera terminal: escuchar al ESP32

Si la UI está instalada como servicio, detenla para liberar el puerto:

```bash
sudo systemctl stop zerovolts-ui.service
```

Si la ejecutaste manualmente, ciérrala. Después configura el puerto y escucha:

```bash
sudo stty -F /dev/ttyAMA5 115200 raw -echo cs8 -parenb -cstopb -crtscts -ixon -ixoff
sudo cat /dev/ttyAMA5
```

**Deja esta terminal abierta.** Aquí aparecerán las respuestas. Los comandos siguientes se escriben en la segunda terminal.

### 2. Segunda terminal: comprobar comunicación y versión

Envía:

```bash
printf 'PING\n' | sudo tee /dev/ttyAMA5 >/dev/null
```

En la primera terminal debe aparecer:

```text
PONG
```

Si por alguna razon esta lamada da ERR y el firmware esta correctamente en el ESP32, reinicia el ESP32 apretando RESET y volver a probar.

### Qué hace cada parámetro al escuchar

`stty` configura cómo Linux trata los bytes del puerto; no envía una orden al firmware. En sus ajustes, el prefijo `-` desactiva una función. Los parámetros se aplican de izquierda a derecha.

| Parte | Qué hace y por qué se usa |
|---|---|
| `sudo` | Ejecuta el programa siguiente con permisos de administrador para acceder al dispositivo serie. |
| `stty` | Consulta o modifica la configuración de un dispositivo de terminal, incluido un puerto UART. |
| `-F /dev/ttyAMA5` | Aplica los ajustes al puerto indicado, en lugar de a la terminal donde escribes. `/dev/ttyAMA5` es el archivo de dispositivo que Linux usa para acceder a ese UART. |
| `115200` | Fija la velocidad de entrada y salida a 115200 baudios, igual que el ESP32. |
| `raw` | Desactiva el procesamiento habitual de una terminal: lectura por líneas, conversión de retornos de carro y saltos de línea, señales por caracteres de control y procesamiento de salida. Permite leer bytes sin esperar una línea completa. Incluye `min 1 time 0`: una lectura espera al menos un byte, sin timeout. |
| `-echo` | Evita que Linux retransmita automáticamente al ESP32 los bytes recibidos. Sin este ajuste podría devolver al firmware sus propias respuestas. |
| `cs8` | Selecciona 8 bits de datos por carácter. Es el **8** de 8N1. |
| `-parenb` | Desactiva la generación y el uso de un bit de paridad en las tramas. Es la **N** de 8N1: sin paridad. |
| `-cstopb` | Selecciona un bit de parada; `cstopb` sin el guion seleccionaría dos. Es el **1** de 8N1. |
| `-crtscts` | Desactiva el control de flujo por hardware RTS/CTS. El enlace utiliza TX, RX y GND, sin esas líneas adicionales. |
| `-ixon` | Evita que los caracteres XON/XOFF recibidos pausen o reanuden la transmisión de Linux. |
| `-ixoff` | Evita que Linux envíe XON/XOFF para pedir al otro extremo que pause o reanude el envío. |
| `cat /dev/ttyAMA5` | Lee los bytes que llegan por UART y los escribe en la salida estándar, que aquí es la pantalla de esta terminal. No envía comandos. |

`raw` ya desactiva `ixon` e `ixoff`; se repiten para dejar explícito que no usamos control de flujo por software. El eco se desactiva por separado con `-echo`.

En `sudo systemctl stop zerovolts-ui.service`, `systemctl` administra servicios de systemd, `stop` detiene el servicio y `zerovolts-ui.service` identifica la aplicación. Detenerla evita que la UI y `cat` compitan por leer los mismos bytes; no deshabilita su arranque futuro.

### Qué hace cada parte al enviar

Para el ejemplo `printf 'PING\n' | sudo tee /dev/ttyAMA5 >/dev/null`:

| Parte | Qué hace y por qué se usa |
|---|---|
| `printf` | Produce los bytes indicados por su cadena de formato. No añade un salto de línea por sí solo. |
| `'PING\n'` | Las comillas simples hacen que el shell entregue el texto a `printf` sin interpretarlo. `printf` convierte `\n` en un salto de línea LF, byte `0x0A`. Se envían los bytes de `PING` y ese terminador. |
| `\|` fuera de las comillas | Es una tubería del shell: conecta la salida de `printf` con la entrada del siguiente programa. No se transmite como parte del comando UART. |
| `sudo tee /dev/ttyAMA5` | Ejecuta `tee` con permisos de administrador. `tee` toma los bytes de su entrada y los copia tanto al puerto serie indicado como a su salida estándar. |
| `>` | Redirige la salida estándar de `tee` al destino que sigue. No redirige los mensajes de error. |
| `/dev/null` | Descarta lo que recibe. Aquí oculta la copia local de `PING` que imprime `tee`; no elimina los bytes enviados al UART ni las respuestas que muestra `cat` en la otra terminal. |

Usamos `sudo tee` porque es el programa que abre el puerto para escribir. En una expresión como `sudo printf 'PING\n' > /dev/ttyAMA5`, el shell abre el destino de `>` antes de ejecutar `sudo`; por eso esa alternativa puede fallar por permisos.

`PING`, `INFO` y `BLE_SCAN` son los comandos que entiende el firmware. `printf`, `sudo`, `tee` y las redirecciones pertenecen a Linux; el ESP32 no los recibe.

Cuando una orden UART contiene separadores `|`, deben permanecer dentro de las comillas:

```bash
printf 'BLE_CONNECT|AA:BB:CC:DD:EE:FF|0\n' | sudo tee /dev/ttyAMA5 >/dev/null
```

Los `|` dentro de las comillas se transmiten al ESP32 y separan los argumentos del protocolo. El `|` que está fuera conecta los programas de Linux. Sustituye la MAC y el `0` por la dirección y el `addr_type` recibidos en el escaneo. El salto de línea final permite al firmware reconocer que la orden está completa.


Consulta la versión:

```bash
printf 'INFO\n' | sudo tee /dev/ttyAMA5 >/dev/null
```

La versión actual del catálogo devuelve:

```text
INFO:OK|firmware=0.1.0
```

### 3. Consultar los comandos disponibles

```bash
printf 'CAPS\n' | sudo tee /dev/ttyAMA5 >/dev/null
```

Recibirás una línea `CAPS:COMMAND|name=...` por comando y, al terminar, `CAPS:DONE`. CAPS lista los handlers registrados; actualmente también incluye Sub-GHz aunque no se haya detectado el CC1101.

Para obtener el listado mediante HELP:

```bash
printf 'HELP\n' | sudo tee /dev/ttyAMA5 >/dev/null
```

Recibirás líneas `HELP:COMMAND|name=...` y una línea final `HELP:DONE`.

### 4. Consultar cómo se usa un comando

Usa `command=` y el nombre exacto del comando. Por ejemplo:

```bash
printf 'HELP|command=SUBGHZ_CAPTURE\n' | sudo tee /dev/ttyAMA5 >/dev/null
```

HELP describe `freq_hz` y `timeout_ms`, sus unidades y orden, las respuestas posibles y las notas de uso. Entre las líneas de respuesta aparecerán:

```text
HELP:RESPONSE|name=SUBGHZ_CAPTURE:DATA|ends_operation=0
HELP:RESPONSE|name=SUBGHZ_CAPTURE:DONE|ends_operation=1
```

`ends_operation=0` indica que ese mensaje no termina la captura. Debes seguir leyendo hasta un resultado terminal: `DONE`, `FAIL` o `ERROR`. La consulta de ayuda termina con `HELP:DONE`; consultar ayuda no inicia la captura.

### 5. Consultar qué contiene una respuesta

Si HELP anuncia una respuesta, consulta su `name` con **`message=`**:

```bash
printf 'HELP|message=SUBGHZ_CAPTURE:DATA\n' | sudo tee /dev/ttyAMA5 >/dev/null
```

Recibirás `HELP:MESSAGE`, una línea `HELP:FIELD` por campo (`capture_id`, `seq`, `chunks`, `count` y `timings`) y el cierre `HELP:DONE`.

Otro ejemplo para consultar la confirmación de un bloque de envío:

```bash
printf 'HELP|message=SUBGHZ_SEND:ACK\n' | sudo tee /dev/ttyAMA5 >/dev/null
```

**Regla práctica:** `command=` explica una orden que envías; `message=` explica una respuesta que recibes. Usa `command` y `message` en minúsculas, y los nombres de comandos y respuestas exactamente como aparecen en el listado. Envía cada consulta por separado y espera `HELP:DONE` o `HELP:ERROR`.

### 6. Ejecutar una operación: escaneo BLE

```bash
printf 'BLE_SCAN\n' | sudo tee /dev/ttyAMA5 >/dev/null
```

Recibirás `BLE_SCAN:START`, cero o más resultados `BLE_SCAN:DEVICE` y `BLE_SCAN:DONE`, aproximadamente después de 15 segundos. Espera a que termine antes de iniciar otra operación.

### 7. Terminar y volver a la UI

Pulsa `Ctrl+C` en la primera terminal para detener `cat`. Esto finaliza la escucha local; no cancela operaciones del ESP32. Si antes detuviste el servicio, vuelve a iniciarlo:

```bash
sudo systemctl start zerovolts-ui.service
```

Si ejecutabas la UI manualmente, vuelve a abrirla como lo hacías antes.

### Errores frecuentes

| Lo que ves | Qué revisar |
|---|---|
| `PING` responde pero INFO, CAPS o HELP devuelven `ERR` | Puede estar cargado un firmware anterior. Compilar el proyecto no actualiza por sí solo el ESP32; hay que cargar el binario. |
| `HELP:ERROR\|reason=unknown_command` al consultar `SUBGHZ_CAPTURE:DATA` | Es una respuesta; usa `HELP\|message=SUBGHZ_CAPTURE:DATA`. |
| `HELP:ERROR\|reason=invalid_arguments` | Usa solo un selector, `command=` o `message=`, en minúsculas y con un valor no vacío. |
| No aparece ninguna respuesta | Comprueba que `cat` siga abierto, que la UI esté cerrada, el puerto, el cableado y el `\n` final de `printf`. |

Para compilar y cargar la versión actual, ejecuta desde el equipo conectado al ESP32 por USB:

```bash
cd /home/zerovolts/git/zerovolts_firmware
pio run -e esp32dev --target upload
```

`cd` cambia al directorio del proyecto; modifica la ruta a tu equipo. `pio` es la CLI de PlatformIO, `run` ejecuta la compilacion, `-e esp32dev` selecciona ese entorno y `--target upload` compila y cargar el firmware. Después de una carga correcta, repite las consultas desde la Raspberry.


## Comandos implementados

| Comando | Respuesta o efecto |
|---|---|
| `PING` | `PONG` |
| `INFO` | `INFO:OK\|firmware=0.1.0`, leyendo la versión del JSON. Ante fallos del catálogo, `INFO:ERROR\|reason=...`. |
| `CAPS` | Una línea `CAPS:COMMAND\|name=<comando>` por cada comando registrado, seguida de `CAPS:DONE`. |
| `HELP`, `HELP\|command=<comando>` o `HELP\|message=<respuesta>` | Lista comandos o describe parámetros, respuestas y campos. Termina con `HELP:DONE` o `HELP:ERROR`. |
| `BLE_SCAN` | `BLE_SCAN:START`, resultados `BLE_SCAN:DEVICE` y `BLE_SCAN:DONE` tras 15 segundos. |
| `BLE_CONNECT\|AA:BB:CC:DD:EE:FF\|0` | `BLE_CONNECT:START`, seguido de `BLE_CONNECT:OK\|mac=...\|mtu=...` o `BLE_CONNECT:FAIL\|mac=...\|reason=...`. El último argumento es el tipo de dirección BLE del resultado de escaneo. |
| `BLE_DISCONNECT` | Solicita cerrar la conexión activa; el evento de cierre solicitado genera `BLE_DISCONNECT:OK\|mac=...`. Una pérdida de conexión genera `BLE_CONNECT:LOST\|mac=...\|reason=...`. |
| `SUBGHZ_CAPTURE\|freq_hz=433920000\|timeout_ms=30000` | Inicia captura; responde con `START`, bloques `DATA` y `DONE`, o `FAIL`/`SUBGHZ_CAPTURE:ERROR`. Mantener los campos en ese orden. |
| `SUBGHZ_SEND:BEGIN\|id=1\|freq_hz=433920000\|repeats=3\|count=4\|chunks=1` | Inicia carga de pulsos; responde `SUBGHZ_SEND:BEGIN_OK\|id=1`. |
| `SUBGHZ_SEND:DATA\|id=1\|seq=0\|count=4\|timings=+350,-1050,+1050,-350` | Añade pulsos; responde `SUBGHZ_SEND:ACK\|id=1\|seq=0`. |
| `SUBGHZ_SEND:END\|id=1` | Transmite la señal completa; responde `SUBGHZ_SEND:DONE\|id=1` o `SUBGHZ_SEND:FAIL\|id=1\|reason=...`. |

`BLE_DISCOVER`, `SUBGHZ_CHECK_MODULE` y `SUBGHZ_SEND` sin sufijo tienen constantes en el header, pero no un handler en la tabla de comandos actual. El discovery se dispara automáticamente tras la conexión; la comprobación del CC1101 ocurre durante el arranque.

`CAPS` recorre el mismo registro que utiliza el dispatcher, por lo que incluye los once comandos ejecutables, también `PING`, `INFO`, `CAPS` y `HELP`. Los nombres se devuelven con su token UART exacto, como `BLE_SCAN` o `SUBGHZ_SEND:BEGIN`. La implementación actual lista los comandos registrados: incluye Sub-GHz aunque no se haya detectado el CC1101.

## Ayuda consultable con HELP

`HELP` consulta metadatos del protocolo; no inicia escaneos, conexiones, capturas ni transmisiones. Las descripciones devueltas se leen de `catalog.json`, incorporado completo al binario. El ESP32 lo interpreta con `cJSON` únicamente al consultar INFO o los detalles de HELP y liberala memoria antes de responder con el marcador terminal.

Hay tres formas de consulta. Los nombres son sensibles a mayúsculas:

| Solicitud | Contenido de la respuesta |
|---|---|
| `HELP` | Una línea `HELP:COMMAND\|name=<comando>` por cada handler registrado. |
| `HELP\|command=SUBGHZ_SEND:DATA` | Descripción del comando, sus parámetros, restricciones, posibles respuestas y notas de uso. |
| `HELP\|message=SUBGHZ_SEND:ACK` | Descripción del mensaje y de cada uno de sus campos. |

Cada consulta correcta termina con **una sola línea `HELP:DONE`**. Una consulta inválida termina con **`HELP:ERROR` y no envía `HELP:DONE`**. Espera ese cierre antes de enviar otra consulta; todavía no hay IDs de solicitud para correlacionar consultas simultáneas.

### Qué significa cada línea de ayuda

| Mensaje | Campos y significado |
|---|---|
| `HELP:COMMAND` | `name` identifica una orden ejecutable. Al consultar una orden concreta también contiene `description`. |
| `HELP:PARAM` | Describe un parámetro de entrada mediante `name`, `type`, sus atributos y `description`. |
| `HELP:RESPONSE` | `name` es un mensaje que puede responder al comando; `ends_operation` indica si cierra la solicitud u operación consultada. |
| `HELP:MESSAGE` | Identifica el mensaje consultado con `name` y explica su finalidad en `description`. |
| `HELP:FIELD` | Describe un campo de respuesta mediante `name`, `type`, sus atributos y `description`. Un mensaje sin campos devuelve solo `HELP:MESSAGE` y `HELP:DONE`. |
| `HELP:NOTE` | `description` explica requisitos de secuencia, relaciones entre parámetros y limitaciones actuales del handler. |
| `HELP:DONE` | Terminó correctamente la consulta de ayuda. No contiene campos. |
| `HELP:ERROR` | `reason` explica el error y cierra la consulta. |

Los atributos de `HELP:PARAM` y `HELP:FIELD` aparecen cuando corresponden:

| Atributo | Interpretación |
|---|---|
| `required` | `1` indica un parámetro o campo obligatorio; `0`, opcional. |
| `position` | Posición desde 1 cuando el parser exige un orden. |
| `encoding` | `positional`: enviar solo el valor; `key_value`: enviar `nombre=valor`; `hex`: valor expresado en hexadecimal. |
| `type` | `string`: texto; `mac`: dirección BLE; `bool`: 0 o 1; `u8`, `u16`, `u32`, `u64`: enteros sin signo; `i32`, `i64`: enteros con signo; `list_i32`: lista de enteros con signo. El número indica el ancho en bits. |
| `unit` | Unidad del valor: `Hz`, `ms`, `us` o `dBm`. |
| `min`, `max` | Límites del valor escalar. |
| `max_bytes` | Longitud máxima del texto en bytes. |
| `separator=comma` | Los elementos de la lista se separan con comas. |
| `item_min`, `item_max`, `item_nonzero` | Límites de cada elemento de una lista y si debe ser distinto de cero. |
| `length_field` | Nombre del campo cuya cantidad debe coincidir con la longitud de la lista. |
| `exclusive_with` | Selector que no puede acompañar al actual. `command` y `message` son excluyentes. |
| `constraint` | Relación adicional entre campos, explicada a continuación y en `HELP:NOTE`. |

Las relaciones actuales de `constraint` son:

| Valor | Regla |
|---|---|
| `ceil_count_div_16` | `chunks` debe ser `ceil(count / 16)`. |
| `active_transfer_id` | `id` debe coincidir con la transferencia activa aceptada por `BEGIN`. |
| `next_sequence` | `seq` debe ser el siguiente índice esperado, empezando en cero y sin saltos. |
| `min_16_remaining` | `count` debe ser el menor entre 16 y la cantidad de duraciones que faltan por cargar. |
| `complete_active_transfer_id` | El ID debe coincidir con la transferencia activa y deben haberse recibido todos los bloques y duraciones. |

La ayuda describe la implementación actual.

### Ejemplo: qué devuelve SUBGHZ_SEND:DATA

Con el puerto ya configurado y `cat` escuchando en la primera terminal, consulta desde la segunda:

```bash
printf 'HELP|command=SUBGHZ_SEND:DATA\n' | sudo tee /dev/ttyAMA5 >/dev/null
```

La ayuda describe `id`, `seq`, `count` y `timings`, y anuncia dos respuestas: `SUBGHZ_SEND:ACK` y `SUBGHZ_SEND:FAIL`. En este comando, `ends_operation=1` cierra **la solicitud de ese bloque**, no toda la transferencia. `ACK` confirma que el bloque fue cargado; la radio solo transmite después de `SUBGHZ_SEND:END`.

Consulta los campos de la confirmación:

```bash
printf 'HELP|message=SUBGHZ_SEND:ACK\n' | sudo tee /dev/ttyAMA5 >/dev/null
```

Respuesta:

```text
HELP:MESSAGE|name=SUBGHZ_SEND:ACK|description=Block accepted; send the next DATA block or END
HELP:FIELD|name=id|type=u32|required=1|description=Client transfer identifier
HELP:FIELD|name=seq|type=u32|required=1|description=Accepted block index
HELP:DONE
```

Para ver el listado completo:

```bash
printf 'HELP\n' | sudo tee /dev/ttyAMA5 >/dev/null
```

También puedes consultar la propia ayuda, sus errores o los mensajes de arranque:

```text
HELP|command=HELP
HELP|message=HELP:PARAM
HELP|message=HELP:ERROR
HELP|message=SUBGHZ_CHECK_MODULE:FAIL
```

### Consultas inválidas

| `reason` | Cuándo se devuelve |
|---|---|
| `invalid_arguments` | Selector desconocido, valor vacío, campos repetidos, separadores sobrantes o uso simultáneo de `command` y `message`. |
| `unknown_command` | El nombre no pertenece al registro de handlers. |
| `unknown_message` | El nombre no corresponde a un mensaje documentado y emitido por este firmware. |
| `catalog_unavailable` | No se pudo interpretar el JSON, por sintaxis inválida o por falta de memoria. |
| `catalog_invalid` | El JSON se interpretó pero su estructura, referencia de campos o longitud de respuesta no es válida. |

Por ejemplo, `HELP|command=BLE_DISCOVER` responde `HELP:ERROR|reason=unknown_command`: el descubrimiento es automático, no una orden ejecutable. Los tokens reservados que todavía no se emiten, como `BLE_SCAN:UPDATE` y `BLE_DISCOVER:DESC`, tampoco se anuncian como mensajes disponibles.

## Resultados BLE

Los resultados de escaneo tienen este formato:

```text
BLE_SCAN:DEVICE|name=...|mac=AA:BB:CC:DD:EE:FF|rssi=-60|manufacturer=...|service=...|appearance=...|connectable=1|addr_type=0
```

Durante el descubrimiento se emiten:

```text
BLE_DISCOVER:START
BLE_DISCOVER:SERVICE|svc=1|uuid=...
BLE_DISCOVER:CHAR|svc=1|char=1|uuid=...|props=0x02|handle=42
BLE_DISCOVER:DONE|services=1|chars=1
```

Los índices de servicio y característica comienzan en 1. Ante un fallo se puede recibir `BLE_DISCOVER:FAIL|reason=...`. El campo `chars` del resumen actual suma las categorías readable, writable y notify/indicate, por lo que una característica con varias propiedades puede contarse más de una vez.

## Captura y carga de pulsos

Una captura entrega uno o más bloques y después el resumen. Esquema de los mensajes:

```text
SUBGHZ_CAPTURE:START
SUBGHZ_CAPTURE:DATA|capture_id=...|seq=0|chunks=...|count=...|timings=-10375,+254,...
SUBGHZ_CAPTURE:DONE|capture_id=...|freq_hz=433920000|count=...|chunks=...|buffer_full=0
```

- `capture_id` se genera con el temporizador del ESP32 en microsegundos; identifica los bloques de esa captura.
- `seq` comienza en 0. En `DATA`, `count` es la cantidad de valores del bloque; en `DONE`, es el total.
- Cada bloque de captura contiene hasta **32 duraciones**. `buffer_full=1` indica que se alcanzó el límite de 256 valores y la señal puede estar truncada.
- Un timeout de captura genera `SUBGHZ_CAPTURE:FAIL|reason=timeout`.

Para transmitir, el host debe enviar `BEGIN`, esperar `BEGIN_OK`, enviar cada `DATA` esperando su `ACK` y finalmente enviar `END`. La transferencia utiliza un `id` no nulo de 32 bits. Admite entre **1 y 256 duraciones**, **1 a 50 repeticiones** y bloques de **16 valores**, excepto el último, que puede ser menor. `chunks` debe ser `ceil(count / 16)`.

Los valores de TX deben ser distintos de cero y estar entre **-32767 y +32767 µs**. El signo determina el nivel lógico. El protocolo valida ID, secuencia, cantidad y formato de los valores. Un nuevo `BEGIN` válido reemplaza la transferencia incompleta anterior; una carga inactiva durante más de 5 segundos falla con `reason=upload_timeout`. Un `END` duplicado no vuelve a transmitir.

## Catálogo JSON embebido y memoria

`catalog.json` es la única fuente editable de la versión devuelta por INFO y de la documentación de HELP. Los nombres de comandos y respuestas y los límites operativos continúan definidos explícitamente en `zv_com_protocol.h`; los handlers y su registro están en el firmware.

El archivo se incorpora como texto a la flash mediante `EMBED_TXTFILES` en CMake y `board_build.embed_txtfiles` en PlatformIO. El binario contiene todo: no se necesita una partición de archivos, cargar un JSON por separado ni generar tablas C.

El JSON tiene cuatro secciones:

| Sección | Contenido |
|---|---|
| `firmware_version` | Versión que responde INFO, por ejemplo `"0.1.0"`. |
| `commands` | Objetos identificados por el token UART exacto, con `syntax`, `description`, referencias de `params`, `responses` y `notes` opcionales. Cada respuesta tiene `name` y `ends_operation`. |
| `messages` | Objetos identificados por el token exacto de respuesta, con `description` y referencia de `fields` opcional. |
| `field_sets` | Listas de campos o parámetros compartidos. Cada entrada tiene `name`, `type`, `attributes` y `description`; `attributes` es un objeto JSON con las restricciones. |

Por ejemplo, `commands["SUBGHZ_SEND:DATA"].params` apunta al conjunto `data_params`. La confirmación `messages["SUBGHZ_SEND:ACK"].fields` apunta a `ack_fields`. Estas referencias evitan repetir campos y reducen el árbol que se reserva al interpretar el archivo.

Cada vez que se actualize o agregue una nueva funcionalidad al firmware el JSON sera actualizado tanto la version como el nuevo comando, Al actualizar el JSON es encsario re compilar el firmware. Agregar una operación ejecutable también requiere su constante, handler y registro en C, además de su documentación JSON.

### Cuándo se usa y libera RAM

- `INFO`, `HELP|command=...` y `HELP|message=...` interpretan el catálogo completo directamente desde el texto en flash. No copian todo el archivo a un buffer de RAM ni conservan una caché.
- El árbol de `cJSON` y sus textos existen solo durante la consulta. Se liberan con `cJSON_Delete()` antes de `INFO:OK`, `INFO:ERROR`, `HELP:DONE` o `HELP:ERROR`, también ante errores.
- `PING`, `CAPS`, `HELP` sin argumentos y las consultas de ayuda rechazadas antes de buscar documentación no interpretan el JSON.
- Los detalles se validan antes de emitirlos para evitar una respuesta de ayuda parcial por errores de estructura o longitud. El cierre exitoso es siempre una sola línea `HELP:DONE`; los errores no envían DONE.

`INFO:ERROR` utiliza los mismos motivos de catálogo que HELP: `catalog_unavailable` o `catalog_invalid`. CMake incorpora el JSON al binario directamente.

## Integración del header C compartido

Para un cliente C o C++, agrega esta carpeta al include path:

```make
PROTOCOL_DIR ?= ../zerovolts_firmware/protocol
CPPFLAGS += -I$(PROTOCOL_DIR)
```

```c
#include "zv_com_protocol.h"

/* El transporte agrega el salto de línea al enviar. */
uart_send_line(ZV_COM_REQ_BLE_SCAN);
```

Los tokens conservan los prefijos `ZV_COM_REQ_` y `ZV_COM_RES_`. No hay un archivo C de tablas que compilar ni dependencia de `cJSON` para usar este header. La UI ya usa este mecanismo mediante `PROTOCOL_DIR`.


## Convencion de nombres y migracion

Las ordenes y mensajes usan `SUBSISTEMA_OPERACION[:EVENTO]`. BLE usa el
prefijo `BLE_` y Sub-GHz usa `SUBGHZ_`. Los comandos generales, como `PING`,
no llevan subsistema. Los campos usan `snake_case`, con unidades cuando
corresponde: `freq_hz`, `timeout_ms`. Las duraciones de `timings` se expresan
en microsegundos; su signo indica HIGH (positivo) o LOW (negativo).

`OK` confirma una accion y `DONE` indica el final de una operacion. 

La comprobacion de arranque emite `SUBGHZ_CHECK_MODULE:OK` o
`SUBGHZ_CHECK_MODULE:FAIL`. `SUBGHZ_SEND:BEGIN` y el resumen de captura
usan `freq_hz`.
La UI ya consume el header compartido y usa los tokens y campos nuevos.

## Alcance de esta base

El header comparte tokens y límites operativos. INFO y los detalles de HELP leen el catálogo JSON embebido bajo demanda y liberan su memoria al terminar. CAPS y el listado de HELP recorren el registro de handlers.
