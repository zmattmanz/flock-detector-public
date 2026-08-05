

# Flock-Detector 3.0: Detector de Vigilancia Impulsado por XIAO

Una herramienta avanzada de exploración WiFi y BLE (Bluetooth de Baja Energía) construida sobre el **Seeed Studio XIAO ESP32-S3**. Identifica y registra hardware de vigilancia — incluidas **cámaras ALPR de Flock Safety**, **detectores de disparos Raven** (SoundThinking/ShotSpotter) y dispositivos de monitoreo relacionados — en tiempo real con registros CSV etiquetados con GPS, para su uso en solicitudes FOIA, auditorías de privacidad y mapeo comunitario a través de [deflock.me](https://deflock.me).

---

## Novedades en la v3.0

- **Escalada de Alarmas** — El patrón sonoro varía según el nivel de confianza: 1 pitido para MEDIO, 3 pitidos para ALTO, 5 pitidos rápidos para CIERTO. Sabe si debe detenerse solo por el sonido.
- **Persistencia de Sesión** — Los contadores de detección y tiempo de actividad sobrev "ven" los ciclos de encendido/apagado mediante almacenamiento flash LittleFS. Guarda cada 60 segundos y restaura al iniciar.
- **Re-detección con Ventana de Tiempo** — La misma dirección MAC se vuelve a registrar después de 5 minutos con coordenadas GPS frescas, lo que permite la confirmación multi-paso de instalaciones fijas.
- **Tiempo de Permanencia de Canal Adaptativo** — 500 ms en los canales 1, 6, 11 (no superpuestos, donde es más probable que operen las cámaras Flock), 200 ms en todos los demás.
- **Seguimiento de Tendencia RSSI** — Realiza un seguimiento de la intensidad de la señal a lo largo del tiempo para los dispositivos detectados. Un patrón de ascenso-pico-descenso (característico de pasar frente a una instalación fija) obtiene un bono de confianza. Los dispositivos que aparecen repentinamente a corta distancia (teléfonos, coches que pasan) no.
- **Validación de Formato de SSID WiFi** — El formato hexadecimal específico `Flock-XXXX` (donde XXXX es una dirección MAC parcial) obtiene una puntuación más alta que una coincidencia genérica del subcadena "flock", reduciendo falsos positivos de redes de consumo con nombres similares.
- **Verificación de Tipo de Dirección BLE** — Las direcciones BLE públicas y aleatorias estáticas (usadas por las baterías de Flock) obtienen un bono de confianza. Las direcciones aleatorias resolvibles (teléfonos que rotan cada ~15 minutos) no.

---

## Características

- **Escaneo de Doble Banda** — Monitoreo promiscuo de WiFi simultáneo (2.4 GHz, canales 1–13) y escaneo de anuncios BLE mediante la coexistencia ESP32, asignado a núcleos de CPU separados para cero contención.
- **Puntuación de Confianza Multi-Método** — Cada método de detección contribuye con puntos ponderados a una puntuación de confianza (0–100%). Múltiples señales corroborativas se acumulan. La alarma se activa en el umbral del 40%. Las puntuaciones se registran en CSV y se muestran en la OLED como MEDIO / ALTO / CIERTO.
- **Métodos de Detección** — Coincidencia de prefijo MAC OUI (40 pts), coincidencia de patrón SSID (50 pts), validación de formato SSID (65 pts), coincidencia de nombre de dispositivo BLE (45 pts), detección de ID de empresa fabricante (60 pts, 0x09C8 / XUNTONG), extracción de número de serie TN (80 pts), huella digital UUID de servicio Raven (70–90 pts), análisis de tipo de dirección BLE (+10 pts), análisis de tendencia RSSI (+15 pts), bono de corroboración multi-método (+20 pts).
- **Huella Digital de Firmware Raven** — Clasifica automáticamente los dispositivos Raven detectados como firmware 1.1.x (legado), 1.2.x o 1.3.x según los UUIDs de servicio BLE que publican. La versión del firmware se registra para análisis posterior.
- **Seguimiento del Método de Detección** — Cada detección registra *cuáles* heurísticas activaron la coincidencia (`mac_prefix`, `ssid_pattern`, `ssid_fmt`, `ble_name`, `mfg_id_0x09C8`, `tn_serial`, `penguin_num`, `raven_service_uuid`, `static_addr`, `pub_addr`), lo que permite el análisis de falsos positivos y el ajuste de firmas.
- **Registro CSV Geoespacial** — Guarda las detecciones en `FlockLog_XXX.csv` con numeración automática en MicroSD con coordenadas GPS, altitud, velocidad, rumbo, marcas de tiempo, RSSI, puntuaciones de confianza y metadatos completos del dispositivo.
- **7 Pantallas OLED** — Estado del escáner (con indicador de canal principal), estadísticas de detección (Flock WiFi / Flock BLE / Raven, sesión + de por vida), detalle de la última captura con confianza, alimentación de señales en vivo, coordenadas GPS, gráfico de barras de actividad e indicador de proximidad de señal.
- **Modo Furtivo** — Mantenga presionado el botón para apagar la pantalla y el zumbador mientras el escaneo continúa en silencio.
- **Persistencia de Sesión** — Contadores de por vida almacenados en flash a través de LittleFS. Las estadísticas sobreviven a los ciclos de encendido/apagado y se acumulan entre sesiones.
- **Integración de la Placa de Expansión** — Soporte completo para la Base de Expansión XIAO: OLED, zumbador, ranura para tarjeta MicroSD y botón de usuario.

---

## Hardware

| Componente | Pieza | Notas |
|-----------|------|-------|
| Microcontrolador | [Seeed Studio XIAO ESP32-S3](https://www.seeedstudio.com/XIAO-ESP32S3-p-5627.html) | Doble núcleo 240 MHz, WiFi + BLE 5.0 |
| Placa Base | [Seeed Studio Expansion Board for XIAO](https://www.seeedstudio.com/Seeeduino-XIAO-Expansion-board-p-4746.html) | OLED (SSD1306 128×64), zumbador, MicroSD, botón, conector de batería |
| Antena | Antena de Barra 2.4 GHz (2.81 dBi) | SMA o U.FL según su variante S3 |
| Módulo GPS | NEO-6MV2 | Conectado mediante cable Groove a jumper (4 pines hembra) al puerto UART Groove de la placa de expansión |
| Caja | Caja de agua ABS | Con bridas de cable para antena y alimentación USB-C |
| Almacenamiento | Tarjeta MicroSD (FAT32) | Cualquier tamaño; los registros son archivos CSV pequeños |

### Cableado

La placa de expansión maneja la mayoría de las conexiones. El módulo GPS se conecta al puerto UART Groove:

| Pin GPS | Pin XIAO | Función |
|---------|----------|---------|
| TX | D7 (RX) | Datos NMEA GPS a ESP32 |
| RX | D6 (TX) | No se usa pero está conectado |
| VCC | 3V3 | Alimentación |
| GND | GND | Tierra |

---

## Metodología de Detección

Las firmas de detección se derivan de datos de campo recopilados por la comunidad de detección de vigilancia, incluidos conjuntos de datos de [deflock.me](https://deflock.me), investigación Raven de [GainSec](https://github.com/GainSec), trabajo de ID de fabricante de Will Greenberg y el proyecto FlockBack.

### Puntuación de Confianza

Cada método de detección contribuye con puntos ponderados a una puntuación de confianza acumulativa. Múltiples métodos independientes que corroboran el mismo dispositivo suman sus puntuaciones. La alarma se activa en 40 puntos (MEDIO), con etiquetas asignadas en 70 (ALTO) y 85 (CIERTO).

| Método | Puntos | Ejemplo |
|--------|--------|---------|
| Prefijo MAC OUI | 40 | Prefijo conocido de Flock/Murata/LiteOn |
| Patrón SSID (genérico) | 50 | Contiene "flock", "Penguin", etc. |
| Formato SSID (específico) | 65 | Coincide con el formato hexadecimal `Flock-XXXX` |
| Nombre dispositivo BLE | 45 | "FS Ext Battery", "FlockCam", etc. |
| ID Fabricante (0x09C8) | 60 | ID de empresa XUNTONG en datos mfg BLE |
| UUID Raven (individual) | 70 | Un UUID de servicio Raven conocido |
| UUID Raven (3+) | 90 | Múltiples UUIDs Raven de un mismo dispositivo |
| Serie TN en datos mfg | 80 | Patrón como TN72023022000771 |
| Nombre numérico Penguin | 15 | Decimal de 10 dígitos (firmware post-marzo 2025) |
| **Bonos** | | |
| RSSI fuerte (> -50 dBm) | +10 | Dispositivo muy cercano |
| Multi-método (2+ señales) | +20 | Métodos independientes corroboran |
| Dirección BLE estática/pública | +10 | Dirección consistente, no rotativa |
| Firma RF estacionaria | +15 | Patrón de ascenso-pico-descenso RSSI |

**Ejemplo:** Un módulo BLE aleatorio de Murata (OUI 08:3a:88) obtiene 40 puntos (MEDIO). Una batería Flock real con el mismo OUI + ID de fabricante XUNTONG + nombre "FS Ext Battery" + dirección estática + firma estacionaria suma 40 + 60 + 45 + 10 + 15 + 20 = 100 (CIERTO).

### WiFi (Modo Promiscuo)

Capta tramas de gestión 802.11 (balizas y solicitudes de sonda) en todos los 13 canales con tiempos de permanencia adaptativos (500 ms en los canales 1/6/11, 200 ms en los demás). Un filtro de hardware (`WIFI_PROMIS_FILTER_MASK_MGMT`) asegura que solo las tramas de gestión lleguen al `callback`, reduciendo la carga de CPU entre 10–50× en entornos RF congestionados. Coincide con:

- **Patrones SSID** — `flock`, `FS Ext Battery`, `Penguin`, `Pigvision`, `FlockOS`, `flocksafety`, `FS_`
- **Formato SSID** — Validación específica del formato hexadecimal `Flock-XXXX` (mayor confianza que la coincidencia genérica)
- **Prefijos MAC OUI** — 24 prefijos conocidos asociados con hardware de Flock Safety y sus proveedores de módulos (Cradlepoint, Murata, LiteOn, Espressif, Sierra Wireless)

### BLE (Escaneo Activo NimBLE)

Escanea anuncios BLE cada 3 segundos con una duración de 2 segundos y un intervalo/ventana de 97 ms (un número primo reduce el aliasing con los intervalos comunes de anuncios BLE). La supresión de duplicados está habilitada dentro de los ciclos de escaneo. Coincide con:

- **Patrones de nombre de dispositivo** — `FS Ext Battery`, `Penguin`, `Flock`, `Pigvision`, `FlockCam`, `FS-`
- **Nombres numéricos Penguin** — Cadenas decimales de 10 dígitos (el firmware de marzo de 2025 eliminó el prefijo "Penguin-")
- **Prefijos MAC OUI** — Misma base de datos de prefijos que WiFi (24 entradas)
- **ID de empresa fabricante** — `0x09C8` (XUNTONG), asociado con hardware BLE de Flock Safety
- **Números de serie TN** — Patrón ASCII "TN" en la carga de datos del fabricante XUNTONG (p. ej., TN72023022000771)
- **Tipo de dirección BLE** — Direcciones públicas y aleatorias estáticas (baterías Flock) vs. aleatorias resolvibles (teléfonos)
- **UUIDs de servicio Raven** — 8 UUIDs de servicio BLE conocidos a través de las versiones de firmware 1.1.x a 1.3.x:

| Prefijo UUID | Servicio | Firmware |
|-------------|---------|----------|
| `0x180A` | Información del Dispositivo | Todos |
| `0x3100` | Ubicación GPS | 1.2.x+ |
| `0x3200` | Gestión de Energía (batería/solar) | 1.2.x+ |
| `0x3300` | Estado de la Red (LTE/WiFi) | 1.2.x+ |
| `0x3400` | Estadísticas de Carga | 1.3.x |
| `0x3500` | Diagnóstico de Errores/Fallos | 1.3.x |
| `0x1809` | Termómetro de Salud (legado) | 1.1.x |
| `0x1819` | Localización y Navegación (legado) | 1.1.x |

### Seguimiento de Tendencia RSSI

Cuando un dispositivo supera el umbral de alarma, el firmware comienza a rastrear su RSSI a lo largo de múltiples observaciones (hasta 5 muestras en 15 segundos). Una instalación fija produce una curva característica de ascenso-pico-descenso al pasar frente a ella: la señal se fortalece al acercarse, alcanza un pico y luego se desvanece. Un teléfono en el bolsillo de alguien o un dispositivo en un coche que pasa aparece repentinamente a corta distancia y desaparece tan rápido. Los dispositivos que exhiben el patrón estacionario reciben un bono de confianza de +15. Cada dispositivo solo se puntúa una vez para evitar la inflación.

---

## Instalación

### Requisitos Previos

- Arduino IDE con el paquete de tarjetas **esp32** instalado
- Selección de tarjeta: **Seeed Studio XIAO ESP32S3**
- **Esquema de partición**: Seleccione un esquema de partición con soporte para LittleFS (p. ej., "Default 4MB with spiffs" funciona — LittleFS usa la misma partición)

### Bibliotecas Requeridas

Instale a través del Administrador de Bibliotecas de Arduino IDE:

| Biblioteca | Autor | Propósito |
|---------|--------|---------|
| NimBLE-Arduino | h2zero | Escaneo BLE |
| ArduinoJson | Benoit Blanchon | (disponible para exportación JSON futura) |
| Adafruit SSD1306 | Adafruit | Pantalla OLED |
| Adafruit GFX | Adafruit | Primitivas gráficas |
| TinyGPS++ | Mikal Hart | Parseo de NMEA GPS |

LittleFS está incluido en el núcleo de Arduino para ESP32 y no requiere una instalación separada.

### Flasheo

1. Conecte el XIAO ESP32-S3 mediante USB-C.
2. Seleccione **Seeed Studio XIAO ESP32S3** como la tarjeta.
3. Flashee `FlockDetection_v3.0.ino`.
4. Inserte una tarjeta MicroSD formateada en FAT32.
5. Encienda — escuche el pit de inicio de dos tonos (grave → agudo).
6. Verifique la salida serie para ver datos de sesión restaurados (si existen sesiones anteriores).

---

## Uso

### Controles de Botón

| Acción | Función |
|--------|---------|
| Presión corta (< 1 seg) | Cambiar a la siguiente pantalla |
| Presión larga (> 1 seg) | Alternar modo furtivo (pantalla + zumbador apagados) |

### Pantallas de Visualización

| # | Pantalla | Descripción |
|---|--------|---------|
| 0 | Escáner | Estado del escaneo en vivo, canal actual (* en canales principales), tiempo de actividad, barrido animado |
| 1 | Estadísticas | Contadores de detección: Flock WiFi / Flock BLE / Raven (sesión + de por vida), tiempo de actividad total |
| 2 | Última Captura | Detección más reciente: tipo, MAC, RSSI, puntuación de confianza + etiqueta |
| 3 | Alimentación en Vivo | Registro en vivo de todas las señales cercanas (detecciones resaltadas con % de confianza) |
| 4 | GPS | Coordenadas, velocidad, rumbo, contador de satélites, estado de la señal |
| 5 | Gráfico de Actividad | Gráfico de barras de detecciones por segundo en los últimos 25 segundos |
| 6 | Proximidad | Barra visual RSSI con etiquetas de distancia cualitativa y % de confianza |

### Escalada de Alarmas

El patrón sonoro de la alarma varía según el nivel de confianza para que pueda evaluar las detecciones por oído mientras conduce:

| Confianza | Pitos | Frecuencia | Significado |
|------------|-------|-----------|---------|
| MEDIO (40–69%) | 1 corto | 1000 Hz | Algo podría estar aquí |
| ALTO (70–84%) | 3 pitidos | 1200 Hz | Detección probable |
| CIERTO (85–100%) | 5 rápidos | 1500 Hz | Dispositivo confirmado |

El tiempo de enfriamiento de 60 segundos entre alarmas evita pitidos continuos al pasar frente a un conjunto de dispositivos.

### Formato del Registro CSV

Los registros se guardan en `/FlockLog_XXX.csv` en la tarjeta MicroSD con nombres de archivo autoincrementales. Columnas:

```
Uptime_ms, Date_Time, Channel, Capture_Type, Protocol, RSSI, MAC_Address,
Device_Name, TX_Power, Detection_Method, Confidence, Confidence_Label,
Extra_Data, Latitude, Longitude, Speed_MPH, Heading_Deg, Altitude_M
```

`Capture_Type` es uno de: `FLOCK_WIFI`, `FLOCK_BLE`, `RAVEN_BLE`

`Detection_Method` contiene etiquetas separadas por espacios que indican qué heurísticas se activaron (p. ej., `mac mfg_0x09C8 name static_addr`).

`Confidence` es la puntuación numérica (0–100) y `Confidence_Label` es MEDIO, ALTO o CIERTO.

### Persistencia de Sesión

Los contadores de por vida se almacenan en flash LittleFS en `/flock_session.dat` y se guardan cada 60 segundos. Al iniciar, el firmware restaura las estadísticas de por vida anteriores y las imprime en serie:

```
Restored: WiFi=47 BLE=23 Time=02:34:15 Total=70
```

Para restablecer las estadísticas de por vida, borre la partición flash o elimine el archivo mediante serie/código.

### Re-detección con Ventana de Tiempo

El búfer circular de deduplicación MAC (200 entradas) ahora marca con fecha cada entrada. Si se ve la misma MAC nuevamente después de 5 minutos, se vuelve a registrar con coordenadas GPS frescas. Esto permite:

- Confirmar instalaciones fijas desde múltiples pases en diferentes calles
- Construir evidencia FOIA más sólida ("detectado en esta ubicación en 3 pases separados en 20 minutos")
- Distinguir cámaras fijas de unidades móviles en remolques

---

## Arquitectura

El firmware utiliza ambos núcleos del ESP32-S3:

- **Núcleo 0** — Tarea del escáner dedicada: cambio de canal WiFi (permanencia adaptativa) y planificación del escaneo BLE
- **Núcleo 1** — Bucle principal: parseo de GPS, renderizado OLED, manejo de botones, vaciado de tarjeta SD, salida de alarma, persistencia de sesión, expiración del rastreador RSSI

Un mutex de FreeRTOS protege todo el estado compartido (contadores de detección, búferes de registro, datos de visualización) entre núcleos. Las escrituras en SD se almacenan en búfer y se vacían cada 10 segundos o cuando el búfer alcanza 10 entradas, lo que ocurra primero.

### Presupuesto de Memoria

| Estructura | Tamaño | Notas |
|-----------|------|-------|
| Búfer circular MAC | 200 entradas | Con marca de tiempo, ventana de re-detección de 5 min |
| Rastreador RSSI | 16 dispositivos × 5 muestras | Expiración de 15 segundos, puntuado una vez por dispositivo |
| Búfer de escritura SD | 10 entradas | Vacío a la tarjeta por temporizador o contador |
| Sesión LittleFS | ~64 bytes | 4 contadores guardados en flash |

---

## Créditos y Reconocimientos

Este proyecto se basa en el trabajo de la comunidad de detección de vigilancia:

- **[Colonel Panic / flock-you](https://github.com/colonelpanichacks/flock-you)** — Lógica de detección original, investigación de identificación MAC/SSID y la plataforma de hardware OUI-SPY. Disponible en [colonelpanic.tech](https://colonelpanic.tech).
- **[f1yaw4y / FlockSquawk](https://github.com/f1yaw4y/FlockSquawk)** — Inspiración principal para la IU y la implementación lista para el campo.
- **[Will Greenberg (@wgreenberg)](https://github.com/wgreenberg)** — Método de detección de ID de empresa fabricante BLE (0x09C8 / XUNTONG).
- **[GainSec](https://github.com/GainSec)** — Conjunto de datos de UUIDs de servicios BLE Raven (`raven_configurations.json`) que permite la detección de dispositivos de vigilancia acústica SoundThinking/ShotSpotter en las versiones de firmware 1.1.7, 1.2.0 y 1.3.1. También documentó el formato SSID `Flock-XXXX` y los cambios del firmware Penguin de marzo de 2025.
- **[DeFlock (FoggedLens/deflock)](https://github.com/FoggedLens/deflock)** — Datos de ubicación de ALPR crowdsourced y metodologías de detección. Visite [deflock.me](https://deflock.me) para contribuir con avistamientos.
- **[FlockBack](https://github.com/FlockBack)** — Contribuciones de datos de detección comunitarios.

---

## Aspectos Legales

Esta herramienta está destinada a la investigación de seguridad, auditoría de privacidad, documentación FOIA y fines educativos. Detectar la presencia de hardware de vigilancia en espacios públicos es legal en la mayoría de las jurisdicciones. Cumpla siempre con las leyes locales sobre escaneo inalámbrico e interceptación de señales.

---

## Licencia

MIT
