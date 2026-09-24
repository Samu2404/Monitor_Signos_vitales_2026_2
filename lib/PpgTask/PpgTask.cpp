#include "PpgTask.h"


// #define PPG_DEBUG 1

PpgTask::PpgTask(NextionHMI& hmi): _hmi(hmi), _shared{}, _filter{} {
    
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

    ppg_filter_init(&_filter, sampleRateHz, PpgHpFc, PpgLpFc);   // Antes de crear la tarea

    _hmi.configWaveform(GraphId, GraphChannel, GraphRateHz);   // Antes de crear la tarea: la gráfica ya está registrada cuando llega la primera muestra
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
        uint16_t sample= analogRead(analogPin);
        uint32_t now= micros();
        
        // Filtrado PPG (pasa-altas 4 Hz)
        float filtered = ppg_filter_process(&_filter, (float)sample);

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

        // Señal filtrada -> rango del ADC (0..4095) para la gráfica
        int32_t g = (int32_t)lroundf(filtered * GraphGain) + GraphOffset;
        if (g < 0)    g = 0;
        if (g > 4095) g = 4095;

        _hmi.updateWaveform(GraphId, GraphChannel, (uint16_t)g);   // Misma muestra que usó el detector
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