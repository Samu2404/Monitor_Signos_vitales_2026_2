#include "AlarmTask.h"
#include <math.h>
#include <string.h>

AlarmTask::AlarmTask(EcgTask& ecg, PpgTask& ppg, BpTask& bp, BtempTask& temp, AlarmBuzzer& buzzer, AlarmLeds& leds)
    : _ecg(ecg), _ppg(ppg), _bp(bp), _temp(temp), _buzzer(buzzer), _leds(leds) {
    memcpy(_snapshot.alarms, _alarms.getAlarmString(), sizeof(_snapshot.alarms));
}

bool AlarmTask::begin() {
    _mutex = xSemaphoreCreateMutex();
    if (_mutex == nullptr) {
        return false;
    }
    _leds.begin();                      // Verde desde el arranque
    if (!_buzzer.begin()) {
        return false;
    }
    return xTaskCreatePinnedToCore(_taskEntry, "AlarmTask", TaskStackSize, this,
                                   TaskPriority, &_taskHandle, TaskCore) == pdPASS;
}

AlarmTask::Snapshot AlarmTask::getSnapshot() const {
    Snapshot s;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    s = _snapshot;
    xSemaphoreGive(_mutex);
    return s;
}

void AlarmTask::_taskEntry(void* self) {
    static_cast<AlarmTask*>(self)->_taskLoop();
}

void AlarmTask::_taskLoop() {
    // Momento de partida: referencia para medir cada periodo de 1 s
    TickType_t lastWake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(PeriodMs));

        VitalSigns v = _source ? _source() : _collect();   // 1. Reúne los datos de ECG, PPG y BP
        _evaluateAlarms(v);                                 // 2. Evalúa las reglas de alarma
        AlarmPriority prio = _alarms.highestPriority();
        _buzzer.setPriority(prio);                          // 3. Suena / calla según las alarmas
        _leds.show(prio);                                   //    y enciende el LED de esa gravedad

        xSemaphoreTake(_mutex, portMAX_DELAY);              // 4. Deja la copia para BLETask
        _snapshot.vitals = v;
        memcpy(_snapshot.alarms, _alarms.getAlarmString(), sizeof(_snapshot.alarms));
        xSemaphoreGive(_mutex);
    }
}

void AlarmTask::_evaluateAlarms(const VitalSigns& v) {
    using namespace AlarmConfig;

    {   // Frecuencia cardiaca alta
        bool hrHighOn  = v.hr > HR_HIGH_ON;
        bool hrHighOff = v.hr == 0 || v.hr < HR_HIGH_OFF;
        _alarms.setAlarm(AlarmType::HR_HIGH,
            _hrHighRule.update(hrHighOn, hrHighOff, VITAL_PERSIST_ON_S, VITAL_PERSIST_OFF_S));
    }
    {   // Frecuencia cardiaca baja
        bool hrLowOn  = v.hr > 0 && v.hr < HR_LOW_ON;
        bool hrLowOff = v.hr == 0 || v.hr > HR_LOW_OFF;
        _alarms.setAlarm(AlarmType::HR_LOW,
            _hrLowRule.update(hrLowOn, hrLowOff, VITAL_PERSIST_ON_S, VITAL_PERSIST_OFF_S));
    }
    {   // SpO2 baja
        bool spo2LowOn  = v.spo2 > 0 && v.spo2 < SPO2_LOW_ON;
        bool spo2LowOff = v.spo2 == 0 || v.spo2 > SPO2_LOW_OFF;
        _alarms.setAlarm(AlarmType::SPO2_LOW,
            _spo2LowRule.update(spo2LowOn, spo2LowOff, VITAL_PERSIST_ON_S, VITAL_PERSIST_OFF_S));
    }
    {   // Temperatura alta
        bool tempHighOn  = v.temperature > TEMP_HIGH_ON;
        bool tempHighOff = v.temperature <= 0.0f || v.temperature < TEMP_HIGH_OFF;
        _alarms.setAlarm(AlarmType::TEMP_HIGH,
            _tempHighRule.update(tempHighOn, tempHighOff, TEMP_PERSIST_ON_S, TEMP_PERSIST_OFF_S));
    }
    {   // Temperatura baja
        bool tempLowOn  = v.temperature > 0.0f && v.temperature < TEMP_LOW_ON;
        bool tempLowOff = v.temperature <= 0.0f || v.temperature > TEMP_LOW_OFF;
        _alarms.setAlarm(AlarmType::TEMP_LOW,
            _tempLowRule.update(tempLowOn, tempLowOff, TEMP_PERSIST_ON_S, TEMP_PERSIST_OFF_S));
    }
}

VitalSigns AlarmTask::_collect() {
    // Una sola lectura por tarea: datos coherentes y un solo acceso a cada mutex
    EcgTask::Snapshot ecg = _ecg.getSnapshot();
    PpgTask::Snapshot ppg = _ppg.getSnapshot();
    BpTask::Snapshot  bp  = _bp.getSnapshot();
    BtempTask::Snapshot temp = _temp.getSnapshot();

    VitalSigns v;   // Todo empieza en 0 = "sin dato" (hrv y rr se quedan así)

    // FC del ECG: float -> entero redondeado
    v.hr = ecg.bpm > 0.0f ? (uint16_t)lroundf(ecg.bpm) : 0;

    // PPG: se limita ANTES de convertir a uint8_t para que no se desborde
    v.pr   = ppg.pulseRate;
    v.spo2 = ppg.spo2 > 100 ? 100 : (uint8_t)ppg.spo2;

    // Presión: solo con calibración y con ambas señales presentes (contrato §5)
    bool bpValid = bp.calibrated && v.hr > 0 && v.pr > 0 && bp.sbp > 0.0f && bp.dbp > 0.0f;
    v.sys = bpValid ? (uint16_t)lroundf(bp.sbp) : 0;
    v.dia = bpValid ? (uint16_t)lroundf(bp.dbp) : 0;

    // Temperatura: 0 = sin dato; las reglas de temperatura ya lo ignoran
    v.temperature = temp.celsius;

    return v;
}
