#pragma once

#include <Arduino.h>
#include <NextionHMI.h>
#include <biosignals.h>

// #define DEBUG 1

class EcgTask {
public:
    struct Snapshot {
        float bpm ;
        uint32_t beats;
        uint16_t sample;
    };

    EcgTask(NextionHMI& hmi);
    void begin();
    Snapshot  getSnapshot();
private:
    
    static constexpr uint8_t analogPin= 34;
    static constexpr float sampleRateHz = 1000.0f/3.0f;
    static constexpr uint8_t GraphRateHz= 144;
    static constexpr uint8_t GraphId= 1;
    static constexpr uint8_t GraphChannel= 0;
    static constexpr uint8_t SamplePeriodMs= 3;
    
    static constexpr size_t TaskStackSize= 4096;
    static constexpr uint8_t TaskPriority= 2;   // Mayor que la tarea del HMI (1) para no perder el ritmo de 3 ms
    static constexpr uint8_t TaskCore= 0;       // Core distinto al de NextionHMI y loop()

    static void _taskEntry(void* self);   // Trampolín: FreeRTOS pide una función sin 'this'
    void _taskLoop();                     // Bucle de la tarea: muestrea, calcula y publica

    NextionHMI& _hmi;
    PanTompkins _pt;                      // Miembro directo (va a .bss), no en la pila de la tarea
    Snapshot _shared;                     // Último resultado publicado; protegido por _mutex
    SemaphoreHandle_t _mutex = nullptr;
    TaskHandle_t _taskHandle = nullptr;
    
    // Comprueba si los electrodos están conectados y actualiza la pantalla
    static volatile bool s_leadOffFlag;          // La ISR solo la marca; _electrodeCheck() la consume
    static constexpr uint8_t isrPinLOMinus= 17;  // LOD- del AD8232 (electrodo derecho)
    static constexpr uint8_t isrPinLOPlus= 16;   // LOD+ del AD8232 (electrodo izquierdo)
    static void IRAM_ATTR _onLeadOffChange();    // ISR: código mínimo, sin 'this'
    void _electrodeCheck();                      // Llamada desde _taskLoop(); NO es la ISR
};
