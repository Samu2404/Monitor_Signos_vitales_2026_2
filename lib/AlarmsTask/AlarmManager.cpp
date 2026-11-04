
#include "AlarmManager.h"

#include <cstdio>
#include <cstring>

// ========================================
// TEXTOS DE LA BARRA DE ALARMAS
// ========================================

// Indexado por AlarmType (posiciones 5 y 6 libres: nunca se activan)
static const char* const AlarmNames[7] = {
    "FC ALTA", "FC BAJA", "SpO2 BAJA", "TEMP ALTA", "TEMP BAJA", nullptr, nullptr
};

// Indexado por TechAlarm
static const char* const TechNames[] = { "PA: RECALIBRAR" };
static const AlarmPriority TechPriorities[] = { AlarmPriority::MINOR };

// ========================================
// CONSTRUCTOR
// ========================================

AlarmManager::AlarmManager() {
    // Estado inicial definido en el header
}

AlarmPriority AlarmManager::defaultPriority(AlarmType alarm) {

    switch (alarm) {

        case AlarmType::HR_HIGH:
        case AlarmType::HR_LOW:
        case AlarmType::SPO2_LOW:
            return AlarmPriority::CRITICAL;

        case AlarmType::TEMP_HIGH:
        case AlarmType::TEMP_LOW:
            return AlarmPriority::MEDIUM;

        default:                                  // Posiciones libres
            return AlarmPriority::NONE;
    }
}

// ========================================
// ACTUALIZAR ALARMA
// ========================================

bool AlarmManager::setAlarm(
    AlarmType alarm,
    bool active
) {

    return setAlarm(alarm, active, defaultPriority(alarm));
}

bool AlarmManager::setAlarm(
    AlarmType alarm,
    bool active,
    AlarmPriority priority
) {

    uint8_t index =
        static_cast<uint8_t>(alarm);

    // La posicion 8 esta reservada
    if (index >= 7) {
        return false;
    }

    priorities_[index] = priority;

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
// PRIORIDAD MAS ALTA ACTIVA
// ========================================

AlarmPriority AlarmManager::highestPriority() const {

    char unused[1];

    return barText(unused, sizeof(unused));
}

AlarmPriority AlarmManager::priorityExcept(uint8_t ignored) const {

    AlarmPriority result = AlarmPriority::NONE;

    for (uint8_t i = 0; i < 7; i++) {

        if (alarmStates_[i] == '1' && !((ignored >> i) & 1u) && priorities_[i] > result) {
            result = priorities_[i];
        }
    }

    for (uint8_t i = 0; i < static_cast<uint8_t>(TechAlarm::COUNT); i++) {

        if (((techStates_ >> i) & 1u) && TechPriorities[i] > result) {
            result = TechPriorities[i];
        }
    }

    return result;
}

// ========================================
// ALARMAS TECNICAS
// ========================================

void AlarmManager::setTech(
    TechAlarm alarm,
    bool active
) {

    uint8_t index = static_cast<uint8_t>(alarm);

    if (index >= static_cast<uint8_t>(TechAlarm::COUNT)) {
        return;
    }

    if (active) {
        techStates_ |= (uint8_t)(1u << index);
    } else {
        techStates_ &= (uint8_t)~(1u << index);
    }
}

bool AlarmManager::isTechActive(
    TechAlarm alarm
) const {

    uint8_t index = static_cast<uint8_t>(alarm);

    if (index >= static_cast<uint8_t>(TechAlarm::COUNT)) {
        return false;
    }

    return (techStates_ >> index) & 1u;
}

// ========================================
// TEXTO DE LA BARRA DE ALARMAS
// ========================================

AlarmPriority AlarmManager::barText(
    char* buf,
    size_t len
) const {

    const char* top = nullptr;
    AlarmPriority topPriority = AlarmPriority::NONE;
    uint8_t count = 0;

    // En empate gana la primera (orden de AlarmType, después las técnicas)
    for (uint8_t i = 0; i < 7; i++) {

        if (alarmStates_[i] != '1' || AlarmNames[i] == nullptr) {
            continue;
        }

        count++;

        if (top == nullptr || priorities_[i] > topPriority) {
            top = AlarmNames[i];
            topPriority = priorities_[i];
        }
    }

    for (uint8_t i = 0; i < static_cast<uint8_t>(TechAlarm::COUNT); i++) {

        if (!((techStates_ >> i) & 1u)) {
            continue;
        }

        count++;

        if (top == nullptr || TechPriorities[i] > topPriority) {
            top = TechNames[i];
            topPriority = TechPriorities[i];
        }
    }

    if (len == 0) {
        return topPriority;
    }

    if (top == nullptr) {
        buf[0] = '\0';
    } else if (count > 1) {
        snprintf(buf, len, "%s +%u", top, (unsigned)(count - 1));
    } else {
        snprintf(buf, len, "%s", top);
    }

    return topPriority;
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

    techStates_ = 0;

    // No modificar si todas estan apagadas
    if (strcmp(alarmStates_, "00000000") == 0) {
        return;
    }

    memcpy(alarmStates_, "00000000", 9);

    changed_ = true;
}
