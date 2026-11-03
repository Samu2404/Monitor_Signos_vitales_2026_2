# AFE4950: adquisición de ECG, PPG y parámetros de SpO2

Este ejemplo adquiere ECG y PPG de un canal con el módulo AFE4950 y un ESP32 (500 Hz, watermark del FIFO de 3 muestras). Además, **una vez por segundo** agrega al paquete los parámetros necesarios para calcular la saturación de oxígeno (SpO2):

- Estado del sensor
- Componentes AC y DC del canal **infrarrojo (IR)**
- Componentes AC y DC del canal **rojo (RED)**

La adquisición se controla por el puerto serial (115200 baudios) con los comandos `start` y `stop`.

## Conexiones

Son las mismas del ejemplo `Single-ECG-PPG-acquisition`:

- **ECG**: RA, LA y RL a los pines del mismo nombre del módulo.
- **Sensor de pulsioximetría (pinza)**: LED a 5V / TX2 y fotodiodo a P1+ (ánodo) / P1− (cátodo).

## Comandos

| Comando | Descripción |
|---------|-------------|
| `start` | Inicia la adquisición |
| `stop`  | Detiene la adquisición |

## Formato de los datos

Todo lo que se transmite son **palabras de 4 bytes**:

```
 byte 0   byte 1   byte 2   byte 3
+--------+--------+--------+--------+
|  LSB   |  MID   |  MSB   |  TAG   |
+--------+--------+--------+--------+
 \_____ valor de 24 bits _____/
```

El **TAG** indica qué contiene la palabra:

| TAG    | Contenido | Frecuencia |
|--------|-----------|------------|
| `0x02` | Cabecera (valor 0): inicio de paquete | Cada paquete |
| `0x03` | POINTER_DIFF: el paquete trae POINTER_DIFF + 1 muestras | Cada paquete |
| `0x05` | Muestra del FIFO (24 bits, complemento a 2) | Cada paquete |
| `0x06` | ESTADO del sensor | 1 vez por segundo |
| `0x07` | AC del canal IR | 1 vez por segundo |
| `0x08` | DC del canal IR | 1 vez por segundo |
| `0x09` | AC del canal rojo | 1 vez por segundo |
| `0x0A` | DC del canal rojo | 1 vez por segundo |

### Paquete normal

```
[00 00 00 02]  cabecera
[PD 00 00 03]  POINTER_DIFF
[b0 b1 b2 05]  muestra 1
[b0 b1 b2 05]  muestra 2
...            (POINTER_DIFF + 1 muestras)
```

Las muestras siguen el ciclo **ECG, ECG, PPG, ECG, ECG, PPG, ...**

Para pasar una muestra a voltios: armar el valor de 24 bits, interpretarlo en complemento a 2 y multiplicarlo por `1.2 / 2097151`.

### Paquete con parámetros de SpO2 (una vez por segundo)

Al final de las muestras se agregan 5 palabras, siempre en este orden:

```
[ ST SEG 00 06 ]  ESTADO
[ b0 b1  b2 07 ]  AC_IR
[ b0 b1  b2 08 ]  DC_IR
[ b0 b1  b2 09 ]  AC_RED
[ b0 b1  b2 0A ]  DC_RED
```

**ESTADO** (palabra `0x06`):

| Byte | Significado |
|------|-------------|
| byte 0 (`ST`) | 0 = sin señal (no hay dedo o la señal no es válida) · 1 = estabilizando · 2 = señal válida |
| byte 1 (`SEG`) | En estado 1: segundos que faltan para que la señal sea válida |

**AC y DC** (palabras `0x07` a `0x0A`):

- Son enteros **sin signo** de 24 bits, en **cuentas del ADC**.
- Solo tienen valores cuando ESTADO = 2. En los demás estados llegan en 0.
- AC = amplitud pico a pico de la componente pulsátil. DC = nivel medio de la señal. Ambos se calculan sobre una ventana de ~2 s.

## Cálculo de SpO2

1. Relación de ratios:

   ```
   R = (AC_RED / DC_RED) / (AC_IR / DC_IR)
   ```

2. Curva de calibración del sistema:

   ```
   SpO2 (%) = 110 − 25 · R
   ```

Solo se debe mostrar la SpO2 cuando ESTADO = 2. Al poner el dedo, el sensor primero detecta el pulso, luego pasa a "estabilizando" durante 5 s y después a "señal válida".

## Cálculo de la frecuencia cardíaca

Se obtiene de las señales reales:

- **PPG**: detectando los picos de cada pulso.
- **ECG**: detectando los picos R.

La frecuencia es `60 / (tiempo promedio entre picos en segundos)` latidos por minuto.
