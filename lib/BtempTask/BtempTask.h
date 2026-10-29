#pragma once

#include <Arduino.h>
#include "NextionHMI.h"
#include "biosignals.h"

// Temperatura corporal con termistor YSI 400 en un puente de Wheatstone + amplificador,
// leído por el ADC. Tarea propia lenta: SamplesPerUpdate lecturas cada SamplePeriodMs,
// promedio, suavizado exponencial y publicación una vez por segundo.
class BtempTask {
public:
    struct Snapshot {
        float    celsius;   // 0 = sin dato (sonda suelta, en corto o aún sin medir)
        uint16_t mv;        // Último promedio del ADC [mV]
    };

    BtempTask(NextionHMI& hmi) : _hmi(hmi) {}

    /** @brief Configura el ADC y crea la tarea
     *  @param pin GPIO del ADC1 (con BLE activo el ADC2 no se puede usar) */
    bool begin(uint8_t pin);

    Snapshot getSnapshot();

private:
    static constexpr uint32_t SamplePeriodMs   = 100;
    static constexpr uint8_t  SamplesPerUpdate = 10;      // 10 × 100 ms = 1 actualización/s
    static constexpr float    SmoothAlpha      = 0.3f;    // Suavizado exponencial (1 = sin suavizar)

    static constexpr size_t  TaskStackSize = 3072;
    static constexpr uint8_t TaskPriority  = 1;           // Menor que la tarea de muestreo (2)
    static constexpr uint8_t TaskCore      = 1;

    NextionHMI& _hmi;
    uint8_t _pin = 0;
    Snapshot _shared{};                 // Último resultado publicado; protegido por _mutex
    SemaphoreHandle_t _mutex = nullptr;
    TaskHandle_t _taskHandle = nullptr;
    float _smoothed = 0.0f;             // 0 = reiniciar el suavizado con la próxima lectura válida
    float _shown = -1.0f;               // Último valor enviado al HMI

    static void _taskEntry(void* self);
    void _taskLoop();
};
