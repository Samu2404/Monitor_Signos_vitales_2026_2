#include "AlarmBuzzer.h"

// ========================================
// PATRONES DE SONIDO
// ========================================

// Media: 3 pitidos de 200 ms y pausa larga
static const uint16_t MediumSteps[] = {200, 200, 200, 200, 200, 4800};

// Alta: 3 pitidos, pausa corta, 2 pitidos y pausa
static const uint16_t HighSteps[] = {100, 100, 100, 100, 100, 300, 100, 100, 100, 1500};

// ========================================
// EASTER EGG: LA CUCARACHA
// ========================================

struct Note {
    uint16_t hz;    // 0 = silencio
    uint16_t ms;
};

enum : uint16_t {
    N_G4 = 392, N_A4 = 440, N_B4 = 494,
    N_C5 = 523, N_D5 = 587, N_E5 = 659, N_F5 = 698, N_G5 = 784, N_A5 = 880
};

static constexpr uint16_t Corchea = 150;
static constexpr uint16_t Negra   = 2 * Corchea;

static const Note Cucaracha[] = {
    // La cu-ca-ra-cha, la cu-ca-ra-cha
    {N_G4, Corchea}, {N_G4, Corchea}, {N_G4, Corchea}, {N_C5, Negra + Corchea}, {N_E5, Negra}, {0, Negra},
    {N_G4, Corchea}, {N_G4, Corchea}, {N_G4, Corchea}, {N_C5, Negra + Corchea}, {N_E5, Negra}, {0, Negra},
    // ya no pue-de ca-mi-nar
    {N_C5, Corchea}, {N_C5, Corchea}, {N_B4, Corchea}, {N_B4, Corchea}, {N_A4, Corchea}, {N_A4, Corchea},
    {N_G4, 2 * Negra}, {0, Negra},
    // por-que no tie-ne, por-que le fal-ta
    {N_G4, Corchea}, {N_G4, Corchea}, {N_G4, Corchea}, {N_B4, Negra + Corchea}, {N_D5, Negra}, {0, Negra},
    {N_G4, Corchea}, {N_G4, Corchea}, {N_G4, Corchea}, {N_B4, Negra + Corchea}, {N_D5, Negra}, {0, Negra},
    // ...y el final que resuelve en do
    {N_G5, Corchea}, {N_A5, Corchea}, {N_G5, Corchea}, {N_F5, Corchea}, {N_E5, Corchea}, {N_D5, Corchea},
    {N_C5, 2 * Negra},
};

AlarmBuzzer::AlarmBuzzer(uint8_t pin, bool passive)
    : _pin(pin), _passive(passive) {
}

// ========================================
// INICIALIZACION
// ========================================

bool AlarmBuzzer::begin() {
    if (_passive) {
        ledcSetup(LedcChannel, ToneHighHz, LedcBits);
        ledcAttachPin(_pin, LedcChannel);
    } else {
        pinMode(_pin, OUTPUT);
    }
    _off();

    return xTaskCreatePinnedToCore(_taskEntry, "AlarmBuzzer", TaskStackSize, this,
                                   TaskPriority, &_taskHandle, TaskCore) == pdPASS;
}

// ========================================
// CAMBIAR PRIORIDAD
// ========================================

void AlarmBuzzer::setPriority(AlarmPriority priority) {
    if (priority == _priority) {
        return;                         // Mismo patrón: no se interrumpe
    }
    _priority = priority;
    if (_taskHandle) {
        xTaskNotifyGive(_taskHandle);   // Despierta la tarea para cambiar de patrón ya
    }
}

bool AlarmBuzzer::playEasterEgg() {
    if (_priority != AlarmPriority::NONE) {
        return false;                   // Con una alarma sonando no hay bromas
    }
    _easterEgg = true;
    if (_taskHandle) {
        xTaskNotifyGive(_taskHandle);
    }
    return true;
}

// ========================================
// TAREA
// ========================================

void AlarmBuzzer::_taskEntry(void* self) {
    static_cast<AlarmBuzzer*>(self)->_taskLoop();
}

void AlarmBuzzer::_taskLoop() {
    for (;;) {
        AlarmPriority p = _priority;

        if (p == AlarmPriority::NONE) {
            if (_easterEgg) {
                _playMelody();
                continue;
            }
            _off();
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   // Duerme hasta que haya alarma
            continue;
        }

        Pattern pat = (p == AlarmPriority::CRITICAL)
            ? Pattern{HighSteps,   sizeof(HighSteps)   / sizeof(HighSteps[0]),   ToneHighHz}
            : Pattern{MediumSteps, sizeof(MediumSteps) / sizeof(MediumSteps[0]), ToneMediumHz};

        for (uint8_t i = 0; i < pat.count; i++) {
            bool on = (i % 2) == 0;             // Pasos pares = pitido, impares = pausa
            if (on) {
                _on(pat.toneHz);
            } else {
                _off();
            }
            if (_wait(pat.steps[i])) {
                break;                          // Cambió la prioridad: reinicia con el nuevo patrón
            }
        }
    }
}

void AlarmBuzzer::_playMelody() {
    _easterEgg = false;

    for (const Note& n : Cucaracha) {
        if (n.hz == 0) {
            _off();
        } else {
            _on(n.hz);                          // En buzzer activo solo se oye el ritmo
        }
        // Una alarma corta la melodía al instante (otra petición la reinicia)
        if (_wait(n.ms - 20) || _priority != AlarmPriority::NONE) {
            break;
        }
        _off();                                 // Pequeño corte para separar notas repetidas
        if (_wait(20)) {
            break;
        }
    }
    _off();
}

bool AlarmBuzzer::_wait(uint32_t ms) {
    // La notificación de setPriority() corta la espera al instante
    return ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ms)) > 0;
}

// ========================================
// SALIDA
// ========================================

void AlarmBuzzer::_on(uint32_t toneHz) {
    if (_passive) {
        ledcWriteTone(LedcChannel, toneHz);     // Onda cuadrada al 50 %
    } else {
        digitalWrite(_pin, HIGH);
    }
}

void AlarmBuzzer::_off() {
    if (_passive) {
        ledcWrite(LedcChannel, 0);
    } else {
        digitalWrite(_pin, LOW);
    }
}
