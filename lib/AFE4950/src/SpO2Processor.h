#ifndef __SPO2_PROCESSOR_H__
#define __SPO2_PROCESSOR_H__

#include <Arduino.h>

/*
 * SpO2Processor
 * -------------
 * Procesa las muestras PPG que entrega el AFE4950 y, una vez por segundo,
 * genera los parámetros que se transmiten para el cálculo de SpO2:
 *
 *   - ESTADO del sensor (sin señal / estabilizando / señal válida)
 *   - AC y DC del canal infrarrojo (IR)
 *   - AC y DC del canal rojo (RED)
 *
 * El estado pasa a "señal válida" cuando se detecta una señal de pulso
 * estable durante SPO2_STABILIZATION_MS milisegundos.
 *
 * Los umbrales de detección (SPO2_*) pueden requerir ajuste según el sensor
 * y la ganancia configurada en el AFE.
 */

// ---- Parámetros de detección (ajustables) ----
#define SPO2_BLOCK_MS            1000     // Periodo de actualización de parámetros (ms)
#define SPO2_STABILIZATION_MS    5000     // Tiempo con señal estable antes de "válido" (ms)
#define SPO2_MIN_AC_COUNTS       100      // Amplitud pico a pico mínima del pulso (cuentas ADC)
#define SPO2_SATURATION_COUNTS   2000000  // |DC| por encima de esto se considera saturado
#define SPO2_MIN_BPM             40
#define SPO2_MAX_BPM             180
#define SPO2_BEAT_WINDOW_MS      5000     // Ventana para validar el ritmo del pulso (ms)
#define SPO2_MAX_INVALID_BLOCKS  2        // Bloques inválidos seguidos antes de "sin señal"

class SpO2Processor {

    public:

        enum State : uint8_t {
            NO_SIGNAL     = 0,
            STABILIZING   = 1,
            VALID         = 2
        };

        struct Output {
            uint8_t  state;
            uint8_t  secondsRemaining;   // Solo en STABILIZING
            uint32_t acIR;
            uint32_t dcIR;
            uint32_t acRed;
            uint32_t dcRed;
        };

        SpO2Processor();

        void reset(uint32_t nowMs);

        // Procesa una muestra PPG (valor con signo, cuentas del ADC)
        void addSample(int32_t value, uint32_t nowMs);

        // true una vez por bloque, cuando hay parámetros nuevos para enviar
        bool update(uint32_t nowMs);

        const Output& output() const { return _out; }

    private:

        // Filtros
        bool    _initialized;
        float   _smooth;        // Señal suavizada (reduce ruido)
        float   _baseline;      // Línea base: media móvil de ~1 s (componente DC)
        float   _fsEstimate;    // Frecuencia de muestreo PPG estimada (Hz)

        // Estadísticas del bloque actual y del anterior
        uint32_t _blockStart;
        uint32_t _blockCount;
        double   _blockSum;
        float    _blockMin, _blockMax;
        float    _prevMin, _prevMax;
        double   _prevSum;
        uint32_t _prevCount;
        bool     _havePrev;

        // Media móvil de ~1 s (señal diezmada a ~50 Hz)
        static const uint8_t BASE_LEN = 50;
        float    _baseBuf[BASE_LEN];
        uint8_t  _baseHead;
        uint8_t  _baseFilled;
        double   _baseSum;
        float    _decimAcc;
        uint16_t _decimCount;

        // Detección de latidos
        float    _dMin, _dMax;      // Rango de (señal - línea base) en el bloque actual
        float    _dCenter, _dHyst;  // Umbral central e histéresis (del bloque anterior)
        float    _prevBlockP2P;     // Pico a pico del bloque anterior
        bool     _armed;            // Histéresis: la señal pasó por debajo del umbral inferior
        static const uint8_t BEAT_BUF = 16;
        uint32_t _beatTimes[BEAT_BUF];
        uint8_t  _beatHead;
        uint8_t  _beatCount;

        // Máquina de estados
        State    _state;
        uint32_t _stableSince;
        uint8_t  _invalidBlocks;

        // Parámetros del canal rojo
        float    _target;       // Valor de referencia del bloque actual (%)
        float    _redDcRatio;   // DC_red / DC_ir
        uint32_t _rng;

        Output   _out;

        void   registerBeat(uint32_t nowMs);
        float  beatsPerMinute(uint32_t nowMs, uint8_t& beatsInWindow);
        float  randUniform(float lo, float hi);
        void   computeRedChannel(float acIR, float dcIR);
};

#endif // __SPO2_PROCESSOR_H__
