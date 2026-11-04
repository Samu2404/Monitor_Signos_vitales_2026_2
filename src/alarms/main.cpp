// Prueba de alarmas con el debugger del Nextion Editor: signos simulados entran a la AlarmTask
// real (reglas, persistencias, prioridades, buzzer y LEDs de la placa) y se ven en la pantalla
// la barra de alarmas, los números de main y la pausa de audio. Sin sensores.
// Se compila solo en el entorno alarms
//
// Escenarios: automáticos en ronda, o fijos desde el área de instrucciones del debugger:
//   printh 23 02 45 <n>    ('E') n = 01..0B escenario fijo, 00 = vuelve a la ronda automática
// No usar el grupo 'T' (54): EasyNex lo reserva para sus triggerN() y nunca llega al programa
#include <Arduino.h>
#include <stdarg.h>
#include <string.h>
#include "NextionHMI.h"
#include "EcgTask.h"
#include "PpgTask.h"
#include "BpTask.h"
#include "BtempTask.h"
#include "AlarmTask.h"
#include "Settings.h"

// Igual que el firmware real: UART0 (RX 3 / TX 1), el mismo USB que usa el debugger
HardwareSerial&    HmiSerial = Serial;
constexpr uint8_t  PinHmiRx  = 3;
constexpr uint8_t  PinHmiTx  = 1;
constexpr uint32_t HmiBaud   = 115200;

// Pines de la placa (como src/firmware/main.cpp)
constexpr uint8_t PinBuzzer     = 13;
constexpr bool    BuzzerPassive = true;
constexpr uint8_t PinLedGreen   = 27;
constexpr uint8_t PinLedBlue    = 25;
constexpr uint8_t PinLedYellow  = 33;
constexpr uint8_t PinLedRed     = 32;

// ========================================
// ESCENARIOS
// ========================================

// temp < 0: rampa de fiebre (de 37,5 a 39,6 °C): pasa de leve a media sin cambiar de escenario
constexpr float Rampa = -1.0f;

struct Escenario {
    const char* nombre;
    const char* espera;       // Qué debería pasar (las persistencias: FC/SpO2 10 s, temperatura 30 s)
    uint16_t    hr, pr;
    uint8_t     spo2;
    float       temp;
};

static const Escenario escenarios[] = {
    {"Normal",            "sin alarmas, LED verde",                  75,  74, 98, 36.8f},
    {"Taquicardia",       "FC ALTA grave a los 10 s",               135, 134, 97, 37.0f},
    {"Bradicardia",       "FC BAJA grave a los 10 s",                42,  41, 96, 36.7f},
    {"SpO2 baja",         "SpO2 BAJA grave a los 10 s",              88,  87, 85, 36.8f},
    {"Fiebre leve",       "TEMP ALTA leve a los 30 s (1 pitido)",    95,  94, 97, 38.5f},
    {"Fiebre alta",       "TEMP ALTA media a los 30 s",             105, 104, 96, 39.5f},
    {"Hipotermia leve",   "TEMP BAJA leve a los 30 s",               60,  59, 96, 34.5f},
    {"Hipotermia",        "TEMP BAJA media a los 30 s",              55,  54, 95, 33.5f},
    {"Fiebre en aumento", "TEMP ALTA leve y luego media (>39,0)",   100,  99, 97, Rampa},
    {"SpO2 baja + fiebre","SpO2 BAJA +1, grave",                     95,  94, 85, 38.5f},
    {"Sin senal",         "se apagan las alarmas (0 = sin dato)",     0,   0,  0, 0.0f},
};
static constexpr uint8_t NumEscenarios = sizeof(escenarios) / sizeof(escenarios[0]);

static constexpr uint32_t EscenarioMs = 60000;   // Ronda automática: alcanza para la persistencia de 30 s
static constexpr uint32_t RampaMs     = 80000;   // Duración de la rampa de fiebre
static constexpr uint32_t ValoresMs   = 500;

// ========================================
// OBJETOS
// ========================================

static NextionHMI  hmi(HmiSerial);
static EcgTask     ecg(hmi);                     // No se arrancan: AlarmTask usa la fuente simulada
static PpgTask     ppg(hmi);
static BpTask      bp(hmi);
static BtempTask   temp(hmi);
static AlarmBuzzer buzzer(PinBuzzer, BuzzerPassive);
static AlarmLeds   leds(PinLedGreen, PinLedBlue, PinLedYellow, PinLedRed);
static AlarmTask   alarmTask(ecg, ppg, bp, temp, buzzer, leds);

static volatile uint8_t  actual = 0;             // Lo lee la tarea de alarmas (fuente simulada)
static volatile uint32_t inicioEscenario = 0;
static bool     automatico = true;
static volatile uint8_t pedido = 0xFF;           // Escenario pedido por 'E' (lo pone la tarea del HMI)
static uint8_t  paginaVista = 0xFF;

// Una sola escritura por aviso: no se mezcla con los comandos del HMI. Aparece en "MCU Return"
static void aviso(const char* fmt, ...) {
    char buf[100];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf) - 4, fmt, ap);
    va_end(ap);
    strcat(buf, "\xFF\xFF\xFF");
    HmiSerial.print(buf);
}

static float temperatura(const Escenario& e) {
    if (e.temp != Rampa) {
        return e.temp;
    }
    uint32_t t = millis() - inicioEscenario;
    if (t > RampaMs) {
        t = RampaMs;
    }
    return 37.5f + 2.1f * t / RampaMs;
}

// Fuente de AlarmTask (corre en su tarea, una vez por segundo)
static VitalSigns simulados() {
    const Escenario& e = escenarios[actual];
    VitalSigns v;
    v.hr   = e.hr;
    v.pr   = e.pr;
    v.spo2 = e.spo2;
    v.temperature = temperatura(e);
    return v;
}

static void elegir(uint8_t i) {
    inicioEscenario = millis();
    actual = i;
    aviso("Escenario %u: %s -> %s", i + 1, escenarios[i].nombre, escenarios[i].espera);
}

// Corre en la tarea del HMI: solo anota el pedido
static void alComando(char grupo, const uint8_t* d, uint8_t n) {
    if (grupo == 'E' && n >= 1) {
        pedido = d[0];
    }
}

static void atenderPedido() {
    uint8_t p = pedido;
    if (p == 0xFF) {
        return;
    }
    pedido = 0xFF;
    if (p == 0) {
        automatico = true;
        aviso("Ronda automatica (cada %lu s)", (unsigned long)(EscenarioMs / 1000));
        elegir(0);
    } else if (p <= NumEscenarios) {
        automatico = false;
        elegir(p - 1);
    } else {
        aviso("Escenario %u no existe (1..%u, 0 = automatico)", p, NumEscenarios);
    }
}

static const char* nombrePrioridad(AlarmPriority p) {
    switch (p) {
        case AlarmPriority::MINOR:    return "LEVE (azul)";
        case AlarmPriority::MEDIUM:   return "MEDIA (amarillo)";
        case AlarmPriority::CRITICAL: return "GRAVE (rojo)";
        default:                      return "ninguna (verde)";
    }
}

// ========================================
// SETUP / LOOP
// ========================================

void setup() {
    hmi.setCommandLog(alComando);               // Antes de begin(): la tarea ya escucha al arrancar
    hmi.begin(HmiBaud, PinHmiRx, PinHmiTx);     // También abre HmiSerial
    alarmTask.setSource(simulados);             // Antes de begin(): sin sensores
    if (!alarmTask.begin()) {
        aviso("ERROR: AlarmTask no arranco (buzzer o mutex)");
    }
    aviso("Prueba de alarmas: printh 23 02 45 <n> fija el escenario (00 = ronda)");
    elegir(0);
}

void loop() {
    atenderPedido();
    if (automatico && millis() - inicioEscenario >= EscenarioMs) {
        elegir((actual + 1) % NumEscenarios);
    }

    // Pausa de audio (53 10 <segundos>) y Reconocer (53 13) desde la pantalla, igual que el firmware
    uint8_t  id;
    uint16_t value;
    while (hmi.takeSetting(id, value)) {
        if (id == (uint8_t)SettingId::PauseAudio) {
            alarmTask.setAudioPause(value);
            aviso("RX 53 10 %u: %s", value, value ? "audio en pausa" : "audio reanudado");
        } else if (id == (uint8_t)SettingId::Acknowledge) {
            alarmTask.acknowledge();
            aviso("RX 53 13: alarmas reconocidas (callan hasta que se apaguen; una nueva suena)");
        } else {
            aviso("RX 53 %02X %u: esta prueba no lo usa", id, value);
        }
    }

    AlarmTask::Snapshot a = alarmTask.getSnapshot();
    hmi.writeAlarmBar(a.bar, (uint8_t)a.priority);

    static uint16_t lastPauseS = 0;
    if (lastPauseS > 0 && a.audioPauseS == 0) {
        hmi.cancelAudioPause();
        aviso("Pausa terminada en el ESP32: main.vaPausa = 0");
    }
    lastPauseS = a.audioPauseS;

    // Cambios de alarma: lo que debería verse en la barra y en los LEDs
    static char lastBar[sizeof(a.bar)] = "?";
    static AlarmPriority lastPrio = AlarmPriority::NONE;
    if (strcmp(a.bar, lastBar) != 0 || a.priority != lastPrio) {
        memcpy(lastBar, a.bar, sizeof(lastBar));
        lastPrio = a.priority;
        aviso("Alarma: \"%s\" prioridad %s, BLE %s", a.bar, nombrePrioridad(a.priority), a.alarms);
    }

    // Números de main (la temperatura de la rampa se ve subir)
    static uint32_t lastValues = 0;
    if (millis() - lastValues >= ValoresMs) {
        lastValues = millis();
        VitalSigns v = simulados();
        hmi.updateValues(v.spo2, v.hr, v.pr, 0, v.temperature, 0);
    }

    uint8_t pagina = hmi.currentPage();
    if (pagina != paginaVista) {
        paginaVista = pagina;
        aviso("RX pagina %u", pagina);
    }
    delay(10);
}
