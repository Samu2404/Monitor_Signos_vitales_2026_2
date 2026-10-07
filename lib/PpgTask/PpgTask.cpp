#include "PpgTask.h"


// #define PPG_DEBUG 1

PpgTask::PpgTask(NextionHMI& hmi): _hmi(hmi), _shared{}, _filter{} {
    
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

    ppg_filter_init(&_filter, sampleRateHz, PpgHpFc, PpgLpFc);   // Antes de crear la tarea

    _hmi.configWaveform(GraphId, GraphChannel, 0);   // Solo registra la escala; el ritmo lo da _graph. Antes de crear la tarea
    xTaskCreatePinnedToCore(_taskEntry, "PpgTask", TaskStackSize, this, TaskPriority, &_taskHandle, TaskCore);
}


void PpgTask::_taskEntry(void* self) {
    static_cast <PpgTask*>(self)->_taskLoop();
}

bool PpgTask::_detectPeak(float x, uint32_t nowUs) {
    PeakDetector& p = _pk;

    p.amp *= PeakAmpDecay;
    if (x > p.amp) p.amp = x;

    float thr = p.amp * PeakThreshFrac;
    bool valid = p.amp >= PeakMinAmp;

    if (!p.above) {
        if (valid && x > thr) {
            p.above = true;
            p.peakVal = x;
            p.peakUs = nowUs;
        }
        return false;
    }

    if (x > p.peakVal) {
        p.peakVal = x;
        p.peakUs = nowUs;
    }
    if (x >= thr) {
        return false;
    }

    // La señal bajó del umbral: el pulso terminó y su máximo está en peakUs
    p.above = false;
    if (p.lastBeatUs == 0) {
        p.lastBeatUs = p.peakUs;
        return false;
    }

    uint32_t rrMs = (p.peakUs - p.lastBeatUs) / 1000UL;
    if (rrMs < PeakRefractoryMs) {
        return false;                   // Rebote / pico secundario: se ignora sin mover lastBeatUs
    }
    p.lastBeatUs = p.peakUs;
    if (rrMs > PeakMaxRrMs) {
        p.rrN = 0;                      // Se perdieron latidos: reinicia el promedio
        return false;
    }

    p.rr[p.rrIdx] = (float)rrMs;
    p.rrIdx = (p.rrIdx + 1) % RrCount;
    if (p.rrN < RrCount) p.rrN++;

    float sum = 0.0f;
    for (uint8_t i = 0; i < p.rrN; i++) sum += p.rr[i];
    p.bpm = (uint16_t)lroundf(60000.0f * p.rrN / sum);
    return true;
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

        // Filtrado PPG (pasa-banda 0.5–4 Hz)
        float filtered = ppg_filter_process(&_filter, (float)ppgSample);

        bool beat = _detectPeak(filtered, now);
        if (_pk.lastBeatUs != 0 && (now - _pk.lastBeatUs) > PeakTimeoutMs * 1000UL) {
            if (_pk.bpm != 0) {
                _pk.bpm = 0;
                _pk.rrN = 0;
                _hmi.writePulseRate(0);
            }
        } else if (beat && _pk.bpm > 0) {
            _hmi.writePulseRate(_pk.bpm);
        }

        Snapshot snap{0, _pk.bpm, sample, filtered};   // SpO2 pendiente de implementar
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