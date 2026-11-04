#pragma once
#include "stdint.h"

namespace AlarmConfig {
    constexpr uint16_t HR_HIGH_ON = 120;  // bpm
    constexpr uint16_t HR_HIGH_OFF = 115; // bpm
    constexpr uint16_t HR_LOW_ON = 50;    // bpm
    constexpr uint16_t HR_LOW_OFF = 55;   // bpm


    constexpr uint16_t SPO2_LOW_ON = 90;   // %
    constexpr uint16_t SPO2_LOW_OFF = 92;  // %

    /**
    constexpr uint16_t BP_SYS_HIGH_ON = 140;  // mmHg
    constexpr uint16_t BP_SYS_HIGH_OFF = 135; // mmHg
    constexpr uint16_t BP_SYS_LOW_ON = 90;    // mmHg
    constexpr uint16_t BP_SYS_LOW_OFF = 95;   // mmHg


    constexpr uint16_t BP_DIA_HIGH_ON = 90;  // mmHg
    constexpr uint16_t BP_DIA_HIGH_OFF = 85; // mmHg
    constexpr uint16_t BP_DIA_LOW_ON = 60;    // mmHg
    constexpr uint16_t BP_DIA_LOW_OFF = 65;   // mmHg
    */

    constexpr float TEMP_HIGH_ON = 38.0f;  // °C
    constexpr float TEMP_HIGH_OFF = 37.0f; // °C
    constexpr float TEMP_LOW_ON = 35.0f;    // °C
    constexpr float TEMP_LOW_OFF = 36.0f;   // °C

    // Temperatura escalonada: leve hasta TEMP_SEVERE_C pasado el límite, media más allá
    constexpr float TEMP_SEVERE_C      = 1.0f;   // °C más allá de TEMP_*_ON (38,0 -> 39,0)
    constexpr float TEMP_SEVERE_HYST_C = 0.2f;   // Vuelve a leve recién 0,2 °C por debajo (38,8)

    // Persistencia [s] (una llamada a update() por segundo)
    constexpr uint8_t VITAL_PERSIST_ON_S  = 10;   // FC y SpO2: segundos para activar
    constexpr uint8_t VITAL_PERSIST_OFF_S = 5;    // FC y SpO2: segundos para apagar
    constexpr uint8_t TEMP_PERSIST_ON_S   = 30;   // Temperatura: segundos para activar
    constexpr uint8_t TEMP_PERSIST_OFF_S  = 10;   // Temperatura: segundos para apagar
}

struct AlarmRule {
    uint8_t count =0 ;
    bool active = false;

    bool update (bool trigger, bool release , uint8_t persistSOn, uint8_t persistSOff);
};

/**
 * @brief Escalón de severidad con histéresis (p. ej. temperatura leve -> media)
 * @param wasSevere Estado anterior
 * @param excess    Cuánto pasó el valor el límite de disparo (positivo = fuera de rango)
 * @param delta     Exceso a partir del cual pasa a severa
 * @param hyst      Margen para volver a no severa (sale con excess <= delta - hyst)
 */
inline bool alarmSevere(bool wasSevere, float excess, float delta, float hyst) {
    return wasSevere ? excess > delta - hyst : excess > delta;
}

