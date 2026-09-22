#include "PpgTask.h"


// #define PPG_DEBUG 1

PpgTask::PpgTask(NextionHMI& hmi): _hmi(hmi), _shared{} {
    
}

void PpgTask::begin() {
    if (_taskHandle != nullptr) {
        return; // La tarea ya está en ejecución
    }
    analogReadResolution(12); // Configura la resolución de lectura analógica a 12 bits
    analogSetAttenuation(ADC_11db); // Configura la atenuación del ADC a 11 dB

    _mutex = xSemaphoreCreateMutex();
    if (_mutex == nullptr) {
        return;
    }

    _hmi.configWaveform(GraphId, GraphChannel, GraphRateHz);   // Antes de crear la tarea: la gráfica ya está registrada cuando llega la primera muestra
    xTaskCreatePinnedToCore(_taskEntry, "PpgTask", TaskStackSize, this, TaskPriority, &_taskHandle, TaskCore);
}


void PpgTask::_taskEntry(void* self) {
    static_cast <PpgTask*>(self)->_taskLoop();
}

void PpgTask::_taskLoop (){
    #ifdef PPG_DEBUG
        Serial.println("PpgTask: Iniciando bucle de muestreo");
        uint32_t lastUs= micros();
        uint32_t dtMin= UINT32_MAX, dtMax= 0, count = 0;
    #endif

    TickType_t lastWake = xTaskGetTickCount();

    for (;;){
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(SamplePeriodMs));
        uint16_t sample= analogRead(analogPin);
        uint32_t now= micros();
        
        // Procesar la muestra

        Snapshot snap{0, 0, sample}; // Aquí deberías implementar el cálculo de SpO2 y frecuencia cardíaca
            if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(1)) == pdTRUE) {
            _shared= snap;
                xSemaphoreGive(_mutex);
            }

        _hmi.updateWaveform(GraphId, GraphChannel, sample);   // Misma muestra que usó el detector
    };
}





 PpgTask:: Snapshot PpgTask::getSnapshot() {
    Snapshot snap{};
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(1)) == pdTRUE) {
        snap = _shared;
        xSemaphoreGive(_mutex);
    };
    return snap;
}