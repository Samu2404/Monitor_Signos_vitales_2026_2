
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
    LEAD_DISCONNECTED = 5,
    BATTERY_LOW = 6,
    RESERVED = 7

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
