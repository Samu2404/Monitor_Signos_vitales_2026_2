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
        bool push (const uint8_t word[4], Frame& out);
        void reset();

        private:
        static uint32_t _toSigned24(uint32_t raw);
        int32_t _ecg[2] = {};
        uint8_t _phase = 0; 
        uint32_t _index = 0;
}; 