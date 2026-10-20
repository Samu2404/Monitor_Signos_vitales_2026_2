/**
 * AfeSensorTask.cpp — Adquisición completa del AFE4950 en una sola tarea
 *
 * Basado en la rama Refactorizacion_AFE4950. Reúne en un solo archivo lo que
 * antes estaba en lib/afe4950_sensor (driver + parser + tarea):
 *
 *   1. Inicializa SPI y el AFE4950 (con cálculo de parámetros de SpO2).
 *   2. Una tarea FreeRTOS lee cada paquete del FIFO y lo decodifica.
 *   3. Al final de cada ciclo guarda todos los valores en la variable
 *      global `afeData`, protegida para leerse desde otra tarea/núcleo.
 *
 * Dependencia: librería lib/AFE4950 (AFE4950.h, Registers, SpO2Processor).
 *
 * Formato del paquete (palabras de 4 bytes: valor 24 bits LE + TAG):
 *   0x02 cabecera | 0x03 pointerDiff | 0x05 muestra (ciclo ECG, ECG, PPG)
 *   0x06 ESTADO   | 0x07 AC IR | 0x08 DC IR | 0x09 AC RED | 0x0A DC RED
 *   (el bloque 0x06–0x0A llega una vez por segundo)
 *
 * Uso:
 *   // Copiar esta interfaz en el archivo que lo use (o en un .h)
 *   struct AfeData { ... };            // igual a la definida abajo
 *   bool afeBegin();
 *   bool afeGetData(AfeData& out);
 *
 *   void setup() { afeBegin(); }
 *   void loop() {
 *       AfeData d;
 *       if (afeGetData(d)) { // usar d.ecg1, d.ecg2, d.ppg, d.spo2, ... }
 *   }
 */

#include <Arduino.h>
#include <SPI.h>
#include <math.h>
#include "AFE4950.h"

// ============================ Interfaz ======================================

struct AfeData {
    // Señales (se actualizan con cada muestra, ~cada 10 ms por paquete)
    int32_t  ecg1;            // Primera muestra ECG del ciclo (24 bits con signo)
    int32_t  ecg2;            // Segunda muestra ECG del ciclo
    int32_t  ppg;             // Muestra PPG
    uint32_t index;           // Contador de muestras completas (ECG, ECG, PPG)

    // Oximetría (se actualiza una vez por segundo)
    uint8_t  spo2State;       // 0 = sin señal, 1 = estabilizando, 2 = válida
    uint8_t  spo2SecRemaining;// Segundos que faltan para "válida" (en estado 1)
    uint32_t acIR;            // AC canal infrarrojo (cuentas)
    uint32_t dcIR;            // DC canal infrarrojo
    uint32_t acRed;           // AC canal rojo
    uint32_t dcRed;           // DC canal rojo
    float    spo2;            // SpO2 [%], 0 si no es válida
    uint32_t spo2Updates;     // Contador de bloques de oximetría recibidos
};

bool afeBegin();                  // Inicializa el AFE y arranca la tarea
bool afeGetData(AfeData& out);    // Copia los últimos valores; true si hay datos nuevos

// ============================ Configuración =================================

namespace {

// Pines (ESP32 DevKit V4)
constexpr uint8_t PinSclk  = 18;
constexpr uint8_t PinMiso  = 19;
constexpr uint8_t PinMosi  = 23;
constexpr uint8_t PinCs    = 5;
constexpr uint8_t PinReset = 4;
constexpr uint8_t PinRdy   = 27;

// Tarea
constexpr uint32_t    PollPeriodMs  = 10;
constexpr uint32_t    TaskStackSize = 6144;
constexpr UBaseType_t TaskPriority  = 2;   // Igual que EcgTask/PpgTask
constexpr BaseType_t  TaskCore      = 0;   // Core distinto a NextionHMI y loop()

// TAGs del paquete
constexpr uint8_t TagHeader    = 0x02;
constexpr uint8_t TagCount     = 0x03;
constexpr uint8_t TagSample    = 0x05;
constexpr uint8_t TagSpO2State = 0x06;
constexpr uint8_t TagAcIR      = 0x07;
constexpr uint8_t TagDcIR      = 0x08;
constexpr uint8_t TagAcRed     = 0x09;
constexpr uint8_t TagDcRed     = 0x0A;

// Curva de SpO2 (mismos valores que lib/pam_tomkinps/biosignals.h)
constexpr uint8_t SpO2StateValid = 2;
constexpr float   SpO2CalA       = 110.0f;
constexpr float   SpO2CalB       = 25.0f;
constexpr float   SpO2MinPct     = 80.0f;
constexpr float   SpO2MaxPct     = 100.0f;

// ============================ Estado ========================================

AFE4950      afe;
TaskHandle_t taskHandle = nullptr;

// Variables de salida: las escribe la tarea, se leen con afeGetData()
AfeData      afeData = {};
bool         afeDataNew = false;
portMUX_TYPE afeDataMux = portMUX_INITIALIZER_UNLOCKED;

// Estado del decodificador (solo lo usa la tarea)
int32_t  ecgBuf[2] = {};
uint8_t  phase = 0;          // Posición en el ciclo ECG, ECG, PPG
uint32_t sampleIndex = 0;
uint8_t  spo2Next = 0;       // Próximo TAG esperado del bloque de oximetría (0 = ninguno)

// ============================ Utilidades ====================================

int32_t toSigned24(uint32_t raw) {
    return (int32_t)(raw << 8) >> 8;
}

bool isValidTag(uint8_t tag) {
    return tag == TagHeader || tag == TagCount || (tag >= TagSample && tag <= TagDcRed);
}

// SpO2 = A - B * R,  R = (AC_RED / DC_RED) / (AC_IR / DC_IR)
float spo2FromParams(const AfeData& d) {
    if (d.spo2State != SpO2StateValid) return 0.0f;
    if (d.acIR == 0 || d.dcIR == 0 || d.acRed == 0 || d.dcRed == 0) return 0.0f;

    float r    = ((float)d.acRed * (float)d.dcIR) / ((float)d.dcRed * (float)d.acIR);
    float spo2 = roundf((SpO2CalA - SpO2CalB * r) * 10.0f) / 10.0f;   // Resolución 0.1 %

    if (spo2 < SpO2MinPct) return 0.0f;     // Señal mala, no hipoxemia
    if (spo2 > SpO2MaxPct) spo2 = SpO2MaxPct;
    return spo2;
}

// ============================ Decodificador =================================

// Procesa el bloque de oximetría; true cuando llega el último TAG (DC RED)
bool decodeSpO2(uint8_t tag, uint32_t raw, AfeData& work) {
    if (tag == TagSpO2State) {
        work.spo2State        = raw & 0xFF;
        work.spo2SecRemaining = (raw >> 8) & 0xFF;
        work.acIR = work.dcIR = work.acRed = work.dcRed = 0;
        spo2Next = TagAcIR;
        return false;
    }
    if (tag != spo2Next) {
        spo2Next = 0;       // Fuera de secuencia: se descarta el bloque
        return false;
    }
    switch (tag) {
        case TagAcIR:  work.acIR  = raw; break;
        case TagDcIR:  work.dcIR  = raw; break;
        case TagAcRed: work.acRed = raw; break;
        case TagDcRed: work.dcRed = raw; break;
    }
    if (tag == TagDcRed) {
        spo2Next = 0;
        return true;
    }
    spo2Next++;
    return false;
}

/**
 * Decodifica un paquete completo del AFE sobre `out`.
 * @param newSample true si se completó al menos una muestra ECG/ECG/PPG
 * @param newSpO2   true si se completó un bloque de oximetría
 */
void decodePacket(const uint8_t* pkt, size_t len, AfeData& out, AfeData& spo2Work,
                  bool& newSample, bool& newSpO2) {
    for (size_t i = 0; i + 4 <= len; i += 4) {
        uint8_t  tag = pkt[i + 3];
        uint32_t raw = (uint32_t)pkt[i] | ((uint32_t)pkt[i + 1] << 8) | ((uint32_t)pkt[i + 2] << 16);

        if (!isValidTag(tag)) {
            phase = 0;
            spo2Next = 0;
            continue;
        }

        if (tag >= TagSpO2State) {
            if (decodeSpO2(tag, raw, spo2Work)) {
                out.spo2State        = spo2Work.spo2State;
                out.spo2SecRemaining = spo2Work.spo2SecRemaining;
                out.acIR  = spo2Work.acIR;
                out.dcIR  = spo2Work.dcIR;
                out.acRed = spo2Work.acRed;
                out.dcRed = spo2Work.dcRed;
                out.spo2  = spo2FromParams(out);
                out.spo2Updates++;
                newSpO2 = true;
            }
            continue;
        }
        spo2Next = 0;

        if (tag != TagSample) {
            continue;       // Las cabeceras no cambian de fase
        }

        int32_t value = toSigned24(raw);
        if (phase < 2) {
            ecgBuf[phase++] = value;
            continue;
        }
        out.ecg1  = ecgBuf[0];
        out.ecg2  = ecgBuf[1];
        out.ppg   = value;
        out.index = sampleIndex++;
        phase = 0;
        newSample = true;
    }
}

// ============================ Tarea =========================================

void afeTask(void*) {
    AfeData local = {};      // Copia de trabajo de la tarea
    AfeData spo2Work = {};
    TickType_t lastWake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(PollPeriodMs));

        bool newSample = false;
        bool newSpO2 = false;

        while (afe.ready()) {
            size_t len = afe.getBytesToSend();
            const uint8_t* pkt = afe.getData();
            decodePacket(pkt, len, local, spo2Work, newSample, newSpO2);
        }

        // Al final del ciclo se guardan las variables de salida
        if (newSample || newSpO2) {
            portENTER_CRITICAL(&afeDataMux);
            afeData = local;
            afeDataNew = true;
            portEXIT_CRITICAL(&afeDataMux);
        }
    }
}

} // namespace

// ============================ API pública ===================================

bool afeBegin() {
    if (taskHandle != nullptr) {
        return true;
    }

    SPI.begin(PinSclk, PinMiso, PinMosi, PinCs);
    if (!afe.configure(PinSclk, PinMiso, PinMosi, PinCs, PinReset, PinRdy)) {   // Bloquea ~2 s
        return false;
    }
    afe.enableSpO2(true);
    afe.enableCapture();

    return xTaskCreatePinnedToCore(afeTask, "AfeTask", TaskStackSize, nullptr,
                                   TaskPriority, &taskHandle, TaskCore) == pdPASS;
}

bool afeGetData(AfeData& out) {
    portENTER_CRITICAL(&afeDataMux);
    out = afeData;
    bool isNew = afeDataNew;
    afeDataNew = false;
    portEXIT_CRITICAL(&afeDataMux);
    return isNew;
}
