#pragma once
#include <Arduino.h>
#include "AlarmManager.h"     // AlarmPriority

// ========================================
// CLASE ALARM LEDS
// ========================================

/**
 * @brief 4 LEDs de gravedad: siempre hay uno solo encendido.
 *
 *   Verde    -> normal (sin alarmas)
 *   Azul     -> leve
 *   Amarillo -> media
 *   Rojo     -> grave
 *
 * Cada LED va del GPIO a una resistencia (~220-330 ohm) y a GND.
 */
class AlarmLeds {
public:
    AlarmLeds(uint8_t pinGreen, uint8_t pinBlue, uint8_t pinYellow, uint8_t pinRed);

    void begin();

    void show(AlarmPriority priority);

private:
    // Indexado por AlarmPriority: NONE, MINOR, MEDIUM, CRITICAL
    uint8_t _pins[4];
};
