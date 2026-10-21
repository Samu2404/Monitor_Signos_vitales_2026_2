#pragma once
#include <Arduino.h>
#include "AlarmManager.h"     // AlarmPriority

// ========================================
// CLASE ALARM BUZZER
// ========================================

/**
 * @brief Hace sonar un buzzer según la prioridad de alarma activa.
 *
 * Corre en su propia tarea para que los patrones (pitido / pausa) no bloqueen
 * a quien evalúa las alarmas. setPriority() se puede llamar desde cualquier tarea:
 * si la prioridad no cambia, el patrón sigue sin reiniciarse.
 */
class AlarmBuzzer {
public:
    /**
     * @param pin     GPIO del buzzer
     * @param passive true = buzzer pasivo (se excita con PWM a toneHz);
     *                false = buzzer activo (solo se enciende/apaga el pin)
     */
    AlarmBuzzer(uint8_t pin, bool passive = true);

    bool begin();

    void setPriority(AlarmPriority priority);

    AlarmPriority priority() const { return _priority; }

    /**
     * @brief Easter egg: toca "La Cucaracha" una vez.
     * @return false si hay una alarma sonando (las alarmas siempre tienen prioridad).
     *         Si una alarma se activa a mitad de la melodía, la melodía se corta.
     */
    bool playEasterEgg();

private:
    static constexpr uint8_t  LedcChannel   = 4;      // Canal LEDC libre
    static constexpr uint8_t  LedcBits      = 8;
    static constexpr uint32_t ToneMediumHz  = 1500;   // Tono más grave para prioridad media
    static constexpr uint32_t ToneHighHz    = 2700;   // Tono agudo (resonancia típica de buzzers)

    static constexpr size_t  TaskStackSize = 2048;
    static constexpr uint8_t TaskPriority  = 1;
    static constexpr uint8_t TaskCore      = 1;

    // Patrón: duraciones en ms alternando encendido / apagado, empezando por encendido
    struct Pattern {
        const uint16_t* steps;
        uint8_t         count;
        uint32_t        toneHz;
    };

    static void _taskEntry(void* self);
    void _taskLoop();
    void _playMelody();
    void _on(uint32_t toneHz);
    void _off();
    // Espera ms; devuelve true si la prioridad cambió (hay que reiniciar el patrón)
    bool _wait(uint32_t ms);

    uint8_t _pin;
    bool    _passive;
    volatile AlarmPriority _priority = AlarmPriority::NONE;
    volatile bool _easterEgg = false;     // Melodía pendiente de tocar
    TaskHandle_t _taskHandle = nullptr;
};
