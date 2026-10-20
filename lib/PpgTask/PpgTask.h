#pragma once 

#include <Arduino.h>
#include <NextionHMI.h>
#include <WaveDecimator.h>
#include "biosignals.h"
#include "WaveScaler.h"

class PpgTask {
    public: 
        struct Snapshot {
            uint16_t spo2;
            uint16_t pulseRate;
            int32_t  sample;      // muestra que entró a process() (ADC 0..4095 o AFE con signo)
            float    filtered;    // muestra tras el filtro PPG (centrada en 0)
        };

        using SampleSource = uint16_t (*)();   // Devuelve una muestra 0..4095 (p. ej. señal simulada)

        PpgTask(NextionHMI& hmi);

        /**
         * @brief Modo ADC: crea una tarea propia que muestrea cada SamplePeriodMs
         * @param source nullptr = leer el ADC (analogPin); si no, esta función (simulación)
         */
        void begin(SampleSource source = nullptr);

        /**
         * @brief Modo externo (AFE): no crea tarea; las muestras llegan por process()
         * @param fs Frecuencia de muestreo de las muestras que se entregarán [Hz]
         */
        void beginExternal(float fs);

        /**
         * @brief Procesa una muestra: filtro + detector, snapshot, gráfica y HMI
         * @param sample Muestra ya orientada (sístole hacia arriba)
         * @param tUs    Instante de la muestra [us]. 0 = calcularlo contando muestras (fs exacta)
         */
        void process(int32_t sample, uint32_t tUs = 0);

        void setSpO2(float spo2);


        Snapshot getSnapshot();

    private:
        
        static constexpr uint8_t analogPin= 35;
        static constexpr uint8_t SamplePeriodMs= 4;
        static constexpr float ADCsampleRateHz = 1000.0f/SamplePeriodMs;   // 250 Hz
        static constexpr uint16_t GraphDecimation= 4;   // Muestras por ventana: 2·250/4 = 125 puntos/s en la gráfica
        static constexpr uint8_t GraphId= 2;
        static constexpr uint8_t GraphChannel= 0;
        static constexpr float GraphMinAmp = 20.0f; // Amplitud minima de la grafica



        // --- Señal y gráfica (el filtro y el detector viven en PpgProcessor, biosignals) ---
        static constexpr bool  InvertSignal  = false;  // Solo modo ADC: true = invierte (4095 - x) si el sensor baja de voltaje en sístole
        static constexpr bool  GraphFiltered = false;  // false = graficar la señal cruda; true = la filtrada

        static constexpr size_t TaskStackSize= 4096;
        static constexpr uint8_t TaskPriority= 2;   // Mayor que la tarea del HMI (1) para no perder el ritmo de 4 ms
        static constexpr uint8_t TaskCore= 0;     // Core distinto al de NextionHMI y loop()
        
        NextionHMI& _hmi;
        WaveDecimator _graph{GraphDecimation};   // Solo lo usa process()
        Snapshot _shared;                     // Último resultado publicado; protegido por _mutex
        SemaphoreHandle_t _mutex = nullptr;
        TaskHandle_t _taskHandle = nullptr;
        PpgProcessor _proc;                   
        WaveScaler _scaler;

        uint16_t _shownSpO2 = 0;                 // Último valor enviado al HMI (para mandar el 0 una sola vez)
        uint16_t _shownPulseRate = 0;         // Último valor enviado al HMI (para mandar el 0 una sola vez)
        SampleSource _source = nullptr;       // Fuente de muestras alternativa al ADC
        float    _fs = ADCsampleRateHz;       // Frecuencia de muestreo del modo activo
        uint32_t _n = 0;                      // Muestras procesadas (base de tiempo cuando tUs = 0)

        bool _init(float fs);                 // Lo común a ambos modos
        static void _taskEntry(void* self);
        void _taskLoop();
};