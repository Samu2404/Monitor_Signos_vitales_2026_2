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
    TEST_ASSERT_EQUAL_HEX8(0x1F, Settings().alarmEnable);
}

void test_fabrica_igual_a_limites_adulto() {
    Settings s;
    s.hrHigh = 200;
    settingsDefaultLimits(s, PatientType::Adult);
    Settings fabrica;
    TEST_ASSERT_EQUAL_MEMORY(&fabrica, &s, sizeof(Settings));
}

void test_limites_pediatrico() {
    Settings s;
    settingsDefaultLimits(s, PatientType::Pediatric);
    TEST_ASSERT_EQUAL_UINT16(75,  s.hrLow);
    TEST_ASSERT_EQUAL_UINT16(160, s.hrHigh);
    TEST_ASSERT_EQUAL_UINT16(70,  s.sysLow);
    TEST_ASSERT_EQUAL_UINT16(70,  s.diaHigh);
    TEST_ASSERT_TRUE(settingsValid(s));
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
    // sys1 = btHr + btSp*2 + btT*4 + btNoSig*8 + btPA*16: FC y temperatura encendidas
    TEST_ASSERT_TRUE(settingsApply(s, SettingId::AlarmEnable, 0x05));
    TEST_ASSERT_EQUAL_HEX8(0x05, s.alarmEnable);
    TEST_ASSERT_TRUE(settingsApply(s, SettingId::AlarmEnable, 0x10));   // Solo PA
    TEST_ASSERT_FALSE(settingsApply(s, SettingId::AlarmEnable, 0x20));
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
    TEST_ASSERT_FALSE(settingsApply(s, SettingId::NewPatient, 0));
    TEST_ASSERT_FALSE(settingsApply(s, (SettingId)0x06, 1));   // Id libre
}

void test_tipo_y_cama_fuera_de_rango() {
    Settings s;
    TEST_ASSERT_FALSE(settingsApply(s, SettingId::PatientType, 2));   // Sin neonatal
    TEST_ASSERT_FALSE(settingsApply(s, SettingId::Bed, 0));
    TEST_ASSERT_FALSE(settingsApply(s, SettingId::Bed, 100));
    TEST_ASSERT_EQUAL_UINT8(1, s.bed);
}

// ========================================
// PACIENTE (pat_config)
// ========================================

void test_nuevo_paciente_borra_id_y_conserva_cama() {
    Settings s;
    settingsApply(s, SettingId::PatientType, 1);
    settingsApply(s, SettingId::Bed, 7);
    settingsApply(s, SettingId::HrHigh, 200);
    settingsSetPatientId(s, "ABC123", 6);
    settingsNewPatient(s, 1800000000);
    TEST_ASSERT_EQUAL_STRING("", s.patientId);
    TEST_ASSERT_EQUAL_UINT8(7, s.bed);
    TEST_ASSERT_EQUAL_UINT16(160, s.hrHigh);                    // Límite pediátrico
    TEST_ASSERT_EQUAL_UINT32(1800000000, s.admittedAt);
}

void test_id_se_recorta_a_12() {
    Settings s;
    TEST_ASSERT_TRUE(settingsSetPatientId(s, "1234567890ABCDEF", 16));
    TEST_ASSERT_EQUAL_STRING("1234567890AB", s.patientId);
    TEST_ASSERT_TRUE(settingsValid(s));
}

void test_id_rechaza_no_imprimibles() {
    Settings s;
    settingsSetPatientId(s, "OK", 2);
    TEST_ASSERT_FALSE(settingsSetPatientId(s, "A\xFF" "B", 3));
    TEST_ASSERT_EQUAL_STRING("OK", s.patientId);
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

void test_pa_incoherente_no_es_valida() {
    Settings s;
    settingsApply(s, SettingId::DiaLow, 95);                    // diaHigh = 90
    TEST_ASSERT_FALSE(settingsValid(s));
}

void setup() {
    delay(2000);                                       // Da tiempo a abrir el puerto serie
    UNITY_BEGIN();
    RUN_TEST(test_fabrica_es_valida);
    RUN_TEST(test_fabrica_todas_las_alarmas_encendidas);
    RUN_TEST(test_fabrica_igual_a_limites_adulto);
    RUN_TEST(test_limites_pediatrico);
    RUN_TEST(test_cambia_umbral_fc);
    RUN_TEST(test_temperatura_en_decimas_como_el_xfloat);
    RUN_TEST(test_mascara_de_alarmas);
    RUN_TEST(test_ble_cualquier_valor_no_cero_es_encendido);
    RUN_TEST(test_volumen_no_baja_del_minimo);
    RUN_TEST(test_temperatura_fuera_de_rango);
    RUN_TEST(test_spo2_fuera_de_rango);
    RUN_TEST(test_acciones_no_son_valores);
    RUN_TEST(test_tipo_y_cama_fuera_de_rango);
    RUN_TEST(test_nuevo_paciente_borra_id_y_conserva_cama);
    RUN_TEST(test_id_se_recorta_a_12);
    RUN_TEST(test_id_rechaza_no_imprimibles);
    RUN_TEST(test_lote_que_sube_ambos_limites_de_fc);
    RUN_TEST(test_lote_incoherente_no_es_valido);
    RUN_TEST(test_pa_incoherente_no_es_valida);
    UNITY_END();
}

void loop() {}
