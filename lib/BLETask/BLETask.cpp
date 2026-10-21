#include "BLETask.h"
#include "SnapshotFormatter.h"
#include <string.h>

BLETask::BLETask(AlarmTask& alarms)
    : _alarmTask(alarms) {
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

        AlarmTask::Snapshot s = _alarmTask.getSnapshot();   // 1. Signos y alarmas ya evaluados
        _publishSnapshot(s.vitals);                         // 2. Texto -> característica Snapshot
        _publishAlarms(s.alarms);                           // 3. Notifica si cambiaron las alarmas
    }
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

void BLETask::_publishAlarms(const char* alarms) {
    if (strcmp(alarms, _lastAlarms) == 0) {
        return;                       // Caso normal: nada nuevo que notificar
    }
    if (_ble.updateAlarms(alarms)) {
        strncpy(_lastAlarms, alarms, sizeof(_lastAlarms));   // Solo si se envió; si falló, se reintenta en 1 s
    }
}
