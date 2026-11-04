#include <Arduino.h>
#include <unity.h>
#include "AlarmRule.h"
#include "AlarmManager.h"
#include "AlarmBuzzer.h"
#include "AlarmLeds.h"
#include "AudioPause.h"
#include "AlarmAck.h"

// Buzzer y LEDs en la placa: mismos pines que main.cpp
static const uint8_t PinBuzzer     = 13;
static const bool    BuzzerPassive = true;
static AlarmBuzzer buzzer(PinBuzzer, BuzzerPassive);
static AlarmLeds   leds(27, 25, 33, 32);      // Verde, azul, amarillo, rojo

void setUp() {}
void tearDown() {}

// ========================================
// ALARM RULE (persistencia e histéresis)
// ========================================

void test_regla_activa_tras_persistencia() {
    AlarmRule r;
    for (int i = 0; i < 9; i++) {
        TEST_ASSERT_FALSE(r.update(true, false, 10, 5));   // 9 s: todavía no
    }
    TEST_ASSERT_TRUE(r.update(true, false, 10, 5));        // 10 s: se activa
}

void test_regla_disparo_interrumpido_reinicia() {
    AlarmRule r;
    for (int i = 0; i < 9; i++) {
        r.update(true, false, 10, 5);
    }
    r.update(false, false, 10, 5);                          // Un segundo normal: vuelve a 0
    for (int i = 0; i < 9; i++) {
        TEST_ASSERT_FALSE(r.update(true, false, 10, 5));
    }
    TEST_ASSERT_TRUE(r.update(true, false, 10, 5));
}

void test_regla_se_apaga_tras_persistencia() {
    AlarmRule r;
    for (int i = 0; i < 10; i++) {
        r.update(true, false, 10, 5);
    }
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE(r.update(false, true, 10, 5));    // 4 s liberada: sigue activa
    }
    TEST_ASSERT_FALSE(r.update(false, true, 10, 5));       // 5 s: se apaga
}

void test_regla_zona_histeresis_mantiene() {
    AlarmRule r;
    for (int i = 0; i < 10; i++) {
        r.update(true, false, 10, 5);
    }
    for (int i = 0; i < 20; i++) {
        TEST_ASSERT_TRUE(r.update(false, false, 10, 5));   // Ni disparo ni liberación: sigue igual
    }
}

// ========================================
// ALARM MANAGER
// ========================================

void test_manager_texto_y_cambios() {
    AlarmManager m;
    TEST_ASSERT_EQUAL_STRING("00000000", m.getAlarmString());
    TEST_ASSERT_FALSE(m.hasActiveAlarms());

    TEST_ASSERT_TRUE(m.setAlarm(AlarmType::SPO2_LOW, true));
    TEST_ASSERT_EQUAL_STRING("00100000", m.getAlarmString());
    TEST_ASSERT_TRUE(m.hasChanged());
    TEST_ASSERT_TRUE(m.hasActiveAlarms());

    m.clearChanged();
    TEST_ASSERT_FALSE(m.setAlarm(AlarmType::SPO2_LOW, true));   // Mismo estado: no es cambio
    TEST_ASSERT_FALSE(m.hasChanged());
}

void test_manager_reservada_no_se_escribe() {
    AlarmManager m;
    TEST_ASSERT_FALSE(m.setAlarm(AlarmType::RESERVED, true));
    TEST_ASSERT_EQUAL_STRING("00000000", m.getAlarmString());
}

void test_prioridad_sin_alarmas() {
    AlarmManager m;
    TEST_ASSERT_EQUAL(AlarmPriority::NONE, m.highestPriority());
}

void test_prioridad_media_temperatura() {
    AlarmManager m;
    m.setAlarm(AlarmType::TEMP_HIGH, true);
    TEST_ASSERT_EQUAL(AlarmPriority::MEDIUM, m.highestPriority());
}

void test_prioridad_critica_gana() {
    AlarmManager m;
    m.setAlarm(AlarmType::TEMP_LOW, true);
    m.setAlarm(AlarmType::HR_HIGH, true);
    TEST_ASSERT_EQUAL(AlarmPriority::CRITICAL, m.highestPriority());

    m.setAlarm(AlarmType::HR_HIGH, false);                      // Queda solo la de temperatura
    TEST_ASSERT_EQUAL(AlarmPriority::MEDIUM, m.highestPriority());
}

void test_texto_ble_sigue_con_8_posiciones() {
    // Las posiciones 5 y 6 quedaron libres: siempre '0', la app BLE no cambia
    AlarmManager m;
    m.setAlarm(AlarmType::HR_HIGH, true);
    m.setAlarm(AlarmType::HR_LOW, true);
    m.setAlarm(AlarmType::SPO2_LOW, true);
    m.setAlarm(AlarmType::TEMP_HIGH, true);
    m.setAlarm(AlarmType::TEMP_LOW, true);
    TEST_ASSERT_EQUAL_STRING("11111000", m.getAlarmString());
}

// ========================================
// PRIORIDAD POR ALARMA, TECNICAS Y BARRA DE ALARMAS
// ========================================

void test_temperatura_leve_y_luego_media() {
    AlarmManager m;
    m.setAlarm(AlarmType::TEMP_HIGH, true, AlarmPriority::MINOR);
    TEST_ASSERT_EQUAL(AlarmPriority::MINOR, m.highestPriority());
    m.clearChanged();

    m.setAlarm(AlarmType::TEMP_HIGH, true, AlarmPriority::MEDIUM);   // Siguió subiendo
    TEST_ASSERT_EQUAL(AlarmPriority::MEDIUM, m.highestPriority());
    TEST_ASSERT_FALSE(m.hasChanged());                              // El texto BLE no cambió
}

void test_tecnica_no_va_al_texto_ble() {
    AlarmManager m;
    m.setTech(TechAlarm::BP_CAL_LOST, true);
    TEST_ASSERT_TRUE(m.isTechActive(TechAlarm::BP_CAL_LOST));
    TEST_ASSERT_EQUAL_STRING("00000000", m.getAlarmString());
    TEST_ASSERT_FALSE(m.hasChanged());
    TEST_ASSERT_EQUAL(AlarmPriority::MINOR, m.highestPriority());

    m.setTech(TechAlarm::BP_CAL_LOST, false);
    TEST_ASSERT_EQUAL(AlarmPriority::NONE, m.highestPriority());
}

void test_barra_sin_alarmas_vacia() {
    AlarmManager m;
    char bar[24] = "x";
    TEST_ASSERT_EQUAL(AlarmPriority::NONE, m.barText(bar, sizeof(bar)));
    TEST_ASSERT_EQUAL_STRING("", bar);
}

void test_barra_muestra_la_mas_grave_y_cuenta_las_demas() {
    AlarmManager m;
    char bar[24];
    m.setAlarm(AlarmType::TEMP_HIGH, true, AlarmPriority::MINOR);
    m.setTech(TechAlarm::BP_CAL_LOST, true);
    TEST_ASSERT_EQUAL(AlarmPriority::MINOR, m.barText(bar, sizeof(bar)));
    TEST_ASSERT_EQUAL_STRING("TEMP ALTA +1", bar);              // Empate: la primera

    m.setAlarm(AlarmType::SPO2_LOW, true);
    TEST_ASSERT_EQUAL(AlarmPriority::CRITICAL, m.barText(bar, sizeof(bar)));
    TEST_ASSERT_EQUAL_STRING("SpO2 BAJA +2", bar);
}

void test_escalon_de_temperatura_con_histeresis() {
    using namespace AlarmConfig;
    bool severe = false;
    severe = alarmSevere(severe, 0.5f, TEMP_SEVERE_C, TEMP_SEVERE_HYST_C);   // 38,5: leve
    TEST_ASSERT_FALSE(severe);
    severe = alarmSevere(severe, 1.1f, TEMP_SEVERE_C, TEMP_SEVERE_HYST_C);   // 39,1: media
    TEST_ASSERT_TRUE(severe);
    severe = alarmSevere(severe, 0.9f, TEMP_SEVERE_C, TEMP_SEVERE_HYST_C);   // 38,9: sigue media
    TEST_ASSERT_TRUE(severe);
    severe = alarmSevere(severe, 0.7f, TEMP_SEVERE_C, TEMP_SEVERE_HYST_C);   // 38,7: vuelve a leve
    TEST_ASSERT_FALSE(severe);
}

// ========================================
// AUDIO PAUSE (pausa del sonido de alarma)
// ========================================

void test_pausa_cuenta_y_termina() {
    AudioPause p;
    p.set("10000000", 120);                                 // 53 10 78 00
    TEST_ASSERT_TRUE(p.active());
    TEST_ASSERT_EQUAL_UINT16(AudioPause::DurationS, p.secondsLeft());
    for (uint16_t i = 1; i < AudioPause::DurationS; i++) {
        p.tick("10000000");
    }
    TEST_ASSERT_EQUAL_UINT16(1, p.secondsLeft());          // La misma alarma no corta la pausa
    p.tick("10000000");
    TEST_ASSERT_FALSE(p.active());                          // A los 2 min el sonido vuelve
}

void test_pausa_alarma_nueva_la_corta() {
    AudioPause p;
    p.set("10000000", 120);
    p.tick("00000000");                                     // Se va la alarma: la pausa sigue
    TEST_ASSERT_TRUE(p.active());
    p.tick("10000000");                                     // Vuelve la misma: sigue
    TEST_ASSERT_TRUE(p.active());
    p.tick("10100000");                                     // Aparece SpO2 baja: se corta
    TEST_ASSERT_FALSE(p.active());
}

void test_pausa_cero_la_cancela() {
    AudioPause p;
    p.set("00000000", 120);                                 // También sin alarmas (pre-silencio)
    TEST_ASSERT_TRUE(p.active());
    p.set("00000000", 0);                                   // Segundo toque: la pantalla manda 0
    TEST_ASSERT_FALSE(p.active());
    TEST_ASSERT_EQUAL_UINT16(0, p.secondsLeft());
}

void test_pausa_se_recorta_al_maximo() {
    AudioPause p;
    p.set("00000000", 600);                                 // Valor de más: no calla 10 min
    TEST_ASSERT_EQUAL_UINT16(AudioPause::DurationS, p.secondsLeft());
}

void test_pausa_repetida_es_idempotente() {
    // Estado explícito: dos 120 seguidos (mensaje duplicado) no la cancelan, como hacía el toggle
    AudioPause p;
    p.set("00000000", 120);
    p.set("00000000", 120);
    TEST_ASSERT_TRUE(p.active());
}

// ========================================
// RECONOCER (53 13)
// ========================================

void test_reconocer_calla_las_activas() {
    AlarmManager m;
    AlarmAck a;
    m.setAlarm(AlarmType::SPO2_LOW, true);
    a.acknowledge(m.getAlarmString());
    TEST_ASSERT_EQUAL(AlarmPriority::NONE, m.priorityExcept(a.acked()));      // Buzzer callado
    TEST_ASSERT_EQUAL(AlarmPriority::CRITICAL, m.highestPriority());         // LED y barra siguen
}

void test_reconocer_alarma_nueva_suena() {
    AlarmManager m;
    AlarmAck a;
    m.setAlarm(AlarmType::TEMP_HIGH, true, AlarmPriority::MINOR);
    a.acknowledge(m.getAlarmString());
    m.setAlarm(AlarmType::HR_HIGH, true);
    a.tick(m.getAlarmString());
    TEST_ASSERT_EQUAL(AlarmPriority::CRITICAL, m.priorityExcept(a.acked()));
}

void test_reconocida_que_vuelve_suena() {
    AlarmManager m;
    AlarmAck a;
    m.setAlarm(AlarmType::HR_LOW, true);
    a.acknowledge(m.getAlarmString());
    m.setAlarm(AlarmType::HR_LOW, false);                       // Se resolvió: se olvida
    a.tick(m.getAlarmString());
    m.setAlarm(AlarmType::HR_LOW, true);                        // Vuelve
    a.tick(m.getAlarmString());
    TEST_ASSERT_EQUAL(AlarmPriority::CRITICAL, m.priorityExcept(a.acked()));
}

void test_reconocer_no_calla_las_tecnicas() {
    AlarmManager m;
    AlarmAck a;
    m.setTech(TechAlarm::BP_CAL_LOST, true);
    a.acknowledge(m.getAlarmString());
    TEST_ASSERT_EQUAL(AlarmPriority::MINOR, m.priorityExcept(a.acked()));
}

// ========================================
// LEDS EN LA PLACA (se verifican a la vista)
// ========================================

void test_leds_secuencia() {
    // Mirar: verde, azul, amarillo, rojo (1,5 s cada uno), siempre uno solo encendido
    leds.begin();
    const AlarmPriority orden[] = {
        AlarmPriority::NONE, AlarmPriority::MINOR, AlarmPriority::MEDIUM, AlarmPriority::CRITICAL
    };
    for (AlarmPriority p : orden) {
        leds.show(p);
        delay(1500);
    }
    leds.show(AlarmPriority::NONE);                             // Termina en verde
}

// ========================================
// BUZZER EN LA PLACA (se verifica de oído)
// ========================================

void test_buzzer_arranca() {
    TEST_ASSERT_TRUE(buzzer.begin());
}

void test_buzzer_patron_leve() {
    // Escuchar: 1 pitido suave y nada más (leve no se repite). LED azul
    buzzer.setPriority(AlarmPriority::MINOR);
    leds.show(AlarmPriority::MINOR);
    TEST_ASSERT_EQUAL(AlarmPriority::MINOR, buzzer.priority());
    delay(2000);
}

void test_buzzer_patron_critico() {
    // Escuchar: 3 + 2 pitidos agudos rápidos, 2 veces. LED rojo
    buzzer.setPriority(AlarmPriority::CRITICAL);
    leds.show(AlarmPriority::CRITICAL);
    TEST_ASSERT_EQUAL(AlarmPriority::CRITICAL, buzzer.priority());
    delay(5000);
}

void test_buzzer_patron_medio() {
    // Escuchar: 3 pitidos graves lentos (el patrón dura ~6 s). LED amarillo
    buzzer.setPriority(AlarmPriority::MEDIUM);
    leds.show(AlarmPriority::MEDIUM);
    TEST_ASSERT_EQUAL(AlarmPriority::MEDIUM, buzzer.priority());
    delay(6000);
}

void test_buzzer_silencio() {
    // Escuchar: debe callarse de inmediato. LED verde
    buzzer.setPriority(AlarmPriority::NONE);
    leds.show(AlarmPriority::NONE);
    TEST_ASSERT_EQUAL(AlarmPriority::NONE, buzzer.priority());
    delay(2000);
}

void test_buzzer_cucaracha() {
    // Escuchar: "La Cucaracha" completa (~7 s)
    TEST_ASSERT_TRUE(buzzer.playEasterEgg());
    delay(7500);
}

void test_cucaracha_bloqueada_con_alarma() {
    // Con una alarma sonando no se acepta, y una alarma corta la melodía
    buzzer.setPriority(AlarmPriority::CRITICAL);
    TEST_ASSERT_FALSE(buzzer.playEasterEgg());
    buzzer.setPriority(AlarmPriority::NONE);

    TEST_ASSERT_TRUE(buzzer.playEasterEgg());
    delay(1500);
    buzzer.setPriority(AlarmPriority::CRITICAL);   // Escuchar: corta a patrón crítico
    delay(2500);
    buzzer.setPriority(AlarmPriority::NONE);
    delay(1000);
}

void setup() {
    delay(2000);                                       // Da tiempo a abrir el puerto serie
    UNITY_BEGIN();
    RUN_TEST(test_regla_activa_tras_persistencia);
    RUN_TEST(test_regla_disparo_interrumpido_reinicia);
    RUN_TEST(test_regla_se_apaga_tras_persistencia);
    RUN_TEST(test_regla_zona_histeresis_mantiene);
    RUN_TEST(test_manager_texto_y_cambios);
    RUN_TEST(test_manager_reservada_no_se_escribe);
    RUN_TEST(test_prioridad_sin_alarmas);
    RUN_TEST(test_prioridad_media_temperatura);
    RUN_TEST(test_prioridad_critica_gana);
    RUN_TEST(test_texto_ble_sigue_con_8_posiciones);
    RUN_TEST(test_temperatura_leve_y_luego_media);
    RUN_TEST(test_tecnica_no_va_al_texto_ble);
    RUN_TEST(test_barra_sin_alarmas_vacia);
    RUN_TEST(test_barra_muestra_la_mas_grave_y_cuenta_las_demas);
    RUN_TEST(test_escalon_de_temperatura_con_histeresis);
    RUN_TEST(test_pausa_cuenta_y_termina);
    RUN_TEST(test_pausa_alarma_nueva_la_corta);
    RUN_TEST(test_pausa_cero_la_cancela);
    RUN_TEST(test_pausa_se_recorta_al_maximo);
    RUN_TEST(test_pausa_repetida_es_idempotente);
    RUN_TEST(test_reconocer_calla_las_activas);
    RUN_TEST(test_reconocer_alarma_nueva_suena);
    RUN_TEST(test_reconocida_que_vuelve_suena);
    RUN_TEST(test_reconocer_no_calla_las_tecnicas);
    RUN_TEST(test_leds_secuencia);
    RUN_TEST(test_buzzer_arranca);
    RUN_TEST(test_buzzer_patron_leve);
    RUN_TEST(test_buzzer_patron_critico);
    RUN_TEST(test_buzzer_patron_medio);
    RUN_TEST(test_buzzer_silencio);
    RUN_TEST(test_buzzer_cucaracha);
    RUN_TEST(test_cucaracha_bloqueada_con_alarma);
    UNITY_END();
}

void loop() {}
