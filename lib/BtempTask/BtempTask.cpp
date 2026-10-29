#include "BtempTask.h"
#include <math.h>

bool BtempTask::begin(uint8_t pin) {
    if (_mutex != nullptr) {
        return true;   // Ya iniciado
    }
    _mutex = xSemaphoreCreateMutex();
    if (_mutex == nullptr) {
        return false;
    }
    _pin = pin;
    analogReadResolution(12);
    analogSetPinAttenuation(_pin, ADC_11db);   // Rango ~0–3.1 V
    return xTaskCreatePinnedToCore(_taskEntry, "BtempTask", TaskStackSize, this,
                                   TaskPriority, &_taskHandle, TaskCore) == pdPASS;
}

BtempTask::Snapshot BtempTask::getSnapshot() {
    if (_mutex == nullptr) {
        return {};     // Sin begin(): sin dato (AlarmTask lo lee igual)
    }
    Snapshot s;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    s = _shared;
    xSemaphoreGive(_mutex);
    return s;
}

void BtempTask::_taskEntry(void* self) {
    static_cast<BtempTask*>(self)->_taskLoop();
}

void BtempTask::_taskLoop() {
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        uint32_t sum = 0;
        for (uint8_t i = 0; i < SamplesPerUpdate; i++) {
            sum += analogReadMilliVolts(_pin);    // Usa la calibración de fábrica del ADC (eFuse)
            vTaskDelayUntil(&last, pdMS_TO_TICKS(SamplePeriodMs));
        }
        uint16_t mv = sum / SamplesPerUpdate;
        float t = ysi400_temp_from_mv(mv);

        if (t > 0.0f) {
            _smoothed = (_smoothed > 0.0f) ? _smoothed + SmoothAlpha * (t - _smoothed) : t;
        } else {
            _smoothed = 0.0f;                     // Sonda suelta: sin dato y el suavizado vuelve a empezar
        }
        float shown = roundf(_smoothed * 10.0f) / 10.0f;   // Resolución 0.1 °C

        xSemaphoreTake(_mutex, portMAX_DELAY);
        _shared.celsius = shown;
        _shared.mv      = mv;
        xSemaphoreGive(_mutex);

        if (shown != _shown) {                    // Al HMI solo cuando cambia
            _hmi.writeTemperature(shown);
            _shown = shown;
        }
    }
}
