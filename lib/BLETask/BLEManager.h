
#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

// ========================================
// LIBRERIAS
// ========================================

#include <NimBLEDevice.h>
#include <stdint.h>

// ========================================
// CLASE BLE MANAGER
// ========================================

class BLEManager : private NimBLEServerCallbacks {

public:

    // Inicializar servidor BLE
    bool begin();

    // Actualizar mediciones del monitor
    bool updateSnapshot(const char* data);

    // Actualizar estado de alarmas
    bool updateAlarms(const char* data);

    // Consultar estado de conexion
    bool isConnected() const;

private:

    // ====================================
    // OBJETOS GATT
    // ====================================

    NimBLEServer* server_ = nullptr;

    NimBLEService* service_ = nullptr;

    NimBLECharacteristic* snapshotChar_ = nullptr;

    NimBLECharacteristic* alarmsChar_ = nullptr;

    // ====================================
    // ESTADO INTERNO
    // ====================================

    bool initialized_ = false;

    char lastAlarms_[9] = "00000000";

    // ====================================
    // CALLBACKS BLE
    // ====================================

    void onConnect(
        NimBLEServer* server,
        NimBLEConnInfo& connInfo
    ) override;

    void onDisconnect(
        NimBLEServer* server,
        NimBLEConnInfo& connInfo,
        int reason
    ) override;

    void onMTUChange(
        uint16_t mtu,
        NimBLEConnInfo& connInfo
    ) override;

};

#endif // BLE_MANAGER_H
