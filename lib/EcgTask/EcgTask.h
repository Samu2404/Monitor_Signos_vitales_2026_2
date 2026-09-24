#pragma once

#include <Arduino.h>
#include <NextionHMI.h>
#include <biosignals.h>

// #define DEBUG 1

// Detección de electrodo desconectado (LOD+/LOD- del AD8232). Desactivada: sin ISR, sin corte de la señal.
#define ENABLE_LEAD_OFF_DETECT 0

class EcgTask {
public:
    struct Snapshot {
        float bpm ;
        uint32_t beats;
        uint16_t sample;
    };

    /**
     * @brief Constructor de la clase EcgTask
     * @param hmi Referencia al objeto NextionHMI
     */
    EcgTask(NextionHMI& hmi);

    /**
     * @brief Inicializa la tarea de muestreo de ECG
     * @note Esta función debe llamarse antes de iniciar el bucle principal del programa
     *       y antes de que se llame a getSnapshot().
     */
    void begin();
    Snapshot  getSnapshot();

private:
    
    static constexpr uint8_t analogPin= 34;
    static constexpr float sampleRateHz = 1000.0f/2.0f;
    static constexpr uint16_t GraphRateHz= 50;
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
    static void IRAM_ATTR _onLeadOffChange();    // ISR: código mínimo
    bool _leadOffState= false;                   // Estado actual de los electrodos; lo lee _electrodeCheck()   
    void _electrodeCheck();                      // Llamada desde _taskLoop(); 
};
