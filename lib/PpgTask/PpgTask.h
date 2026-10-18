#pragma once 

#include <Arduino.h>
#include <NextionHMI.h>
#include <WaveDecimator.h>
#include "biosignals.h"

class PpgTask {
    public: 
        struct Snapshot {
            uint16_t spo2;
            uint16_t pulseRate;
            uint16_t sample;      // muestra cruda del ADC
            float    filtered;    // muestra tras el filtro PPG (centrada en 0)
        };

        using SampleSource = uint16_t (*)();   // Devuelve una muestra 0..4095 (p. ej. señal simulada)

        PpgTask(NextionHMI& hmi);
        void begin(SampleSource source = nullptr);   // nullptr = leer el ADC (analogPin)
        Snapshot getSnapshot();

    private:
        
        static constexpr uint8_t analogPin= 35;
        static constexpr uint8_t SamplePeriodMs= 4;
        static constexpr float ADCsampleRateHz = 1000.0f/SamplePeriodMs;   // 250 Hz
        static constexpr uint16_t GraphDecimation= 4;   // Muestras por ventana: 2·250/4 = 125 puntos/s en la gráfica
        static constexpr uint8_t GraphId= 2;
        static constexpr uint8_t GraphChannel= 0;

        // --- Señal y gráfica (el filtro y el detector viven en PpgProcessor, biosignals) ---
        static constexpr bool  InvertSignal  = false;  // true = invierte la muestra (4095 - x) si el sensor baja de voltaje en sístole
        static constexpr bool  GraphFiltered = false;  // false = graficar la señal cruda; true = la filtrada
        static constexpr float GraphGain   = 1.0f;   // escala de la señal filtrada en la gráfica
        static constexpr int32_t GraphOffset = 2048; // la salida filtrada es bipolar: se centra a media escala

        static constexpr size_t TaskStackSize= 4096;
        static constexpr uint8_t TaskPriority= 2;   // Mayor que la tarea del HMI (1) para no perder el ritmo de 4 ms
        static constexpr uint8_t TaskCore= 0;     // Core distinto al de NextionHMI y loop()
        
        NextionHMI& _hmi;
        WaveDecimator _graph{GraphDecimation};   // Solo lo usa _taskLoop
        Snapshot _shared;                     // Último resultado publicado; protegido por _mutex
        SemaphoreHandle_t _mutex = nullptr;
        TaskHandle_t _taskHandle = nullptr;
        PpgProcessor _proc;                   // Filtro + detector de pulsos. Solo lo usa _taskLoop (no requiere mutex)
        uint16_t _shownPulseRate = 0;         // Último valor enviado al HMI (para mandar el 0 una sola vez)
        SampleSource _source = nullptr;       // Fuente de muestras alternativa al ADC


        static void _taskEntry(void* self);
        void _taskLoop();
};
