#include <Arduino.h>
#include <unity.h>
#include "Settings.h"

void setUp() {}
void tearDown() {}

// ========================================
// VALORES DE FABRICA
// ========================================

void test_fabrica_es_valida() {
    TEST_ASSERT_TRUE(settingsValid(Settings()));
}

void test_fabrica_todas_las_alarmas_encendidas() {
    TEST_ASSERT_EQUAL_HEX8(0x0F, Settings().alarmEnable);
}

// ========================================
// CAMBIOS VALIDOS (ids y valores como los manda la pantalla)
// ========================================

void test_cambia_umbral_fc() {
    Settings s;
    TEST_ASSERT_TRUE(settingsApply(s, SettingId::HrHigh, 140));
    TEST_ASSERT_EQUAL_UINT16(140, s.hrHigh);
}

void test_temperatura_en_decimas_como_el_xfloat() {
    Settings s;
    TEST_ASSERT_TRUE(settingsApply(s, SettingId::TempHigh, 390));   // xTHi.val = 390 -> 39,0 °C
    TEST_ASSERT_EQUAL_UINT16(390, s.tempHigh);
}

void test_mascara_de_alarmas() {
    Settings s;
    // sys1 = btHr + btSp*2 + btT*4 + btNoSig*8: FC y temperatura encendidas
    TEST_ASSERT_TRUE(settingsApply(s, SettingId::AlarmEnable, 0x05));
    TEST_ASSERT_EQUAL_HEX8(0x05, s.alarmEnable);
    TEST_ASSERT_FALSE(settingsApply(s, SettingId::AlarmEnable, 0x10));
}

void test_ble_cualquier_valor_no_cero_es_encendido() {
    Settings s;
    TEST_ASSERT_TRUE(settingsApply(s, SettingId::Ble, 0));
    TEST_ASSERT_FALSE(s.bleEnabled);
    TEST_ASSERT_TRUE(settingsApply(s, SettingId::Ble, 7));
    TEST_ASSERT_TRUE(s.bleEnabled);
}

// ========================================
// RANGOS (fuera de rango: s no cambia)
// ========================================

void test_volumen_no_baja_del_minimo() {
    Settings s;
    TEST_ASSERT_FALSE(settingsApply(s, SettingId::Volume, SettingsLimits::VolumeMin - 1));
    TEST_ASSERT_EQUAL_UINT8(100, s.volume);
    TEST_ASSERT_TRUE(settingsApply(s, SettingId::Volume, SettingsLimits::VolumeMin));
}

void test_temperatura_fuera_de_rango() {
    Settings s;
    TEST_ASSERT_FALSE(settingsApply(s, SettingId::TempHigh, 421));   // 42,1 °C
    TEST_ASSERT_FALSE(settingsApply(s, SettingId::TempLow, 299));
    TEST_ASSERT_EQUAL_UINT16(350, s.tempLow);
}

void test_spo2_fuera_de_rango() {
    Settings s;
    TEST_ASSERT_FALSE(settingsApply(s, SettingId::Spo2Low, 100));
    TEST_ASSERT_FALSE(settingsApply(s, SettingId::Spo2Low, 49));
    TEST_ASSERT_EQUAL_UINT16(90, s.spo2Low);
}

void test_acciones_no_son_valores() {
    Settings s;
    TEST_ASSERT_FALSE(settingsApply(s, SettingId::PauseAudio, 0));
    TEST_ASSERT_FALSE(settingsApply(s, SettingId::Acknowledge, 0));
    TEST_ASSERT_FALSE(settingsApply(s, (SettingId)0x06, 1));   // Id libre
}

// ========================================
// LOTE DEL BOTON GUARDAR (coherencia al final)
// ========================================

void test_lote_que_sube_ambos_limites_de_fc() {
    // Orden de la pantalla: primero nHrLo, después nHrHi. Subir los dos por encima del alto
    // actual deja un instante bajo >= alto, pero el lote completo es válido
    Settings s;                                                 // 50 / 120
    TEST_ASSERT_TRUE(settingsApply(s, SettingId::HrLow, 130));
    TEST_ASSERT_FALSE(settingsValid(s));                        // A mitad del lote
    TEST_ASSERT_TRUE(settingsApply(s, SettingId::HrHigh, 160));
    TEST_ASSERT_TRUE(settingsValid(s));                         // Al terminar
}

void test_lote_incoherente_no_es_valido() {
    Settings s;
    settingsApply(s, SettingId::TempLow, 380);
    settingsApply(s, SettingId::TempHigh, 370);
    TEST_ASSERT_FALSE(settingsValid(s));                        // SettingsStore lo revierte
}

void setup() {
    delay(2000);                                       // Da tiempo a abrir el puerto serie
    UNITY_BEGIN();
    RUN_TEST(test_fabrica_es_valida);
    RUN_TEST(test_fabrica_todas_las_alarmas_encendidas);
    RUN_TEST(test_cambia_umbral_fc);
    RUN_TEST(test_temperatura_en_decimas_como_el_xfloat);
    RUN_TEST(test_mascara_de_alarmas);
    RUN_TEST(test_ble_cualquier_valor_no_cero_es_encendido);
    RUN_TEST(test_volumen_no_baja_del_minimo);
    RUN_TEST(test_temperatura_fuera_de_rango);
    RUN_TEST(test_spo2_fuera_de_rango);
    RUN_TEST(test_acciones_no_son_valores);
    RUN_TEST(test_lote_que_sube_ambos_limites_de_fc);
    RUN_TEST(test_lote_incoherente_no_es_valido);
    UNITY_END();
}

void loop() {}
