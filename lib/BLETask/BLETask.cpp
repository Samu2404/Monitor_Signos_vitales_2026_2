#include "BLETask.h"
#include "SnapshotFormatter.h"
#include "math.h"

BLETask::BLETask(EcgTask& ecg, PpgTask& ppg, BpTask& bp)
    : _ecg(ecg), _ppg(ppg), _bp(bp) {
}

bool BLETask:: begin(){
    if (_ble.begin()== false){
        return false;
    }
    xTaskCreatePinnedToCore(_taskEntry, "BLETask", TaskStackSize, this , TaskPriority, &_taskHandle,TaskCore);
    return true;
};

void BLETask :: _taskEntry(void* self){
    static_cast <BLETask*>(self)->_taskLoop();
}

void BLETask::_taskLoop() {
    // Momento de partida: referencia para medir cada periodo de 1 s
    TickType_t lastWake = xTaskGetTickCount();

    for (;;) {
        // Duerme hasta cumplir 1 s desde la vuelta anterior
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(BLEConfig::SNAPSHOT_INTERVAL_MS));

        VitalSigns v = _source ? _source() : _collect();   // 1. Reúne los datos de ECG, PPG y BP
        _evaluateAlarms(v);            // 2. Evalúa las reglas de alarma
        _publishSnapshot(v);         // 2. Texto -> característica Snapshot
        _publishAlarms();            // 3. Notifica si cambiaron las alarmas
    }
}



void BLETask::_evaluateAlarms(const VitalSigns& v) {
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

VitalSigns BLETask::_collect() {
    // Una sola lectura por tarea: datos coherentes y un solo acceso a cada mutex
    EcgTask::Snapshot ecg = _ecg.getSnapshot();
    PpgTask::Snapshot ppg = _ppg.getSnapshot();
    BpTask::Snapshot  bp  = _bp.getSnapshot();

    VitalSigns v;   // Todo empieza en 0 = "sin dato" (temperature, hrv y rr se quedan así)

    // FC del ECG: float -> entero redondeado
    v.hr = ecg.bpm > 0.0f ? (uint16_t)lroundf(ecg.bpm) : 0;

    // PPG: se limita ANTES de convertir a uint8_t para que no se desborde
    v.pr   = ppg.pulseRate;
    v.spo2 = ppg.spo2 > 100 ? 100 : (uint8_t)ppg.spo2;

    // Presión: solo con calibración y con ambas señales presentes (contrato §5)
    bool bpValid = bp.calibrated && v.hr > 0 && v.pr > 0 && bp.sbp > 0.0f && bp.dbp > 0.0f;
    v.sys = bpValid ? (uint16_t)lroundf(bp.sbp) : 0;
    v.dia = bpValid ? (uint16_t)lroundf(bp.dbp) : 0;

    return v;
}

void BLETask::_publishSnapshot(const VitalSigns& v) {
    char buffer[BLEConfig::SNAPSHOT_MAX_LEN + 1];   // 31 caracteres + '\0'

    if (!SnapshotFormatter::format(v, buffer, sizeof(buffer))) {
        Serial.println("[BLE] Error formateando Snapshot");
        return;
    }
    if (!_ble.updateSnapshot(buffer)) {
        Serial.println("[BLE] Error actualizando Snapshot");
    }
}

void BLETask::_publishAlarms() {
    if (!_alarms.hasChanged()) {
        return;                       // Caso normal: nada nuevo que notificar
    }
    if (_ble.updateAlarms(_alarms.getAlarmString())) {
        _alarms.clearChanged();       // Solo si se envió; si falló, se reintenta en 1 s
    }
}