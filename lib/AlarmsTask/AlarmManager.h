
#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H

#include <stdint.h>

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
    MINOR    = 1,     // Leve (LED azul): hoy ninguna alarma la usa. 1 pitido cada ~15 s
    MEDIUM   = 2,     // Media (LED amarillo): temperatura. 3 pitidos lentos cada ~5 s
    CRITICAL = 3      // Grave (LED rojo): FC y SpO2. 3 + 2 pitidos rápidos cada ~2,5 s
};

// ========================================
// CLASE ALARM MANAGER
// ========================================

class AlarmManager {

public:

    AlarmManager();

    bool setAlarm(AlarmType alarm, bool active);

    bool isActive(AlarmType alarm) const;

    bool hasActiveAlarms() const;

    // Prioridad más alta entre las alarmas activas
    AlarmPriority highestPriority() const;

    const char* getAlarmString() const;

    bool hasChanged() const;

    void clearChanged();

    void reset();

private:

    // 8 caracteres + terminador nulo
    char alarmStates_[9] = "00000000";

    // Cambios pendientes de publicacion BLE
    bool changed_ = false;

};

#endif // ALARM_MANAGER_H
