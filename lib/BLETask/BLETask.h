#pragma once
#include <Arduino.h>
#include "BLEConfig.h"
#include "BLEManager.h"
#include "VitalSigns.h"
#include "AlarmTask.h"


class BLETask {
    public:
        // Las alarmas las evalúa AlarmTask; BLETask solo publica lo que ella deja
        explicit BLETask (AlarmTask& alarms);

        /** @brief Inicializa la tarea BLE
         * @return true si se pudo inicializar; false si no se pudo inicializar (p. ej. BLE no soportado)
         */
        bool begin();
    private:

        static constexpr size_t TaskStackSize= 4096;
        static constexpr uint8_t TaskPriority= 1;   // Menor que la tarea de muestreo (2) para no perder el ritmo de 3-4 ms
        static constexpr uint8_t TaskCore= 1;       // Core distinto al de Next

        static void _taskEntry(void* self);
        void _publishSnapshot(const VitalSigns& v);
        void _publishAlarms(const char* alarms);
        void _taskLoop();

        AlarmTask&   _alarmTask;
        BLEManager   _ble;
        char         _lastAlarms[9] = "";       // Último estado notificado ("" = nunca)
        TaskHandle_t _taskHandle = nullptr;
};
