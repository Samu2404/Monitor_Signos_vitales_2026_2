#pragma once 
#include "Arduino.h"

class AfeParser {
    public : 
        struct Frame {
            int32_t ecg[2]; //  Contiene ambas muestras del ECG
            int32_t ppg; // Contiene  la  muestra del PPG
            uint32_t index; // Contiene el indice  de la muestra
        };
        
        /**
         * @brief  procesa una palabra de 4 bytes del buffer 
         * @return true cuando se completa una trama y queda en out
         */
        bool push (const uint8_t byte, Frame& out);
        void reset();

        private:
        static constexpr uint8_t TagHeader = 0x02;
        static constexpr uint8_t TagCount = 0x03;
        static constexpr uint8_t TagSample = 0x05;

        static int32_t _toSigned24(uint32_t raw);
        uint8_t _word[4] = {};
        uint8_t _n = 0;
        int32_t _ecg[2] = {};
        uint8_t _phase = 0; 
        uint32_t _index = 0;
}; 