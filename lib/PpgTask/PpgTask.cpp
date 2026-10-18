#include "PpgTask.h"


// #define PPG_DEBUG 1

PpgTask::PpgTask(NextionHMI& hmi): _hmi(hmi), _shared{}, _proc{} {
    
}

void PpgTask::begin(SampleSource source) {
    if (_taskHandle != nullptr) {
        return; // La tarea ya está en ejecución
    }
    _source = source;
    analogReadResolution(12); // Configura la resolución de lectura analógica a 12 bits
    analogSetAttenuation(ADC_11db); // Configura la atenuación del ADC a 11 dB

    _mutex = xSemaphoreCreateMutex();
    if (_mutex == nullptr) {
        return;
    }

    ppg_init(&_proc, ADCsampleRateHz);   // Filtro + detector. Antes de crear la tarea

    _hmi.configWaveform(GraphId, GraphChannel, 0);   // Solo registra la escala; el ritmo lo da _graph. Antes de crear la tarea
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
        uint16_t sample= _source ? _source() : analogRead(analogPin);
        uint32_t now= micros();

        // El sensor baja de voltaje con más sangre: se invierte para que el pico sistólico quede arriba
        uint16_t ppgSample = InvertSignal ? (uint16_t)(4095 - sample) : sample;

        // Filtro pasa-banda 0.5–4 Hz + detector de pulsos (incluye el timeout de 3 s)
        PPG_Result r = ppg_process(&_proc, (float)ppgSample, now);
        float filtered = r.filtered;
        uint16_t pulseRate = (uint16_t)lroundf(r.bpm);

        if (r.beat && pulseRate > 0) {
            _hmi.writePulseRate(pulseRate);
            _shownPulseRate = pulseRate;
        } else if (pulseRate == 0 && _shownPulseRate != 0) {
            _hmi.writePulseRate(0);           // Se perdió el pulso: se avisa una sola vez
            _shownPulseRate = 0;
        }

        Snapshot snap{0, pulseRate, sample, filtered};   // SpO2 pendiente de implementar
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(1)) == pdTRUE) {
            _shared= snap;
            xSemaphoreGive(_mutex);
        }

        uint16_t g = ppgSample;               // Señal cruda (ya invertida si InvertSignal)
        if (GraphFiltered) {
            // Señal filtrada -> rango del ADC (0..4095) para la gráfica
            int32_t f = (int32_t)lroundf(filtered * GraphGain) + GraphOffset;
            if (f < 0)    f = 0;
            if (f > 4095) f = 4095;
            g = (uint16_t)f;
        }

        if (_graph.push(g)) {   // Mín y máx de cada ventana
            _hmi.graphWaveform(GraphId, GraphChannel, _graph.first());
            _hmi.graphWaveform(GraphId, GraphChannel, _graph.second());
        }
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