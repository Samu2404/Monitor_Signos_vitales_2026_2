#include "afe4950_task.h"

bool AfeSensor::begin(uint8_t sclkPin, uint8_t misoPin, uint8_t mosiPin, uint8_t csPin, uint8_t resetPin, uint8_t rdyPin) {
    if (_ready) {
        return true; // ya se inicializo el AFE4950
    }

    SPI.begin(sclkPin, misoPin, mosiPin, csPin);
    _ready = _afe.configure(sclkPin, misoPin, mosiPin, csPin, resetPin, rdyPin);
    return _ready;
}


void AfeSensor::start() {
    if (!_ready) {
        return; // no se ha inicializado el AFE4950
    }
    _afe.enableCapture();
}

void AfeSensor::stop() {
    if (!_ready) {
        return; // no se ha inicializado el AFE4950
    }
    _afe.disableCapture();
}

