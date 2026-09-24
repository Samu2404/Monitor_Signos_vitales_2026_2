#include <EcgTask.h>

volatile bool EcgTask::s_leadOffFlag = false;

EcgTask::EcgTask(NextionHMI& hmi) : _hmi(hmi), _shared{} {
}

void EcgTask::begin() {
    if (_taskHandle != nullptr) {
        return; // La tarea ya está en ejecución
    }
    analogReadResolution(12); // Configura la resolución de lectura analógica a 12 bits
    analogSetAttenuation(ADC_11db); // Configura la atenuación del ADC a 11 dB

    pt_init(&_pt, sampleRateHz);
    _mutex = xSemaphoreCreateMutex();
    if (_mutex == nullptr) {
        return;
    }
    #if ENABLE_LEAD_OFF_DETECT
    pinMode(isrPinLOMinus, INPUT_PULLUP);
    pinMode(isrPinLOPlus, INPUT_PULLUP);
    // CHANGE: nos interesan ambos flancos (RISING = se soltó, FALLING = se reconectó).
    // La ISR solo marca la bandera; _electrodeCheck() decide el estado leyendo los pines.
    attachInterrupt(digitalPinToInterrupt(isrPinLOMinus), _onLeadOffChange, CHANGE);
    attachInterrupt(digitalPinToInterrupt(isrPinLOPlus),  _onLeadOffChange, CHANGE);
    #endif

    _hmi.configWaveform(GraphId, GraphChannel, GraphRateHz);   // Antes de crear la tarea: la gráfica ya está registrada cuando llega la primera muestra
    xTaskCreatePinnedToCore(_taskEntry, "EcgTask", TaskStackSize, this, TaskPriority, &_taskHandle, TaskCore);
}

void  EcgTask:: _taskEntry(void* self) {
    static_cast <EcgTask*>(self)->_taskLoop();
}

void EcgTask :: _taskLoop (){
    #ifdef DEBUG
        Serial.println("EcgTask: Iniciando bucle de muestreo");
            uint32_t lastUs= micros();
            uint32_t dtMin= UINT32_MAX, dtMax= 0, count = 0;

    #endif

    TickType_t lastWake = xTaskGetTickCount();

    for (;;){

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(SamplePeriodMs));
        #if ENABLE_LEAD_OFF_DETECT
        _electrodeCheck();
        #endif
        uint16_t sample= analogRead(analogPin);
        uint32_t now= micros();

        #if ENABLE_LEAD_OFF_DETECT
        if (_leadOffState) {
           Snapshot snap{0.0f, 0, sample};
              if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(1)) == pdTRUE) {
                 _shared= snap;
                 xSemaphoreGive(_mutex);
                }
        continue;
        }
        #endif

        PT_Result result= pt_process_t(&_pt,(float) sample, now);
        Snapshot snap{result.bpm, pt_beats(&_pt), sample};
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(1)) == pdTRUE) {
            _shared= snap;
            xSemaphoreGive(_mutex);
        }

        _hmi.updateWaveform(GraphId, GraphChannel, sample);   // Misma muestra que usó el detector

        if (result.beat && result.bpm > 0.0f) {
            _hmi.writeBPM((int)result.bpm);
        }
        
        #ifdef DEBUG
        uint32_t dt = now - lastUs;
        lastUs = now;
        if (dt < dtMin) dtMin = dt;
        if (dt > dtMax) dtMax = dt;
        if (result.beat) {
            Serial.printf("LATIDO #%lu  bpm=%.1f  inst=%.1f\n",
                          (unsigned long)pt_beats(&_pt), result.bpm, result.bpm_inst);
        }
        if (++count >= 333) {                    // ~1 vez por segundo
            Serial.printf("dt %lu..%lu us | raw=%u integ=%.0f umbral=%.0f bpm=%.1f\n",
                          (unsigned long)dtMin, (unsigned long)dtMax,
                          sample, result.integrated, result.threshold, result.bpm);
            dtMin = UINT32_MAX; dtMax = 0; count = 0;
        }
        #endif
    }
}

EcgTask::Snapshot EcgTask::getSnapshot() {
    Snapshot snap {};
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(1)) == pdTRUE) {
        snap = _shared;
        xSemaphoreGive(_mutex);
    }
    return snap;
}

#if ENABLE_LEAD_OFF_DETECT
void IRAM_ATTR EcgTask::_onLeadOffChange() {
    s_leadOffFlag = true;   // Solo señaliza; nada de I/O ni de HMI aquí dentro
}

void EcgTask::_electrodeCheck() {
    if (!s_leadOffFlag) {
        return; // Sin cambios desde la última revisión
    }
    s_leadOffFlag = false;

    // AD8232 en modo DC (AC/DC=GND, 3 electrodos): HIGH = desconectado, LOW = conectado
    bool minusOff = digitalRead(isrPinLOMinus) == HIGH;
    bool plusOff  = digitalRead(isrPinLOPlus)  == HIGH;
    bool _leadoffState = minusOff || plusOff;
    
    if (_leadoffState ) {
        pt_reset(&_pt); 
    }
    // TODO: exponer un método en NextionHMI (p. ej. showElectrodeStatus(bool)) y llamarlo aquí,
    // y opcionalmente pausar/anular el BPM mientras desconectado == true para no mostrar datos falsos.
    #ifdef DEBUG
    Serial.printf("Electrodo: LOD-=%d LOD+=%d -> %s\n",
                  minusOff, plusOff, _leadoffState ? "DESCONECTADO" : "OK");
    #endif
}
#endif // ENABLE_LEAD_OFF_DETECT