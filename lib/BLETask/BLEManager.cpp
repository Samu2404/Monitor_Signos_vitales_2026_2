
#include "BLEManager.h"

#include <Arduino.h>
#include <cstring>

#include "BLEConfig.h"

// ============================================
// 1. INICIALIZACION DEL SERVIDOR BLE
// ============================================

bool BLEManager::begin() {

    if (initialized_) {
        return true;
    }

    // Inicializar NimBLE
    if (!NimBLEDevice::init(BLE_DEVICE_NAME)) {
        BLE_LOG.println("[BLE] Error iniciando NimBLE");
        return false;
    }

    // Configurar MTU local
    if (!NimBLEDevice::setMTU(
            BLEConfig::MTU_TARGET)) {
        BLE_LOG.println("[BLE] Error configurando MTU");
        return false;
    }

    // Crear servidor GATT
    server_ = NimBLEDevice::createServer();

    if (server_ == nullptr) {
        BLE_LOG.println("[BLE] Error creando servidor");
        return false;
    }

    // Registrar callbacks
    server_->setCallbacks(this, false);

    // Reanunciar despues de desconexion
    server_->advertiseOnDisconnect(true);

    // ========================================
    // CREAR SERVICIO GATT
    // ========================================

    service_ = server_->createService(
        BLE_UUID_SERVICE
    );

    if (service_ == nullptr) {
        BLE_LOG.println("[BLE] Error creando servicio");
        return false;
    }

    // ========================================
    // CREAR CARACTERISTICAS
    // ========================================

    // Snapshot: READ
    snapshotChar_ =
        service_->createCharacteristic(
            BLE_UUID_SNAPSHOT,
            NIMBLE_PROPERTY::READ,
            BLEConfig::SNAPSHOT_MAX_LEN
        );

    // Alarmas: READ + NOTIFY
    alarmsChar_ =
        service_->createCharacteristic(
            BLE_UUID_ALARMS,
            NIMBLE_PROPERTY::READ |
            NIMBLE_PROPERTY::NOTIFY,
            BLEConfig::ALARMS_LEN
        );

    if (snapshotChar_ == nullptr ||
        alarmsChar_ == nullptr) {

        BLE_LOG.println(
            "[BLE] Error creando caracteristicas"
        );
        return false;
    }

    // ========================================
    // VALORES INICIALES
    // ========================================

    snapshotChar_->setValue(
        "0,0,0,0.0,0,0,0,0"
    );

    alarmsChar_->setValue(
        "00000000"
    );

    // ========================================
    // INICIAR SERVIDOR GATT
    // ========================================

    if (!server_->start()) {
        BLE_LOG.println("[BLE] Error iniciando GATT");
        return false;
    }

    // ========================================
    // CONFIGURAR PUBLICIDAD BLE
    // ========================================

    NimBLEAdvertising* advertising =
        NimBLEDevice::getAdvertising();

    if (advertising == nullptr) {
        BLE_LOG.println("[BLE] Error en publicidad");
        return false;
    }

    // Anuncio principal: UUID del servicio
    NimBLEAdvertisementData advData;

    advData.setFlags(0x06);
    advData.addServiceUUID(
        NimBLEUUID(BLE_UUID_SERVICE)
    );

    // Respuesta de escaneo: nombre MonitorSV
    NimBLEAdvertisementData scanData;

    scanData.setName(BLE_DEVICE_NAME);

    advertising->enableScanResponse(true);

    if (!advertising->setAdvertisementData(advData)) {
        BLE_LOG.println("[BLE] Error en anuncio");
        return false;
    }

    if (!advertising->setScanResponseData(scanData)) {
        BLE_LOG.println("[BLE] Error en scan response");
        return false;
    }

    // Iniciar publicidad continuamente
    if (!advertising->start()) {
        BLE_LOG.println("[BLE] Error iniciando publicidad");
        return false;
    }

    initialized_ = true;

    BLE_LOG.println("[BLE] Servidor iniciado");

    return true;
}


// ============================================
// 2. ACTUALIZACION DE SNAPSHOT
// ============================================

bool BLEManager::updateSnapshot(const char* data) {

    if (!initialized_ ||
        data == nullptr ||
        snapshotChar_ == nullptr) {
        return false;
    }

    // Verificar longitud maxima
    size_t length = strnlen(
        data,
        BLEConfig::SNAPSHOT_MAX_LEN + 1
    );

    if (length == 0 ||
        length > BLEConfig::SNAPSHOT_MAX_LEN) {
        return false;
    }

    // Limites de campos enteros
    const uint16_t limits[8] = {
        220, 200, 100, 0,
        999, 99, 300, 200
    };

    size_t pos = 0;

    // Validar exactamente ocho campos
    for (uint8_t field = 0; field < 8; field++) {

        if (pos >= length) {
            return false;
        }

        uint16_t value = 0;
        uint8_t digits = 0;

        // Leer parte entera
        while (pos < length &&
               data[pos] >= '0' &&
               data[pos] <= '9') {

            if (digits >= 3) {
                return false;
            }

            value = value * 10 +
                    (data[pos] - '0');

            digits++;
            pos++;
        }

        if (digits == 0) {
            return false;
        }

        // Campo 4: temperatura
        if (field == 3) {

            if (value > 99) {
                return false;
            }

            // Exigir punto decimal
            if (pos >= length ||
                data[pos] != '.') {
                return false;
            }

            pos++;

            // Exigir un decimal
            if (pos >= length ||
                data[pos] < '0' ||
                data[pos] > '9') {
                return false;
            }

            pos++;

        } else {

            // Verificar valor maximo
            if (value > limits[field]) {
                return false;
            }
        }

        // Verificar separadores
        if (field < 7) {

            if (pos >= length ||
                data[pos] != ',') {
                return false;
            }

            pos++;

        } else {

            if (pos != length) {
                return false;
            }
        }
    }

    // Actualizar valor GATT sin terminador nulo
    // setValue() devuelve void en NimBLE 2.5.1
    snapshotChar_->setValue(
        reinterpret_cast<const uint8_t*>(data),
        length
    );

    return true;
}


// ============================================
// 3. ACTUALIZACION DE ALARMAS
// ============================================

bool BLEManager::updateAlarms(const char* data) {

    if (!initialized_ ||
        data == nullptr ||
        alarmsChar_ == nullptr) {
        return false;
    }

    // Verificar longitud exacta
    size_t length = strnlen(
        data,
        BLEConfig::ALARMS_LEN + 1
    );

    if (length != BLEConfig::ALARMS_LEN) {
        return false;
    }

    // Validar caracteres binarios
    for (size_t i = 0; i < length; i++) {

        if (data[i] != '0' &&
            data[i] != '1') {
            return false;
        }
    }

    // Posicion 8 reservada
    if (data[7] != '0') {
        return false;
    }

    // Notificar solo cuando hay cambios
    if (strcmp(data, lastAlarms_) == 0) {
        return true;
    }

    // Actualizar caracteristica GATT
    alarmsChar_->setValue(
        reinterpret_cast<const uint8_t*>(data),
        length
    );

    // Guardar el nuevo estado
    memcpy(lastAlarms_, data, length);
    lastAlarms_[length] = '\0';

    BLE_LOG.print("[BLE] Alarmas: ");
    BLE_LOG.println(lastAlarms_);

    // Notificar a clientes suscritos
    if (isConnected()) {

        if (!alarmsChar_->notify()) {
            BLE_LOG.println(
                "[BLE] Notificacion no enviada"
            );
        }
    }

    return true;
}


// ============================================
// 4. ESTADO DE CONEXION
// ============================================

bool BLEManager::isConnected() const {

    if (!initialized_ || server_ == nullptr) {
        return false;
    }

    return server_->getConnectedCount() > 0;
}


// ============================================
// 5. CALLBACK DE CONEXION
// ============================================

void BLEManager::onConnect(
    NimBLEServer* server,
    NimBLEConnInfo& connInfo
) {

    BLE_LOG.println("[BLE] Cliente conectado");

    // Permitir solamente un cliente
    if (server->getConnectedCount() > 1) {

        server->disconnect(
            connInfo.getConnHandle()
        );

        return;
    }

    // Detener anuncios durante conexion
    NimBLEAdvertising* advertising =
        NimBLEDevice::getAdvertising();

    if (advertising != nullptr &&
        advertising->isAdvertising()) {

        advertising->stop();
    }
}


// ============================================
// 6. CALLBACK DE DESCONEXION
// ============================================

void BLEManager::onDisconnect(
    NimBLEServer* server,
    NimBLEConnInfo& connInfo,
    int reason
) {

    BLE_LOG.println("[BLE] Cliente desconectado");

    BLE_LOG.printf(
        "[BLE] Motivo: %d\n",
        reason
    );

    // advertiseOnDisconnect(true)
    // reinicia automaticamente los anuncios
}


// ============================================
// 7. CALLBACK DE CAMBIO DE MTU
// ============================================

void BLEManager::onMTUChange(
    uint16_t mtu,
    NimBLEConnInfo& connInfo
) {

    BLE_LOG.printf(
        "[BLE] MTU negociado: %u bytes\n",
        mtu
    );
}
