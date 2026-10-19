#include "BLETask.h"


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

        VitalSigns v = _collect();   // 1. Reúne los datos de ECG, PPG y BP
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

