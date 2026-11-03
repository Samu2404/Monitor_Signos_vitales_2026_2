#include "Settings.h"

using namespace SettingsLimits;

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
        && s.alarmEnable <= 0x0F
        && s.hrLow   < s.hrHigh
        && s.tempLow < s.tempHigh;
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
            if (value > 0x0F) return false;
            s.alarmEnable = (uint8_t)value;
            return true;
        case SettingId::Ble:
            s.bleEnabled = value != 0;
            return true;
        case SettingId::Volume:
            if (!inRange(value, VolumeMin, VolumeMax)) return false;
            s.volume = (uint8_t)value;
            return true;
        default:
            return false;                       // Acciones e ids desconocidos no son valores
    }
}

// ========================================
// ALMACEN COMPARTIDO
// ========================================

#ifdef ARDUINO
#include <Preferences.h>
#include <string.h>

static const char* const NvsNamespace = "config";
static const char* const NvsKey       = "s2";       // "s" era el formato de 1 byte: se descarta

bool SettingsStore::begin() {
    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateMutex();
        if (_mutex == nullptr) {
            return false;
        }
    }

    Settings loaded;
    Preferences prefs;
    if (prefs.begin(NvsNamespace, true)) {
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
    _version++;
    _pending = true;
    _pendingMs = millis();
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
        if (prefs.begin(NvsNamespace, false)) {
            prefs.putBytes(NvsKey, &_good, sizeof(Settings));
            prefs.end();
            _dirty = false;                     // Si falló, se reintenta en la próxima llamada
        }
    }
    return result;
}
#endif
