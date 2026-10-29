#pragma once
#include <Arduino.h>
#include "VitalSigns.h"
#include "AlarmRule.h"
#include "AlarmManager.h"
#include "AlarmBuzzer.h"
#include "AlarmLeds.h"
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
    };

    /** @brief Últimos signos vitales y estado de alarmas (copia protegida por mutex) */
    Snapshot getSnapshot() const;

private:
    static constexpr uint32_t PeriodMs      = 1000;   // Las persistencias de AlarmConfig son en s
    static constexpr size_t   TaskStackSize = 4096;
    static constexpr uint8_t  TaskPriority  = 1;      // Menor que la tarea de muestreo (2)
    static constexpr uint8_t  TaskCore      = 1;

    static void _taskEntry(void* self);
    void _taskLoop();
    VitalSigns _collect();
    void _evaluateAlarms(const VitalSigns& v);

    AlarmRule _hrHighRule;
    AlarmRule _hrLowRule;
    AlarmRule _spo2LowRule;
    AlarmRule _tempHighRule;
    AlarmRule _tempLowRule;

    EcgTask&     _ecg;
    PpgTask&     _ppg;
    BpTask&      _bp;
    BtempTask&   _temp;
    AlarmBuzzer& _buzzer;
    AlarmLeds&   _leds;
    AlarmManager _alarms;

    Snapshot _snapshot = {};
    mutable SemaphoreHandle_t _mutex = nullptr;
    TaskHandle_t _taskHandle = nullptr;
    VitalsSource _source = nullptr;
};
