#pragma once 

#include <Arduino.h>
#include <NextionHMI.h>
#include "biosignals.h"

class PpgTask {
    public: 
        struct Snapshot {
            uint16_t spo2;
            uint16_t pulseRate;
            uint16_t sample;      // muestra cruda del ADC
            float    filtered;    // muestra tras el filtro PPG (centrada en 0)
        };

        PpgTask(NextionHMI& hmi);
        void begin();
        Snapshot getSnapshot();

    private:
        
        static constexpr uint8_t analogPin= 35;
        static constexpr float sampleRateHz = 1000.0f/4.0f;
        static constexpr uint8_t GraphRateHz= 144;
        static constexpr uint8_t GraphId= 2;
        static constexpr uint8_t GraphChannel= 0;
        static constexpr uint8_t SamplePeriodMs= 3;

        // --- Filtro PPG (independiente del Pan-Tompkins del ECG) ---
        static constexpr float PpgHpFc = PPG_HP_FC;  // 0.5 Hz, Butterworth orden 2
        static constexpr float PpgLpFc = PPG_LP_FC;  // 4 Hz, Butterworth orden 2 (0 = desactivado)
        static constexpr float GraphGain   = 1.0f;   // escala de la señal filtrada en la gráfica
        static constexpr int32_t GraphOffset = 2048; // la salida filtrada es bipolar: se centra a media escala
        
        // --- Detector de picos PPG (umbral adaptativo sobre la señal filtrada) ---
        static constexpr float    PeakAmpDecay   = 0.999f;   // Caída por muestra de la amplitud estimada (~2 s de vida media)
        static constexpr float    PeakThreshFrac = 0.5f;     // Umbral = fracción de la amplitud estimada
        static constexpr float    PeakMinAmp     = 20.0f;    // Amplitud mínima (cuentas ADC) para aceptar pulsos; evita disparar con ruido
        static constexpr uint32_t PeakRefractoryMs = 300;    // Máx. ~200 lpm
        static constexpr uint32_t PeakMaxRrMs  = 2000;       // Mín. 30 lpm
        static constexpr uint32_t PeakTimeoutMs = 3000;      // Sin pulsos este tiempo -> pulseRate = 0
        static constexpr uint8_t  RrCount      = 4;          // Intervalos RR promediados

        struct PeakDetector {
            float    amp = 0.0f;         // Amplitud pico estimada
            bool     above = false;      // ¿La señal está sobre el umbral?
            float    peakVal = 0.0f;     // Máximo del pulso en curso
            uint32_t peakUs = 0;         // Instante de ese máximo
            uint32_t lastBeatUs = 0;     // Último pulso aceptado (0 = ninguno)
            float    rr[RrCount] = {};   // Últimos RR [ms]
            uint8_t  rrIdx = 0, rrN = 0;
            uint16_t bpm = 0;
        };
        PeakDetector _pk;                     // Solo lo usa _taskLoop (no requiere mutex)
        bool _detectPeak(float filtered, uint32_t nowUs);   // true en el muestreo en que se acepta un pulso

        static constexpr size_t TaskStackSize= 4096;
        static constexpr uint8_t TaskPriority= 2;   // Mayor que la tarea del HMI (1) para no perder el ritmo de 3 ms
        static constexpr uint8_t TaskCore= 0;     // Core distinto al de NextionHMI y loop()
        
        NextionHMI& _hmi;
        Snapshot _shared;                     // Último resultado publicado; protegido por _mutex
        SemaphoreHandle_t _mutex = nullptr;
        TaskHandle_t _taskHandle = nullptr;
        PpgFilter _filter;                    // Solo lo usa _taskLoop (no requiere mutex)


        static void _taskEntry(void* self);
        void _taskLoop();
};
