#pragma once
#include <stdint.h>

/**
 * @brief Pausa temporal del audio de alarma (IEC 60601-1-8: "audio paused").
 *
 * Mientras dura, el buzzer calla pero las alarmas siguen en pantalla y en los LEDs.
 * Termina sola al acabar el tiempo, cuando la pantalla manda 0 o en cuanto
 * aparece una alarma que no estaba activa al empezar la pausa.
 *
 * El estado es explícito (no un toggle): la pantalla lleva la cuenta regresiva en main.vaPausa
 * y manda los segundos (53 10 <segundos>), así ESP32 y pantalla no se desincronizan.
 *
 * Trabaja con el texto de AlarmManager::getAlarmString(): un '1' por alarma activa.
 */
class AudioPause {
public:
    static constexpr uint16_t DurationS = 120;   // Duración del botón y máximo aceptado

    /** @brief Pausa de seconds segundos (recortada a DurationS); 0 = reanudar el audio ya */
    void set(const char* alarms, uint16_t seconds) {
        _left = seconds > DurationS ? DurationS : seconds;
        _known = mask(alarms);
    }

    /** @brief Una vez por segundo: descuenta y corta la pausa si aparece una alarma nueva */
    void tick(const char* alarms) {
        if (!active()) {
            return;
        }
        uint8_t now = mask(alarms);
        if (now & ~_known) {
            _left = 0;                  // Alarma nueva: el sonido vuelve ya
            return;
        }
        _left--;
    }

    bool     active() const      { return _left > 0; }
    uint16_t secondsLeft() const { return _left; }

    /** @brief Texto de alarmas ("00100000") a máscara: bit i = AlarmType i activa */
    static uint8_t mask(const char* alarms) {
        uint8_t m = 0;
        for (uint8_t i = 0; i < 8 && alarms[i] != '\0'; i++) {
            if (alarms[i] == '1') {
                m |= (uint8_t)(1u << i);
            }
        }
        return m;
    }

private:
    uint16_t _left  = 0;    // Segundos que faltan (0 = sin pausa)
    uint8_t  _known = 0;    // Alarmas activas al empezar la pausa (bit i = AlarmType i)
};
