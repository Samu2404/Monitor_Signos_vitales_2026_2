#pragma once
#include <Arduino.h>
#include "VitalSigns.h"
#include "AlarmRule.h"
#include "AlarmManager.h"
#include "AlarmBuzzer.h"
#include "AlarmLeds.h"
#include "AudioPause.h"
#include "AlarmAck.h"
#include "EcgTask.h"
#include "PpgTask.h"
#include "BpTask.h"
#include "BtempTask.h"

/**
 * @brief Tarea de alarmas: cada 1 s reúne los signos vitales, evalúa las reglas,
 *        hace sonar el buzzer y enciende el LED según la prioridad activa.
 *
 * Es independiente de BLE: el buzzer suena aunque BLE no arranque.
 * BLETask solo lee getSnapshot() para publicar.
 */
class AlarmTask {
public:
    AlarmTask(EcgTask& ecg, PpgTask& ppg, BpTask& bp, BtempTask& temp, AlarmBuzzer& buzzer, AlarmLeds& leds);

    bool begin();

    using VitalsSource = VitalSigns (*)();
    void setSource(VitalsSource src) { _source = src; }    // Opcional: datos simulados

    struct Snapshot {
        VitalSigns vitals;
        char       alarms[9];     // "00000000": una posición por AlarmType
        uint16_t   audioPauseS;   // Segundos de audio en pausa que faltan (0 = sin pausa)
        AlarmPriority priority;   // Mayor prioridad activa (también las técnicas)
        char       bar[24];       // Texto de la barra de alarmas ("" = sin alarmas)
    };

    /** @brief Últimos signos vitales y estado de alarmas (copia protegida por mutex) */
    Snapshot getSnapshot() const;

    /** @brief Botón "Pausar audio" (53 10 <segundos>): calla el buzzer seconds s (máx.
     *         AudioPause::DurationS); 0 lo reactiva. Se puede llamar desde cualquier tarea */
    void setAudioPause(uint16_t seconds) { _pauseRequestS = seconds; }

    /** @brief Botón "Reconocer" (53 13): calla el buzzer de las alarmas activas mientras sigan
     *         activas (ver AlarmAck). Se puede llamar desde cualquier tarea */
    void acknowledge() { _ackRequest = true; }

    /** @brief Nuevo paciente: la calibración de PA se descartó a propósito, sin aviso "PA: RECALIBRAR".
     *         Llamarlo junto con el descarte en BpTask. Se puede llamar desde cualquier tarea */
    void forgetBpCalibration() { _bpForget = true; }

private:
    static constexpr uint32_t PeriodMs      = 1000;   // Las persistencias de AlarmConfig son en s
    static constexpr size_t   TaskStackSize = 4096;
    static constexpr uint8_t  TaskPriority  = 1;      // Menor que la tarea de muestreo (2)
    static constexpr uint8_t  TaskCore      = 1;

    static void _taskEntry(void* self);
    void _taskLoop();
    VitalSigns _collect();
    void _evaluateAlarms(const VitalSigns& v);
    void _evaluateTech();

    AlarmRule _hrHighRule;
    AlarmRule _hrLowRule;
    AlarmRule _spo2LowRule;
    AlarmRule _tempHighRule;
    AlarmRule _tempLowRule;
    bool _tempHighSevere = false;          // Temperatura alta en media (si no, leve)
    bool _tempLowSevere  = false;

    // Estado de la PA (lo deja _collect; con datos simulados queda sin calibrar: sin aviso)
    bool _bpCalibrated    = false;
    bool _bpCalibrating   = false;
    bool _bpWasCalibrated = false;         // Estuvo calibrada desde la última recalibración
    volatile bool _bpForget = false;       // Pedido de forgetBpCalibration()

    EcgTask&     _ecg;
    PpgTask&     _ppg;
    BpTask&      _bp;
    BtempTask&   _temp;
    AlarmBuzzer& _buzzer;
    AlarmLeds&   _leds;
    AlarmManager _alarms;
    AudioPause   _pause;
    static constexpr int32_t NoPauseRequest = -1;
    volatile int32_t _pauseRequestS = NoPauseRequest;   // Segundos pedidos; los atiende la tarea en su próximo ciclo
    AlarmAck      _ack;
    volatile bool _ackRequest = false;     // Botón Reconocer; lo atiende la tarea en su próximo ciclo

    Snapshot _snapshot = {};
    mutable SemaphoreHandle_t _mutex = nullptr;
    TaskHandle_t _taskHandle = nullptr;
    VitalsSource _source = nullptr;
};
