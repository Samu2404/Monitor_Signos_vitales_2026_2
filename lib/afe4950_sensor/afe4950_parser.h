#pragma once 
#include "Arduino.h"

class AfeParser {
    public : 
        struct Frame {
            int32_t ecg[2]; //  Contiene ambas muestras del ECG
            int32_t ppg; // Contiene  la  muestra del PPG
            uint32_t index; // Contiene el indice  de la muestra
        };

        struct SpO2Params {
            uint8_t state;
            uint8_t secRemaining;
            uint32_t acIR;
            uint32_t dcIR;
            uint32_t acRed;
            uint32_t dcRed;
        };
        /**
         * @brief  procesa una palabra de 4 bytes del buffer 
         * @return true cuando se completa una trama y queda en out
         */
        bool push (const uint8_t byte, Frame& out);
        
        /**
         * @brief  toma los parámetros de SpO2 del buffer
         * @return true cuando se completa una trama y queda en out
         */
        bool takeSpO2(SpO2Params& out);
        void reset();

    private:
        
        static constexpr uint8_t TagHeader = 0x02;
        static constexpr uint8_t TagCount = 0x03;
        static constexpr uint8_t TagSample = 0x05;
        static constexpr uint8_t TagSpO2State = 0x06;   // Bloque de oximetría: ESTADO
        static constexpr uint8_t TagAcIR = 0x07;
        static constexpr uint8_t TagDcIR = 0x08;
        static constexpr uint8_t TagAcRed = 0x09;
        static constexpr uint8_t TagDcRed = 0x0A;       // Último TAG del bloque



        static int32_t _toSigned24(uint32_t raw);
        static bool _isValidTag(uint8_t tag);
        void _pushSpO2(uint8_t tag, uint32_t raw);
        uint8_t _word[4] = {};
        uint8_t _n = 0;
        int32_t _ecg[2] = {};
        uint8_t _phase = 0; 
        uint32_t _index = 0;

        SpO2Params _spo2Work = {        };
        uint8_t _spo2Next= 0;
        SpO2Params _spo2Out = {        };
        bool _spo2Ready = false;
}; 