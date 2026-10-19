#pragma once
#include "Arduino.h"
#include "SPI.h"
#include "AFE4950.h"

class AfeSensor {
    public:
        AfeSensor() = default;
        AfeSensor(const AfeSensor&) = delete;
        AfeSensor& operator=(const AfeSensor&) = delete;

        
        /** @brief Inicializa el bus SPI Con los pines segun se asigen y configura el AFE
        * @return true si la inicializacion fue exitosa, false si hubo error
        * @note Genera un bloqueo de 2 segundos al inicializar el AFE4950
        */
        bool begin ( uint8_t sclkPin = 18, uint8_t misoPin = 19, uint8_t mosiPin = 23, uint8_t csPin = 5, uint8_t resetPin = 4, uint8_t rdyPin = 27);


        void start(); // habilita la adquisicion de datos del AFE4950
        void stop(); // deshabilita la adquisicion de datos del AFE4950
        bool readByte(uint8_t& b);   // true si había 4 bytes disponibles


    private:
        AFE4950 _afe;
        bool _ready = false;
};