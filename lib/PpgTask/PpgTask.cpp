#include "PpgTask.h"




PpgTask::PpgTask(NextionHMI& hmi): _hmi(hmi), _shared{}, _proc{} {
    
}

bool PpgTask::_init(float fs) {
    _mutex = xSemaphoreCreateMutex();
    if (_mutex == nullptr) {
        return false;
    }
    _fs = fs;
    ppg_init(&_proc, fs);                            // Filtro + detector
    _hmi.configWaveform(GraphId, GraphChannel, 0);   // Solo registra la escala; el ritmo lo da _graph
    _scaler.begin(fs, GraphMinAmp);
    return true;
}

void PpgTask::begin(SampleSource source) {
    if (_mutex != nullptr) {
        return; // Ya iniciado (en cualquier modo)
    }
    _source = source;
    analogReadResolution(12); // Configura la resolución de lectura analógica a 12 bits
    analogSetAttenuation(ADC_11db); // Configura la atenuación del ADC a 11 dB

    if (!_init(ADCsampleRateHz)) {   // Antes de crear la tarea
        return;
    }
    xTaskCreatePinnedToCore(_taskEntry, "PpgTask", TaskStackSize, this, TaskPriority, &_taskHandle, TaskCore);
}

void PpgTask::beginExternal(float fs) {
    if (_mutex != nullptr) {
        return; // Ya iniciado (en cualquier modo)
    }
    _init(fs);   // Sin tarea: process() lo llama el productor (AfeTask)
}

void PpgTask::process(int32_t sample, uint32_t tUs) {
    if (_mutex == nullptr) {
        return; // No se llamó a begin()/beginExternal()
    }
    if (tUs == 0) {
        tUs = (uint32_t)((uint64_t)_n * 1000000ULL / _fs);   // fs exacta (AFE): tiempo por conteo de muestras
    }
    _n++;

    // Filtro pasa-banda 0.5–4 Hz + detector de pulsos (incluye el timeout de 3 s)
    PPG_Result r = ppg_process(&_proc, (float)sample, tUs);
    uint16_t pulseRate = (uint16_t)lroundf(r.bpm);

    if (r.beat && pulseRate > 0) {
        _hmi.writePulseRate(pulseRate);
        _shownPulseRate = pulseRate;
    } else if (pulseRate == 0 && _shownPulseRate != 0) {
        _hmi.writePulseRate(0);           // Se perdió el pulso: se avisa una sola vez
        _shownPulseRate = 0;
    }

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(1)) == pdTRUE) {
        _shared.pulseRate= pulseRate;
        _shared.sample   = sample;
        _shared.filtered = r.filtered;
        xSemaphoreGive(_mutex);
    }

    // Gráfica: cruda o filtrada, centrada y escalada (el detector ya usó la original)
    uint32_t g = _scaler.scale(GraphFiltered ? r.filtered : (float)sample);
    if (_graph.push(g)) {   // Mín y máx de cada ventana
        _hmi.graphWaveform(GraphId, GraphChannel, _graph.first());
        _hmi.graphWaveform(GraphId, GraphChannel, _graph.second());
    }
}


void PpgTask::_taskEntry(void* self) {
    static_cast <PpgTask*>(self)->_taskLoop();
}

void PpgTask::_taskLoop (){
    #ifdef PPG_DEBUG
        Serial.println("PpgTask: Iniciando bucle de muestreo");
    #endif

    TickType_t lastWake = xTaskGetTickCount();

    for (;;){
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(SamplePeriodMs));
        uint16_t sample= _source ? _source() : analogRead(analogPin);
        uint32_t now= micros();

        // El sensor baja de voltaje con más sangre: se invierte para que el pico sistólico quede arriba
        uint16_t ppgSample = InvertSignal ? (uint16_t)(4095 - sample) : sample;

        process(ppgSample, now);   // El ADC tiene jitter: aquí sí conviene la marca de tiempo real
    }
}


PpgTask::Snapshot PpgTask::getSnapshot() {
    Snapshot snap{};
    if (_mutex == nullptr) {
        return snap;
    }
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(1)) == pdTRUE) {
        snap = _shared;
        xSemaphoreGive(_mutex);
    }
    return snap;
}

void PpgTask::setSpO2(float spo2) {
    if (_mutex == nullptr) {
        return;
    }
    uint16_t value = (uint16_t)lroundf(spo2);
    if (value > 100) value = 100;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(1)) == pdTRUE) {
        _shared.spo2 = value;
        xSemaphoreGive(_mutex);
    }
    if (value != _shownSpO2) {
        _hmi.writeSpO2(value);
        _shownSpO2 = value;
    }
}