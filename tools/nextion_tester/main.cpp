// ========================================
// PROBADOR DE LA PANTALLA NEXTION
// ========================================
//
// Programa independiente del firmware del monitor: simula los signos vitales,
// responde a lo que envía la pantalla y muestra todo por el monitor serie (USB).
// Sirve para comprobar el archivo .HMI sin sensores conectados.
//
//   pio run -e nextion_test -t upload && pio device monitor
//
// Escribe una tecla en el monitor serie (115200 baudios); 'h' muestra la ayuda.
//
// No usa la librería NextionHMI: habla el mismo protocolo directamente, así
// también prueba lo que el firmware todavía no envía (alarmas, BLE, reloj).

#include <Arduino.h>
#include <EasyNextionLibrary.h>
#include <math.h>

// ========================================
// CONFIGURACION (ajustar a tu pantalla)
// ========================================

static constexpr uint32_t NextionBaud = 9600;   // El mismo que usa main.cpp (bauds del Nextion)
static constexpr uint8_t  RxPin = 16;
static constexpr uint8_t  TxPin = 17;

// Número de cada página en el panel Page del editor (el que envía su printh 23 02 50 XX).
// main y full_wf son fijos en el firmware; el resto depende de tu orden.
static constexpr uint8_t PageMain   = 0;
static constexpr uint8_t PageFullWf = 1;
static constexpr uint8_t PageCfgAlm = 2;
static constexpr uint8_t PageCal    = 3;
static constexpr uint8_t PageCfgBle = 7;

// Ids de los Waveform (fijos en el firmware: EcgTask::GraphId y PpgTask::GraphId)
static constexpr uint8_t WaveEcg = 1;
static constexpr uint8_t WavePpg = 2;

// Puntos por segundo de cada onda. A 9600 baudios no caben más de ~25 por onda;
// con 115200 se puede subir a 125
static constexpr uint32_t WaveRateHz = NextionBaud >= 115200 ? 125 : 25;

// Colores (RGB565) de la guía de diseño
static constexpr uint16_t ColText     = 57084;
static constexpr uint16_t ColWhite    = 65535;
static constexpr uint16_t ColGray     = 23342;
static constexpr uint16_t ColBar      = 4324;
static constexpr uint16_t ColEcg      = 24148;
static constexpr uint16_t ColSpo2     = 34237;
static constexpr uint16_t ColTemp     = 65070;
static constexpr uint16_t ColAlarmBar = 41317;   // Fondo de tAlarm en alarma grave
static constexpr uint16_t ColAlarmMed = 60644;   // Fondo de tAlarm en alarma media
static constexpr uint16_t ColStripe   = 57929;   // Franja lateral en alarma
static constexpr uint16_t ColAlarmNum = 62642;   // Número del parámetro en alarma

// ========================================
// ESTADO SIMULADO
// ========================================

enum Alarm : uint8_t { AlarmNone, AlarmHrHigh, AlarmSpo2Low, AlarmTempHigh };
enum BleState : uint8_t { BleOff, BleWaiting, BleConnected };

struct Sim {
    uint16_t hr = 75, spo2 = 98, pr = 75;
    float    temp = 36.8f;
    uint16_t sys = 0, dia = 0;
    Alarm    alarm = AlarmNone;

    uint8_t  calState = 0;           // 0 = sin calibrar, 1 = calibrando, 2 = calibrado
    uint8_t  calProgress = 0;
    uint16_t calSys = 0, calDia = 0;

    uint16_t pauseLeft = 0;          // Segundos de audio en pausa (0 = sin pausa)
    Alarm    pauseAlarm = AlarmNone; // Alarma activa al pausar (otra distinta corta la pausa)

    BleState ble = BleWaiting;
    bool     waves = true;
    uint8_t  page = PageMain;
    uint8_t  fullWfId = 0;           // 0 = full_wf aún no eligió onda: se envían las dos
};

static Sim sim;
static EasyNex myNex(Serial2);
static char lastCmd[48] = "";        // Último comando enviado: para explicar los errores
static uint8_t lastError = 0;        // Último código de error recibido (0 = ninguno)

// ========================================
// ENVIO A LA PANTALLA
// ========================================

static void sendCmd(const char* cmd) {
    strncpy(lastCmd, cmd, sizeof(lastCmd) - 1);
    myNex.writeStr(cmd);
}

static void sendNum(const char* obj, int32_t value) {
    snprintf(lastCmd, sizeof(lastCmd), "%s=%ld", obj, (long)value);
    myNex.writeNum(obj, value);
}

static void sendTxt(const char* obj, const char* txt) {
    snprintf(lastCmd, sizeof(lastCmd), "%s=\"%s\"", obj, txt);
    myNex.writeStr(obj, txt);
}

static const char* pageName(uint8_t p) {
    if (p == PageMain)   return "main";
    if (p == PageFullWf) return "full_wf";
    if (p == PageCfgAlm) return "cfg_alm";
    if (p == PageCal)    return "cal";
    if (p == PageCfgBle) return "cfg_ble";
    return "otra";
}

// Los textos van en ISO-8859-1 (la codificación de las fuentes del Nextion):
// las letras con tilde se escriben con su código, p. ej. "\xF3" = ó
static const char* alarmText(Alarm a) {
    switch (a) {
        case AlarmHrHigh:   return "FC alta";
        case AlarmSpo2Low:  return "SpO2 baja";
        case AlarmTempHigh: return "Temperatura alta";
        default:            return "Sin alarmas";
    }
}

// --- Barra de estado (todas las páginas) ---

static void showPause() {
    if (sim.pauseLeft == 0) {
        sendCmd("vis pPause,0");
        sendTxt("tPause.txt", "");
        return;
    }
    char txt[8];
    snprintf(txt, sizeof(txt), "%u:%02u", sim.pauseLeft / 60, sim.pauseLeft % 60);
    sendCmd("vis pPause,1");
    sendTxt("tPause.txt", txt);
}

static void showClock() {
    uint32_t min = millis() / 60000UL;
    char txt[8];
    snprintf(txt, sizeof(txt), "%02lu:%02lu", (unsigned long)((12 + min / 60) % 24), (unsigned long)(min % 60));
    sendTxt("tClock.txt", txt);
}

static void showBleBar() {
    sendNum("tBle.pco", sim.ble == BleConnected ? ColSpo2 : ColGray);
}

static void showAlarmBar() {
    // En las páginas de configuración tAlarm lleva el título: solo se toca en main y full_wf
    if (sim.page != PageMain && sim.page != PageFullWf) {
        return;
    }
    if (sim.alarm == AlarmNone) {
        sendNum("tAlarm.bco", ColBar);
        sendNum("tAlarm.pco", ColText);
    } else if (sim.alarm == AlarmTempHigh) {
        sendNum("tAlarm.bco", ColAlarmMed);
        sendNum("tAlarm.pco", 0);
    } else {
        sendNum("tAlarm.bco", ColAlarmBar);
        sendNum("tAlarm.pco", ColWhite);
    }
    sendTxt("tAlarm.txt", alarmText(sim.alarm));
}

// --- Página main ---

static void showMainNumbers() {
    if (sim.page != PageMain) {
        return;
    }
    sendNum("nHR.val", sim.hr);
    sendNum("nSpO2.val", sim.spo2);
    sendNum("nPR.val", sim.pr);
    sendNum("nT.val", lroundf(sim.temp * 10.0f));     // Xfloat con vvs1=1
    sendNum("nSistolica.val", sim.sys);
    sendNum("nDiastolica.val", sim.dia);
    sendNum("nCalEst.val", sim.calState);
    sendNum("jCal.val", sim.calProgress);

    // Colores de alarma: franja lateral y número
    bool hr   = sim.alarm == AlarmHrHigh;
    bool spo2 = sim.alarm == AlarmSpo2Low;
    bool temp = sim.alarm == AlarmTempHigh;
    sendNum("rHr.bco", hr ? ColStripe : ColEcg);
    sendNum("nHR.pco", hr ? ColAlarmNum : ColEcg);
    sendNum("rSp.bco", spo2 ? ColStripe : ColSpo2);
    sendNum("nSpO2.pco", spo2 ? ColAlarmNum : ColSpo2);
    sendNum("rT.bco", temp ? ColStripe : ColTemp);
    sendNum("nT.pco", temp ? ColAlarmNum : ColTemp);
}

// --- Página cfg_ble ---

static void showBlePage() {
    if (sim.page != PageCfgBle) {
        return;
    }
    static const char* const Text[] = {"Apagado", "Esperando conexi\xF3n", "Conectado"};
    static const uint16_t TextCol[] = {36020, ColTemp, ColSpo2};
    static const uint16_t DotCol[]  = {ColGray, ColTemp, ColSpo2};
    sendTxt("tBleState.txt", Text[sim.ble]);
    sendNum("tBleState.pco", TextCol[sim.ble]);
    sendNum("rBleDot.bco", DotCol[sim.ble]);
}

static void refreshPage() {
    showPause();
    showClock();
    showBleBar();
    showAlarmBar();
    showMainNumbers();
    showBlePage();
}

// --- Ondas sintéticas (0-255, como las que envía el firmware) ---

static uint8_t ecgSample(float p) {
    float y = 100.0f;
    if (p > 0.10f && p < 0.20f) y += 12.0f * sinf((p - 0.10f) / 0.10f * PI);    // P
    else if (p >= 0.25f && p < 0.27f) y -= 15.0f;                               // Q
    else if (p >= 0.27f && p < 0.30f) y += 120.0f * sinf((p - 0.27f) / 0.03f * PI); // R
    else if (p >= 0.30f && p < 0.33f) y -= 30.0f;                               // S
    else if (p > 0.45f && p < 0.65f) y += 25.0f * sinf((p - 0.45f) / 0.20f * PI);   // T
    return (uint8_t)constrain(y, 0.0f, 255.0f);
}

static uint8_t ppgSample(float p) {
    float y = p < 0.2f ? sinf(p / 0.2f * HALF_PI)
                       : expf(-(p - 0.2f) * 4.0f) + 0.15f * expf(-powf((p - 0.45f) / 0.05f, 2.0f));
    return (uint8_t)constrain(50.0f + 150.0f * y, 0.0f, 255.0f);
}

static void sendWaves() {
    static uint32_t last = 0;
    static float phaseEcg = 0.0f, phasePpg = 0.0f;
    uint32_t now = micros();
    if (!sim.waves || now - last < 1000000UL / WaveRateHz) {
        return;
    }
    last = now;

    bool onMain = sim.page == PageMain;
    bool onFull = sim.page == PageFullWf;
    if (!onMain && !onFull) {
        return;
    }
    char cmd[24];
    float step = (sim.hr ? sim.hr : 60) / 60.0f / WaveRateHz;   // Fracción de latido por punto
    phaseEcg = fmodf(phaseEcg + step, 1.0f);
    phasePpg = fmodf(phasePpg + step, 1.0f);

    if (onMain || sim.fullWfId == 0 || sim.fullWfId == WaveEcg) {
        snprintf(cmd, sizeof(cmd), "add %u,0,%u", WaveEcg, sim.hr ? ecgSample(phaseEcg) : 100);
        sendCmd(cmd);
    }
    if (onMain || sim.fullWfId == 0 || sim.fullWfId == WavePpg) {
        snprintf(cmd, sizeof(cmd), "add %u,0,%u", WavePpg, sim.spo2 ? ppgSample(phasePpg) : 50);
        sendCmd(cmd);
    }
}

// ========================================
// LO QUE ENVIA LA PANTALLA
// ========================================

static const char* errorText(uint8_t code) {
    switch (code) {
        case 0x00: return "instruccion invalida";
        case 0x02: return "id de componente invalido";
        case 0x03: return "id de pagina invalido";
        case 0x04: return "id de imagen invalido";
        case 0x05: return "id de fuente invalido";
        case 0x12: return "id de waveform o canal invalido";
        case 0x1A: return "objeto o atributo inexistente";
        case 0x1B: return "operacion invalida";
        case 0x1C: return "asignacion fallida";
        case 0x1E: return "numero de parametros invalido";
        case 0x20: return "caracter de escape invalido";
        case 0x23: return "nombre demasiado largo";
        case 0x24: return "buffer serie lleno (bajar la tasa de ondas)";
        default:   return nullptr;
    }
}

static void onPage(uint8_t page) {
    sim.page = page;
    sim.fullWfId = 0;               // Como el firmware: full_wf envía las dos hasta que elija
    Serial.printf("[pantalla] pagina %u (%s)\n", page, pageName(page));
    refreshPage();
}

static void startPause() {
    if (sim.pauseLeft > 0) {
        sim.pauseLeft = 0;
        Serial.println("  -> pausa cancelada: el audio vuelve");
    } else {
        sim.pauseLeft = 120;
        sim.pauseAlarm = sim.alarm;
        Serial.println("  -> audio en pausa 2:00");
    }
    showPause();
}

static void startCalibration(uint16_t sbp, uint16_t dbp) {
    if (sbp < 70 || sbp > 250 || dbp < 40 || dbp > 150 || sbp < dbp + 10) {
        Serial.println("  -> RECHAZADA: el firmware (BpTask::startCalibration) tambien la rechazaria");
        return;
    }
    sim.calSys = sbp;
    sim.calDia = dbp;
    sim.calState = 1;
    sim.calProgress = 0;
    sim.sys = sim.dia = 0;
    Serial.println("  -> calibrando: en main deben verse jCal y nCalEst=1 durante 10 s");
    showMainNumbers();
}

static void onSetting(uint8_t key, uint16_t value) {
    switch (key) {
        case 0x01: Serial.printf("  FC bajo = %u lpm\n", value); break;
        case 0x02: Serial.printf("  FC alto = %u lpm\n", value); break;
        case 0x03: Serial.printf("  SpO2 bajo = %u %%\n", value); break;
        case 0x04: Serial.printf("  Temp bajo = %.1f C\n", value / 10.0f); break;
        case 0x05: Serial.printf("  Temp alto = %.1f C\n", value / 10.0f); break;
        case 0x06: Serial.printf("  Volumen = %u %%\n", value); break;
        case 0x07: Serial.printf("  Alarmas activas: FC=%u SpO2=%u Temp=%u SinSenal=%u\n",
                                 value & 1, (value >> 1) & 1, (value >> 2) & 1, (value >> 3) & 1); break;
        case 0x08:
            Serial.printf("  BLE = %s\n", value ? "ON" : "OFF");
            sim.ble = value ? BleWaiting : BleOff;
            showBleBar();
            showBlePage();
            break;
        case 0x09: Serial.printf("  Intervalo BLE = %u s\n", value); break;
        default:   Serial.printf("  clave desconocida 0x%02X = %u\n", key, value); break;
    }
}

// EasyNex llama a esta función con cada comando "# <len> <grupo> <datos>" que no es de página
void easyNexReadCustomCommand() {
    uint8_t group = myNex.cmdGroup;
    uint8_t len   = myNex.cmdLength;   // Incluye el byte del grupo

    switch (group) {
        case 'W': {
            sim.fullWfId = (uint8_t)myNex.readByte();
            Serial.printf("[pantalla] W: onda elegida en full_wf = %u (%s)\n", sim.fullWfId,
                          sim.fullWfId == WaveEcg ? "ECG" : sim.fullWfId == WavePpg ? "Pleth" : "?");
            break;
        }
        case 'C': {
            uint8_t sbp = (uint8_t)myNex.readByte();
            uint8_t dbp = (uint8_t)myNex.readByte();
            Serial.printf("[pantalla] C: calibrar PA con %u/%u\n", sbp, dbp);
            startCalibration(sbp, dbp);
            break;
        }
        case 'A': {
            uint8_t cmd = (uint8_t)myNex.readByte();
            static const char* const Names[] = {"?", "Pausar audio", "Reconocer", "Probar sonido",
                                                "Limites auto", "Desconectar BLE"};
            Serial.printf("[pantalla] A %02X: %s\n", cmd, cmd <= 5 ? Names[cmd] : "?");
            if (cmd == 0x01) {
                startPause();
            } else if (cmd == 0x05 && sim.ble == BleConnected) {
                sim.ble = BleWaiting;
                showBleBar();
                showBlePage();
            }
            break;
        }
        case 'S': {
            uint8_t key = (uint8_t)myNex.readByte();
            uint8_t lo  = (uint8_t)myNex.readByte();
            uint8_t hi  = (uint8_t)myNex.readByte();
            Serial.printf("[pantalla] S %02X:", key);
            onSetting(key, (uint16_t)(lo | (hi << 8)));
            break;
        }
        default: {
            Serial.printf("[pantalla] grupo desconocido '%c' (0x%02X), datos:", group, group);
            for (uint8_t i = 1; i < len; i++) {
                Serial.printf(" %02X", (uint8_t)myNex.readByte());
            }
            Serial.println();
            break;
        }
    }
}

// Lee la pantalla: comandos propios (#), respuesta a sendme (0x66) y códigos de error
static void listen() {
    while (Serial2.available() > 0) {
        int c = Serial2.peek();
        if (c == '#') {
            if (Serial2.available() < 3) {
                return;
            }
            myNex.currentPageId = -1;
            myNex.NextionListen();
            if (myNex.currentPageId >= 0) {
                onPage((uint8_t)myNex.currentPageId);
            }
        } else if (c == 0x66) {
            if (Serial2.available() < 2) {
                return;
            }
            Serial2.read();
            onPage((uint8_t)Serial2.read());
        } else {
            uint8_t code = (uint8_t)Serial2.read();
            if (code == 0xFF) {
                continue;                       // Terminadores
            }
            if (code == 0x88) {
                Serial.println("[pantalla] se reinicio: se vuelve a preguntar la pagina");
                sendCmd("bkcmd=2");
                sendCmd("sendme");
                continue;
            }
            const char* err = errorText(code);
            if (err) {
                lastError = code;
                Serial.printf("[ERROR 0x%02X] %s. Ultimo comando enviado: %s\n", code, err, lastCmd);
            }
        }
    }
}

// ========================================
// VERIFICACION DE OBJETOS ('v')
// ========================================

// Envía un comando, espera la respuesta y dice si la pantalla lo aceptó
static bool check(const char* cmd, const char* note) {
    lastError = 0;
    sendCmd(cmd);
    uint32_t t0 = millis();
    while (millis() - t0 < 120) {
        listen();
    }
    const char* err = lastError ? errorText(lastError) : nullptr;
    Serial.printf("  %-4s %-34s %s%s%s\n", lastError ? "FALLA" : "ok", cmd, note,
                  err ? " -> " : "", err ? err : "");
    return lastError == 0;
}

static void verifyPage() {
    bool waves = sim.waves;
    sim.waves = false;                  // Sin ondas, cada error corresponde a su comando
    delay(100);
    listen();
    Serial.printf("\n=== Verificacion de la pagina %u (%s) ===\n", sim.page, pageName(sim.page));
    uint8_t fails = 0;

    // Barra de estado: en todas las páginas
    fails += !check("tPause.txt=\"\"", "(firmware)");
    fails += !check("vis pPause,0", "(firmware; opcional fuera de main)");
    fails += !check("tClock.txt=\"12:00\"", "(pendiente en firmware)");
    fails += !check("tBle.pco=34237", "(pendiente en firmware)");

    if (sim.page == PageMain) {
        fails += !check("nHR.val=75", "(firmware)");
        fails += !check("nSpO2.val=98", "(firmware)");
        fails += !check("nPR.val=75", "(firmware)");
        fails += !check("nT.val=368", "(firmware; debe ser Xfloat vvs1=1)");
        fails += !check("nSistolica.val=0", "(firmware)");
        fails += !check("nDiastolica.val=0", "(firmware)");
        fails += !check("nCalEst.val=0", "(firmware)");
        fails += !check("jCal.val=0", "(firmware)");
        fails += !check("add 1,0,128", "(firmware: Waveform ECG id 1)");
        fails += !check("add 2,0,128", "(firmware: Waveform Pleth id 2)");
        fails += !check("tAlarm.txt=\"Sin alarmas\"", "(pendiente en firmware)");
        fails += !check("rHr.bco=24148", "(pendiente en firmware)");
        fails += !check("rSp.bco=34237", "(pendiente en firmware)");
        fails += !check("rT.bco=65070", "(pendiente en firmware)");
        check("nHRV.val=0", "(opcional)");
        check("nRR.val=0", "(opcional)");
    } else if (sim.page == PageFullWf) {
        fails += !check("add 1,0,128", "(firmware: Waveform ECG id 1)");
        fails += !check("add 2,0,128", "(firmware: Waveform Pleth id 2)");
    } else if (sim.page == PageCfgBle) {
        fails += !check("tBleState.txt=\"Conectado\"", "(pendiente en firmware)");
        fails += !check("rBleDot.bco=34237", "(pendiente en firmware)");
    }

    Serial.printf("=== %u falla(s). Las 'opcional' no cuentan ===\n\n", fails);
    sim.waves = waves;
    refreshPage();                      // Deja la página como estaba
}

// ========================================
// MENU DEL MONITOR SERIE
// ========================================

static void help() {
    Serial.println(
        "\n=== Probador Nextion ===\n"
        "  h  ayuda                     v  verificar objetos de la pagina actual\n"
        "  1  normal (75 lpm, 98 %, 36.8 C)\n"
        "  2  alarma FC alta (grave)    3  alarma SpO2 baja (grave)\n"
        "  4  alarma temperatura (media)\n"
        "  0  sin datos (sensores sueltos: todo en 0)\n"
        "  p  pausa de audio (como el boton)   c  calibracion simulada 120/80\n"
        "  b  estado BLE: apagado -> esperando -> conectado\n"
        "  w  ondas on/off              s  estado actual\n"
        "En la pantalla: navega por las paginas y pulsa los botones; aqui se ve lo que envian.\n");
}

static void setScenario(char key) {
    Alarm before = sim.alarm;
    switch (key) {
        case '1': sim.hr = 75;  sim.spo2 = 98; sim.pr = 75;  sim.temp = 36.8f; sim.alarm = AlarmNone;     break;
        case '2': sim.hr = 135; sim.spo2 = 97; sim.pr = 135; sim.temp = 36.9f; sim.alarm = AlarmHrHigh;   break;
        case '3': sim.hr = 88;  sim.spo2 = 86; sim.pr = 88;  sim.temp = 36.8f; sim.alarm = AlarmSpo2Low;  break;
        case '4': sim.hr = 95;  sim.spo2 = 97; sim.pr = 95;  sim.temp = 38.6f; sim.alarm = AlarmTempHigh; break;
        case '0': sim.hr = 0;   sim.spo2 = 0;  sim.pr = 0;   sim.temp = 0.0f;  sim.alarm = AlarmNone;     break;
    }
    if (sim.calState == 2) {
        sim.sys = sim.hr ? sim.calSys : 0;   // Sin latidos no hay presión
        sim.dia = sim.hr ? sim.calDia : 0;
    }
    if (sim.pauseLeft > 0 && sim.alarm != AlarmNone && sim.alarm != before && sim.alarm != sim.pauseAlarm) {
        sim.pauseLeft = 0;                    // Alarma nueva: corta la pausa (como AudioPause)
        Serial.println("  -> alarma nueva: la pausa se corta");
    }
    Serial.printf("[simulador] escenario %c: %s\n", key, alarmText(sim.alarm));
    refreshPage();
}

static void printState() {
    Serial.printf("pagina=%u (%s) FC=%u SpO2=%u PR=%u T=%.1f PA=%u/%u alarma=%s\n",
                  sim.page, pageName(sim.page), sim.hr, sim.spo2, sim.pr, sim.temp, sim.sys, sim.dia,
                  alarmText(sim.alarm));
    Serial.printf("cal=%u (%u %%) pausa=%u s BLE=%u ondas=%s full_wf=%u baud=%lu ondas/s=%lu\n",
                  sim.calState, sim.calProgress, sim.pauseLeft, sim.ble, sim.waves ? "si" : "no",
                  sim.fullWfId, (unsigned long)NextionBaud, (unsigned long)WaveRateHz);
}

static void handleKey(char key) {
    switch (key) {
        case 'h': help(); break;
        case 'v': verifyPage(); break;
        case '0': case '1': case '2': case '3': case '4': setScenario(key); break;
        case 'p': startPause(); break;
        case 'c': startCalibration(120, 80); break;
        case 'b':
            sim.ble = (BleState)((sim.ble + 1) % 3);
            Serial.printf("[simulador] BLE = %u\n", sim.ble);
            showBleBar();
            showBlePage();
            break;
        case 'w': sim.waves = !sim.waves; Serial.printf("[simulador] ondas %s\n", sim.waves ? "on" : "off"); break;
        case 's': printState(); break;
        default: break;
    }
}

// ========================================
// TAREAS DE 1 SEGUNDO
// ========================================

static void everySecond() {
    static uint32_t last = 0;
    if (millis() - last < 1000) {
        return;
    }
    last += 1000;

    if (sim.pauseLeft > 0) {
        sim.pauseLeft--;
        if (sim.pauseLeft == 0) {
            Serial.println("[simulador] fin de la pausa: el audio vuelve");
        }
        showPause();
    }

    if (sim.calState == 1) {
        sim.calProgress += 10;
        if (sim.calProgress >= 100) {
            sim.calProgress = 100;
            sim.calState = 2;
            sim.sys = sim.hr ? sim.calSys : 0;
            sim.dia = sim.hr ? sim.calDia : 0;
            Serial.printf("[simulador] calibrado: PA %u/%u\n", sim.sys, sim.dia);
        }
        showMainNumbers();
    }

    static uint32_t lastMinute = 0;
    if (millis() / 60000UL != lastMinute) {
        lastMinute = millis() / 60000UL;
        showClock();
    }
}

// ========================================
// ARDUINO
// ========================================

void setup() {
    Serial.begin(115200);
    Serial2.begin(NextionBaud, SERIAL_8N1, RxPin, TxPin);
    myNex.begin(NextionBaud);
    delay(500);

    sendCmd("bkcmd=2");                 // La pantalla avisa cada comando que falla
    sendCmd("sendme");                  // Pregunta en qué página está
    help();
}

void loop() {
    while (Serial.available() > 0) {
        handleKey((char)Serial.read());
    }
    listen();
    sendWaves();
    everySecond();
}
