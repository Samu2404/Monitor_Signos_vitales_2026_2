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
    float sbp = 0.0f, dbp = 0.0f;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(1)) == pdTRUE) {
        calReq = _calRequest;
        sbp = _calSbp;
        dbp = _calDbp;
        _calRequest = false;
        xSemaphoreGive(_mutex);
    }
    if (calReq) {
        bp_start_calibration(&_bp, sbp, dbp);
    }

    BP_Result r = bp_on_beat(&_bp);
    if (!r.beat) {
        return;                                  // Latido descartado o primero de la serie
    }
    _beats++;

    Snapshot snap{r.ptt_avg, r.pir_avg, r.sbp, r.dbp, r.calibrating, r.calibrated, _beats};
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(1)) == pdTRUE) {
        _shared = snap;
        xSemaphoreGive(_mutex);
    }
}

void BpTask::startCalibration(float sbp, float dbp) {
    if (_mutex == nullptr) {
        return;
    }
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        _calSbp = sbp;
        _calDbp = dbp;
        _calRequest = true;                      // Se aplica en el próximo latido
        xSemaphoreGive(_mutex);
    }
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