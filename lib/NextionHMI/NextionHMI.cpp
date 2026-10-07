#include "NextionHMI.h"

#define GraphHeight 255

// Descomenta para ver en el monitor serie (Serial/USB) cada punto que se envía a las gráficas.
// El Nextion debe estar en otro UART (Serial2); si comparte Serial, la depuración corrompe el enlace
// #define NEXTION_DEBUG

const char* const NextionHMI::NumObjects[ValCount] = {
    "nSpO2.val",
    "nHR.val",
    "nPR.val",
    "nHRV.val",
    "nT.val",
    "nRR.val"
};

NextionHMI* NextionHMI::_instance = nullptr;

void easyNexReadCustomCommand (){
    if (NextionHMI::_instance != nullptr) {
        NextionHMI::_instance->_onCustomCommand();
    }
}


NextionHMI::NextionHMI(HardwareSerial& serial)
    : _myNex(serial), _serial(serial) {
    for (uint8_t i = 0; i < ValCount; i++) {
        _wanted[i] = Unknown;
    }
    _invalidate();
}

void NextionHMI::begin(uint32_t baudRate, uint8_t rxPin, uint8_t txPin, uint32_t RefreshRate) {

    _baudRate = baudRate;
    _serial.begin(_baudRate, SERIAL_8N1, rxPin, txPin);
    _myNex.begin(_baudRate);

    if (RefreshRate > 0) {
        _refreshRate = RefreshRate;
    }

    if (_queue == nullptr) {
        _queue = xQueueCreate(QueueSize, sizeof(msg));
    }

    if (_vitalsBox == nullptr) {
        _vitalsBox = xQueueCreate(1, sizeof(Vitals));
    }
    if (_vitalsBox == nullptr || _queue == nullptr) {
        return ;
    }
    _instance = this;
    if (_taskHandle == nullptr) {
        xTaskCreatePinnedToCore(_taskEntry, "NextionHMI", TaskStackSize, this, TaskPriority, &_taskHandle, TaskCore);
    }
}

void NextionHMI::graphWaveform(uint8_t id, uint8_t channel, uint32_t value) {
    if (_queue == nullptr) {
        return;
    }
    msg m{msg::Wave, id, channel, (int32_t)value};
    xQueueSend(_queue, &m, 0);
}


bool  NextionHMI:: configWaveform(uint8_t id, uint8_t channel, uint32_t rateHz, uint32_t MapValue) {
    if (_queue == nullptr) {
        return false ;
    }
    msg m{msg::WaveConfig, id, channel, (int32_t)rateHz, MapValue};
    return xQueueSend(_queue, &m, 0) == pdTRUE;
}

void NextionHMI:: updateWaveform (uint8_t id, uint8_t channel, uint32_t value) {
    if (_queue == nullptr) {
        return;
    }
    msg m{msg::WaveUpdate, id, channel, (int32_t)value};
    xQueueSend(_queue, &m, 0);
}

void NextionHMI::writeSpO2(int spo2) {
    _enqueueNum(ValSpO2, spo2);
}

void NextionHMI::writeBPM(int bpm) {
    _enqueueNum(ValBPM, bpm);
}


void NextionHMI::writePulseRate(int pulseRate) {
    _enqueueNum(ValPulseRate, pulseRate);
}

void NextionHMI::writeHRvariance(int hrVariance) {
    _enqueueNum(ValHRV, hrVariance);
}

void NextionHMI::writeTemperature(float temperature) {
    _enqueueNum(ValTemperature, (int32_t)temperature);
}

void NextionHMI::writeRespirationRate(int respirationRate) {
    _enqueueNum(ValRespiration, respirationRate);
}


void NextionHMI::updateValues(int spo2, int bpm, int pulseRate, int hrVariance, float temperature, int respirationRate) {
    if (_vitalsBox == nullptr) {
        return;
    }
    Vitals vitals{spo2, bpm,pulseRate, hrVariance, temperature, respirationRate};
    xQueueOverwrite(_vitalsBox, &vitals);
}


void NextionHMI::invalidateCache() {
    if (_queue == nullptr) {
        return;
    }
    msg m{msg::Invalidate, 0, 0, 0};   // La caché solo la toca la tarea del HMI
    xQueueSend(_queue, &m, 0);
}


NextionHMI::WaveChannel* NextionHMI::_findWaveform(uint8_t id, uint8_t channel) {
    for (uint8_t i = 0; i < _waveCount; i++) {
        if (_waves[i].id == id && _waves[i].channel == channel) {
            return &_waves[i];
        }
    }
    return nullptr;
}

void NextionHMI::_setNum(ValueId slot, int32_t value) {
    _wanted[slot] = value;
    _syncNum(slot);
}

void NextionHMI::_syncNum(ValueId slot) {
    if (!_showsNumbers() || _wanted[slot] == Unknown || _lastValues[slot] == _wanted[slot]) {
        return;
    }
    _myNex.writeNum(NumObjects[slot], _wanted[slot]);
    _lastValues[slot] = _wanted[slot];
}

void NextionHMI::_invalidate() {
    for (uint8_t i = 0; i < ValCount; i++) {
        _lastValues[i] = Unknown;
    }
    _lastValuesTime = millis() - ValuesPeriodMs;  // El próximo updateValues() no espera
}

void NextionHMI::_onPageLoaded(uint8_t page) {
    _page = page;
    _fullWfId = AllWaves;  // Por defecto, la página FullWf muestra todas las gráficas
    _invalidate();   // Al cargar una página el Nextion pierde lo que mostraba
    for (uint8_t i = 0; i < ValCount; i++) {
        _syncNum(static_cast<ValueId>(i));
    }
#ifdef NEXTION_DEBUG
    Serial.printf("[nextion] pagina %u\n", page);
#endif
}

void NextionHMI::_listen() {
    while (_serial.available() > 0) {
        int c = _serial.peek();
        if (c == '#') {                                  // Comando propio: # <len> <grupo> <datos>
            if (_serial.available() < 3) {
                return;                                  // Espera al resto sin bloquear la tarea
            }
            _myNex.currentPageId = NoPageEvent;
            _myNex.NextionListen();
            if (_myNex.currentPageId != NoPageEvent) {   // Llegó printh 23 02 50 <página>
                _onPageLoaded((uint8_t)_myNex.currentPageId);
            }
        } else if (c == 0x66) {                          // Respuesta a "sendme": 66 <página> FF FF FF
            if (_serial.available() < 2) {
                return;
            }
            _serial.read();
            _onPageLoaded((uint8_t)_serial.read());
        } else {
            _serial.read();   // Códigos de estado/error y terminadores FF: NextionListen() se trabaría 100 ms con ellos
        }
    }
}

void NextionHMI::_onCustomCommand() {
    switch (_myNex.cmdGroup){
        case 'W':                                   // printh 23 02 57 <id>: gráfica visible en full_wf
            _fullWfId = (uint8_t)_myNex.readByte();
            break;
        default:                                    // Grupo desconocido: se descartan sus datos
            for (uint8_t i =1 ; i < _myNex.cmdLength; i++){
                _myNex.readByte();
            }
            break;
    }
}

void NextionHMI::_sendWave(uint8_t id, uint8_t channel, uint32_t value, uint32_t MapValue) {
    if (value > MapValue) {
        value = MapValue;                                          // Nunca pasar del tope de la gráfica
    }
    uint32_t y = (uint32_t)((uint64_t)value * GraphHeight / MapValue);   // 64 bits: evita desbordar con AFE de 24 bits
    char command[32];
    snprintf(command, sizeof(command), "add %d,%d,%lu", id, channel, (unsigned long)y);
#ifdef NEXTION_DEBUG
    static uint32_t lastSent[8] = {0};                 // Temporal: depuración (solo para ids 0-7)
    uint32_t now = micros();
    Serial.printf("[nextion] graf %d: %s (dt=%lu us)\n", id, command, (unsigned long)(now - lastSent[id & 7]));
    lastSent[id & 7] = now;
#endif
    _myNex.writeStr(command);
}

void NextionHMI::_taskEntry(void* self) {
    static_cast<NextionHMI*>(self)->_taskLoop();
}


void NextionHMI::_taskLoop() {
    _myNex.writeStr("bkcmd=0");   // Sin respuestas de estado: el RX solo trae comandos útiles
    _myNex.writeStr("sendme");    // Pregunta la página actual (por si el Nextion ya estaba encendido)

    msg m;
    for (;;) {
        if (xQueueReceive(_queue, &m, pdMS_TO_TICKS(TaskPollMs)) == pdTRUE) {
            _handle(m);
        }
        _listen();
        _flushVitals();
    }
}

void NextionHMI::_handle(const msg& m) {
    switch (m.type) {
        case msg::Wave: {
            if (!_showsWave(m.id)) {
                break;
            }
            WaveChannel* wave = _findWaveform(m.id, m.channel);
            _sendWave(m.id, m.channel, (uint32_t)m.value, wave ? wave->mapValue : DefaultMapValue);
            break;
        }
        case msg::Num :
            if (m.id < ValCount) {
                _setNum(static_cast<ValueId>(m.id), m.value);
            }
            break;
        case msg:: WaveUpdate:
            if (_showsWave(m.id)) {
                _updateWave(m.id, m.channel, (uint32_t)m.value);
            }
            break;
        case msg::WaveConfig:
            _configWave(m.id, m.channel, (uint32_t)m.value, m.mapValue);
            break;
        case msg::Invalidate:
            _onPageLoaded(_page);
            break;
        default:
            break;
    }
}

void NextionHMI::_enqueueNum(ValueId slot, int32_t value) {
    if (_queue == nullptr) {
        return;
    }
    msg m{msg::Num, (uint8_t)slot, 0, value};
    xQueueSend(_queue, &m, 0);
}



bool NextionHMI::_configWave(uint8_t id, uint8_t channel, uint32_t rateHz, uint32_t MapValue) {
    uint32_t period = rateHz ? 1000000UL  / rateHz : 0;
    if (MapValue == 0) {
        MapValue = DefaultMapValue;   // Evita la división por cero al escalar
    }

    WaveChannel* wave = _findWaveform(id, channel);
    if (wave == nullptr) {
        if (_waveCount >= MaxWaveforms) {
            return false;
        }
        wave = &_waves[_waveCount++];
        wave->id = id;
        wave->channel = channel;
        wave->lastTime = micros() - period;  // El primer punto se envía de inmediato
    }
    wave->period = period;
    wave->mapValue = MapValue;
    return true;
}

void NextionHMI::_updateWave(uint8_t id, uint8_t channel, uint32_t value) {
    WaveChannel* wave = _findWaveform(id, channel);
    if (wave == nullptr) {
        if (!_configWave(id, channel, _refreshRate)) {
            return;
        }
        wave = _findWaveform(id, channel);
    }

    uint32_t now = micros();
    uint32_t elapsed = now - wave->lastTime;
    if (elapsed < wave->period) {
        return;
    }
    // Avanza un periodo exacto para no acumular deriva; si se atrasó mucho, resincroniza
    wave->lastTime = (elapsed >= 2 * wave->period) ? now : wave->lastTime + wave->period;
    _sendWave(id, channel, value, wave->mapValue);
}

void NextionHMI::_flushVitals() {
    uint32_t now = millis();
    if (now - _lastValuesTime < ValuesPeriodMs) {
        return;
    }
    Vitals v;
    if (xQueuePeek(_vitalsBox, &v,0) != pdTRUE) {
        return;
    }
    _lastValuesTime = now;

    _setNum(ValSpO2, v.spo2);
    _setNum(ValBPM, v.bpm);
    _setNum(ValPulseRate, v.pulseRate);
    _setNum(ValHRV, v.hrVariance);
    _setNum(ValTemperature, (int32_t)v.temperature);
    _setNum(ValRespiration, v.respirationRate);
}
