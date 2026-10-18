#pragma once
#include "Arduino.h"
#include "SPI.h"
#include "AFE4950.h"

class Afe4950 {
    public:
        /** @brief Inicializa el bus SPI Con los pines segun se asigen y configura el AFE
        * @return true si la inicializacion fue exitosa, false si hubo error
        * @note Genera un bloqueo de 2 segundos al inicializar el AFE4950
        */
        bool begin ( uint8_t sclkPin = 18, uint8_t misoPin = 19, uint8_t mosiPin = 23, uint8_t csPin = 5, uint8_t resetPin = 4, uint8_t rdyPin = 2);


        void start(); // habilita la adquisicion de datos del AFE4950
        void stop(); // deshabilita la adquisicion de datos del AFE4950
    private:
        AFE4950 _afe;
        bool _ready = false;
};