// Simulador de la pantalla: manda signos, gráficas y el flujo de calibración con el mismo
// NextionHMI del firmware, para probar el .HMI sin sensores. Se compila solo en el entorno nextion-sim
#include <Arduino.h>
#include <stdarg.h>
#include "NextionHMI.h"
#include "Settings.h"

// 0 = debugger del Nextion Editor por el USB (sin comandos: escenarios automáticos, avisos en "MCU Return")
// 1 = pantalla en Serial2 (física, o el Nextion Editor a través del puente); comandos por el monitor serie
// El entorno nextion-sim-2esp lo pone en 1 desde platformio.ini
#ifndef SIM_PANTALLA_FISICA
#define SIM_PANTALLA_FISICA 0
#endif

#if SIM_PANTALLA_FISICA
HardwareSerial&   HmiSerial = Serial2;
constexpr uint8_t PinHmiRx  = 27;
constexpr uint8_t PinHmiTx  = 17;
#else
HardwareSerial&   HmiSerial = Serial;
constexpr uint8_t PinHmiRx  = 3;
constexpr uint8_t PinHmiTx  = 1;
#endif

constexpr uint32_t HmiBaud = 115200;

struct Escenario {
    const char* nombre;
    int   hr, pr, spo2, hrv;
    float temp;
    int   rr;
};

static const Escenario escenarios[] = {
    {"Normal",       75,  74, 98, 45, 36.8f, 16},
    {"Taquicardia", 135, 134, 97, 20, 37.0f, 22},
    {"Bradicardia",  42,  41, 96, 60, 36.7f, 12},
    {"SpO2 baja",    88,  87, 85, 30, 36.8f, 24},
    {"Fiebre",      105, 104, 96, 25, 38.9f, 20},
    {"Hipotermia",   55,  54, 95, 40, 34.5f, 10},
    {"Sin senal",     0,   0,  0,  0, 36.8f,  0},   // Pérdida: el 0 se muestra sin esperar el promedio
};
static constexpr uint8_t NumEscenarios = sizeof(escenarios) / sizeof(escenarios[0]);

// Mismas gráficas que EcgTask / PpgTask
static constexpr uint8_t  EcgId = 1, PpgId = 2, Canal = 0;
static constexpr uint32_t MapOnda      = 4095;
static constexpr uint32_t EcgPeriodoUs = 8000;    // 125 puntos/s, como EcgTask
static constexpr uint32_t PpgPeriodoUs = 16000;   // 62 puntos/s, como PpgTask

static constexpr uint32_t SignosMs    = 1000;
static constexpr uint32_t EscenarioMs = 10000;    // Modo automático
static constexpr uint32_t CalMs       = 15000;    // Duración simulada de la calibración
static constexpr uint32_t CalPasoMs   = 500;      // Cada cuánto se avanza el progreso

static NextionHMI hmi(HmiSerial);
static uint8_t  actual        = 0;
static bool     automatico    = true;
static bool     ondas         = true;
static uint32_t ultimoCambio  = 0, ultimosSignos = 0, ultimoCal = 0;
static uint32_t ultimaEcg     = 0, ultimaPpg = 0;
static float    faseEcg       = 0, fasePpg = 0;
static uint8_t  paginaVista   = 0xFF;

// Calibración simulada: 0 = sin calibrar, 1 = calibrando, 2 = calibrado (igual que BpTask)
static int      calEstado = 0;
static uint32_t calInicio = 0;
static int      calSbp = 0, calDbp = 0;

// Una sola escritura: en modo debugger no se debe mezclar con los comandos del HMI
static void aviso(const char* fmt, ...) {
    char buf[100];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf) - 4, fmt, ap);
    va_end(ap);
#if SIM_PANTALLA_FISICA
    Serial.printf("[NXSIM] %s\n", buf);
#else
    strcat(buf, "\xFF\xFF\xFF");             // En el debugger aparece en "MCU Return"
    HmiSerial.print(buf);
#endif
}

static void elegir(uint8_t i) {
    actual       = i;
    ultimoCambio = millis();
    aviso("Escenario %u: %s", i + 1, escenarios[i].nombre);
}

static void ayuda() {
    aviso("Comandos: 1-%u escenario | a auto | c calibrar 120/80 | p perder PA", NumEscenarios);
    aviso("          i reenviar numeros | w ondas on/off | h ayuda");
}

// ---------- Comandos que manda la pantalla ----------

static Settings ajustes;                     // Solo para revisar rangos con settingsApply()

static const char* nombreAjuste(uint8_t id) {
    switch ((SettingId)id) {
        case SettingId::HrLow:        return "FC baja";
        case SettingId::HrHigh:       return "FC alta";
        case SettingId::Spo2Low:      return "SpO2 baja";
        case SettingId::TempLow:      return "Temp baja";
        case SettingId::TempHigh:     return "Temp alta";
        case SettingId::AlarmEnable:  return "Alarmas activas";
        case SettingId::Ble:          return "BLE";
        case SettingId::Volume:       return "Volumen";
        case SettingId::PauseAudio:   return "Pausar audio";
        case SettingId::FactoryReset: return "Valores de fabrica";
        case SettingId::TestBuzzer:   return "Probar buzzer";
        case SettingId::Acknowledge:  return "Reconocer alarma";
        default:                      return "id desconocido";
    }
}

// Corre en la tarea del HMI: muestra cada comando propio que llega y cómo se interpreta
static void alComando(char grupo, const uint8_t* d, uint8_t n) {
    char hex[3 * 16 + 1] = "";
    for (uint8_t i = 0; i < n && i < 16; i++) {
        snprintf(hex + 3 * i, 4, "%02X ", d[i]);
    }
    aviso("RX # grupo '%c' (%u bytes): %s", grupo, n, hex);

    switch (grupo) {
        case 'W':
            if (n >= 1) aviso("   -> grafica visible en full_wf: id %u", d[0]);
            else        aviso("   -> ERROR: falta el id de la grafica");
            break;
        case 'C':
            if (n >= 2) aviso("   -> calibracion pedida: %u/%u", d[0], d[1]);
            else        aviso("   -> ERROR: faltan SBP y DBP");
            break;
        case 'S':
            if (n >= 3) {
                uint16_t valor = d[1] | (d[2] << 8);   // prints <valor>,2: little endian
                bool ok = (SettingId)d[0] >= SettingId::PauseAudio ||
                          settingsApply(ajustes, (SettingId)d[0], valor);
                aviso("   -> ajuste %s (0x%02X) = %u %s", nombreAjuste(d[0]), d[0], valor,
                      ok ? "" : "FUERA DE RANGO");
            } else {
                aviso("   -> ERROR: el ajuste necesita id + 2 bytes");
            }
            break;
        default:
            aviso("   -> grupo que el firmware no maneja");
            break;
    }
}

// ---------- Ondas sintéticas ----------

static float gauss(float x, float c, float w) {
    float d = (x - c) / w;
    return expf(-d * d);
}

static uint32_t muestraEcg(float f) {        // f: fase del latido 0..1
    float v = 0.10f * gauss(f, 0.18f, 0.035f)    // P
            - 0.12f * gauss(f, 0.30f, 0.008f)    // Q
            + 1.00f * gauss(f, 0.32f, 0.010f)    // R
            - 0.25f * gauss(f, 0.34f, 0.010f)    // S
            + 0.25f * gauss(f, 0.58f, 0.050f);   // T
    return (uint32_t)(MapOnda * (0.35f + 0.55f * v));
}

static uint32_t muestraPpg(float f) {
    float v = gauss(f, 0.25f, 0.09f) + 0.35f * gauss(f, 0.50f, 0.06f);   // Sistólica + dicrótica
    return (uint32_t)(MapOnda * (0.15f + 0.65f * v));
}

static uint32_t plana() {                    // Sin señal: línea base con ruido
    return MapOnda / 2 + (int)random(-20, 21);
}

static void avanzarOndas() {
    uint32_t ahora = micros();
    const Escenario& e = escenarios[actual];

    while (ahora - ultimaEcg >= EcgPeriodoUs) {
        ultimaEcg += EcgPeriodoUs;
        faseEcg += (EcgPeriodoUs / 1e6f) * e.hr / 60.0f;
        if (faseEcg >= 1.0f) faseEcg -= 1.0f;
        if (ondas) hmi.graphWaveform(EcgId, Canal, e.hr ? muestraEcg(faseEcg) : plana());
    }
    while (ahora - ultimaPpg >= PpgPeriodoUs) {
        ultimaPpg += PpgPeriodoUs;
        fasePpg += (PpgPeriodoUs / 1e6f) * e.pr / 60.0f;
        if (fasePpg >= 1.0f) fasePpg -= 1.0f;
        if (ondas) hmi.graphWaveform(PpgId, Canal, e.pr ? muestraPpg(fasePpg) : plana());
    }
}

// ---------- Signos ----------

static void publicarSignos() {
    const Escenario& e = escenarios[actual];
    int r = e.hr ? (int)random(-2, 3) : 0;   // Que los números se muevan (y se note el promedio de 2 s)

    hmi.writeBPM(e.hr + r);
    hmi.writePulseRate(e.pr ? e.pr + r : 0);
    hmi.writeSpO2(e.spo2 ? min(e.spo2 + r / 2, 100) : 0);
    hmi.writeHRvariance(e.hrv);
    hmi.writeTemperature(e.temp + random(-1, 2) * 0.1f);
    hmi.writeRespirationRate(e.rr);

    if (calEstado == 2) {
        hmi.writeBloodPressure(calSbp + (int)random(-2, 3), calDbp + (int)random(-2, 3));
    }
}

// ---------- Calibración ----------

static void iniciarCal(int sbp, int dbp) {
    // Mismos límites que BpTask::startCalibration()
    if (sbp < 70 || sbp > 250 || dbp < 40 || dbp > 150 || sbp < dbp + 10) {
        aviso("Calibracion %d/%d rechazada (valores invalidos)", sbp, dbp);
        return;
    }
    if (escenarios[actual].hr == 0) {
        aviso("Calibracion %d/%d rechazada (sin senal)", sbp, dbp);
        return;
    }
    calSbp    = sbp;
    calDbp    = dbp;
    calEstado = 1;
    calInicio = ultimoCal = millis();
    hmi.writeCalibrationStatus(1, 0);
    aviso("Calibrando con %d/%d", sbp, dbp);
}

static void perderPA() {                     // Igual que BpTask::checkTimeout() al perder los latidos
    if (calEstado == 0) return;
    calEstado = 0;
    hmi.writeBloodPressure(0, 0);
    hmi.writeCalibrationStatus(0, 0);        // Vuelve a aparecer el botón Cal
    aviso("PA perdida: sin calibrar");
}

static void avanzarCal(uint32_t ahora) {
    if (calEstado != 1 || ahora - ultimoCal < CalPasoMs) return;
    ultimoCal = ahora;
    uint32_t progreso = (ahora - calInicio) * 100 / CalMs;
    if (progreso >= 100) {
        calEstado = 2;
        hmi.writeCalibrationStatus(2, 100);
        hmi.writeBloodPressure(calSbp, calDbp);
        aviso("Calibrado: PA %d/%d", calSbp, calDbp);
    } else {
        hmi.writeCalibrationStatus(1, (int)progreso);
    }
}

// ---------- Comandos (solo con pantalla física) ----------

static void leerComandos() {
#if SIM_PANTALLA_FISICA
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c >= '1' && c < '1' + NumEscenarios) {
            automatico = false;
            elegir(c - '1');
        } else if (c == 'a') {
            automatico = true;
            aviso("Modo automatico");
            elegir(actual);
        } else if (c == 'c') {
            iniciarCal(120, 80);
        } else if (c == 'p') {
            perderPA();
        } else if (c == 'i') {
            hmi.invalidateCache();
            aviso("Cache invalidada: se reenvian los numeros");
        } else if (c == 'w') {
            ondas = !ondas;
            aviso("Ondas %s", ondas ? "activas" : "detenidas");
        } else if (c == 'h') {
            ayuda();
        }
    }
#endif
}

void setup() {
#if SIM_PANTALLA_FISICA
    Serial.begin(115200);
#endif
    randomSeed(esp_random());
    hmi.setCommandLog(alComando);            // Antes de begin(): la tarea ya escucha al arrancar
    hmi.begin(HmiBaud, PinHmiRx, PinHmiTx);  // También abre HmiSerial
    hmi.configWaveform(EcgId, Canal, 0, MapOnda);
    hmi.configWaveform(PpgId, Canal, 0, MapOnda);
    hmi.writeCalibrationStatus(0, 0);        // Botón Cal visible, como BpTask

    ultimaEcg = ultimaPpg = micros();
    ayuda();
    elegir(0);
}

void loop() {
    uint32_t ahora = millis();
    leerComandos();

    uint8_t pagina = hmi.currentPage();
    if (pagina != paginaVista) {
        paginaVista = pagina;
        static const char* const nombres[] = {"main", "full_wf", "cfg_alarmas", "cal"};
        aviso("RX pagina %u (%s)", pagina, pagina < 4 ? nombres[pagina] : "?");
    }

    int sbp = 0, dbp = 0;
    if (hmi.takeCalibrationRequest(sbp, dbp)) {
        iniciarCal(sbp, dbp);                // Pedido desde la pantalla (comando # C)
    }
    avanzarCal(ahora);

    if (automatico && ahora - ultimoCambio >= EscenarioMs) {
        elegir((actual + 1) % NumEscenarios);
    }
    if (escenarios[actual].hr == 0) {
        perderPA();                          // Sin señal: la calibración deja de valer
    }

    if (ahora - ultimosSignos >= SignosMs) {
        ultimosSignos = ahora;
        publicarSignos();
    }
    avanzarOndas();
    delay(1);
}
