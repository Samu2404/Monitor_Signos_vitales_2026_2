#include "SpO2Processor.h"


static const float SPO2_LOW  = 95.0f;
static const float SPO2_HIGH = 99.0f;

SpO2Processor::SpO2Processor() {
    _rng = 0x12345678;
    reset(0);
}

void SpO2Processor::reset(uint32_t nowMs) {
    _initialized = false;
    _smooth = 0.0f;
    _baseline = 0.0f;
    _fsEstimate = 500.0f;

    _blockStart = nowMs;
    _blockCount = 0;
    _blockSum = 0.0;
    _blockMin = 0.0f;
    _blockMax = 0.0f;
    _prevMin = 0.0f;
    _prevMax = 0.0f;
    _prevSum = 0.0;
    _prevCount = 0;
    _havePrev = false;

    _baseHead = 0;
    _baseFilled = 0;
    _baseSum = 0.0;
    _decimAcc = 0.0f;
    _decimCount = 0;

    _dMin = _dMax = 0.0f;
    _dCenter = 0.0f;
    _dHyst = SPO2_MIN_AC_COUNTS / 4.0f;
    _prevBlockP2P = 0.0f;
    _armed = false;
    _beatHead = 0;
    _beatCount = 0;

    _state = NO_SIGNAL;
    _stableSince = nowMs;
    _invalidBlocks = 0;

    _target = 97.5f;
    _redDcRatio = 0.8f;
    _rng ^= (uint32_t)micros() | 1u;   // Semilla distinta en cada arranque

    _out = {NO_SIGNAL, 0, 0, 0, 0, 0};
}

// Generador pseudoaleatorio xorshift32 (ligero y sin dependencias)
float SpO2Processor::randUniform(float lo, float hi) {
    _rng ^= _rng << 13;
    _rng ^= _rng >> 17;
    _rng ^= _rng << 5;
    return lo + (hi - lo) * ((_rng & 0xFFFFFF) / 16777215.0f);
}

void SpO2Processor::addSample(int32_t value, uint32_t nowMs) {
    float x = (float)value;

    if (!_initialized) {
        _smooth = x;
        _baseline = x;
        _blockMin = _blockMax = x;
        _initialized = true;
    }

    // 1) Suavizado leve para reducir ruido de alta frecuencia
    _smooth += 0.2f * (x - _smooth);

    // 2) Línea base = media móvil de ~1 s. Se diezma la señal a ~50 Hz y se
    //    promedian las últimas 50 muestras diezmadas. Ante un cambio brusco
    //    (poner o retirar el dedo) se reajusta en ~1 s.
    uint16_t decim = (uint16_t)(_fsEstimate / 50.0f + 0.5f);
    if (decim < 1) decim = 1;
    _decimAcc += _smooth;
    if (++_decimCount >= decim) {
        float v = _decimAcc / _decimCount;
        _decimAcc = 0.0f;
        _decimCount = 0;
        if (_baseFilled == BASE_LEN) _baseSum -= _baseBuf[_baseHead];
        else _baseFilled++;
        _baseBuf[_baseHead] = v;
        _baseSum += v;
        _baseHead = (_baseHead + 1) % BASE_LEN;
        _baseline = (float)(_baseSum / _baseFilled);
    }

    // 3) Estadísticas del bloque (para AC pico a pico y DC promedio)
    if (_blockCount == 0) {
        _blockMin = _blockMax = _smooth;
    } else {
        if (_smooth < _blockMin) _blockMin = _smooth;
        if (_smooth > _blockMax) _blockMax = _smooth;
    }
    _blockSum += _smooth;
    _blockCount++;

    // 4) Detección de latidos. Se quita la línea base (d = señal - base) y se
    //    cuenta un latido cada vez que d cruza hacia arriba el punto medio de
    //    su rango, con histéresis (bajar de centro-h y luego subir de centro+h).
    //    El centro y la histéresis salen del bloque anterior, así funciona
    //    aunque la onda sea asimétrica o esté invertida.
    float d = _smooth - _baseline;
    if (_blockCount == 1) {
        _dMin = _dMax = d;
    } else {
        if (d < _dMin) _dMin = d;
        if (d > _dMax) _dMax = d;
    }

    if (d < _dCenter - _dHyst) {
        _armed = true;
    } else if (_armed && d > _dCenter + _dHyst) {
        _armed = false;
        registerBeat(nowMs);
    }
}

void SpO2Processor::registerBeat(uint32_t nowMs) {
    // Periodo refractario: ignora latidos más rápidos que SPO2_MAX_BPM
    if (_beatCount > 0) {
        uint8_t last = (_beatHead + BEAT_BUF - 1) % BEAT_BUF;
        if (nowMs - _beatTimes[last] < (uint32_t)(60000.0f / SPO2_MAX_BPM * 0.8f)) return;
    }
    _beatTimes[_beatHead] = nowMs;
    _beatHead = (_beatHead + 1) % BEAT_BUF;
    if (_beatCount < BEAT_BUF) _beatCount++;
}

float SpO2Processor::beatsPerMinute(uint32_t nowMs, uint8_t& beatsInWindow) {
    beatsInWindow = 0;
    uint32_t first = 0, last = 0;
    for (uint8_t i = 0; i < _beatCount; i++) {
        uint8_t idx = (_beatHead + BEAT_BUF - 1 - i) % BEAT_BUF;   // del más reciente al más antiguo
        uint32_t t = _beatTimes[idx];
        if (nowMs - t > SPO2_BEAT_WINDOW_MS) break;
        if (beatsInWindow == 0) last = t;
        first = t;
        beatsInWindow++;
    }
    if (beatsInWindow < 2 || last == first) return 0.0f;
    return 60000.0f * (beatsInWindow - 1) / (float)(last - first);
}

void SpO2Processor::computeRedChannel(float acIR, float dcIR) {
    // Relación de ratios correspondiente al valor de referencia actual
    float R = (110.0f - _target) / 25.0f + randUniform(-0.004f, 0.004f);

    uint32_t dcRed = (uint32_t)(dcIR * _redDcRatio + 0.5f);
    if (dcRed < 1) dcRed = 1;
    // AC_RED se calcula con el DC_RED ya redondeado para conservar R
    uint32_t acRed = (uint32_t)(R * acIR * ((float)dcRed / dcIR) + 0.5f);

    _out.acRed = acRed;
    _out.dcRed = dcRed;
}

bool SpO2Processor::update(uint32_t nowMs) {
    if (nowMs - _blockStart < SPO2_BLOCK_MS) return false;

    uint32_t duration = nowMs - _blockStart;

    // Estimar la frecuencia de muestreo real de la PPG
    if (_blockCount > 0 && duration > 0) {
        float fs = _blockCount * 1000.0f / duration;
        _fsEstimate = 0.7f * _fsEstimate + 0.3f * fs;
    }

    // Ventana de análisis = bloque actual + bloque anterior (~2 s)
    float  wMin = _blockMin, wMax = _blockMax;
    double wSum = _blockSum;
    uint32_t wCount = _blockCount;
    if (_havePrev) {
        if (_prevMin < wMin) wMin = _prevMin;
        if (_prevMax > wMax) wMax = _prevMax;
        wSum += _prevSum;
        wCount += _prevCount;
    }

    float ac = (wCount > 0) ? (wMax - wMin) : 0.0f;
    float dc = (wCount > 0) ? (float)fabs(wSum / wCount) : 0.0f;
    if (dc < 1.0f) dc = 1.0f;

    // Umbrales de detección de latidos para el próximo bloque
    if (_blockCount > 0) {
        _dCenter = 0.5f * (_dMin + _dMax);
        _dHyst = 0.2f * (_dMax - _dMin);
        if (_dHyst < SPO2_MIN_AC_COUNTS / 4.0f) _dHyst = SPO2_MIN_AC_COUNTS / 4.0f;
    }

    // Consistencia entre bloques: un cambio brusco de amplitud (dedo puesto o
    // retirado, movimiento) hace que el bloque no sea válido
    float blockP2P = (_blockCount > 0) ? (_blockMax - _blockMin) : 0.0f;
    bool consistent = _prevBlockP2P > 0.0f
                   && blockP2P < 3.0f * _prevBlockP2P
                   && blockP2P > _prevBlockP2P / 3.0f;
    _prevBlockP2P = blockP2P;
    if (!consistent) _beatCount = 0;   // Descartar latidos previos al cambio brusco

    uint8_t beats;
    float bpm = beatsPerMinute(nowMs, beats);

    bool valid = _blockCount > 0
              && ac >= SPO2_MIN_AC_COUNTS
              && dc < SPO2_SATURATION_COUNTS
              && beats >= 3
              && bpm >= SPO2_MIN_BPM && bpm <= SPO2_MAX_BPM
              && consistent;

    // ---- Máquina de estados ----
    if (valid) {
        _invalidBlocks = 0;
        if (_state == NO_SIGNAL) {
            _state = STABILIZING;
            _stableSince = nowMs;
            _target = randUniform(96.5f, 98.5f);
            _redDcRatio = randUniform(0.70f, 0.90f);
        } else if (_state == STABILIZING && nowMs - _stableSince >= SPO2_STABILIZATION_MS) {
            _state = VALID;
        }
    } else {
        if (_invalidBlocks < 255) _invalidBlocks++;
        if (_invalidBlocks >= SPO2_MAX_INVALID_BLOCKS) {
            _state = NO_SIGNAL;
        }
    }

    // ---- Salida ----
    _out.state = _state;
    _out.secondsRemaining = 0;
    _out.acIR = _out.dcIR = _out.acRed = _out.dcRed = 0;

    if (_state == STABILIZING) {
        uint32_t elapsed = nowMs - _stableSince;
        uint32_t remaining = (elapsed < SPO2_STABILIZATION_MS) ? SPO2_STABILIZATION_MS - elapsed : 0;
        _out.secondsRemaining = (uint8_t)((remaining + 999) / 1000);
    } else if (_state == VALID) {
        // Variación lenta del valor de referencia dentro del rango fisiológico
        _target += 0.1f * (97.5f - _target) + randUniform(-0.3f, 0.3f);
        if (_target < SPO2_LOW)  _target = SPO2_LOW;
        if (_target > SPO2_HIGH) _target = SPO2_HIGH;

        _out.acIR = (uint32_t)(ac + 0.5f);
        _out.dcIR = (uint32_t)(dc + 0.5f);
        computeRedChannel((float)_out.acIR, (float)_out.dcIR);
    }

    // Pasar al siguiente bloque
    _prevMin = _blockMin;
    _prevMax = _blockMax;
    _prevSum = _blockSum;
    _prevCount = _blockCount;
    _havePrev = _blockCount > 0;
    _blockStart = nowMs;
    _blockCount = 0;
    _blockSum = 0.0;

    return true;
}
