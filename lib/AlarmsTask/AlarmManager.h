
#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H

#include <stdint.h>
#include <stddef.h>

// ========================================
// IDENTIFICADORES DE ALARMA
// ========================================

enum class AlarmType : uint8_t {

    HR_HIGH = 0,
    HR_LOW = 1,
    SPO2_LOW = 2,
    TEMP_HIGH = 3,
    TEMP_LOW = 4,
    // Posiciones 5 y 6: libres. El texto BLE sigue con 8 caracteres y ahí siempre va '0'
    RESERVED = 7

};

// ========================================
// PRIORIDAD DE ALARMA (define el sonido del buzzer y el LED encendido)
// Ordenadas de menor a mayor: se pueden comparar con < y >
// ========================================

enum class AlarmPriority : uint8_t {
    NONE     = 0,     // Normal (LED verde): silencio
    MINOR    = 1,     // Leve (LED azul): temperatura poco fuera de rango, PA sin calibrar. 1 pitido al activarse
    MEDIUM   = 2,     // Media (LED amarillo): temperatura muy fuera de rango. 3 pitidos lentos cada ~5 s
    CRITICAL = 3      // Grave (LED rojo): FC y SpO2. 3 + 2 pitidos rápidos cada ~2,5 s
};

// ========================================
// ALARMAS TECNICAS (no van en el texto BLE de 8 posiciones: solo pantalla, LEDs y buzzer)
// ========================================

enum class TechAlarm : uint8_t {
    BP_CAL_LOST = 0,   // La PA estaba calibrada y se perdió (sin latidos del ECG): hay que recalibrar
    COUNT
};

// ========================================
// CLASE ALARM MANAGER
// ========================================

class AlarmManager {

public:

    AlarmManager();

    // Con la prioridad por defecto del tipo (FC y SpO2 grave, temperatura media)
    bool setAlarm(AlarmType alarm, bool active);

    // Con prioridad propia (p. ej. temperatura leve o media según cuánto pasó el límite).
    // Cambiar solo la prioridad no cuenta como cambio para BLE: el texto no cambia
    bool setAlarm(AlarmType alarm, bool active, AlarmPriority priority);

    bool isActive(AlarmType alarm) const;

    bool hasActiveAlarms() const;

    // Alarmas técnicas: no cambian el texto BLE
    void setTech(TechAlarm alarm, bool active);

    bool isTechActive(TechAlarm alarm) const;

    // Prioridad más alta entre las alarmas activas (también las técnicas)
    AlarmPriority highestPriority() const;

    // Igual, sin las alarmas de ignored (bit i = AlarmType i): p. ej. las reconocidas, para el buzzer
    AlarmPriority priorityExcept(uint8_t ignored) const;

    /**
     * @brief Texto para la barra de alarmas de la pantalla: la alarma de mayor prioridad
     *        y cuántas más hay activas (p. ej. "SpO2 BAJA +1"). "" si no hay ninguna
     * @return Prioridad de la alarma mostrada (define el color de la barra)
     */
    AlarmPriority barText(char* buf, size_t len) const;

    const char* getAlarmString() const;

    bool hasChanged() const;

    void clearChanged();

    void reset();

private:

    static AlarmPriority defaultPriority(AlarmType alarm);

    // 8 caracteres + terminador nulo
    char alarmStates_[9] = "00000000";

    // Prioridad de cada alarma (vale solo si está activa)
    AlarmPriority priorities_[7] = {};

    // Bit i = TechAlarm i activa
    uint8_t techStates_ = 0;

    // Cambios pendientes de publicacion BLE
    bool changed_ = false;

};

#endif // ALARM_MANAGER_H
