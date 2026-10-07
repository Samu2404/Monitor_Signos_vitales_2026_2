#pragma once

#include <Arduino.h>

// Diezma una señal por cantidad de muestras conservando su envolvente: por cada ventana
// de Factor muestras entrega el mínimo y el máximo en el orden en que ocurrieron.
// Así un pico angosto (onda R) siempre llega a la gráfica, caiga donde caiga el corte.
// Debe usarlo una sola tarea (el productor de la señal): no tiene protección entre tareas.
class WaveDecimator {
public:
    explicit WaveDecimator(uint16_t factor) : _factor(factor > 0 ? factor : 1) {}

    // Agrega una muestra. Devuelve true cuando se completa la ventana;
    // entonces first() y second() tienen los dos puntos a graficar, en orden temporal
    bool push(uint32_t v) {
        if (_n == 0 || v < _min) { _min = v; _minIdx = _n; }
        if (_n == 0 || v > _max) { _max = v; _maxIdx = _n; }
        if (++_n < _factor) {
            return false;
        }
        _n = 0;
        return true;
    }

    uint32_t first()  const { return _minIdx <= _maxIdx ? _min : _max; }
    uint32_t second() const { return _minIdx <= _maxIdx ? _max : _min; }

private:
    uint16_t _factor;
    uint16_t _n = 0;
    uint32_t _min = 0, _max = 0;
    uint16_t _minIdx = 0, _maxIdx = 0;
};
