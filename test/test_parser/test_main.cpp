#include <Arduino.h>
#include <unity.h>
#include "afe4950_parser.h"

// Alimenta el parser byte a byte; guarda hasta 'max' tramas y devuelve cuántas salieron
static int alimentar(const uint8_t* bytes, size_t len, AfeParser::Frame* out, int max) {
    AfeParser p;
    AfeParser::Frame f;
    int n = 0;
    for (size_t i = 0; i < len; i++) {
        if (p.push(bytes[i], f) && n < max) out[n++] = f;
    }
    return n;
}

static const uint8_t PAQUETE[] = {
    0x00,0x00,0x00,0x02,  0x05,0x00,0x00,0x03,               // cabecera, pointerDiff = 5
    0x01,0x00,0x00,0x05,  0xFF,0xFF,0xFF,0x05,  0x10,0x00,0x00,0x05,
    0x00,0x00,0x80,0x05,  0xFF,0xFF,0x7F,0x05,  0x00,0x10,0x00,0x05,
};

// Muestras ECG=1, ECG=2, PPG=3 en medio de las cuales llega el bloque de oximetría
static const uint8_t PAQUETE_SPO2[] = {
    0x00,0x00,0x00,0x02,  0x05,0x00,0x00,0x03,               // cabecera, pointerDiff
    0x01,0x00,0x00,0x05,  0x02,0x00,0x00,0x05,               // ECG, ECG (falta el PPG)
    0x02,0x05,0x00,0x06,                                     // ESTADO = 2, 5 s restantes
    0x10,0x27,0x00,0x07,                                     // AC_IR  = 10000
    0x40,0x42,0x0F,0x08,                                     // DC_IR  = 1000000
    0xFF,0xFF,0xFF,0x09,                                     // AC_RED = 16777215 (sin signo)
    0xA0,0x86,0x01,0x0A,                                     // DC_RED = 100000
    0x03,0x00,0x00,0x05,                                     // PPG: completa la trama
};

// Alimenta un parser ya creado, para poder consultar takeSpO2() después
static int alimentarEn(AfeParser& p, const uint8_t* bytes, size_t len, AfeParser::Frame* out, int max) {
    AfeParser::Frame f;
    int n = 0;
    for (size_t i = 0; i < len; i++) {
        if (p.push(bytes[i], f) && n < max) out[n++] = f;
    }
    return n;
}

void setUp() {}       // Unity los llama antes y después de cada prueba
void tearDown() {}

void test_paquete_completo() {
    AfeParser::Frame f[4];
    TEST_ASSERT_EQUAL_INT(2, alimentar(PAQUETE, sizeof(PAQUETE), f, 4));
    TEST_ASSERT_EQUAL_INT32(1, f[0].ecg[0]);
    TEST_ASSERT_EQUAL_INT32(-1, f[0].ecg[1]);
    TEST_ASSERT_EQUAL_INT32(16, f[0].ppg);
    TEST_ASSERT_EQUAL_UINT32(0, f[0].index);
    TEST_ASSERT_EQUAL_INT32(-8388608, f[1].ecg[0]);   // mínimo de 24 bits
    TEST_ASSERT_EQUAL_INT32(8388607, f[1].ecg[1]);    // máximo de 24 bits
    TEST_ASSERT_EQUAL_UINT32(1, f[1].index);
}

void test_resincroniza_tras_basura() {
    uint8_t buf[sizeof(PAQUETE) + 1];
    buf[0] = 0xAA;                                     // byte basura al inicio
    memcpy(buf + 1, PAQUETE, sizeof(PAQUETE));
    AfeParser::Frame f[4];
    TEST_ASSERT_EQUAL_INT(2, alimentar(buf, sizeof(buf), f, 4));
    TEST_ASSERT_EQUAL_INT32(1, f[0].ecg[0]);
}

void test_spo2_bloque_completo() {
    AfeParser p;
    AfeParser::Frame f[2];
    AfeParser::SpO2Params sp;
    TEST_ASSERT_EQUAL_INT(1, alimentarEn(p, PAQUETE_SPO2, sizeof(PAQUETE_SPO2), f, 2));
    TEST_ASSERT_EQUAL_INT32(1, f[0].ecg[0]);           // El bloque no movió la fase
    TEST_ASSERT_EQUAL_INT32(2, f[0].ecg[1]);
    TEST_ASSERT_EQUAL_INT32(3, f[0].ppg);

    TEST_ASSERT_TRUE(p.takeSpO2(sp));
    TEST_ASSERT_EQUAL_UINT8(2, sp.state);
    TEST_ASSERT_EQUAL_UINT8(5, sp.secRemaining);
    TEST_ASSERT_EQUAL_UINT32(10000, sp.acIR);
    TEST_ASSERT_EQUAL_UINT32(1000000, sp.dcIR);
    TEST_ASSERT_EQUAL_UINT32(16777215, sp.acRed);      // No se interpreta con signo
    TEST_ASSERT_EQUAL_UINT32(100000, sp.dcRed);
    TEST_ASSERT_FALSE(p.takeSpO2(sp));                 // Se entrega una sola vez
}

void test_spo2_incompleto_se_descarta() {
    static const uint8_t BYTES[] = {
        0x02,0x00,0x00,0x06,  0x10,0x27,0x00,0x07,  0x40,0x42,0x0F,0x08,   // ESTADO, AC_IR, DC_IR
        0x01,0x00,0x00,0x05,                                               // muestra en medio
        0xFF,0xFF,0xFF,0x09,  0xA0,0x86,0x01,0x0A,                         // AC_RED, DC_RED
    };
    AfeParser p;
    AfeParser::Frame f[1];
    AfeParser::SpO2Params sp;
    alimentarEn(p, BYTES, sizeof(BYTES), f, 1);
    TEST_ASSERT_FALSE(p.takeSpO2(sp));
}

void test_spo2_sin_estado_se_descarta() {
    static const uint8_t BYTES[] = {                   // Falta el TAG 0x06
        0x10,0x27,0x00,0x07,  0x40,0x42,0x0F,0x08,  0xFF,0xFF,0xFF,0x09,  0xA0,0x86,0x01,0x0A,
    };
    AfeParser p;
    AfeParser::Frame f[1];
    AfeParser::SpO2Params sp;
    alimentarEn(p, BYTES, sizeof(BYTES), f, 1);
    TEST_ASSERT_FALSE(p.takeSpO2(sp));
}

void test_spo2_resincroniza_tras_basura() {
    uint8_t buf[sizeof(PAQUETE_SPO2) + 2];
    buf[0] = 0xAA;                                     // Dos bytes basura al inicio
    buf[1] = 0xBB;
    memcpy(buf + 2, PAQUETE_SPO2, sizeof(PAQUETE_SPO2));
    AfeParser p;
    AfeParser::Frame f[2];
    AfeParser::SpO2Params sp;
    TEST_ASSERT_EQUAL_INT(1, alimentarEn(p, buf, sizeof(buf), f, 2));
    TEST_ASSERT_EQUAL_INT32(3, f[0].ppg);
    TEST_ASSERT_TRUE(p.takeSpO2(sp));
    TEST_ASSERT_EQUAL_UINT32(100000, sp.dcRed);
}


void setup() {
    delay(2000);                                       // Da tiempo a abrir el puerto serie
    UNITY_BEGIN();
    RUN_TEST(test_paquete_completo);
    RUN_TEST(test_resincroniza_tras_basura);
    RUN_TEST(test_spo2_bloque_completo);
    RUN_TEST(test_spo2_incompleto_se_descarta);
    RUN_TEST(test_spo2_sin_estado_se_descarta);
    RUN_TEST(test_spo2_resincroniza_tras_basura);
    UNITY_END();
}

void loop() {}