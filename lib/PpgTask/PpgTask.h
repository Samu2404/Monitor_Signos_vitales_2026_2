#pragma once 

#include <Arduino.h>
#include <NextionHMI.h>

class PpgTask {
    public: 
        struct Snapshot {
            uint16_t spo2;
            uint16_t pulseRate;
            uint16_t sample;
        };

        PpgTask(NextionHMI& hmi);
        void begin();
        Snapshot getSnapshot();

    private:
        
        static constexpr uint8_t analogPin= 35;
        static constexpr float sampleRateHz = 1000.0f/3.0f;
        static constexpr uint8_t GraphRateHz= 144;
        static constexpr uint8_t GraphId= 2;
        static constexpr uint8_t GraphChannel= 0;
        static constexpr uint8_t SamplePeriodMs= 3;
        
        static constexpr size_t TaskStackSize= 4096;
        static constexpr uint8_t TaskPriority= 2;   // Mayor que la tarea del HMI (1) para no perder el ritmo de 3 ms
        static constexpr uint8_t TaskCore= 0;     // Core distinto al de NextionHMI y loop()
        
        NextionHMI& _hmi;
        Snapshot _shared;                     // Último resultado publicado; protegido por _mutex
        SemaphoreHandle_t _mutex = nullptr;
        TaskHandle_t _taskHandle = nullptr;


        static void _taskEntry(void* self);
        void _taskLoop();
};