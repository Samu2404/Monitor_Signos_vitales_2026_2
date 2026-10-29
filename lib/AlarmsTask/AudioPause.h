#pragma once
#include <stdint.h>

/**
 * @brief Pausa temporal del audio de alarma (IEC 60601-1-8: "audio paused").
 *
 * Mientras dura, el buzzer calla pero las alarmas siguen en pantalla y en los LEDs.
 * Termina sola a los DurationS segundos, al pulsar otra vez el botón o en cuanto
 * aparece una alarma que no estaba activa al empezar la pausa.
 *
 * Trabaja con el texto de AlarmManager::getAlarmString(): un '1' por alarma activa.
 */
class AudioPause {
public:
    static constexpr uint16_t DurationS = 120;

    /** @brief Botón pulsado: empieza la pausa, o la termina si ya estaba en curso */
    void toggle(const char* alarms) {
        if (active()) {
            _left = 0;
            return;
        }
        _left = DurationS;
        _known = _mask(alarms);
    }

    /** @brief Una vez por segundo: descuenta y corta la pausa si aparece una alarma nueva */
    void tick(const char* alarms) {
        if (!active()) {
            return;
        }
        uint8_t now = _mask(alarms);
        if (now & ~_known) {
            _left = 0;                  // Alarma nueva: el sonido vuelve ya
            return;
        }
        _left--;
    }

    bool     active() const      { return _left > 0; }
    uint16_t secondsLeft() const { return _left; }

private:
    static uint8_t _mask(const char* alarms) {
        uint8_t m = 0;
        for (uint8_t i = 0; i < 8 && alarms[i] != '\0'; i++) {
            if (alarms[i] == '1') {
                m |= (uint8_t)(1u << i);
            }
        }
        return m;
    }

    uint16_t _left  = 0;    // Segundos que faltan (0 = sin pausa)
    uint8_t  _known = 0;    // Alarmas activas al empezar la pausa (bit i = AlarmType i)
};
