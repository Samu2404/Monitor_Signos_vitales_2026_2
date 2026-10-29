# Diseño de la pantalla Nextion

Pantalla de **800 × 480 px** en horizontal. Todas las coordenadas son `x, y, w, h` en píxeles,
con el origen en la esquina superior izquierda.

| Imagen de referencia | Archivo |
|---|---|
| Página principal (en alarma: FC alta, audio en pausa) | `mockup_main.png` / `.svg` |
| Configuración → Alarmas | `mockup_cfg_alarmas.png` / `.svg` |

> Para armar la página, importa el PNG en el editor (*Picture* → `+`), ponlo como fondo
> provisional (`sta = image`, `pic = n`) y coloca los componentes encima. Cuando termines, vuelve a poner
> `sta = solid color` con `bco = 2146` y borra la imagen para no gastar flash.

---

## 1. Páginas

El firmware ya conoce estos IDs (`NextionHMI::Page`). **El orden en el editor debe ser este:**

| ID | Nombre | Uso |
|---|---|---|
| 0 | `main` | Ondas + parámetros (mockup principal) |
| 1 | `full_wf` | Una onda a pantalla completa |
| 2 | `cfg_alm` | Configuración → Alarmas (mockup de configuración) |
| 3 | `cal` | Calibración de PA |
| 4… | `cfg_pac`, `cfg_sen`, `cfg_pan`, `cfg_ble`, `cfg_srv` | Resto de pestañas (mismo marco que `cfg_alm`) |

En **todas** las páginas, el *Preinitialize Event* avisa al ESP32 de la página cargada:

```
printh 23 02 50 XX     // XX = ID de la página en hexadecimal (00, 01, 02…)
```

---

## 2. Paleta (RGB565 para `bco` / `pco`)

| Uso | HEX | RGB565 |
|---|---|---|
| Fondo de página | `#0B0F14` | **2146** |
| Pestaña no seleccionada | `#11171E` | 4259 |
| Barra de estado y teclas | `#151C24` | 4324 |
| Pestaña seleccionada / tecla pulsada | `#1C2A38` | 6471 |
| Líneas divisorias | `#222B35` | 8518 |
| Botones − / + y riel del slider | `#3A4654` | 14890 |
| Texto deshabilitado | `#5A6672` | 23342 |
| Texto secundario (encabezados) | `#8A96A3` | 36020 |
| Texto principal | `#D8DEE6` | 57084 |
| Blanco | `#FFFFFF` | 65535 |
| **ECG / FC** | `#5DCAA5` | 24148 |
| **Pleth / SpO2 / PR / BLE** | `#85B7EB` | 34237 |
| **PA** | `#ED93B1` | 60566 |
| **Temperatura / pausa de audio** | `#FAC775` | 65070 |
| Tecla "Pausar audio" | `#EF9F27` | 60644 |
| Alarma: franja lateral | `#E24B4A` | 57929 |
| Alarma: número del parámetro | `#F09595` | 62642 |
| Alarma: fondo de la barra de estado | `#A32D2D` | 41317 |
| Interruptor ON | `#1D9E75` | 7406 |
| Acento (pestaña activa, slider) | `#378ADD` | 13403 |

---

## 3. Fuentes (Tools → Font Generator)

Genéralas en este orden para que coincidan los índices. Usa una fuente sans (Arial / Roboto)
y el conjunto de caracteres **ISO-8859-1** (incluye ñ, á, °).

| `font` | Altura | Estilo | Uso |
|---|---|---|---|
| 0 | 16 px | normal | Etiquetas pequeñas, unidades, límites |
| 1 | 20 px | normal | Barra de estado, teclas, filas de configuración |
| 2 | 24 px | **bold** | Mensaje de alarma, valores de los steppers |
| 3 | 40 px | **bold** | PA, Temperatura |
| 4 | 72 px | **bold** | FC, SpO2 (solo dígitos `0-9 - .` para ahorrar flash) |

---

## 4. Página `main` (ID 0)

### 4.1 Barra de estado (y = 0, h = 40)

| Nombre | Tipo | x | y | w | h | font | bco | pco | Contenido |
|---|---|---|---|---|---|---|---|---|---|
| `tPat` | Text | 0 | 0 | 128 | 40 | 1 | 4324 | 57084 | `Adulto` / `Pediátrico` / `Neonatal` |
| `tAlarm` | Text | 128 | 0 | 420 | 40 | 2 | 4324 | 57084 | Alarma de mayor prioridad. En alarma: `bco=41317`, `pco=65535` |
| `pPause` | Picture | 560 | 8 | 24 | 24 | – | – | – | `icons/mute_24.png`; oculto con `vis pPause,0` al inicio del Preinitialize |
| `tPause` | Text | 588 | 0 | 52 | 40 | 1 | 4324 | 65070 | Cuenta regresiva `1:45` (vacío si no hay pausa); la escribe el ESP32 |
| `tBle` | Text | 640 | 0 | 60 | 40 | 1 | 4324 | 34237 | `BLE` (conectado) / gris 23342 si está desconectado |
| `tClock` | Text | 700 | 0 | 100 | 40 | 1 | 4324 | 57084 | `14:32` |

Todos con `xcen = 1` (centrado) y `ycen = 1`. El icono de altavoz tachado de `tPause` puede ser
un *Picture* de 24 × 24 en `x=560, y=8`.

### 4.2 Ondas (columna izquierda, w = 558)

| Nombre | Tipo | x | y | w | h | font | pco / pco0 | Nota |
|---|---|---|---|---|---|---|---|---|
| `tEcgLbl` | Text | 8 | 42 | 200 | 20 | 0 | 24148 | `ECG II  ×1` |
| `s0` | Waveform | 0 | 64 | 558 | 170 | – | 24148 | 1 canal, `bco=2146`, sin rejilla |
| `tPlLbl` | Text | 8 | 238 | 200 | 20 | 0 | 34237 | `Pleth` |
| `s1` | Waveform | 0 | 260 | 558 | 168 | – | 34237 | 1 canal, `bco=2146` |

El firmware dibuja con `add <id>,<canal>,<valor>`: el **ID** del componente Waveform es el que se registra en el código.

### 4.3 Parámetros (columna derecha, x = 560…800)

Cada bloque lleva a la izquierda una franja de color de 4 px (Text vacío con `bco`; en alarma pasa a 57929).

| Nombre | Tipo | x | y | w | h | font | pco | Nota |
|---|---|---|---|---|---|---|---|---|
| `rHr` | Text (franja) | 560 | 40 | 4 | 120 | – | bco 24148 | |
| `tHrLbl` | Text | 570 | 44 | 80 | 24 | 1 | 24148 | `FC` |
| `tHrUnit` | Text | 730 | 46 | 60 | 20 | 0 | 24148 | `lpm`, `xcen=2` (derecha) |
| `nHrHi` | Number | 570 | 76 | 50 | 20 | 0 | 24148 | Límite alto |
| `nHrLo` | Number | 570 | 128 | 50 | 20 | 0 | 24148 | Límite bajo |
| **`nHR`** | Number | 620 | 72 | 170 | 84 | 4 | 24148 | `xcen=2`. En alarma: `pco=62642` |
| `rSp` | Text (franja) | 560 | 160 | 4 | 120 | – | bco 34237 | |
| `tSpLbl` | Text | 570 | 164 | 80 | 24 | 1 | 34237 | `SpO2` |
| `tSpUnit` | Text | 730 | 166 | 60 | 20 | 0 | 34237 | `%` |
| `nSpLo` | Number | 570 | 230 | 50 | 20 | 0 | 34237 | Límite bajo |
| **`nSpO2`** | Number | 640 | 184 | 150 | 80 | 4 | 34237 | `xcen=2` |
| `tPrLbl` | Text | 570 | 258 | 40 | 20 | 0 | 34237 | `PR` |
| **`nPR`** | Number | 700 | 256 | 90 | 22 | 1 | 34237 | `xcen=2` |
| `rBp` | Text (franja) | 560 | 280 | 4 | 80 | – | bco 60566 | |
| `tBpLbl` | Text | 570 | 284 | 220 | 20 | 0 | 60566 | `PA mmHg (PTT)` |
| **`nSistolica`** | Number | 580 | 310 | 90 | 44 | 3 | 60566 | `xcen=2` |
| `tSlash` | Text | 670 | 310 | 20 | 44 | 3 | 60566 | `/` |
| **`nDiastolica`** | Number | 690 | 310 | 90 | 44 | 3 | 60566 | `xcen=0` |
| `rT` | Text (franja) | 560 | 360 | 4 | 70 | – | bco 65070 | |
| `tTLbl` | Text | 570 | 364 | 100 | 20 | 0 | 65070 | `Temp °C` |
| **`nT`** | **Xfloat** | 640 | 384 | 150 | 44 | 3 | 65070 | `vvs0=2`, **`vvs1=1`** (1 decimal), `xcen=2` |

**En negrita** están los nombres que el firmware ya escribe (`NextionHMI::NumObjects`); no los cambies.
`nHRV`, `nRR`, `nCalEst` y `jCal` siguen existiendo en el código: pon `nHRV` y `nRR` en la página
`main` (pueden ir ocultos con `vis` = 0) y `nCalEst` y `jCal` en la página `cal`.

### 4.4 Líneas divisorias (Postinitialize Event)

```
line 558,40,558,429,8518
line 559,40,559,429,8518
line 0,235,557,235,8518
line 560,159,800,159,8518
line 560,279,800,279,8518
line 560,359,800,359,8518
line 0,429,800,429,8518
```

### 4.5 Teclas (y = 434, w = 152, h = 42, font 1, bco 4324, bco2 6471)

| Nombre | x | Texto | pco | Acción |
|---|---|---|---|---|
| `bPause` | 8 | Pausar audio | 60644 | Pausa el audio 2 min (envía al ESP32) |
| `bAck` | 166 | Reconocer | 57084 | Reconoce la alarma enclavada |
| `bWaves` | 324 | Ondas | 57084 | `page full_wf` |
| `bCalBp` | 482 | Calibrar PA | 57084 | `page cal` |
| `bConfig` | 640 | Config | 57084 | `page cfg_alm` |

---

## 5. Página `cfg_alm` (ID 2)

### 5.1 Barra de estado

Igual que en `main` (`tPat`, `tBle`, `tClock`); `tAlarm` muestra `Configuración · Alarmas`.

### 5.2 Pestañas laterales (x = 0, w = 160, h = 65, font 1)

| Nombre | y | Texto | Destino |
|---|---|---|---|
| `bTabAlm` | 40 | Alarmas | (actual) |
| `bTabPac` | 105 | Paciente | `page cfg_pac` |
| `bTabSen` | 170 | Señales | `page cfg_sen` |
| `bTabPan` | 235 | Pantalla | `page cfg_pan` |
| `bTabBle` | 300 | Bluetooth | `page cfg_ble` |
| `bTabSrv` | 365 | 🔒 Servicio | `page cfg_srv` (con contraseña) |

- Seleccionada: `bco=6471`, `pco=65535` y franja `rTab` de 4 × 65 px con `bco=13403`.
- No seleccionada: `bco=4259`, `pco=57084` (Servicio con `pco=36020`).
- Texto alineado a la izquierda: `xcen=0`, con `x` de texto a 20 px.

### 5.3 Tabla de alarmas

Encabezados (font 0, pco 36020) en y = 56: `Parámetro` x=180, `Activa` centrado en 365,
`Límite bajo` centrado en 510, `Límite alto` centrado en 700.

Filas de **56 px**, desde y = 90. Centro de la fila *k* (0…4): `yc = 118 + 56·k`.

| Fila | Nombre (font 1) | Interruptor (Dual-state 330, yc−18, 70×36) | Límite bajo | Límite alto |
|---|---|---|---|---|
| 0 | FC (lpm) | `btHr` | `nHrLo` | `nHrHi` |
| 1 | SpO2 (%) | `btSp` | `nSpLo` | – (texto `—`, pco 23342) |
| 2 | Temp (°C) | `btT` | `xTLo` (Xfloat, vvs1=1) | `xTHi` (Xfloat, vvs1=1) |
| 3 | Sin señal | `btNoSig` | Texto `Alarma técnica (ECG / PPG)` font 0, pco 36020, x=420 | |
| 4 | Volumen | – | Slider `hVol` (ver 5.4) | |

**Interruptor (Dual-state button):** font 2, `bco0=14890` texto `OFF` (apagado), `bco1=7406` texto `ON` (encendido), `pco=65535`.

**Stepper** (una celda = 3 componentes, alto 40, `y = yc − 20`):

| Pieza | Bajo: x | Alto: x | w | Estilo |
|---|---|---|---|---|
| Botón `−` (`bXxLoM` / `bXxHiM`) | 420 | 610 | 48 | font 2, bco 14890, pco 65535 |
| Valor (`nXxLo` / `nXxHi`) | 468 | 658 | 84 | font 2, bco 2146, pco 57084, centrado |
| Botón `+` (`bXxLoP` / `bXxHiP`) | 552 | 742 | 48 | font 2, bco 14890, pco 65535 |

Separadores de fila (Postinitialize):

```
line 170,90,790,90,8518
line 170,146,790,146,8518
line 170,202,790,202,8518
line 170,258,790,258,8518
line 170,314,790,314,8518
line 170,370,790,370,8518
line 0,429,800,429,8518
for(sys0=104;sys0<=364;sys0+=65) { line 0,sys0,160,sys0,8518 }
```

### 5.4 Volumen

| Nombre | Tipo | x | y | w | h | Propiedades |
|---|---|---|---|---|---|---|
| `tVolMin` | Text | 420 | 332 | 40 | 20 | `30%`, font 0, pco 36020 |
| `hVol` | Slider | 460 | 327 | 260 | 30 | `minval=30`, `maxval=100`, `bco=14890`, `pco=13403` (cursor 57084) |
| `nVol` | Number | 720 | 326 | 50 | 32 | font 2, `xcen=2` + Text `%` |

El mínimo de 30 % es intencional: según IEC 60601-1-8 la alarma no debe poder silenciarse del todo.

### 5.5 Rangos recomendados (pasos de los steppers)

Los valores por defecto coinciden con `lib/AlarmsTask/AlarmRule.h`.

| Límite | Defecto | Mín | Máx | Paso |
|---|---|---|---|---|
| FC bajo | 50 | 30 | 100 | 5 |
| FC alto | 120 | 80 | 220 | 5 |
| SpO2 bajo | 90 | 80 | 98 | 1 |
| Temp bajo | 35.0 | 33.0 | 37.0 | 0.1 (`val` 330…370) |
| Temp alto | 38.0 | 37.0 | 41.0 | 0.1 (`val` 370…410) |
| Volumen | 70 % | 30 % | 100 % | slider |

Regla en la pantalla: `bajo < alto` siempre (al pulsar `+` en bajo, si `nHrLo.val+5 >= nHrHi.val`, no subir).

### 5.6 Teclas (misma fila que en `main`)

| Nombre | x | Texto | Acción |
|---|---|---|---|
| `bAuto` | 8 | Límites auto | Límites alrededor del valor actual |
| `bTest` | 166 | Probar sonido | Hace sonar el buzzer con el volumen elegido |
| `bDefault` | 324 | Por defecto | Restaura la tabla 5.5 |
| `bBack` | 640 | Volver | Envía los ajustes y `page main` (pco 34237) |

---

## 6. Comunicación Nextion → ESP32

Formato de Easy Nextion: `printh 23 <len> <grupo> <datos…>`, donde `len` = 1 (grupo) + nº de bytes de datos.

| Grupo | Bytes | Estado | Cuándo |
|---|---|---|---|
| `P` (0x50) | `23 02 50 <página>` | ✅ implementado | Preinitialize de cada página |
| `W` (0x57) | `23 02 57 <id>` | ✅ implementado | Onda elegida en `full_wf` |
| `C` (0x43) | `23 03 43 <sbp> <dbp>` | ✅ implementado | Botón de calibración en `cal` |
| `A` (0x41) | `23 02 41 01` | ✅ implementado | Tecla `bPause`: pausa o reactiva el audio 2 min |
| `S` (0x53) | `23 04 53 <clave> <valH> <valL>` | 🔜 propuesto | Al pulsar `Volver` en configuración |

> Ojo: el comentario de `NextionHMI.cpp` para la calibración dice `23 02 67`, pero el código
> espera el grupo `'C'` = **0x43** con 2 bytes de datos, es decir `23 03 43 …`.

Ejemplo del botón `bBack` (*Touch Release*), con el grupo `S` propuesto:

```
// clave: 01 FC bajo, 02 FC alto, 03 SpO2 bajo, 04 T bajo, 05 T alto, 06 volumen, 07 máscara ON/OFF
printh 23 04 53 01
prints nHrLo.val,2
printh 23 04 53 02
prints nHrHi.val,2
...
page main
```

`prints <var>,2` envía el valor en 2 bytes *little-endian*; el lado del ESP32 lo leerá así cuando se implemente `#S`.
