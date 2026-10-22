#include <Arduino.h>
#include <unity.h>
#include "biosignals.h"

// DC iguales en ambos canales: R = AC_RED / AC_IR, así los casos se leen fácil
static const uint32_t DC = 100000;

void setUp() {}
void tearDown() {}

void test_valor_tipico() {
    // R = 0.5 -> 110 - 25*0.5 = 97.5
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 97.5f, spo2_from_params(2, 1000, DC, 500, DC));
}

void test_dc_distintos() {
    // R = (600/50000) / (1000/100000) = 1.2 -> 80
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 80.0f, spo2_from_params(2, 1000, 100000, 600, 50000));
}

void test_limite_inferior_incluido() {
    // R = 1.2 -> exactamente 80: todavía es válido
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 80.0f, spo2_from_params(2, 1000, DC, 1200, DC));
}

void test_bajo_80_se_descarta() {
    // R = 1.3 -> 77.5: se toma como señal mala
    TEST_ASSERT_EQUAL_FLOAT(0.0f, spo2_from_params(2, 1000, DC, 1300, DC));
}

void test_sobre_100_se_acota() {
    // R = 0.2 -> 105: se acota a 100
    TEST_ASSERT_EQUAL_FLOAT(100.0f, spo2_from_params(2, 1000, DC, 200, DC));
}

void test_estado_no_valido() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, spo2_from_params(0, 1000, DC, 500, DC));   // sin señal
    TEST_ASSERT_EQUAL_FLOAT(0.0f, spo2_from_params(1, 1000, DC, 500, DC));   // estabilizando
}

void test_ac_o_dc_en_cero() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, spo2_from_params(2, 0,    DC, 500, DC));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, spo2_from_params(2, 1000, 0,  500, DC));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, spo2_from_params(2, 1000, DC, 0,   DC));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, spo2_from_params(2, 1000, DC, 500, 0));
}

void test_valores_maximos_24_bits() {
    // Todo en 0xFFFFFF: R = 1 -> 85, sin desborde en los productos
    const uint32_t M = 0xFFFFFF;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 85.0f, spo2_from_params(2, M, M, M, M));
}

void setup() {
    delay(2000);                                       // Da tiempo a abrir el puerto serie
    UNITY_BEGIN();
    RUN_TEST(test_valor_tipico);
    RUN_TEST(test_dc_distintos);
    RUN_TEST(test_limite_inferior_incluido);
    RUN_TEST(test_bajo_80_se_descarta);
    RUN_TEST(test_sobre_100_se_acota);
    RUN_TEST(test_estado_no_valido);
    RUN_TEST(test_ac_o_dc_en_cero);
    RUN_TEST(test_valores_maximos_24_bits);
    UNITY_END();
}

void loop() {}
