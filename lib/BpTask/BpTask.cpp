#include "BpTask.h"

// Resumen de bp cada 1 s por Serial. Con el debugger del Nextion, Serial es el enlace de la
// pantalla: se ve en "MCU Return" (vista S). Las líneas terminan en FF FF FF para no desincronizarla
// #define BP_DEBUG

void BpTask::beginExternal(float fsEcg, float fsPpg, bool ppgIsLight) {
    if (_mutex != nullptr) {
        return;                                  // Ya iniciado
    }
    _mutex = xSemaphoreCreateMutex();
    if (_mutex == nullptr) {
        return;
    }
    bp_init(&_bp, fsEcg, fsPpg);
    _bp.ppg_sign = ppgIsLight ? 1 : -1;          // Internamente el PPG se guarda como luz
    _hmi.writeCalibrationStatus(0, 0);           // Botón Cal visible, sin calibrar
}

void BpTask::pushEcg(int32_t sample) {
    if (_mutex == nullptr) {
        return;
    }
    bp_push_ecg(&_bp, sample);
}

void BpTask::pushPpg(int32_t sample) {
    if (_mutex == nullptr) {
        return;
    }
    bp_push_ppg(&_bp, sample);
    if (sample < _dbgPpgMin) _dbgPpgMin = sample;
    if (sample > _dbgPpgMax) _dbgPpgMax = sample;
}

void BpTask::onBeat() {
    if (_mutex == nullptr) {
        return;
    }

    // El pedido de calibración llega desde otra tarea: se aplica aquí, en el hilo del productor
    bool  calReq = false, lost = false;
    float sbp = 0.0f, dbp = 0.0f;

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        _lastEcgBeatMs = millis();               // Hay señal aunque bp rechace este latido
        calReq = _calRequest;
        lost   = _lostRequest;
        sbp = _calSbp;
        dbp = _calDbp;
        _calRequest  = false;
        _lostRequest = false;
        if (calReq) {
            _shared.lastBeatMs  = millis();      // El plazo de StaleMs cuenta desde el pedido
            _shared.calibrating = true;
            _shared.calibrated  = false;
            _shared.calProgress = 0;
        }
        xSemaphoreGive(_mutex);
    }
    if (lost && !calReq) {
        bp_cancel_calibration(&_bp);             // Se perdió la señal: la calibración ya no vale
    }
    if (calReq) {
        bp_start_calibration(&_bp, sbp, dbp);
        _dbgBeats = 0;
        memset(_dbgReject, 0, sizeof(_dbgReject));
    }

    BP_Result r = bp_on_beat(&_bp);
    _dbgBeats++;
    _dbgReject[r.reject]++;
    if (r.reject == BP_OK || r.reject >= BP_REJ_PTT) {   // Hubo medida de PTT/PIR
        _dbgPtt = r.ptt_ms;
        _dbgPir = r.pir;
    }
    uint8_t progress = r.calibrated ? 100 : (uint8_t)(_bp.n_cal *100 / BP_NCAL);
    if (r.calibrating || r.cal_done) {
        _hmi.writeCalibrationStatus(r.calibrated ? 2 : 1, progress);  // 0=sin calibrar, 1=calibrando, 2=calibrado
    }
    if (!r.beat) {
        return;                                  // Latido descartado o primero de la serie
    }
    _beats++;

    Snapshot snap{r.ptt_avg, r.pir_avg, r.sbp, r.dbp, r.calibrating, r.calibrated, _beats, millis(), progress};
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(1)) == pdTRUE) {
        _shared = snap;
        if (r.calibrated) {
            _shown = true;
        }
        xSemaphoreGive(_mutex);
    }

    if (r.calibrated) {
        _hmi.writeBloodPressure((int)lroundf(r.sbp), (int)lroundf(r.dbp));
    }
}

bool  BpTask::startCalibration(float sbp, float dbp) {
    if (_mutex == nullptr) {
        return false;
    }
    if ( sbp <70 || sbp > 250 || dbp < 40 || dbp > 150 || sbp < dbp + 10) {
        return false;                                  // Valores inválidos
    }
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(1)) != pdTRUE) { // No se pudo tomar el mutex
        return false;
    }
    _calSbp = sbp;
    _calDbp = dbp;
    _calRequest = true;
    xSemaphoreGive(_mutex);
#ifdef BP_DEBUG
    Serial.printf("bp pedido %.0f/%.0f aceptado\xFF\xFF\xFF", sbp, dbp);
#endif
    return true;
}

BpTask::Snapshot BpTask::getSnapshot() {
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


void BpTask::checkTimeout() {
    if (_mutex == nullptr) {
        return;
    }
    bool clear = false, lost = false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(1)) == pdTRUE) {
        uint32_t now = millis();
        if ((now - _shared.lastBeatMs) > StaleMs && _shown) {
            _shown = false;                      // Sin latidos aceptados: se borra la presión una sola vez
            clear = true;
        }
        if ((now - _lastEcgBeatMs) > StaleMs && (_shared.calibrated || _shared.calibrating)) {
            _shared.calibrated  = false;         // Sin latidos del ECG: calibración perdida
            _shared.calibrating = false;
            _shared.calProgress = 0;
            _lostRequest = true;                 // _bp se anula en el hilo de AfeTask (onBeat)
            lost = true;
        }
        xSemaphoreGive(_mutex);
    }
    if (clear) {
        _hmi.writeBloodPressure(0, 0);
    }
    if (lost) {
        _hmi.writeCalibrationStatus(0, 0);       // Vuelve a aparecer el botón Cal
    }
#ifdef BP_DEBUG
    _debugPrint();
#endif
}

void BpTask::_debugPrint() {
    static uint32_t last = 0;
    if (millis() - last < 1000) {
        return;
    }
    last = millis();
    Snapshot s = getSnapshot();
    const uint32_t* n = _dbgReject;
    int32_t ppgMin = _dbgPpgMin, ppgMax = _dbgPpgMax;
    _dbgPpgMin = INT32_MAX;
    _dbgPpgMax = INT32_MIN;
    Serial.printf("bp ppg=%ld..%ld | lat=%lu ok=%lu | ini=%lu pri=%lu rr=%lu ven=%lu sub=%lu dc=%lu ptt=%lu pir=%lu art=%lu"
                  " | ult ptt=%.0f pir=%.3f | cal=%d ok=%d n=%d\xFF\xFF\xFF",
                  (long)ppgMin, (long)ppgMax,
                  (unsigned long)_dbgBeats, (unsigned long)n[BP_OK],
                  (unsigned long)n[BP_REJ_STARTUP], (unsigned long)n[BP_REJ_FIRST], (unsigned long)n[BP_REJ_RR],
                  (unsigned long)n[BP_REJ_WINDOW], (unsigned long)n[BP_REJ_SLOPE], (unsigned long)n[BP_REJ_DC],
                  (unsigned long)n[BP_REJ_PTT], (unsigned long)n[BP_REJ_PIR], (unsigned long)n[BP_REJ_ARTIFACT],
                  _dbgPtt, _dbgPir, s.calibrating, s.calibrated, _bp.n_cal);
}