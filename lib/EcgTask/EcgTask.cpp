#include <EcgTask.h>

volatile bool EcgTask::s_leadOffFlag = false;

EcgTask::EcgTask(NextionHMI& hmi) : _hmi(hmi), _shared{} {
}

bool EcgTask::_init(float fs) {
    pt_init(&_pt, fs);
    _mutex = xSemaphoreCreateMutex();
    if (_mutex == nullptr) {
        return false;
    }
    _hmi.configWaveform(GraphId, GraphChannel, 0);  
    return true;
}

void EcgTask::begin() {

    if (_mutex != nullptr) {
       return;
    }
    analogReadResolution(12); // Configura la resolución de lectura analógica a 12 bits
    analogSetAttenuation(ADC_11db); // Configura la atenuación del ADC a 11 dB

    if (!_init(ADCsampleRateHz)) {
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

    xTaskCreatePinnedToCore(_taskEntry, "EcgTask", TaskStackSize, this, TaskPriority, &_taskHandle, TaskCore);
}

void EcgTask:: beginExternal(float fs){
        if (_mutex != nullptr) {
        return;
        }
        _init(fs);
}

bool EcgTask:: process (int32_t sample, uint32_t tUs){
    if (_mutex == nullptr) {
        return false;
    }

    PT_Result result= tUs ? pt_process_t(&_pt, (float) sample, tUs) :
                        pt_process(&_pt, (float) sample);
    
    Snapshot snap {result.bpm, pt_beats(&_pt),sample};

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(1)) == pdTRUE) {
        _shared= snap;
        xSemaphoreGive(_mutex);
    }

    if (_graph.push((uint32_t) sample)) {   
        _hmi.graphWaveform(GraphId, GraphChannel, _graph.first());
        _hmi.graphWaveform(GraphId, GraphChannel, _graph.second());
    }
    if (result.beat && result.bpm > 0.0f) {
        _hmi.writeBPM((int)result.bpm);
    }
    return result.beat;
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
        process (sample, now);
        
        #ifdef DEBUG
        uint32_t dt = now - lastUs;
        lastUs = now;
        if (dt < dtMin) dtMin = dt;
        if (dt > dtMax) dtMax = dt;
        if (++count >= 500) {                    // ~1 vez por segundo a 500 Hz
            Serial.printf("dt %lu..%lu us | raw=%u bpm=%.1f\n",
                          (unsigned long)dtMin, (unsigned long)dtMax, sample, getSnapshot().bpm);
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