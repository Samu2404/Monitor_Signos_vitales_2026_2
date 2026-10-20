
#ifndef VITAL_SIGNS_H
#define VITAL_SIGNS_H

#include <stdint.h>

// ========================================
// ESTRUCTURA DE SIGNOS VITALES
// ========================================

struct VitalSigns {

    // Frecuencia cardiaca ECG (lpm)
    uint16_t hr = 0;

    // Frecuencia de pulso PPG (lpm)
    uint16_t pr = 0;

    // Saturacion de oxigeno (%)
    uint8_t spo2 = 0;

    // Temperatura corporal (grados C)
    float temperature = 0.0f;

    // Variabilidad cardiaca (ms)
    uint16_t hrv = 0;

    // Frecuencia respiratoria (resp/min)
    uint8_t rr = 0;

    // Presion sistolica estimada (mmHg)
    uint16_t sys = 0;

    // Presion diastolica estimada (mmHg)
    uint16_t dia = 0;

};

#endif // VITAL_SIGNS_H
