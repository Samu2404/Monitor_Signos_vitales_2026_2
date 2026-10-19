
#include <Arduino.h>

// ========================================
// HEADERS DEL PROYECTO
// ========================================

#include "BLEConfig.h"
#include "BLEManager.h"
#include "VitalSigns.h"
#include "SnapshotFormatter.h"
#include "AlarmManager.h"

// ========================================
// OBJETOS GLOBALES
// ========================================

BLEManager ble;
VitalSigns patient;
AlarmManager alarmManager;

// ========================================
// BUFFER SNAPSHOT
// ========================================

char snapshotBuffer[
    BLEConfig::SNAPSHOT_MAX_LEN + 1
];

// ========================================
// VARIABLES DE CONTROL
// ========================================

uint32_t lastSnapshot = 0;
bool bleReady = false;

// ========================================
// PUBLICAR SNAPSHOT
// ========================================

void publishSnapshot() {

    if (!bleReady) {
        return;
    }

    // Convertir mediciones al protocolo BLE
    bool formatted = SnapshotFormatter::format(
        patient,
        snapshotBuffer,
        sizeof(snapshotBuffer)
    );

    if (!formatted) {
        Serial.println("[BLE] Error formateando Snapshot");
        return;
    }

    // Actualizar la caracteristica GATT
    if (!ble.updateSnapshot(snapshotBuffer)) {
        Serial.println("[BLE] Error actualizando Snapshot");
    }
}
