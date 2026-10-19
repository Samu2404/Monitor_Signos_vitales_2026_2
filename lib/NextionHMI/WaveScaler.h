#pragma once

#include <Arduino.h>

// Prepara una señal con signo y escala arbitraria para una gráfica 0..mapValue:
//  1. Quita la línea base con una media móvil exponencial lenta (DC y deriva).
//  2. Sigue la envolvente superior e inferior (suben al instante, se relajan con ampTauS).
//  3. Estira el rango [mínimo, máximo] a toda la altura de la gráfica, dejando un margen.
// Solo para visualizar: el procesamiento (detectores, PTT) debe usar la muestra original.
// La usa una sola tarea (el productor de la señal): no tiene protección entre tareas.
class WaveScaler {
public:
    /** @brief Configura el escalador (llamar antes de scale(), p. ej. en _init de la tarea)
     *  @param fs        Frecuencia de muestreo [Hz]
     *  @param minAmp    Amplitud mínima [unidades de entrada]: por debajo no se amplifica más
     *                   (una señal plana o puro ruido queda plana en vez de llenar la gráfica)
     *  @param mapValue  Rango de la gráfica (el MapValue de configWaveform)
     *  @param baseTauS  Constante de tiempo de la línea base [s]
     *  @param ampTauS   Tiempo con que la escala se adapta a una señal más chica [s]
     */
    void begin(float fs, float minAmp, uint32_t mapValue = 4095, float baseTauS = 1.0f, float ampTauS = 3.0f) {
        _map       = mapValue;
        _minAmp    = minAmp > 0.0f ? minAmp : 1.0f;
        _baseAlpha = 1.0f / (baseTauS * fs);
        _ampDecay  = 1.0f - 1.0f / (ampTauS * fs);
        _primed    = false;
        _hi = _lo  = 0.0f;
    }

    uint32_t scale(float x) {
        if (!_primed) {                        // La 1ª muestra fija la línea base: sin transitorio
            _base = x;
            _primed = true;
        }
        _base += _baseAlpha * (x - _base);     // EMA: sigue el DC y la deriva lenta
        float ac = x - _base;

        _hi *= _ampDecay;                      // Las envolventes se relajan hacia la línea base
        _lo *= _ampDecay;
        if (ac > _hi) _hi = ac;
        if (ac < _lo) _lo = ac;

        float lo = _lo, hi = _hi;
        if (hi - lo < 2.0f * _minAmp) {        // Señal plana o ruido: rango mínimo centrado
            float mid = 0.5f * (hi + lo);
            lo = mid - _minAmp;
            hi = mid + _minAmp;
        }
        float margin = Margin * (float)_map;
        float span   = (float)_map - 2.0f * margin;
        int32_t y = (int32_t)lroundf(margin + span * (ac - lo) / (hi - lo));
        if (y < 0)              y = 0;
        if (y > (int32_t)_map)  y = (int32_t)_map;
        return (uint32_t)y;
    }

private:
    static constexpr float Margin = 0.05f;     // 5 % libre arriba y abajo
    uint32_t _map = 4095;
    float _minAmp = 1.0f;
    float _baseAlpha = 0.0f, _ampDecay = 1.0f;
    float _base = 0.0f, _hi = 0.0f, _lo = 0.0f;
    bool  _primed = false;
};