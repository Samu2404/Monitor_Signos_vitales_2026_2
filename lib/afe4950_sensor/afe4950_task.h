    #pragma once
    #include "Arduino.h"
    #include "afe4950_driver.h"
    #include "afe4950_parser.h"


    class AfeTask {
        public:
            using FrameSink = void (*)(const AfeParser::Frame& f,  void *ctx);
            using ByteSource = bool (*)(uint8_t& b);
            using SpO2Sink = void (*)(const AfeParser::SpO2Params& p, void *ctx);
            AfeTask (AfeSensor& afe);

            /**
             * @brief Crea la tarea de adquisicion 
             * @param sink funcion que recibe las tramas completas
             * @param ctx Puntero a contexto que se pasa a la funcion sink
             * @param source funcion que entrega los bytes de la trama
             */

            void begin (FrameSink sink, void* ctx = nullptr, ByteSource source = nullptr);
            
            /**
             * @brief Registra la funcion que recibe los parámetros de SpO2
             * @param sink funcion que recibe los parámetros de SpO2
             */
            void SetSpO2Sink (SpO2Sink sink) { _spo2Sink = sink; }
            uint32_t frames();

        private:
            static constexpr uint32_t PollPeriodMs = 10; // Periodo de muestreo de la tarea
            static constexpr size_t   TaskStackSize = 6144;
            static constexpr uint8_t  TaskPriority  = 2;      // Igual que EcgTask/PpgTask
            static constexpr uint8_t  TaskCore      = 0;      // Core distinto al de NextionHMI y loop()


            static void _taskEntry (void* self);
            void _taskLoop();

            AfeSensor&   _afe;
            AfeParser    _parser;                             // Solo lo usa _taskLoop
            FrameSink    _sink = nullptr;
            SpO2Sink     _spo2Sink = nullptr;
            void*        _ctx = nullptr;
            ByteSource   _source = nullptr;
            volatile uint32_t _frames = 0;                    
            TaskHandle_t _taskHandle = nullptr;

    };
            