
#ifndef SNAPSHOT_FORMATTER_H
#define SNAPSHOT_FORMATTER_H

#include <stddef.h>
#include "VitalSigns.h"

// ========================================
// FORMATEADOR DE SIGNOS VITALES
// ========================================

class SnapshotFormatter {

public:

    // Convierte VitalSigns a texto ASCII
    // conforme al contrato BLE v0.2
    static bool format(
        const VitalSigns& signs,
        char* buffer,
        size_t bufferSize
    );

};

#endif // SNAPSHOT_FORMATTER_H
