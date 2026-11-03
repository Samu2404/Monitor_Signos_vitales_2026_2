// Simulador BLE: publica signos y alarmas de prueba con el mismo BLEManager del firmware,
// para probar la app sin sensores. Se compila solo en el entorno ble-sim
#include <Arduino.h>
#include "BLEManager.h"

struct Escenario {
    const char* nombre;
    uint16_t    hr, pr, spo2;
    float       temp;
    const char* alarmas;      // Mismo orden que AlarmType: FC alta, FC baja, SpO2 baja, T alta, T baja
};

static const Escenario escenarios[] = {
    {"Normal",                 75,  74, 98, 36.8f, "00000000"},
    {"SpO2 baja",              82,  81, 85, 36.8f, "00100000"},
    {"SpO2 baja + temp alta",  95,  94, 86, 38.6f, "00110000"},
    {"Solo temp alta",         90,  89, 97, 38.6f, "00010000"},
    {"FC alta",               135, 134, 97, 36.9f, "10000000"},
    {"FC baja",                42,  41, 96, 36.7f, "01000000"},
    {"Temp baja",              70,  70, 97, 34.5f, "00001000"},
};
static constexpr uint8_t NumEscenarios = sizeof(escenarios) / sizeof(escenarios[0]);

static constexpr uint32_t PeriodoMs   = 1000;    // Igual que BLEConfig::SNAPSHOT_INTERVAL_MS
static constexpr uint32_t EscenarioMs = 10000;   // Modo automático: cambia de escenario cada 10 s
static constexpr uint32_t CorteMs     = 3000;    // Comando 'f': BLE apagado este tiempo

static BLEManager ble;
static uint8_t    actual       = 0;
static bool       automatico   = true;
static uint32_t   ultimoEnvio  = 0;
static uint32_t   ultimoCambio = 0;
static uint32_t   corteHasta   = 0;              // 0 = sin corte en curso

static void elegir(uint8_t i) {
    actual       = i;
    ultimoCambio = millis();
    Serial.printf("[SIM] Escenario %u: %s (%s)\n", i + 1, escenarios[i].nombre, escenarios[i].alarmas);
}

static void ayuda() {
    Serial.println("[SIM] Comandos: 1-7 escenario fijo | a automatico | f falla BLE 3 s | h ayuda");
    for (uint8_t i = 0; i < NumEscenarios; i++) {
        Serial.printf("[SIM]   %u = %s\n", i + 1, escenarios[i].nombre);
    }
}

static void publicar() {
    const Escenario& e = escenarios[actual];
    int ruido = (int)random(-1, 2);              // -1, 0 o 1: que los números se muevan en la app
    int spo2  = min((int)e.spo2 + ruido, 100);

    char buf[32];
    snprintf(buf, sizeof(buf), "%d,%d,%d,%.1f,%u,%u,%u,%u",
             e.hr + ruido, e.pr + ruido, spo2, e.temp, 45u, 16u, 120u, 80u);

    bool ok = ble.updateSnapshot(buf);
    ble.updateAlarms(e.alarmas);                 // Solo notifica cuando cambia

    Serial.printf("[SIM] %s | %s | %s%s\n", buf, e.alarmas,
                  ble.isConnected() ? "conectado" : "sin cliente",
                  ok ? "" : " | snapshot rechazado");
}

void setup() {
    Serial.begin(115200);
    randomSeed(esp_random());

    if (!ble.begin()) {
        Serial.println("[SIM] Error iniciando BLE");
        while (true) delay(1000);
    }

    ayuda();
    elegir(0);
}

void loop() {
    uint32_t ahora = millis();

    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c >= '1' && c < '1' + NumEscenarios) {
            automatico = false;
            elegir(c - '1');
        } else if (c == 'a') {
            automatico = true;
            Serial.println("[SIM] Modo automatico");
            elegir(actual);
        } else if (c == 'f' && corteHasta == 0) {
            ble.setEnabled(false);               // Desconecta al cliente: la app debe ver "Conexión perdida"
            corteHasta = ahora + CorteMs;
            Serial.println("[SIM] Falla BLE: apagado 3 s");
        } else if (c == 'h') {
            ayuda();
        }
    }

    if (corteHasta != 0 && (int32_t)(ahora - corteHasta) >= 0) {
        ble.setEnabled(true);
        corteHasta = 0;
        Serial.println("[SIM] BLE encendido de nuevo");
    }

    if (automatico && ahora - ultimoCambio >= EscenarioMs) {
        elegir((actual + 1) % NumEscenarios);
    }

    if (ahora - ultimoEnvio >= PeriodoMs) {
        ultimoEnvio = ahora;
        publicar();
    }
}
