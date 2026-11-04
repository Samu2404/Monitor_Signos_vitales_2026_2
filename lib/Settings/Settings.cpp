#include "Settings.h"
#include <string.h>

using namespace SettingsLimits;

// Límites por defecto de cada tipo (índice = PatientType)
struct TypeLimits {
    uint16_t hrLow, hrHigh, spo2Low, tempLow, tempHigh, sysLow, sysHigh, diaLow, diaHigh;
};
static const TypeLimits DefaultLimits[] = {
    //  FC        SpO2  Temp       PA sist    PA diast
    {   50, 120,  90,   350, 380,  90, 160,   50, 90 },   // Adulto (= valores de fábrica)
    {   75, 160,  90,   350, 380,  70, 120,   40, 70 },   // Pediátrico
};

static bool inRange(uint16_t v, uint16_t lo, uint16_t hi) {
    return v >= lo && v <= hi;
}

bool settingsValid(const Settings& s) {
    return inRange(s.volume,   VolumeMin,  VolumeMax)
        && inRange(s.hrHigh,   HrHighMin,  HrHighMax)
        && inRange(s.hrLow,    HrLowMin,   HrLowMax)
        && inRange(s.spo2Low,  Spo2LowMin, Spo2LowMax)
        && inRange(s.tempHigh, TempMin,    TempMax)
        && inRange(s.tempLow,  TempMin,    TempMax)
        && inRange(s.sysHigh,  SysMin,     SysMax)
        && inRange(s.sysLow,   SysMin,     SysMax)
        && inRange(s.diaHigh,  DiaMin,     DiaMax)
        && inRange(s.diaLow,   DiaMin,     DiaMax)
        && inRange(s.bed,      BedMin,     BedMax)
        && s.patientType <= (uint8_t)PatientType::Pediatric
        && s.alarmEnable <= AlarmEnableBit::All
        && s.patientId[PatientIdMax] == '\0'
        && s.hrLow   < s.hrHigh
        && s.tempLow < s.tempHigh
        && s.sysLow  < s.sysHigh
        && s.diaLow  < s.diaHigh;
}

bool settingsApply(Settings& s, SettingId id, uint16_t value) {
    switch (id) {
        case SettingId::HrLow:
            if (!inRange(value, HrLowMin, HrLowMax)) return false;
            s.hrLow = value;
            return true;
        case SettingId::HrHigh:
            if (!inRange(value, HrHighMin, HrHighMax)) return false;
            s.hrHigh = value;
            return true;
        case SettingId::Spo2Low:
            if (!inRange(value, Spo2LowMin, Spo2LowMax)) return false;
            s.spo2Low = value;
            return true;
        case SettingId::TempLow:
            if (!inRange(value, TempMin, TempMax)) return false;
            s.tempLow = value;
            return true;
        case SettingId::TempHigh:
            if (!inRange(value, TempMin, TempMax)) return false;
            s.tempHigh = value;
            return true;
        case SettingId::AlarmEnable:
            if (value > AlarmEnableBit::All) return false;
            s.alarmEnable = (uint8_t)value;
            return true;
        case SettingId::Ble:
            s.bleEnabled = value != 0;
            return true;
        case SettingId::Volume:
            if (!inRange(value, VolumeMin, VolumeMax)) return false;
            s.volume = (uint8_t)value;
            return true;
        case SettingId::PatientType:
            if (value > (uint16_t)PatientType::Pediatric) return false;
            s.patientType = (uint8_t)value;     // Los límites cambian recién con Nuevo paciente
            return true;
        case SettingId::Bed:
            if (!inRange(value, BedMin, BedMax)) return false;
            s.bed = (uint8_t)value;
            return true;
        case SettingId::SysLow:
            if (!inRange(value, SysMin, SysMax)) return false;
            s.sysLow = value;
            return true;
        case SettingId::SysHigh:
            if (!inRange(value, SysMin, SysMax)) return false;
            s.sysHigh = value;
            return true;
        case SettingId::DiaLow:
            if (!inRange(value, DiaMin, DiaMax)) return false;
            s.diaLow = value;
            return true;
        case SettingId::DiaHigh:
            if (!inRange(value, DiaMin, DiaMax)) return false;
            s.diaHigh = value;
            return true;
        default:
            return false;                       // Acciones e ids desconocidos no son valores
    }
}

void settingsDefaultLimits(Settings& s, PatientType type) {
    const TypeLimits& d = DefaultLimits[type == PatientType::Pediatric ? 1 : 0];
    s.hrLow    = d.hrLow;
    s.hrHigh   = d.hrHigh;
    s.spo2Low  = d.spo2Low;
    s.tempLow  = d.tempLow;
    s.tempHigh = d.tempHigh;
    s.sysLow   = d.sysLow;
    s.sysHigh  = d.sysHigh;
    s.diaLow   = d.diaLow;
    s.diaHigh  = d.diaHigh;
    s.alarmEnable = AlarmEnableBit::All;
}

void settingsNewPatient(Settings& s, uint32_t now) {
    memset(s.patientId, 0, sizeof(s.patientId));
    s.admittedAt = now;
    settingsDefaultLimits(s, (PatientType)s.patientType);
}

bool settingsSetPatientId(Settings& s, const char* txt, size_t len) {
    if (len > PatientIdMax) {
        len = PatientIdMax;
    }
    for (size_t i = 0; i < len; i++) {
        uint8_t c = (uint8_t)txt[i];
        if (c < 0x20 || c > 0x7E) return false;
    }
    memset(s.patientId, 0, sizeof(s.patientId));   // Todo en 0: poll() compara con memcmp
    memcpy(s.patientId, txt, len);
    return true;
}

// ========================================
// ALMACEN COMPARTIDO
// ========================================

#ifdef ARDUINO
#include <Preferences.h>

static const char* const NvsNamespace = "config";
static const char* const NvsKey       = "s3";               // "s2" no tenía paciente ni PA
static const char* const NvsOldKeys[] = { "s", "s2" };      // Formatos viejos: se borran una vez

bool SettingsStore::begin() {
    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateMutex();
        if (_mutex == nullptr) {
            return false;
        }
    }

    Settings loaded;
    Preferences prefs;
    if (prefs.begin(NvsNamespace, false)) {             // Lectura/escritura: para borrar las claves viejas
        for (const char* key : NvsOldKeys) {
            if (prefs.isKey(key)) {
                prefs.remove(key);                      // Solo escribe si existía: no gasta flash
            }
        }
        // Si el tamaño no coincide (cambió el struct) o los valores no valen: valores de fábrica
        if (prefs.getBytesLength(NvsKey) == sizeof(Settings)) {
            prefs.getBytes(NvsKey, &loaded, sizeof(Settings));
            if (!settingsValid(loaded)) {
                loaded = Settings();
            }
        }
        prefs.end();
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);
    _s = loaded;
    xSemaphoreGive(_mutex);
    _good = loaded;
    _version++;
    return true;
}

Settings SettingsStore::get() const {
    Settings s;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    s = _s;
    xSemaphoreGive(_mutex);
    return s;
}

void SettingsStore::_changed() {
    _version++;
    _pending = true;
    _pendingMs = millis();
}

bool SettingsStore::set(SettingId id, uint16_t value) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool ok = settingsApply(_s, id, value);
    xSemaphoreGive(_mutex);

    // Aunque no cambie nada se confirma el lote: la pantalla recibe la respuesta igual
    _pending = true;
    _pendingMs = millis();
    if (ok) {
        _version++;
    }
    return ok;
}

void SettingsStore::resetDefaults() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _s = Settings();
    xSemaphoreGive(_mutex);
    _changed();
}

bool SettingsStore::setPatientId(const char* txt, size_t len) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool ok = settingsSetPatientId(_s, txt, len);
    xSemaphoreGive(_mutex);
    if (ok) {
        _changed();
    }
    return ok;
}

void SettingsStore::newPatient(uint32_t now) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    settingsNewPatient(_s, now);
    xSemaphoreGive(_mutex);
    _changed();
}

SettingsStore::Commit SettingsStore::poll() {
    Commit result = Commit::None;

    if (_pending && millis() - _pendingMs >= BatchMs) {
        _pending = false;
        Settings s = get();
        if (settingsValid(s)) {
            if (memcmp(&s, &_good, sizeof(Settings)) != 0) {
                _good = s;
                _dirty = true;
                _dirtyMs = millis();
            }
            result = Commit::Accepted;
        } else {
            xSemaphoreTake(_mutex, portMAX_DELAY);
            _s = _good;                         // Bajo >= alto: vuelve todo lo último válido
            xSemaphoreGive(_mutex);
            _version++;
            result = Commit::Reverted;
        }
    }

    if (_dirty && millis() - _dirtyMs >= SaveDelayMs) {
        Preferences prefs;
        if (prefs.begin(NvsNamespace, false) &&
            prefs.putBytes(NvsKey, &_good, sizeof(Settings)) == sizeof(Settings)) {
            _dirty = false;
        } else {
            _dirtyMs = millis();                // Falló: reintenta en SaveDelayMs, no en cada loop
        }
        prefs.end();
    }
    return result;
}
#endif
