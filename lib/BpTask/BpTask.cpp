#include "BpTask.h"

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
}

void BpTask::onBeat() {
    if (_mutex == nullptr) {
        return;
    }

    // El pedido de calibración llega desde otra tarea: se aplica aquí, en el hilo del productor
    bool  calReq = false;
        bool  calReq = false, lost = false;
    float sbp = 0.0f, dbp = 0.0f;

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(1)) == pdTRUE) {
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
        _bp.calibrated  = false;                 // Se perdió la señal: la calibración ya no vale
        _bp.calibrating = false;
    }
    if (calReq) {
        bp_start_calibration(&_bp, sbp, dbp);
    }

    BP_Result r = bp_on_beat(&_bp);
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
        if ((millis() - _shared.lastBeatMs) > StaleMs) {
            if (_shown) {
                _shown = false;                  // Se borra la presión una sola vez
                clear = true;
            }
            if (_shared.calibrated || _shared.calibrating) {
                _shared.calibrated  = false;     // Calibración perdida
                _shared.calibrating = false;
                _shared.calProgress = 0;
                _lostRequest = true;             // _bp se anula en el hilo de AfeTask (onBeat)
                lost = true;
            }
        }
        xSemaphoreGive(_mutex);
    }
    if (clear) {
        _hmi.writeBloodPressure(0, 0);
    }
    if (lost) {
        _hmi.writeCalibrationStatus(0, 0);       // Vuelve a aparecer el botón Cal
    }
}