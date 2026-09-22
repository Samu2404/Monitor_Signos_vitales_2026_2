#include "NextionHMI.h"

#define GraphHeight 255

// Descomenta SOLO si el Nextion NO está en Serial (UART0); si no, la depuración corrompe el enlace
// #define NEXTION_DEBUG



NextionHMI::NextionHMI(HardwareSerial& serial)
    : _myNex(serial), _serial(serial) {
    invalidateCache();
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
    for (uint8_t i = 0; i < ValCount; i++) {
        _lastValues[i] = INT32_MIN;
    }
    _lastValuesTime = millis() - ValuesPeriodMs;  // El próximo updateValues() no espera
}





NextionHMI::WaveChannel* NextionHMI::_findWaveform(uint8_t id, uint8_t channel) {
    for (uint8_t i = 0; i < _waveCount; i++) {
        if (_waves[i].id == id && _waves[i].channel == channel) {
            return &_waves[i];
        }
    }
    return nullptr;
}

void NextionHMI::_writeNum(ValueId slot, const char* object, int32_t value) {
    _myNex.writeNum(object, value);
    _lastValues[slot] = value;
}

void NextionHMI::_updateNum(ValueId slot, const char* object, int32_t value) {
    if (_lastValues[slot] != value) {
        _writeNum(slot, object, value);
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
    msg m;
    for (;;) {
        if (xQueueReceive(_queue, &m, pdMS_TO_TICKS(TaskPollMs)) == pdTRUE) {
            _handle(m);
        }
        _flushVitals();
    }
}

void NextionHMI::_handle(const msg& m) {
    switch (m.type) {
        case msg::Wave: {
            WaveChannel* wave = _findWaveform(m.id, m.channel);
            _sendWave(m.id, m.channel, (uint32_t)m.value, wave ? wave->mapValue : DefaultMapValue);
            break;
        }
        case msg::Num :{
            static const char* objects  [] = {
                "nSpO2.val",
                "nHR.val",
                "nPR.val",
                "nHRV.val",
                "nT.val",
                "nRR.val"
            };
            static_assert(sizeof(objects) / sizeof(objects[0]) == ValCount, "objects array size mismatch");
            if (m.id < ValCount) {
                _writeNum(static_cast<ValueId>(m.id), objects[m.id], m.value);
            }
            break;
        }
        case msg:: WaveUpdate:
            _updateWave(m.id, m.channel, (uint32_t)m.value);
            break;
        case msg::WaveConfig:
            _configWave(m.id, m.channel, (uint32_t)m.value, m.mapValue);
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

    _updateNum(ValSpO2, "nSpO2.val", v.spo2);
    _updateNum(ValBPM, "nHR.val", v.bpm);
    _updateNum(ValPulseRate, "nPR.val", v.pulseRate);
    _updateNum(ValHRV, "nHRV.val", v.hrVariance);
    _updateNum(ValTemperature, "nT.val", (int32_t)v.temperature);
    _updateNum(ValRespiration, "nRR.val", v.respirationRate);
}