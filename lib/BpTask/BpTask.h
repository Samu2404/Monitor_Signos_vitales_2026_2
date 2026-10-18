#pragma once

#include <Arduino.h>
#include "biosignals.h"

// Presión arterial por PTT + PIR (algoritmo del PDF, en biosignals: bp_*).
// No tiene tarea propia: la alimenta el productor de las muestras (el sink de AfeTask),
// igual que EcgTask/PpgTask en modo externo. ECG y PPG deben llegar con el mismo reloj.
class BpTask {
public:
    struct Snapshot {
        float    pttMs;        // PTT promedio [ms]
        float    pir;          // PIR promedio
        float    sbp, dbp;     // Presión estimada [mmHg] (válida si calibrated)
        bool     calibrating;
        bool     calibrated;
        uint32_t beats;        // Latidos aceptados desde el inicio
    };

    BpTask() = default;
    BpTask(const BpTask&) = delete;              // _bp es grande (~12 KB): nunca copiarlo
    BpTask& operator=(const BpTask&) = delete;

    /**
     * @brief Inicializa el estimador (sin tarea: las muestras llegan por pushEcg/pushPpg)
     * @param fsEcg Frecuencia del ECG [Hz]
     * @param fsPpg Frecuencia del PPG [Hz]
     * @param ppgIsLight true si el PPG crudo es luz (baja con el pulso), como el del AFE4950
     */
    void beginExternal(float fsEcg, float fsPpg, bool ppgIsLight = true);

    void pushEcg(int32_t sample);    // Muestra cruda de ECG
    void pushPpg(int32_t sample);    // Muestra cruda de PPG (con DC)

    /** @brief Llamar cuando EcgTask detecta latido en la última muestra entregada con pushEcg */
    void onBeat();

    /** @brief Pide calibrar con la medida del tensiómetro. Seguro desde otra tarea (p. ej. loop) */
    void startCalibration(float sbp, float dbp);

    Snapshot getSnapshot();

private:
    BpEstimator _bp;                 // Solo lo usa el productor (pushEcg/pushPpg/onBeat)
    SemaphoreHandle_t _mutex = nullptr;
    Snapshot _shared{};              // Último resultado publicado; protegido por _mutex
    bool  _calRequest = false;       // Pedido de calibración pendiente; protegido por _mutex
    float _calSbp = 0.0f, _calDbp = 0.0f;
    uint32_t _beats = 0;
};