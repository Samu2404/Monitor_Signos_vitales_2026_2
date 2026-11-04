#pragma once
#include <stdint.h>
#include "AudioPause.h"     // AudioPause::mask()

/**
 * @brief Reconocer alarma (botón de la pantalla: 53 13).
 *
 * Calla el buzzer de las alarmas activas al pulsar, sin límite de tiempo, mientras sigan activas.
 * Cada alarma reconocida se olvida en cuanto se apaga: si vuelve a dispararse, suena de nuevo.
 * Una alarma que no estaba al reconocer suena normalmente. Pantalla y LEDs no cambian.
 *
 * Trabaja con el texto de AlarmManager::getAlarmString(): un '1' por alarma activa.
 */
class AlarmAck {
public:
    /** @brief Botón pulsado: reconoce todas las alarmas activas ahora */
    void acknowledge(const char* alarms) {
        _acked = AudioPause::mask(alarms);
    }

    /** @brief Una vez por segundo: olvida las reconocidas que ya se apagaron */
    void tick(const char* alarms) {
        _acked &= AudioPause::mask(alarms);
    }

    /** @brief Alarmas reconocidas (bit i = AlarmType i): no hacen sonar el buzzer */
    uint8_t acked() const { return _acked; }

private:
    uint8_t _acked = 0;
};
