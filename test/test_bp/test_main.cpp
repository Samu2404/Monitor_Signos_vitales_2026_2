#include <Arduino.h>
#include <unity.h>
#include "biosignals.h"

static BpEstimator bp;                                 // Global: ~12 KB, no en la pila

void setUp() {
    bp_init(&bp, 500.0f, 250.0f);
    bp.sbp0 = 120; bp.dbp0 = 80; bp.ptt0 = 250; bp.pir0 = 1.02f;   // Calibración simulada
}
void tearDown() {}

void test_modelo_devuelve_la_calibracion() {
    float s, d;
    bp_model(&bp, 250.0f, 1.02f, &s, &d);             // PTT = PTT0 y PIR = PIR0
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, s);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 80.0f, d);
}

void test_ptt_corto_sube_la_sistolica() {
    float s, d;
    bp_model(&bp, 230.0f, 1.02f, &s, &d);             // PTT 20 ms más corto
    TEST_ASSERT_TRUE(s > 120.0f);
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_modelo_devuelve_la_calibracion);
    RUN_TEST(test_ptt_corto_sube_la_sistolica);
    UNITY_END();
}

void loop() {}

