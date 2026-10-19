
#include "AlarmManager.h"

#include <cstring>

// ========================================
// CONSTRUCTOR
// ========================================

AlarmManager::AlarmManager() {
    // Estado inicial definido en el header
}

// ========================================
// ACTUALIZAR ALARMA
// ========================================

bool AlarmManager::setAlarm(
    AlarmType alarm,
    bool active
) {

    uint8_t index =
        static_cast<uint8_t>(alarm);

    // La posicion 8 esta reservada
    if (index >= 7) {
        return false;
    }

    char newState = active ? '1' : '0';

    // No registrar cambios innecesarios
    if (alarmStates_[index] == newState) {
        return false;
    }

    alarmStates_[index] = newState;

    changed_ = true;

    return true;
}

// ========================================
// CONSULTAR ALARMA
// ========================================

bool AlarmManager::isActive(
    AlarmType alarm
) const {

    uint8_t index =
        static_cast<uint8_t>(alarm);

    if (index >= 7) {
        return false;
    }

    return alarmStates_[index] == '1';
}

// ========================================
// CONSULTAR ALARMAS ACTIVAS
// ========================================

bool AlarmManager::hasActiveAlarms() const {

    for (uint8_t i = 0; i < 7; i++) {

        if (alarmStates_[i] == '1') {
            return true;
        }
    }

    return false;
}

// ========================================
// OBTENER TEXTO BLE
// ========================================

const char* AlarmManager::getAlarmString() const {

    return alarmStates_;
}

// ========================================
// CONSULTAR CAMBIOS
// ========================================

bool AlarmManager::hasChanged() const {

    return changed_;
}

// ========================================
// CONFIRMAR PUBLICACION
// ========================================

void AlarmManager::clearChanged() {

    changed_ = false;
}

// ========================================
// RESTABLECER ALARMAS
// ========================================

void AlarmManager::reset() {

    // No modificar si todas estan apagadas
    if (strcmp(alarmStates_, "00000000") == 0) {
        return;
    }

    memcpy(alarmStates_, "00000000", 9);

    changed_ = true;
}
