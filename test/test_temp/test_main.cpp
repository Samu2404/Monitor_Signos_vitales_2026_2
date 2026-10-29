#include <Arduino.h>
#include <unity.h>
#include "biosignals.h"

// Salida del amplificador [mV] para una resistencia del NTC (el circuito al derecho)
static float mvParaR(float rNtc) {
    float va = TEMP_BRIDGE_VEXC_MV * TEMP_BRIDGE_R2_OHM / (TEMP_BRIDGE_R1_OHM + TEMP_BRIDGE_R2_OHM);
    float vb = TEMP_BRIDGE_VEXC_MV * rNtc / (TEMP_BRIDGE_R3_OHM + rNtc);
    return TEMP_AMP_GAIN * (va - vb) + TEMP_AMP_VREF_MV;
}

void setUp() {}
void tearDown() {}

void test_r25_da_25_grados() {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, ysi400_temp_from_r(YSI400_R25_OHM));
}

void test_tabla_37_grados() {
    // Tabla YSI 400: 1355 ohm a 37 °C
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 37.0f, ysi400_temp_from_r(1355.0f));
}

void test_puente_recupera_r() {
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 1355.0f, ysi400_r_from_mv(mvParaR(1355.0f)));
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 2000.0f, ysi400_r_from_mv(mvParaR(2000.0f)));
}

void test_mv_de_37_grados() {
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 37.0f, ysi400_temp_from_mv(mvParaR(1355.0f)));
}

void test_fuera_de_rango() {
    // 5000 ohm ≈ 7.6 °C (< TEMP_MIN_C)
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ysi400_temp_from_mv(mvParaR(5000.0f)));
    // 500 ohm ≈ 66 °C (> TEMP_MAX_C)
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ysi400_temp_from_mv(mvParaR(500.0f)));
}

void test_adc_saturado() {
    // Sonda suelta o en corto: el amplificador se va a un riel
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ysi400_temp_from_mv(0.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ysi400_temp_from_mv(3300.0f));
}

void setup() {
    delay(2000);                                       // Da tiempo a abrir el puerto serie
    UNITY_BEGIN();
    RUN_TEST(test_r25_da_25_grados);
    RUN_TEST(test_tabla_37_grados);
    RUN_TEST(test_puente_recupera_r);
    RUN_TEST(test_mv_de_37_grados);
    RUN_TEST(test_fuera_de_rango);
    RUN_TEST(test_adc_saturado);
    UNITY_END();
}

void loop() {}
