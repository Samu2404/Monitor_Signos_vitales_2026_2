// Prueba de SettingsStore en la placa: lotes, reversión, guardado en NVS y persistencia tras
// reiniciar, sin sensores. También enciende y apaga el BLE real según Settings::bleEnabled,
// como lo hará main.cpp. Se compila en los entornos settings-test y settings-test-serial
#include <Arduino.h>
#include <Preferences.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include "Settings.h"
#include "BLEConfig.h"
#include "BLEManager.h"

// 1 = debugger del Nextion Editor por el USB: los ajustes llegan de la pantalla (grupos 'S' e 'I')
//     y los avisos aparecen en "MCU Return"
// 0 = monitor serie: los ajustes se escriben como comandos de texto (h = ayuda)
// El entorno settings-test-serial lo pone en 0 desde platformio.ini
#ifndef PRUEBA_DEBUGGER
#define PRUEBA_DEBUGGER 1
#endif

#if PRUEBA_DEBUGGER
#include "NextionHMI.h"

// Igual que el firmware real: UART0 (RX 3 / TX 1), el mismo USB que usa el debugger
HardwareSerial&    HmiSerial = Serial;
constexpr uint8_t  PinHmiRx  = 3;
constexpr uint8_t  PinHmiTx  = 1;
constexpr uint32_t HmiBaud   = 115200;

static NextionHMI hmi(HmiSerial);

constexpr uint8_t AlarmCmdBleDisconnect = 0x05;   // printh 23 02 41 05: botón Desconectar de ble_config

// Comando 'S', 'I' o 'A' recibido: la tarea del HMI lo encola y loop() lo aplica (un solo dueño de settings)
struct ComandoHmi {
    char    grupo;
    uint8_t len;
    uint8_t data[16];
};
static QueueHandle_t colaHmi = nullptr;
static uint8_t paginaVista = 0xFF;

// Estado BLE en la pantalla: ícono pBle (barra de estado, todas las páginas) y tBleState + rBleDot
// (ble_config). Los 4 íconos van seguidos en Picture; main.vaBlePic tiene el id del primero
constexpr uint8_t PaginaBle = 4;
// Desconectado = hubo un cliente y se perdió (sigue anunciando); vuelve a Esperando al reencender
enum EstadoBle : int8_t { BleApagado, BleEsperando, BleConectado, BleDesconectado };
static int8_t bleMostrado = -1;               // Lo que tiene la pantalla (-1 = reenviar)
#else
constexpr uint32_t LogBaud = 115200;
constexpr size_t   MaxLine = 64;
#endif

// Mismas claves que Settings.cpp (allí son static): solo para inspeccionar la NVS
static const char* const NvsNamespace = "config";
static const char* const NvsKey       = "s3";
static const char* const NvsOldKeys[] = { "s", "s2" };

// Snapshot fijo: con la app conectada se ve que llegan datos mientras BLE está encendido
static const char* const SnapshotPrueba = "75,74,98,36.8,45,16,120,80";
constexpr uint32_t SnapshotMs = 1000;

static SettingsStore settings;
static BLEManager    ble;
static bool          bleOk = false;           // ble.begin() funcionó
static bool          conectadoVisto = false;
static bool          huboCliente = false;     // Se conectó alguien desde el último encendido del BLE
static uint32_t      ultimoSnapshot = 0;
static uint32_t ultimoCambioMs = 0;           // Para medir cuánto tarda el lote en confirmarse
static uint32_t revisarNvsMs = 0;             // Cuándo comprobar que el lote quedó en la NVS (0 = nada)

// Una sola escritura por aviso: en modo debugger no se mezcla con los comandos del HMI
static void aviso(const char* fmt, ...) {
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf) - 4, fmt, ap);
    va_end(ap);
#if PRUEBA_DEBUGGER
    strcat(buf, "\xFF\xFF\xFF");                // En el debugger aparece en "MCU Return"
    HmiSerial.print(buf);
#else
    Serial.printf("[SET] %s\n", buf);
#endif
}

#if !PRUEBA_DEBUGGER
static void ayuda() {
    aviso("Comandos (terminar con Enter):");
    aviso("  p                 mostrar ajustes actuales");
    aviso("  s <id> <valor>    ajuste como 53 <id> <valor> (id dec o 0x..)");
    aviso("  i <texto>         ID del paciente (grupo 'I')");
    aviso("  n                 Nuevo paciente (53 14)");
    aviso("  t <0|1>           tipo: 0 adulto, 1 pediatrico (53 0A)");
    aviso("  b                 boton BLE: alterna encendido/apagado (53 08)");
    aviso("  l <bajo> <alto>   lote de FC seguido, como el boton Volver");
    aviso("  x                 lote incoherente (temp 38,0 / 37,0): debe revertirse");
    aviso("  f                 valores de fabrica");
    aviso("  v                 verificar lo guardado en NVS");
    aviso("  r                 reiniciar (prueba de persistencia)");
    aviso("  e                 borrar el namespace '%s' de la NVS", NvsNamespace);
    aviso("Ids: 01 FCbaja 02 FCalta 03 SpO2 04 Tbaja 05 Talta 07 mascara 08 BLE 09 vol");
    aviso("     0A tipo 0B cama 0C PAsBaja 0D PAsAlta 0E PAdBaja 0F PAdAlta");
}
#endif

static void mostrar(const char* titulo, const Settings& s) {
    aviso("--- %s (version %lu) ---", titulo, (unsigned long)settings.version());
    aviso("Paciente: %s, cama %u, ID \"%s\", ingreso %lu",
          s.patientType == (uint8_t)PatientType::Pediatric ? "pediatrico" : "adulto",
          s.bed, s.patientId, (unsigned long)s.admittedAt);
    aviso("FC %u-%u  SpO2 >=%u  Temp %u.%u-%u.%u",
          s.hrLow, s.hrHigh, s.spo2Low,
          s.tempLow / 10, s.tempLow % 10, s.tempHigh / 10, s.tempHigh % 10);
    aviso("PA sist %u-%u  diast %u-%u", s.sysLow, s.sysHigh, s.diaLow, s.diaHigh);
    aviso("Mascara 0x%02X  BLE %s  Volumen %u%%  Valido: %s",
          s.alarmEnable, s.bleEnabled ? "ON" : "OFF", s.volume,
          settingsValid(s) ? "si" : "NO");
    if (bleOk) {
        aviso("Radio BLE: %s, %s", ble.isEnabled() ? "encendido" : "apagado",
              ble.isConnected() ? "cliente conectado" : "sin cliente");
    } else {
        aviso("Radio BLE: no arranco (ble.begin() fallo)");
    }
}

// Lo que hará main.cpp: el radio sigue a Settings::bleEnabled cuando el lote se confirma
static void aplicarBle() {
    bool quiero = settings.get().bleEnabled;
    if (!bleOk || quiero == ble.isEnabled()) {
        return;
    }
    bool ok = ble.setEnabled(quiero);
    huboCliente = false;                        // Al apagar o encender: Apagado / Esperando, no Desconectado
    aviso("BLE %s%s",
          quiero ? "ENCENDIDO: anunciando como " BLE_DEVICE_NAME
                 : "APAGADO: sin anuncio, clientes desconectados",
          ok ? "" : " -> ERROR en setEnabled()");
}

static void ajuste(uint8_t id, uint16_t valor) {
    bool ok = settings.set((SettingId)id, valor);
    ultimoCambioMs = millis();
    aviso("53 %02X %u -> %s", id, valor, ok ? "aceptado" : "RECHAZADO (fuera de rango o accion)");
}

static void verificarNvs() {
    Preferences prefs;
    if (!prefs.begin(NvsNamespace, true)) {
        aviso("NVS: el namespace '%s' no existe (nunca se guardo nada)", NvsNamespace);
        return;
    }
    for (const char* key : NvsOldKeys) {
        aviso("Clave vieja '%s': %s", key, prefs.isKey(key) ? "EXISTE (no se borro)" : "no existe");
    }
    size_t len = prefs.getBytesLength(NvsKey);
    if (len != sizeof(Settings)) {
        aviso("Clave '%s': %u bytes (se esperaban %u): aun no se guardo",
              NvsKey, (unsigned)len, (unsigned)sizeof(Settings));
        prefs.end();
        return;
    }
    Settings guardado;
    prefs.getBytes(NvsKey, &guardado, sizeof(Settings));
    prefs.end();

    Settings actual = settings.get();
    mostrar("Guardado en NVS", guardado);
    aviso("Coincide con la RAM: %s", memcmp(&guardado, &actual, sizeof(Settings)) == 0
          ? "si" : "NO (cambio pendiente: se guarda 2 s despues de confirmar)");
}

// Comprobación corta tras cada guardado: lo que hay en la NVS es lo mismo que en RAM
static void revisarNvs() {
    Settings guardado;
    bool ok = false;
    Preferences prefs;
    if (prefs.begin(NvsNamespace, true)) {
        if (prefs.getBytesLength(NvsKey) == sizeof(Settings)) {
            prefs.getBytes(NvsKey, &guardado, sizeof(Settings));
            Settings actual = settings.get();
            ok = memcmp(&guardado, &actual, sizeof(Settings)) == 0;
        }
        prefs.end();
    }
    aviso("NVS: %s", ok ? "guardado, coincide con la RAM" : "NO coincide con la RAM");
}

#if PRUEBA_DEBUGGER
static const char* const NombresPagina[] = {
    "main", "full_wf", "cal_menu", "alarms_config", "ble_config",
    "dis_config", "serv_config", "pat_config", "keybdA", "serv_lock"
};

// Corre en la tarea del HMI: solo copia y encola
static void alComando(char grupo, const uint8_t* d, uint8_t n) {
    if (grupo != 'S' && grupo != 'I' && grupo != 'A') {
        return;
    }
    ComandoHmi c;
    c.grupo = grupo;
    c.len = n < sizeof(c.data) ? n : sizeof(c.data);
    memcpy(c.data, d, c.len);
    xQueueSend(colaHmi, &c, 0);
}

static void procesar(const ComandoHmi& c) {
    if (c.grupo == 'A') {                       // 23 02 41 <cmd>: teclas (la pausa 01 la atiende NextionHMI)
        if (c.len >= 1 && c.data[0] == AlarmCmdBleDisconnect) {
            if (!bleOk || !ble.isEnabled()) {
                aviso("RX A 05: Desconectar, pero BLE esta apagado");
            } else if (!ble.isConnected()) {
                aviso("RX A 05: Desconectar, pero no hay cliente");
            } else {
                bool ok = ble.disconnectAll();  // El cambio de estado lo muestra loop()
                aviso("RX A 05: Desconectar BLE%s", ok ? "" : " -> ERROR en disconnectAll()");
            }
        }
        return;
    }

    if (c.grupo == 'I') {                       // 23 <n+1> 49 <texto>: ID del paciente
        char txt[sizeof(c.data) + 1];
        memcpy(txt, c.data, c.len);
        txt[c.len] = '\0';
        bool ok = settings.setPatientId(txt, c.len);
        ultimoCambioMs = millis();
        aviso("RX I \"%s\" -> %s", txt, ok ? "aceptado" : "RECHAZADO (caracter no imprimible)");
        return;
    }

    if (c.len < 3) {                            // 23 04 53 <id> <valor,2>
        aviso("RX S con %u bytes: falta id + 2 bytes (printh mal escrito?)", c.len);
        return;
    }
    uint8_t  id    = c.data[0];
    uint16_t valor = c.data[1] | (c.data[2] << 8);   // prints <valor>,2: little endian
    switch ((SettingId)id) {
        case SettingId::NewPatient:
            settings.newPatient((uint32_t)time(nullptr));   // Sin RTC todavía: hora desde 1970
            ultimoCambioMs = millis();
            aviso("RX S 14: Nuevo paciente (ID borrado, limites del tipo)");
            break;
        case SettingId::FactoryReset:
            settings.resetDefaults();
            ultimoCambioMs = millis();
            aviso("RX S 11: valores de fabrica");
            break;
        default:
            if (id >= (uint8_t)SettingId::PauseAudio) {
                aviso("RX S %02X: accion que esta prueba no usa", id);
            } else {
                ajuste(id, valor);
            }
            break;
    }
}

// Instrucción para la pantalla en una sola escritura (como aviso(): no se mezcla con la tarea del HMI)
static void enviar(const char* fmt, ...) {
    char buf[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf) - 4, fmt, ap);
    va_end(ap);
    strcat(buf, "\xFF\xFF\xFF");
    HmiSerial.print(buf);
}

// Lo que hará NextionHMI: reenvía el estado BLE cuando cambia o cuando se carga una página
static void mostrarBle() {
    EstadoBle e = (!bleOk || !ble.isEnabled()) ? BleApagado
                : ble.isConnected()            ? BleConectado
                : huboCliente                  ? BleDesconectado
                                               : BleEsperando;
    if (e == bleMostrado) {
        return;
    }
    bleMostrado = e;

    // main.vaBle: el Preinitialize de cada página lo usa para mostrar el ícono sin esperar al ESP32
    enviar("main.vaBle.val=%d", e);
    enviar("pBle.pic=main.vaBlePic.val+%d", e);
    if (paginaVista == PaginaBle) {
        // Orden de EstadoBle: apagado, esperando, conectado, desconectado (colores = íconos)
        static const char* const Texto[]  = {"Apagado", "Esperando conexi\xF3n", "Conectado", "Desconectado"};
        static const uint16_t    ColTxt[] = {23342, 65070, 34237, 62642};
        static const uint16_t    ColPto[] = {23342, 65070, 34237, 62642};
        enviar("tBleState.txt=\"%s\"", Texto[e]);
        enviar("tBleState.pco=%u", ColTxt[e]);
        enviar("rBleDot.bco=%u", ColPto[e]);
    }
}

static void leerPantalla() {
    ComandoHmi c;
    while (xQueueReceive(colaHmi, &c, 0) == pdTRUE) {
        procesar(c);
    }
    uint8_t pagina = hmi.currentPage();
    if (pagina != paginaVista) {
        paginaVista = pagina;
        bleMostrado = -1;                       // La página nueva arranca con su texto por defecto
        aviso("RX pagina %u (%s)", pagina, pagina < sizeof(NombresPagina) / sizeof(NombresPagina[0])
              ? NombresPagina[pagina] : "?");
    }
}
#else
static void borrarNvs() {
    Preferences prefs;
    if (prefs.begin(NvsNamespace, false)) {
        prefs.clear();
        prefs.end();
        aviso("Namespace '%s' borrado. Con 'r' arranca con valores de fabrica", NvsNamespace);
    }
}

static void ejecutar(char* linea) {
    char* cmd = strtok(linea, " ");
    if (cmd == nullptr) {
        return;
    }
    char* a = strtok(nullptr, " ");
    char* b = strtok(nullptr, " ");

    switch (cmd[0]) {
        case 'p':
            mostrar("Actual", settings.get());
            break;
        case 's':
            if (a == nullptr || b == nullptr) {
                aviso("Uso: s <id> <valor>");
                break;
            }
            ajuste((uint8_t)strtol(a, nullptr, 0), (uint16_t)strtol(b, nullptr, 0));
            break;
        case 'i': {
            const char* txt = a ? a : "";       // Sin texto: borra el ID
            bool ok = settings.setPatientId(txt, strlen(txt));
            ultimoCambioMs = millis();
            aviso("ID \"%s\" -> %s", txt, ok ? "aceptado" : "RECHAZADO (caracter no imprimible)");
            break;
        }
        case 'n':
            settings.newPatient((uint32_t)time(nullptr));   // Sin RTC todavía: hora desde 1970
            ultimoCambioMs = millis();
            aviso("Nuevo paciente: ID borrado y limites del tipo actual");
            break;
        case 't':
            ajuste((uint8_t)SettingId::PatientType, a ? (uint16_t)atoi(a) : 0);
            break;
        case 'b':                               // El botón de la pantalla manda el estado nuevo, no un toggle
            ajuste((uint8_t)SettingId::Ble, settings.get().bleEnabled ? 0 : 1);
            break;
        case 'l':
            if (a == nullptr || b == nullptr) {
                aviso("Uso: l <bajo> <alto>");
                break;
            }
            ajuste((uint8_t)SettingId::HrLow,  (uint16_t)atoi(a));   // Mismo orden que la pantalla
            ajuste((uint8_t)SettingId::HrHigh, (uint16_t)atoi(b));
            break;
        case 'x':
            ajuste((uint8_t)SettingId::TempLow,  380);
            ajuste((uint8_t)SettingId::TempHigh, 370);
            break;
        case 'f':
            settings.resetDefaults();
            ultimoCambioMs = millis();
            aviso("Valores de fabrica");
            break;
        case 'v':
            verificarNvs();
            break;
        case 'r':
            aviso("Reiniciando...");
            Serial.flush();
            ESP.restart();
            break;
        case 'e':
            borrarNvs();
            break;
        case 'h':
            ayuda();
            break;
        default:
            aviso("Comando desconocido: %s (h = ayuda)", cmd);
            break;
    }
}

static void leerComandos() {
    static char linea[MaxLine];
    static size_t len = 0;
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            linea[len] = '\0';
            ejecutar(linea);
            len = 0;
        } else if (len < MaxLine - 1) {
            linea[len++] = c;
        }
    }
}
#endif

void setup() {
#if PRUEBA_DEBUGGER
    colaHmi = xQueueCreate(16, sizeof(ComandoHmi));
    hmi.setCommandLog(alComando);               // Antes de begin(): la tarea ya escucha al arrancar
    hmi.begin(HmiBaud, PinHmiRx, PinHmiTx);     // También abre HmiSerial
#else
    Serial.begin(LogBaud);
    delay(1000);                                // Da tiempo a abrir el monitor serie
#endif
    aviso("Prueba de SettingsStore (sizeof(Settings) = %u bytes)", (unsigned)sizeof(Settings));

    if (!settings.begin()) {
        aviso("ERROR: SettingsStore::begin() fallo (sin mutex)");
        return;
    }
    bleOk = ble.begin();                        // Arranca anunciando: si estaba guardado OFF se apaga ya
    aplicarBle();
    mostrar("Cargado al arrancar", settings.get());
    verificarNvs();                             // Las claves viejas ya deberían estar borradas
#if PRUEBA_DEBUGGER
    aviso("Los ajustes llegan de la pantalla. Sin boton: printh 23 04 53 08 00 00 (BLE off)");
#else
    ayuda();
#endif
}

void loop() {
#if PRUEBA_DEBUGGER
    leerPantalla();
#else
    leerComandos();
#endif

    switch (settings.poll()) {
        case SettingsStore::Commit::Accepted:
            aviso("Lote CONFIRMADO a los %lu ms (se guarda en NVS en ~2 s)",
                  (unsigned long)(millis() - ultimoCambioMs));
            revisarNvsMs = millis() + 2500;     // SaveDelayMs de SettingsStore + margen
            aplicarBle();
            mostrar("Confirmado", settings.get());
            break;
        case SettingsStore::Commit::Reverted:
            aviso("Lote REVERTIDO: bajo >= alto, se volvio a lo ultimo valido");
            aplicarBle();
            mostrar("Revertido", settings.get());
            break;
        default:
            break;
    }

    if (revisarNvsMs != 0 && (int32_t)(millis() - revisarNvsMs) >= 0) {
        revisarNvsMs = 0;
        revisarNvs();
    }

    if (bleOk) {
        bool conectado = ble.isConnected();
        if (conectado != conectadoVisto) {
            conectadoVisto = conectado;
            huboCliente |= conectado;
            aviso("BLE: cliente %s", conectado ? "CONECTADO" : "desconectado");
        }
        if (millis() - ultimoSnapshot >= SnapshotMs) {
            ultimoSnapshot = millis();
            ble.updateSnapshot(SnapshotPrueba);
        }
    }
#if PRUEBA_DEBUGGER
    mostrarBle();
#endif
    delay(10);
}
