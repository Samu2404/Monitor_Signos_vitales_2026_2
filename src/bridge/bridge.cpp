// Puente USB <-> UART: convierte un segundo ESP32 en adaptador USB-TTL para que el debugger
// del Nextion Editor haga de pantalla en el Serial2 del ESP32 que corre el firmware o nextion_sim.
// Se compila solo en el entorno bridge
//
// Conexión (cables cruzados y GND común):
//   puente TX 17  ->  RX 27 del ESP32 de pruebas
//   puente RX 16  <-  TX 17 del ESP32 de pruebas
//   puente GND    --  GND
#include <Arduino.h>

constexpr uint32_t Baud  = 115200;   // Igual que HmiBaud y que el debugger del Nextion Editor
constexpr uint8_t  PinRx = 16;
constexpr uint8_t  PinTx = 17;

static uint8_t buf[256];

// Copia lo que haya en "de" hacia "a" sin bloquear
static void pasar(HardwareSerial& de, HardwareSerial& a) {
    int n = de.available();
    if (n <= 0) {
        return;
    }
    n = de.readBytes(buf, min(n, (int)sizeof(buf)));
    a.write(buf, n);
}

void setup() {
    Serial.setRxBufferSize(1024);    // Ráfagas de puntos de las gráficas: que no se pierdan bytes
    Serial2.setRxBufferSize(1024);
    Serial.begin(Baud);
    Serial2.begin(Baud, SERIAL_8N1, PinRx, PinTx);
}

void loop() {
    pasar(Serial, Serial2);          // Nextion Editor -> ESP32 de pruebas (eventos de la pantalla)
    pasar(Serial2, Serial);          // ESP32 de pruebas -> Nextion Editor (números, gráficas, avisos)
}
