#include <Arduino.h>
#include "NextionHMI.h"
#include "EcgTask.h"
#include "PpgTask.h"
#include "BpTask.h"
#include "BtempTask.h"
#include "afe4950_task.h"
#include "AlarmTask.h"
#include "BLETask.h"

// Monitor de signos vitales
//   AFE4950 (ECG + PPG + SpO2) -> EcgTask / PpgTask / BpTask -> Nextion
//   YSI 400 (puente + amplificador) -> BtempTask -> Nextion
//   AlarmTask evalúa los signos cada 1 s y hace sonar el buzzer
//   BLETask publica signos y alarmas
//   LOG_DATOS: registra por Serial (CSV) las tramas crudas y los signos procesados

// ========================================
// CONFIGURACIÓN
// ========================================

// 1 = registrar datos por Serial (el Nextion pasa a Serial2: pantalla física)
// 0 = sin registro (Serial libre para el debugger del Nextion Editor)
#define LOG_DATOS 0

// Cada trama del AFE trae 2 muestras de ECG y 1 de PPG
constexpr float FsEcg = 500.0f;
constexpr float FsPpg = 250.0f;

// Pines según el esquemático de la placa (Proyecto integrador.kicad_sch)

// AFE4950 por SPI (J1)
constexpr uint8_t PinSclk = 18;
constexpr uint8_t PinMiso = 19;
constexpr uint8_t PinMosi = 26;          // J3.10: el símbolo dice "23", pero en el DevKit de 38 pines es el GPIO26
constexpr uint8_t PinCs   = 5;
constexpr uint8_t PinRst  = 4;
constexpr uint8_t PinRdy  = 16;

// Termistor YSI 400 (J20): salida del amplificador del puente al ADC1 (el ADC2 no funciona con BLE)
constexpr uint8_t PinTemp = 34;

// Buzzer de alarmas: GPIO -> R2 4.7k -> base de Q1 (2N3904), buzzer entre 3.3V y el colector
// (pasivo: se excita con PWM; false si es activo)
constexpr uint8_t PinBuzzer     = 13;
constexpr bool    BuzzerPassive = true;

// LEDs de gravedad (GPIO -> LED -> resistencia 220-330 ohm -> GND)
constexpr uint8_t PinLedGreen  = 27;     // Normal  (D4)
constexpr uint8_t PinLedBlue   = 25;     // Leve    (D3)
constexpr uint8_t PinLedYellow = 33;     // Media   (D2)
constexpr uint8_t PinLedRed    = 32;     // Grave   (D1)

// Nextion
#if LOG_DATOS
// En la placa el Nextion va por UART0 (1/3): para registrar hay que cablear la pantalla aparte
// a Serial2 en pines libres (no ruteados en la placa); Serial queda para el registro
HardwareSerial&   HmiSerial = Serial2;
constexpr uint8_t PinHmiRx  = 22;
constexpr uint8_t PinHmiTx  = 17;
#else
// En la placa: UART0 (RX 3 / TX 1) por el conversor de nivel (J11/J12) al Nextion (J13).
// Sirve también para el debugger del Nextion Editor por el USB: no imprimir nada más por Serial
HardwareSerial&   HmiSerial = Serial;
constexpr uint8_t PinHmiRx  = 3;
constexpr uint8_t PinHmiTx  = 1;
#endif

constexpr uint32_t HmiBaud = 115200;     // A 9600 no alcanzan los puntos de las gráficas (bauds=115200 en el .HMI)

// Registro por Serial:
//   D,idx,ecg0,ecg1,ppg                         una línea por trama (250/s)
//   V,ms,hr,pr,spo2,temp,tempMv,sys,dia,ptt,pir,alarmas   signos procesados, 1/s
//   # ...                                       comentarios / avisos
constexpr uint32_t LogBaud       = 921600;  // ~9 kB/s de tramas: a 115200 no queda margen
constexpr size_t   LogQueueLen   = 256;     // ~1 s de tramas de colchón
constexpr uint32_t VitalsLogMs   = 1000;

// ========================================
// OBJETOS
// ========================================

NextionHMI  hmi(HmiSerial);
EcgTask     ecg(hmi);
PpgTask     ppg(hmi);
BpTask      bp(hmi);
BtempTask   temp(hmi);
AfeSensor   afe;
AfeTask     afeTask(afe);
AlarmBuzzer buzzer(PinBuzzer, BuzzerPassive);
AlarmLeds   leds(PinLedGreen, PinLedBlue, PinLedYellow, PinLedRed);
AlarmTask   alarmTask(ecg, ppg, bp, temp, buzzer, leds);
BLETask     bleTask(alarmTask);

#if LOG_DATOS
QueueHandle_t logQueue = nullptr;           // Tramas crudas: AfeTask -> loop()
volatile uint32_t logDropped = 0;           // Tramas que no cupieron en la cola
#endif

// ========================================
// SINKS DEL AFE (corren en la tarea de AfeTask)
// ========================================

void alTrama(const AfeParser::Frame& f, void* ctx) {
    for (int i = 0; i < 2; i++) {
        bp.pushEcg(f.ecg[i]);
        if (ecg.process(f.ecg[i])) {
            bp.onBeat();
        }
    }
    ppg.process(f.ppg);
    bp.pushPpg(f.ppg);

#if LOG_DATOS
    if (xQueueSend(logQueue, &f, 0) != pdTRUE) {   // Sin espera: el registro nunca frena la adquisición
        logDropped++;
    }
#endif
}

void alSpO2(const AfeParser::SpO2Params& p, void* ctx) {
    ppg.setSpO2(spo2_from_params(p.state, p.acIR, p.dcIR, p.acRed, p.dcRed));
}

// ========================================
// REGISTRO (corre en loop(): único que escribe en Serial)
// ========================================

#if LOG_DATOS
void logBegin() {
    Serial.setTxBufferSize(4096);           // Antes de begin(): write() no bloquea en cada línea
    Serial.begin(LogBaud);
    logQueue = xQueueCreate(LogQueueLen, sizeof(AfeParser::Frame));
    Serial.println("# Monitor de signos vitales - registro");
    Serial.println("# D,idx,ecg0,ecg1,ppg");
    Serial.println("# V,ms,hr,pr,spo2,temp,tempMv,sys,dia,ptt,pir,alarmas");
}

void logPoll() {
    static uint32_t lastVitals = 0;
    static uint32_t reportedDrops = 0;
    char line[128];
    int n;

    AfeParser::Frame f;
    while (xQueueReceive(logQueue, &f, 0) == pdTRUE) {
        n = snprintf(line, sizeof(line), "D,%lu,%ld,%ld,%ld\n",
                     (unsigned long)f.index, (long)f.ecg[0], (long)f.ecg[1], (long)f.ppg);
        Serial.write(line, n);
    }

    uint32_t now = millis();
    if (now - lastVitals < VitalsLogMs) {
        return;
    }
    lastVitals = now;

    AlarmTask::Snapshot a = alarmTask.getSnapshot();
    BpTask::Snapshot    b = bp.getSnapshot();
    BtempTask::Snapshot t = temp.getSnapshot();
    n = snprintf(line, sizeof(line), "V,%lu,%u,%u,%u,%.2f,%u,%u,%u,%.1f,%.3f,%s\n",
                 (unsigned long)now, a.vitals.hr, a.vitals.pr, a.vitals.spo2,
                 a.vitals.temperature, t.mv, a.vitals.sys, a.vitals.dia,
                 b.pttMs, b.pir, a.alarms);
    Serial.write(line, n);

    uint32_t d = logDropped;
    if (d != reportedDrops) {
        reportedDrops = d;
        n = snprintf(line, sizeof(line), "# perdidas,%lu\n", (unsigned long)d);
        Serial.write(line, n);
    }
}
#endif

// ========================================
// AFE4950
// ========================================

constexpr uint32_t AfeRetryMs = 5000;       // Sin AFE se reintenta cada 5 s (p. ej. si se conecta tarde)
bool afeOk = false;

bool afeBegin() {
    if (!afe.begin(PinSclk, PinMiso, PinMosi, PinCs, PinRst, PinRdy)) {
#if LOG_DATOS
        Serial.println("# ERROR: AFE4950 no responde");
#else
        // En el debugger aparece en "MCU Return"; FF FF FF para no desincronizar la pantalla
        HmiSerial.print("AFE4950 no responde: sin graficas, FC ni SpO2\xFF\xFF\xFF");
#endif
        return false;                       // Sin AFE no se crea la tarea de captura
    }
    afeTask.SetSpO2Sink(alSpO2);            // Antes de begin(): la tarea ya arranca con el sink
    afeTask.begin(alTrama);                 // También arranca la captura (afe.start())
    return true;
}

// ========================================
// SETUP / LOOP
// ========================================

void setup() {
#if LOG_DATOS
    logBegin();                             // Antes que el AFE: alTrama ya usa la cola
#endif
    hmi.begin(HmiBaud, PinHmiRx, PinHmiTx);  // También abre HmiSerial
    ecg.beginExternal(FsEcg);
    ppg.beginExternal(FsPpg);
    bp.beginExternal(FsEcg, FsPpg, false);  // El AFE entrega la luz con signo negativo (luz = -crudo)

    temp.begin(PinTemp);                    // Si falla, la temperatura queda en 0 (sin dato)
    alarmTask.begin();                      // También arranca el buzzer y los LEDs
    bleTask.begin();                        // Si falla, las alarmas siguen sonando en el buzzer

    afeOk = afeBegin();                     // Si falla, loop() lo reintenta
}

void loop() {
    static uint32_t lastAfeTry = millis();
    if (!afeOk && millis() - lastAfeTry >= AfeRetryMs) {
        afeOk = afeBegin();                 // Bloquea ~2 s mientras el AFE no responda
        lastAfeTry = millis();
    }

    int sbp = 0, dbp = 0;
    if (hmi.takeCalibrationRequest(sbp, dbp)) {
        bp.startCalibration(sbp, dbp);      // Valores inválidos: se ignora el pedido
    }
    bp.checkTimeout();
#if LOG_DATOS
    logPoll();
#endif
    delay(10);                              // La cola guarda ~1 s: vaciarla cada 10 ms sobra
}