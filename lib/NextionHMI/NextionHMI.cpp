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
    "nRR.val",
    "nSistolica.val",
    "nDiastolica.val",
    "nCalEst.val",
    "jCal.val"
};

// Fondo de tAlarm: -, leve, media, grave (mismos colores que el .HMI)
const uint16_t NextionHMI::AlarmBarColors[4] = { 0, 13403, 60644, 41317 };

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
    if (_calBox == nullptr) {
        _calBox = xQueueCreate(1, sizeof(CalRequest));
    }
    if (_alarmBox == nullptr) {
        _alarmBox = xQueueCreate(1, sizeof(AlarmBar));
    }
    if (_settingBox == nullptr) {
        _settingBox = xQueueCreate(SettingQueueSize, sizeof(SettingCmd));
    }

    if (_vitalsBox == nullptr || _queue == nullptr || _calBox == nullptr || _alarmBox == nullptr ||
        _settingBox == nullptr) {
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
    _enqueueNum(ValTemperature, (int32_t)lroundf(temperature * 10.0f));   // xfloat con 1 decimal (vvs1=1)
}

void NextionHMI::writeRespirationRate(int respirationRate) {
    _enqueueNum(ValRespiration, respirationRate);
}

void NextionHMI::writeBloodPressure(int sistolica, int diastolica) {
    _enqueueNum(ValSistolica, sistolica);
    _enqueueNum(ValDiastolica, diastolica);
}

void NextionHMI::writeCalibrationStatus (int state , int progress) {
    _enqueueNum(ValCalState, state);
    _enqueueNum(ValCalProgress, progress);
}



bool NextionHMI::takeCalibrationRequest(int& sbp, int& dbp) {
    CalRequest req;
    if (_calBox == nullptr || xQueueReceive(_calBox, &req, 0) != pdTRUE) {
        return false;
    } 
    sbp = req.sbp;
    dbp = req.dbp;

    return true;
}

bool NextionHMI::takeSetting(uint8_t& id, uint16_t& value) {
    SettingCmd cmd;
    if (_settingBox == nullptr || xQueueReceive(_settingBox, &cmd, 0) != pdTRUE) {
        return false;
    }
    id = cmd.id;
    value = cmd.value;
    return true;
}

void NextionHMI::cancelAudioPause() {
    if (_queue == nullptr) {
        return;
    }
    msg m{msg::CancelPause, 0, 0, 0};
    xQueueSend(_queue, &m, 0);
}

void NextionHMI::writeAlarmBar(const char* text, uint8_t level) {
    if (_alarmBox == nullptr) {
        return;
    }
    if (text == nullptr) {
        text = "";
    }
    if (level == _barQueued.level && strncmp(text, _barQueued.text, sizeof(_barQueued.text) - 1) == 0) {
        return;                         // Sin cambios: no se despierta a la tarea
    }
    AlarmBar bar;
    strncpy(bar.text, text, sizeof(bar.text) - 1);
    bar.text[sizeof(bar.text) - 1] = '\0';
    bar.level = level;
    xQueueOverwrite(_alarmBox, &bar);   // Solo importa la última
    _barQueued = bar;
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
    _barShown = false;
    _lastValuesTime = millis() - ValuesPeriodMs;  // El próximo updateValues() no espera
}

void NextionHMI::_onPageLoaded(uint8_t page) {
    _page = page;
    _fullWfId = AllWaves;  // Por defecto, la página FullWf muestra todas las gráficas
    _invalidate();   // Al cargar una página el Nextion pierde lo que mostraba
    for (uint8_t i = 0; i < ValCount; i++) {
        _syncNum(static_cast<ValueId>(i));
    }
    _syncAlarmBar();       // La barra de estado de cada página arranca con tAlarm oculto
#ifdef NEXTION_DEBUG
    Serial.printf("[nextion] pagina %u\n", page);
#endif
}

void NextionHMI::_syncAlarmBar() {
    if (_barShown) {
        return;
    }
    if (_barWanted.level == 0 || _barWanted.text[0] == '\0') {
        _myNex.writeStr("vis tAlarm,0");
    } else {
        uint8_t level = _barWanted.level > 3 ? 3 : _barWanted.level;
        _myNex.writeStr("tAlarm.txt", _barWanted.text);
        _myNex.writeNum("tAlarm.bco", AlarmBarColors[level]);
        _myNex.writeStr("vis tAlarm,1");
    }
    _barShown = true;
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
    // Se leen todos los datos antes de interpretarlos (cmdLength cuenta también el grupo)
    uint8_t data[MaxCmdData];
    uint8_t len = 0;
    for (uint8_t i = 1; i < _myNex.cmdLength; i++) {
        uint8_t b = (uint8_t)_myNex.readByte();
        if (len < MaxCmdData) {
            data[len++] = b;                        // Los que no caben se descartan
        }
    }
    if (_cmdLog != nullptr) {
        _cmdLog((char)_myNex.cmdGroup, data, len);
    }

    switch (_myNex.cmdGroup){
        case 'W':                                   // printh 23 02 57 <id>: gráfica visible en full_wf
            if (len >= 1) {
                _fullWfId = data[0];
            }
            break;
        case 'C':                                   // printh 23 02 67 <sbp> <dbp>: calibración solicitada
            if (len >= 2) {
                CalRequest req;
                req.sbp = data[0];
                req.dbp = data[1];
                xQueueOverwrite(_calBox, &req);   // Solo se guarda la última solicitud
            }
            break;
        case 'S':                                   // printh 23 04 53 <id> + prints <valor>,2
            if (len >= 3) {
                SettingCmd cmd;
                cmd.id = data[0];
                cmd.value = data[1] | (data[2] << 8);   // prints ...,2: little endian
                xQueueSend(_settingBox, &cmd, 0);   // Cola llena: se pierde (loop() la vacía cada 10 ms)
            }
            break;
        default:                                    // Grupo desconocido: sus datos ya se leyeron
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
    _myNex.writeStr("main.vaPausa.val=0");   // El ESP32 recién arranca sin pausa: la pantalla tampoco

    msg m;
    for (;;) {
        if (xQueueReceive(_queue, &m, pdMS_TO_TICKS(TaskPollMs)) == pdTRUE) {
            _handle(m);
        }
        _listen();
        AlarmBar bar;
        if (xQueueReceive(_alarmBox, &bar, 0) == pdTRUE) {
            _barWanted = bar;
            _barShown = false;
        }
        _syncAlarmBar();
        _flushVitals();
        _flushSlow();
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
            if (m.id >= ValCount) {
                break;
            }
            if (_isSlow(static_cast<ValueId>(m.id))) {
                _pushSlow(static_cast<ValueId>(m.id), m.value);
            } else {
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
        case msg::CancelPause:
            _myNex.writeStr("main.vaPausa.val=0");
            _myNex.writeStr("click mPausaVer,1");   // Oculta pPause y tPause ya, sin esperar a tmPausa
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

void NextionHMI::_pushSlow(ValueId slot, int32_t value) {
    if (value == 0 || _wanted[slot] == 0 || _wanted[slot] == Unknown) {
        _slow[slot] = {};            // Pérdida o primer valor: no se espera a la ventana
        _setNum(slot, value);
        return;
    }
    _slow[slot].sum += value;
    _slow[slot].count++;
}

void NextionHMI::_flushSlow() {
    uint32_t now = millis();
    if (now - _slowTime < SlowPeriodMs) {
        return;
    }
    _slowTime = now;
    for (uint8_t i = 0; i < ValCount; i++) {
        SlowAvg& s = _slow[i];
        if (s.count == 0) {
            continue;
        }
        _setNum(static_cast<ValueId>(i), (s.sum + s.count / 2) / s.count);   // Promedio redondeado
        s = {};
    }
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
    _setNum(ValTemperature, (int32_t)lroundf(v.temperature * 10.0f));      // xfloat con 1 decimal (vvs1=1)
    _setNum(ValRespiration, v.respirationRate);
}
