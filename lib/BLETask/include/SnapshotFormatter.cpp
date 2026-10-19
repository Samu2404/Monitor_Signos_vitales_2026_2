
#include "SnapshotFormatter.h"

#include <stdio.h>
#include <math.h>

#include "BLEConfig.h"

// ========================================
// FUNCION AUXILIAR PARA LIMITAR VALORES
// ========================================

static uint16_t clampValue(
    uint16_t value,
    uint16_t maximum
) {
    return value > maximum ? maximum : value;
}

// ========================================
// FORMATEAR SNAPSHOT
// ========================================

bool SnapshotFormatter::format(
    const VitalSigns& signs,
    char* buffer,
    size_t bufferSize
) {

    // Verificar buffer y capacidad
    if (buffer == nullptr ||
        bufferSize <
        BLEConfig::SNAPSHOT_MAX_LEN + 1) {
        return false;
    }

    // ====================================
    // LIMITAR VALORES AL CONTRATO
    // ====================================

    uint16_t hr = clampValue(
        signs.hr, 220
    );

    uint16_t pr = clampValue(
        signs.pr, 200
    );

    uint16_t spo2 = clampValue(
        signs.spo2, 100
    );

    uint16_t hrv = clampValue(
        signs.hrv, 999
    );

    uint16_t rr = clampValue(
        signs.rr, 99
    );

    uint16_t sys = clampValue(
        signs.sys, 300
    );

    uint16_t dia = clampValue(
        signs.dia, 200
    );

    // ====================================
    // VALIDAR TEMPERATURA
    // ====================================

    float temperature = signs.temperature;

    // Reemplazar valores no validos
    if (!isfinite(temperature) ||
        temperature < 0.0f) {

        temperature = 0.0f;
    }

    if (temperature > 99.9f) {
        temperature = 99.9f;
    }

    // ====================================
    // CONVERTIR A TEXTO ASCII
    // ====================================

    int written = snprintf(
        buffer,
        bufferSize,
        "%u,%u,%u,%.1f,%u,%u,%u,%u",

        static_cast<unsigned>(hr),
        static_cast<unsigned>(pr),
        static_cast<unsigned>(spo2),
        static_cast<double>(temperature),
        static_cast<unsigned>(hrv),
        static_cast<unsigned>(rr),
        static_cast<unsigned>(sys),
        static_cast<unsigned>(dia)
    );

    // ====================================
    // VERIFICAR LONGITUD
    // ====================================

    if (written < 0) {
        return false;
    }

    if (static_cast<size_t>(written) >=
        bufferSize) {
        return false;
    }

    if (written >
        BLEConfig::SNAPSHOT_MAX_LEN) {
        return false;
    }

    return true;
}
