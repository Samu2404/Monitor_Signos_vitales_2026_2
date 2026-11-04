#pragma once
#include <stdint.h>
#include <stddef.h>

// ========================================
// CONFIGURACION DEL MONITOR (páginas pat_config y alarms_config del Nextion)
// ========================================

/** @brief Tipo de paciente (igual que pat_config.btPed: 0 adulto, 1 pediátrico) */
enum class PatientType : uint8_t { Adult = 0, Pediatric = 1 };

constexpr uint8_t PatientIdMax = 12;    // Caracteres del ID (tId en pat_config)

/**
 * @brief Valores que el usuario puede cambiar desde la pantalla.
 *
 * Los OFF de cada alarma (histéresis) no se configuran: salen de los márgenes de AlarmConfig.
 * Orden de campos sin relleno interno: poll() compara con memcmp.
 */
struct Settings {
    uint32_t admittedAt  = 0;      // Hora de ingreso (epoch, s); 0 = sin hora
    uint16_t hrLow       = 50;     // lpm
    uint16_t hrHigh      = 120;    // lpm
    uint16_t spo2Low     = 90;     // %
    uint16_t tempLow     = 350;    // décimas de °C (xfloat con 1 decimal)
    uint16_t tempHigh    = 380;    // décimas de °C
    uint16_t sysLow      = 90;     // mmHg
    uint16_t sysHigh     = 160;    // mmHg
    uint16_t diaLow      = 50;     // mmHg
    uint16_t diaHigh     = 90;     // mmHg
    uint8_t  alarmEnable = 0x1F;   // Bits AlarmEnableBit: cada grupo de alarmas se puede apagar
    bool     bleEnabled  = true;
    uint8_t  volume      = 100;    // % del buzzer pasivo (mínimo VolumeMin: no se puede silenciar del todo)
    uint8_t  patientType = (uint8_t)PatientType::Adult;
    uint8_t  bed         = 1;
    char     patientId[PatientIdMax + 1] = "";   // Vacío = sin ID; el resto siempre en 0
};

/** @brief Bits de Settings::alarmEnable (igual que sys1 en la pantalla: btHr + btSp*2 + btT*4 + btNoSig*8 + btPA*16) */
namespace AlarmEnableBit {
    constexpr uint8_t Hr       = 0x01;   // FC alta y baja
    constexpr uint8_t Spo2     = 0x02;   // SpO2 baja
    constexpr uint8_t Temp     = 0x04;   // Temperatura alta y baja
    constexpr uint8_t NoSignal = 0x08;   // Alarma técnica "sin señal" (aún no implementada)
    constexpr uint8_t Pa       = 0x10;   // PA sistólica y diastólica (solo con BpTask calibrado)
    constexpr uint8_t All      = 0x1F;
}

/**
 * @brief Ids del comando de la pantalla: printh 23 04 53 <id> + prints <valor>,2
 *
 * El valor llega en 2 bytes (little endian, como lo manda prints ...,2).
 * Las temperaturas van tal cual las tiene el xfloat: en décimas (350 = 35,0 °C).
 */
enum class SettingId : uint8_t {
    HrLow       = 0x01,
    HrHigh      = 0x02,
    Spo2Low     = 0x03,
    TempLow     = 0x04,
    TempHigh    = 0x05,
    AlarmEnable = 0x07,
    Ble         = 0x08,
    Volume      = 0x09,
    PatientType = 0x0A,   // 0 adulto, 1 pediátrico (pat_config)
    Bed         = 0x0B,   // 1..99
    SysLow      = 0x0C,
    SysHigh     = 0x0D,
    DiaLow      = 0x0E,
    DiaHigh     = 0x0F,

    // Acciones: no se guardan (el valor se ignora)
    PauseAudio   = 0x10,
    FactoryReset = 0x11,
    TestBuzzer   = 0x12,
    Acknowledge  = 0x13,
    NewPatient   = 0x14   // Borra ID y calibración de PA, límites del tipo
};

namespace SettingsLimits {
    constexpr uint16_t VolumeMin  = 30,  VolumeMax  = 100;
    constexpr uint16_t HrHighMin  = 60,  HrHighMax  = 250;
    constexpr uint16_t HrLowMin   = 20,  HrLowMax   = 150;
    constexpr uint16_t Spo2LowMin = 50,  Spo2LowMax = 99;
    constexpr uint16_t TempMin    = 300, TempMax    = 420;   // décimas de °C (30,0..42,0)
    constexpr uint16_t SysMin     = 50,  SysMax     = 250;   // mmHg
    constexpr uint16_t DiaMin     = 30,  DiaMax     = 150;   // mmHg
    constexpr uint16_t BedMin     = 1,   BedMax     = 99;
}

// ========================================
// LOGICA PURA (sin Arduino: se prueba con Unity)
// ========================================

/**
 * @brief Cambia un campo de s si el valor está en su rango.
 * @note No revisa que bajo < alto: la pantalla manda todos los valores juntos y uno solo puede
 *       quedar incoherente un instante. Eso lo revisa settingsValid() al terminar el lote.
 * @return true si el valor se aceptó (aunque fuera igual al que ya había)
 */
bool settingsApply(Settings& s, SettingId id, uint16_t value);

/** @brief true si todos los campos están en rango y los bajos son menores que los altos */
bool settingsValid(const Settings& s);

/** @brief Límites de alarma por defecto del tipo de paciente, con todas las alarmas encendidas */
void settingsDefaultLimits(Settings& s, PatientType type);

/**
 * @brief Nuevo paciente: borra el ID, anota la hora de ingreso y pone los límites del tipo actual
 * @note Cama, volumen y BLE no cambian. La calibración de PA la descarta quien llama (BpTask)
 */
void settingsNewPatient(Settings& s, uint32_t now);

/**
 * @brief Cambia el ID del paciente (comando 'I'). Se recorta a PatientIdMax caracteres
 * @return false si hay caracteres que no son ASCII imprimible (s no cambia)
 */
bool settingsSetPatientId(Settings& s, const char* txt, size_t len);

// ========================================
// ALMACEN COMPARTIDO (mutex + NVS)
// ========================================

#ifdef ARDUINO
#include <Arduino.h>

class SettingsStore {
public:
    /** @brief Carga de NVS; si no hay nada guardado o es inválido, usa los valores de fábrica */
    bool begin();

    /** @brief Copia actual (protegida por mutex): se puede llamar desde cualquier tarea */
    Settings get() const;

    /** @brief Aplica un campo si está en rango. El lote se confirma en poll() */
    bool set(SettingId id, uint16_t value);

    void resetDefaults();

    /** @brief ID del paciente (comando 'I'). Se confirma y guarda en poll() */
    bool setPatientId(const char* txt, size_t len);

    /** @brief Acción Nuevo paciente (53 14). now = time(nullptr) */
    void newPatient(uint32_t now);

    enum class Commit : uint8_t {
        None,       // Nada pendiente
        Accepted,   // El lote quedó coherente: se guarda en NVS
        Reverted    // El lote dejó bajo >= alto: se volvió a lo último válido
    };

    /**
     * @brief Llamar seguido desde loop(). Cuando pasan BatchMs sin cambios confirma el lote
     *        (o lo revierte) y SaveDelayMs después lo escribe en NVS
     */
    Commit poll();

    /** @brief Sube con cada cambio: las tareas detectan cambios sin comparar todo */
    uint32_t version() const { return _version; }

private:
    static constexpr uint32_t BatchMs     = 300;    // Los 6 mensajes del botón Guardar llegan en ~5 ms
    static constexpr uint32_t SaveDelayMs = 2000;   // No gastar la flash con cada toque

    void _changed();                    // Sube la versión y abre/extiende el lote

    Settings _s;
    Settings _good;                     // Último conjunto coherente (lo que se guarda)
    mutable SemaphoreHandle_t _mutex = nullptr;
    volatile uint32_t _version = 0;
    bool     _pending = false;          // Hay cambios sin confirmar
    uint32_t _pendingMs = 0;
    bool     _dirty = false;            // Hay cambios confirmados sin guardar
    uint32_t _dirtyMs = 0;
};
#endif
