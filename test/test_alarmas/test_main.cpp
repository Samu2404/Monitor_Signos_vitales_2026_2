#include <Arduino.h>
#include <unity.h>
#include "AlarmRule.h"
#include "AlarmManager.h"
#include "AlarmBuzzer.h"
#include "AlarmLeds.h"

// Buzzer y LEDs en la placa: mismos pines que main.cpp
static const uint8_t PinBuzzer     = 25;
static const bool    BuzzerPassive = true;
static AlarmBuzzer buzzer(PinBuzzer, BuzzerPassive);
static AlarmLeds   leds(32, 33, 14, 13);      // Verde, azul, amarillo, rojo

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
    // Escuchar: 1 pitido suave (el siguiente sería a los 15 s). LED azul
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
