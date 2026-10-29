
#ifndef BLE_CONFIG_H
#define BLE_CONFIG_H

#include <stdint.h>
#include <Arduino.h>

// ========================================
// MENSAJES DE DEPURACION
// ========================================

// Descomenta para ver los mensajes [BLE] por Serial. Con el debugger del Nextion, Serial es el
// enlace de la pantalla: el texto sin FF FF FF se pega al siguiente comando y lo invalida
// #define BLE_DEBUG

class BleNullPrint : public Print {
public:
    size_t write(uint8_t) override { return 1; }
    size_t write(const uint8_t*, size_t size) override { return size; }
};

inline Print& bleNullLog() {
    static BleNullPrint p;
    return p;
}

#ifdef BLE_DEBUG
#define BLE_LOG Serial
#else
#define BLE_LOG bleNullLog()
#endif

// ========================================
// IDENTIFICACION DEL DISPOSITIVO
// ========================================

#define BLE_DEVICE_NAME "MonitorSV"

// ========================================
// UUID DEL SERVICIO GATT
// ========================================

#define BLE_UUID_SERVICE \
"7a1e0001-5c3b-4f2a-9d6e-2b8c4a10f001"

// ========================================
// UUID DE LAS CARACTERISTICAS
// ========================================

// Signos vitales (READ)
#define BLE_UUID_SNAPSHOT \
"7a1e0002-5c3b-4f2a-9d6e-2b8c4a10f001"

// Alarmas (READ + NOTIFY)
#define BLE_UUID_ALARMS \
"7a1e0003-5c3b-4f2a-9d6e-2b8c4a10f001"

// ========================================
// PARAMETROS DEL PROTOCOLO BLE
// ========================================

namespace BLEConfig {

    // Longitud maxima de Snapshot en bytes
    constexpr uint8_t SNAPSHOT_MAX_LEN = 31;

    // Numero de posiciones de alarma
    constexpr uint8_t ALARMS_LEN = 8;

    // MTU ATT objetivo
    constexpr uint16_t MTU_TARGET = 64;

    // Actualizacion de Snapshot cada 1 segundo
    constexpr uint32_t SNAPSHOT_INTERVAL_MS = 1000;

    // Tiempo maximo para volver a anunciarse
    constexpr uint32_t RECONNECT_MAX_MS = 5000;

}

#endif // BLE_CONFIG_H
