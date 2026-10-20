#pragma once 
#include <Arduino.h>        
#include "BLEConfig.h"      
#include "BLEManager.h"
#include "VitalSigns.h"
#include "AlarmRule.h"
#include "AlarmManager.h"
#include "EcgTask.h"
#include "PpgTask.h"
#include "BpTask.h"


class BLETask {
    public:
        BLETask (EcgTask& ecg, PpgTask& ppg, BpTask& bp);

        /** @brief Inicializa la tarea BLE
         * @return true si se pudo inicializar; false si no se pudo inicializar (p. ej. BLE no soportado)
         */
        bool begin();
        using VitalsSource = VitalSigns (*)();  
        void setSource(VitalsSource src) { _source = src; }
    private:

        
        AlarmRule _hrHighRule;
        AlarmRule _hrLowRule;
        AlarmRule _spo2LowRule;
        AlarmRule _tempHighRule;
        AlarmRule _tempLowRule;
        AlarmRule _bpSysHighRule;
        AlarmRule _bpSysLowRule;
        AlarmRule _bpDiaHighRule;
        AlarmRule _bpDiaLowRule;


        static constexpr size_t TaskStackSize= 4096;
        static constexpr uint8_t TaskPriority= 1;   // Menor que la tarea de muestreo (2) para no perder el ritmo de 3-4 ms
        static constexpr uint8_t TaskCore= 1;       // Core distinto al de Next

        static void _taskEntry(void* self); 
        VitalSigns _collect()  ;
        void _evaluateAlarms(const VitalSigns& v);
        void _publishSnapshot(const VitalSigns& v);
        void _publishAlarms();
        void _taskLoop();

        EcgTask& _ecg;
        PpgTask& _ppg;
        BpTask&  _bp;
        BLEManager   _ble;
        AlarmManager _alarms;
        TaskHandle_t _taskHandle = nullptr;
        VitalsSource _source = nullptr;
};