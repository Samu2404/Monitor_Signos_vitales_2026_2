#pragma once
/**
 * AfeSensor.h — Driver unificado del AFE4950 (ECG + PPG) para ESP32
 *
 * Reúne en un solo archivo el driver SPI, el parser de tramas y la tarea
 * de adquisición FreeRTOS de lib/afe4950_sensor.
 *
 * Dependencia: librería "AFE4950.h" (clase AFE4950).
 *
 * Valores entregados por cada muestra (AfeData):
 *   - ecg1  : primera muestra de ECG (24 bits con signo)
 *   - ecg2  : segunda muestra de ECG (24 bits con signo)
 *   - ppg   : muestra de PPG          (24 bits con signo)
 *   - index : contador de muestra (incrementa en 1 por trama)
 *
 * Uso típico:
 *
 *   AfeSensor afe;
 *
 *   void setup() {
 *       if (!afe.begin()) { Serial.println("Error AFE4950"); }
 *       afe.startTask();                    // adquisición en segundo plano
 *   }
 *
 *   void loop() {
 *       AfeData d;
 *       if (afe.getLatest(d)) {             // true si hay muestra nueva
 *           Serial.printf("%lu,%ld,%ld,%ld\n", d.index, d.ecg1, d.ecg2, d.ppg);
 *       }
 *   }
 *
 * Alternativas:
 *   - afe.startTask(callback, ctx): recibe TODAS las muestras por callback
 *     (se ejecuta dentro de la tarea del AFE, debe ser rápido).
 *   - afe.poll(d) sin startTask(): lectura manual desde loop().
 */

#include <Arduino.h>
#include <SPI.h>
#include "AFE4950.h"

// Muestra completa entregada por el AFE4950
struct AfeData {
    int32_t  ecg1;
    int32_t  ecg2;
    int32_t  ppg;
    uint32_t index;
};

// Pines por defecto (ESP32 DevKit V1)
struct AfePins {
    uint8_t sclk  = 18;
    uint8_t miso  = 19;
    uint8_t mosi  = 23;
    uint8_t cs    = 5;
    uint8_t reset = 4;
    uint8_t rdy   = 27;
};

class AfeSensor {
public:
    using DataCallback = void (*)(const AfeData& data, void* ctx);

    AfeSensor() = default;
    AfeSensor(const AfeSensor&) = delete;
    AfeSensor& operator=(const AfeSensor&) = delete;

    /**
     * @brief Inicializa el bus SPI y configura el AFE4950
     * @return true si la inicialización fue exitosa
     * @note Bloquea ~2 s durante la configuración del AFE4950
     */
    bool begin(const AfePins& pins = AfePins()) {
        if (_ready) {
            return true;
        }
        SPI.begin(pins.sclk, pins.miso, pins.mosi, pins.cs);
        _ready = _afe.configure(pins.sclk, pins.miso, pins.mosi, pins.cs, pins.reset, pins.rdy);
        return _ready;
    }

    /**
     * @brief Habilita la captura y crea la tarea de adquisición
     * @param cb  callback opcional que recibe cada muestra
     * @param ctx puntero de contexto que se pasa al callback
     * @return true si la tarea quedó corriendo
     */
    bool startTask(DataCallback cb = nullptr, void* ctx = nullptr) {
        if (!_ready) {
            return false;
        }
        if (_taskHandle != nullptr) {
            return true;
        }
        _callback = cb;
        _ctx = ctx;
        start();
        return xTaskCreatePinnedToCore(_taskEntry, "AfeTask", TaskStackSize, this,
                                       TaskPriority, &_taskHandle, TaskCore) == pdPASS;
    }

    // Habilita la captura de datos (necesario solo si se usa poll())
    void start() {
        if (!_ready) {
            return;
        }
        _resetParser();
        _afe.enableCapture();
    }

    // Deshabilita la captura de datos
    void stop() {
        if (!_ready) {
            return;
        }
        _afe.disableCapture();
    }

    /**
     * @brief Copia la última muestra recibida por la tarea
     * @return true si es una muestra nueva desde la última llamada
     */
    bool getLatest(AfeData& out) {
        portENTER_CRITICAL(&_mux);
        out = _latest;
        bool isNew = _hasNew;
        _hasNew = false;
        portEXIT_CRITICAL(&_mux);
        return isNew;
    }

    /**
     * @brief Lectura manual (sin tarea): consume bytes disponibles
     * @return true cuando se completa una muestra y queda en out
     */
    bool poll(AfeData& out) {
        if (!_ready) {
            return false;
        }
        uint8_t b;
        while (_afe.readByte(b)) {
            if (_parse(b, out)) {
                _frames++;
                return true;
            }
        }
        return false;
    }

    // Total de muestras completas recibidas
    uint32_t frames() const { return _frames; }

    bool isReady() const { return _ready; }

private:
    // --- Configuración de la tarea ---
    static constexpr uint32_t PollPeriodMs  = 10;
    static constexpr uint32_t TaskStackSize = 6144;
    static constexpr UBaseType_t TaskPriority = 2;   // Igual que EcgTask/PpgTask
    static constexpr BaseType_t  TaskCore     = 0;   // Core distinto a NextionHMI y loop()

    // --- Etiquetas de las palabras de 4 bytes del AFE4950 ---
    static constexpr uint8_t TagHeader = 0x02;
    static constexpr uint8_t TagCount  = 0x03;
    static constexpr uint8_t TagSample = 0x05;

    // --- Tarea FreeRTOS ---
    static void _taskEntry(void* self) {
        static_cast<AfeSensor*>(self)->_taskLoop();
    }

    void _taskLoop() {
        AfeData d;
        uint8_t b;
        TickType_t lastWake = xTaskGetTickCount();
        for (;;) {
            vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(PollPeriodMs));

            while (_afe.readByte(b)) {
                if (!_parse(b, d)) {
                    continue;
                }
                _frames++;

                portENTER_CRITICAL(&_mux);
                _latest = d;
                _hasNew = true;
                portEXIT_CRITICAL(&_mux);

                if (_callback) {
                    _callback(d, _ctx);
                }
            }
        }
    }

    // --- Parser de tramas ---
    // Cada palabra son 4 bytes: [b0 b1 b2] valor de 24 bits LE + [b3] etiqueta.
    // Una muestra completa = 3 palabras TagSample consecutivas: ECG1, ECG2, PPG.
    static int32_t _toSigned24(uint32_t raw) {
        return (int32_t)(raw << 8) >> 8;
    }

    void _resetParser() {
        _n = 0;
        _phase = 0;
        _index = 0;
    }

    bool _parse(uint8_t byte, AfeData& out) {
        _word[_n++] = byte;
        if (_n < 4) {
            return false;
        }

        uint8_t tag = _word[3];
        if (tag != TagHeader && tag != TagCount && tag != TagSample) {
            // Palabra inválida: desplaza un byte para resincronizar
            _word[0] = _word[1];
            _word[1] = _word[2];
            _word[2] = _word[3];
            _n = 3;
            _phase = 0;
            return false;
        }
        _n = 0;

        if (tag != TagSample) {
            return false;   // Las cabeceras no cambian de fase
        }

        uint32_t raw = (uint32_t)_word[0] | ((uint32_t)_word[1] << 8) | ((uint32_t)_word[2] << 16);
        int32_t value = _toSigned24(raw);

        if (_phase < 2) {
            _ecg[_phase++] = value;
            return false;
        }

        out.ecg1  = _ecg[0];
        out.ecg2  = _ecg[1];
        out.ppg   = value;
        out.index = _index++;
        _phase = 0;
        return true;
    }

    // --- Estado ---
    AFE4950      _afe;
    bool         _ready = false;

    uint8_t      _word[4] = {};
    uint8_t      _n = 0;
    int32_t      _ecg[2] = {};
    uint8_t      _phase = 0;
    uint32_t     _index = 0;

    DataCallback _callback = nullptr;
    void*        _ctx = nullptr;
    TaskHandle_t _taskHandle = nullptr;
    volatile uint32_t _frames = 0;

    AfeData      _latest = {};
    bool         _hasNew = false;
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
};
