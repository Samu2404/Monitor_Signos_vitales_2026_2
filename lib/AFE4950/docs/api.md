# AFE4950 Library API Documentation

## Methods

### `AFE4950()`

Constructor for the `AFE4950` class.

#### Syntax
```cpp
AFE4950 afe;
```

### `bool configure(uint8_t pinSCLK, uint8_t pinMISO, uint8_t pinMOSI, uint8_t pinCS, uint8_t pin_AFE_RST, uint8_t pin_FIFO_RDY)`

Configures the AFE4950 by setting up the SPI pins and initializing necessary parameters.

#### Syntax
```cpp
bool configure(pinSCLK, pinMISO, pinMOSI, pinCS, pin_AFE_RST, pin_FIFO_RDY);
```

#### Parameters
 - `pinSCLK`: Clock pin for SPI.
 - `pinMISO`: MISO (Master In Slave Out) pin for SPI.
 - `pinMOSI`: MOSI (Master Out Slave In) pin for SPI.
 - `pinCS`: Chip Select pin for AFE4950.
 - `pin_AFE_RST_`: Reset pin for AFE4950.
 - `pin_FIFO_RDY`: FIFO Ready pin for AFE4950.

#### Returns
 - `true` if configuration is successful.
 - `false` if configuration is successful.

### `void enableCapture()`

Enables the data capture on the AFE4950.

#### Syntax
```cpp
void enableCapture();
```

### `void disableCapture()`

Disables data capture on the AFE4950.

#### Syntax
```cpp
void disableCapture();
```

### `size_t getBytesToSend()`

Returns the number of bytes available to be sent from the AFE4950 buffer.

#### Syntax
```cpp
size_t getBytesToSend();
```

#### Returns

The number of bytes available in the buffer.

### `bool ready()`

Checks if the AFE4950 has data ready to be read.

#### Syntax
```cpp
bool ready();
```

#### Returns
 - `true` if data is ready for be readout.
 - `false` otherwise.

### `const uint8_t* getData()`

Provides a pointer to the data buffer where the captured data is stored.

#### Syntax
```cpp
const uint8_t* getData();
```

#### Returns

Pointer to the data buffer. Never change the data in the buffer, this is an only read buffer.

### `void enableSpO2(bool enable = true)`

Enables (or disables) the oximetry parameters in the data packet. When enabled, once per second 5 extra words are appended at the end of a packet:

| TAG | Content |
|-----|---------|
| `0x06` | Status: byte 0 = 0 no signal, 1 stabilizing, 2 valid; byte 1 = seconds remaining while stabilizing |
| `0x07` | AC of the IR channel (ADC counts) |
| `0x08` | DC of the IR channel (ADC counts) |
| `0x09` | AC of the RED channel (ADC counts) |
| `0x0A` | DC of the RED channel (ADC counts) |

SpO2 is obtained with `R = (AC_RED/DC_RED) / (AC_IR/DC_IR)` and `SpO2 = 110 - 25*R`. See `examples/ECG-PPG-SpO2-acquisition/readme.md`.

#### Syntax
```cpp
afe.enableSpO2(true);
```
