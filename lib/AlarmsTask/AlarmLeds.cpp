#include "AlarmLeds.h"

AlarmLeds::AlarmLeds(uint8_t pinGreen, uint8_t pinBlue, uint8_t pinYellow, uint8_t pinRed)
    : _pins{pinGreen, pinBlue, pinYellow, pinRed} {
}

void AlarmLeds::begin() {
    for (uint8_t pin : _pins) {
        pinMode(pin, OUTPUT);
    }
    show(AlarmPriority::NONE);      // Arranca en verde
}

void AlarmLeds::show(AlarmPriority priority) {
    uint8_t lit = static_cast<uint8_t>(priority);
    if (lit >= 4) {
        lit = 3;                    // Por seguridad: un valor raro se muestra como grave
    }
    for (uint8_t i = 0; i < 4; i++) {
        digitalWrite(_pins[i], i == lit ? HIGH : LOW);
    }
}
