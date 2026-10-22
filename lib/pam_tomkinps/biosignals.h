#ifndef BIOSIGNALS_H
#define BIOSIGNALS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float b0, b1, b2;   /* coeficientes de alimentación directa (feed-forward) */
    float a1, a2;       /* coeficientes de realimentación (feedback), a0 = 1   */
    float z1, z2;       /* estados internos (memoria) del filtro               */
} Biquad;

/* Pone a cero la memoria del filtro (no cambia los coeficientes). */
void  biquad_reset(Biquad* f);

/* Procesa UNA muestra y devuelve la salida filtrada. */
float biquad_process(Biquad* f, float x);

void  biquad_lowpass (Biquad* f, float fs, float fc, float Q);
void  biquad_highpass(Biquad* f, float fs, float fc, float Q);
void  biquad_bandpass(Biquad* f, float fs, float f0, float Q); /* pico 0 dB   */
void  biquad_notch   (Biquad* f, float fs, float f0, float Q); /* rechaza-banda*/

/* Ajusta los estados como si la entrada llevara tiempo constante en x0.
 * Evita el transitorio del escalón de DC del ADC al arrancar.
 * Devuelve la salida en régimen permanente (ganancia DC * x0). */
float biquad_prime(Biquad* f, float x0);


#define BIO_MAX_STAGES 4
typedef struct {
    Biquad stage[BIO_MAX_STAGES];
    int    n;                 /* nº de etapas activas */
} BiquadChain;

/* chain_init: DEJA LA CASCADA EN CERO. Debe llamarse SIEMPRE antes de usarla,
 * porque una estructura recién declarada contiene basura (incluido 'n').  */
void  chain_init(BiquadChain* c);
void  chain_reset(BiquadChain* c);   /* limpia solo los estados, conserva el diseño */
float chain_process(BiquadChain* c, float x);
void  chain_prime(BiquadChain* c, float x0);   /* arranque en régimen permanente */


/* ================= Filtro PPG (independiente del Pan-Tompkins) ============
 * Cadena propia: pasa-banda 0.5–4 Hz = pasa-altas + pasa-bajas Butterworth
 * de 2º orden. No comparte estados ni parámetros con el ECG. */
#define PPG_HP_FC  0.5f      /* corte pasa-altas PPG [Hz] */
#define PPG_LP_FC  4.0f      /* corte pasa-bajas PPG [Hz] */
#define PPG_BW_Q   0.707f    /* Q Butterworth             */

typedef struct {
    BiquadChain chain;
    float fs;
    bool  primed;            /* ¿ya se inicializaron los estados con la 1ª muestra? */
    bool  initialized;
} PpgFilter;

/* lp_fc <= 0 (o >= fs/2) desactiva el pasa-bajas. */
void  ppg_filter_init   (PpgFilter* p, float fs, float hp_fc, float lp_fc);
void  ppg_filter_reset  (PpgFilter* p);   /* limpia estados, conserva el diseño */
float ppg_filter_process(PpgFilter* p, float x);


/* ================= Procesador PPG: filtro + detector de pulsos ============
 * PpgFilter (pasa-banda 0.5–4 Hz) seguido de un detector de picos con umbral
 * adaptativo sobre la señal filtrada:
 *  - umbral = PPG_PK_THRESH_FRAC de una amplitud estimada que decae;
 *  - mientras la señal está sobre el umbral se sigue su máximo;
 *  - el pulso se acepta al bajar del umbral; su instante es el del máximo;
 *  - FC = promedio de los últimos PPG_PK_RR_COUNT intervalos;
 *  - sin pulsos durante PPG_PK_TIMEOUT_MS la FC vuelve a 0.
 * Igual que PanTompkins: no usa malloc y todo el estado vive en la estructura. */
#define PPG_PK_AMP_TAU_S      4.0f   /* constante de caída de la amplitud [s] (0.999/muestra a 250 Hz) */
#define PPG_PK_THRESH_FRAC    0.5f   /* umbral = fracción de la amplitud estimada */
#define PPG_PK_MIN_AMP        20.0f  /* amplitud mínima por defecto [cuentas]; evita disparar con ruido */
#define PPG_PK_REFRACTORY_MS  300u   /* RR mínimo: máx. ~200 lpm */
#define PPG_PK_MAX_RR_MS      2000u  /* RR máximo: mín. 30 lpm; si se supera se reinicia el promedio */
#define PPG_PK_TIMEOUT_MS     3000u  /* sin pulsos este tiempo -> bpm = 0 */
#define PPG_PK_RR_COUNT       4      /* intervalos RR promediados */

typedef struct {
    float    filtered;   /* muestra tras el pasa-banda (centrada en 0)            */
    float    threshold;  /* umbral actual del detector                            */
    bool     beat;       /* true SOLO en la muestra en que se acepta un pulso     */
    uint32_t peak_us;    /* instante del máximo del pulso aceptado (válido si beat) */
    float    bpm;        /* frecuencia de pulso (0 = sin estimación)              */
} PPG_Result;

typedef struct {
    PpgFilter filter;
    float    min_amp;        /* amplitud mínima para aceptar pulsos; se puede ajustar tras ppg_init */
    float    amp_decay;      /* caída por muestra de 'amp', calculada con fs */

    /* Detector */
    float    amp;            /* amplitud pico estimada */
    bool     above;          /* ¿la señal está sobre el umbral? */
    float    peak_val;       /* máximo del pulso en curso */
    uint32_t peak_us;        /* instante de ese máximo */
    bool     has_last;       /* ¿hay un pulso anterior aceptado? */
    uint32_t last_beat_us;   /* instante del último pulso aceptado */

    /* Frecuencia de pulso */
    float    rr[PPG_PK_RR_COUNT];   /* últimos RR [ms] */
    int      rr_idx, rr_n;
    float    bpm;
    bool     initialized;
} PpgProcessor;

/* Inicializa filtro y detector para una frecuencia de muestreo dada. */
void       ppg_init   (PpgProcessor* p, float fs);

/* Reinicia filtro y detector; conserva fs y min_amp. */
void       ppg_reset  (PpgProcessor* p);

/* Procesa UNA muestra cruda (no filtrada). t_us: instante de la muestra [us];
 * puede venir de micros() o de un contador de muestras (n * 1e6 / fs). */
PPG_Result ppg_process(PpgProcessor* p, float sample, uint32_t t_us);


#define BIO_MWI_MAX 128           /* soporta hasta 256 ms @ 500 Hz */
typedef struct {
    float buf[BIO_MWI_MAX];
    int   size;                   /* tamaño de ventana N */
    int   idx;                    /* puntero del buffer circular */
    float acc;                    /* suma corriente de la ventana */
} MovingIntegrator;

void  mwi_init  (MovingIntegrator* m, int window);   /* fija N y limpia */
void  mwi_reset (MovingIntegrator* m);
float mwi_process(MovingIntegrator* m, float x);

typedef struct {
    float x[5];               /* historial: [0]=x[n] ... [4]=x[n-4] */
} Derivative5;

void  deriv5_init (Derivative5* d);   /* pone el historial a cero */
void  deriv5_reset(Derivative5* d);
float deriv5_process(Derivative5* d, float x);

typedef struct {
    float raw;          /* muestra de entrada (tal cual entró)              */
    float bandpassed;   /* tras pasa-banda 5–15 Hz                          */
    float derivative;   /* tras derivada de 5 puntos                        */
    float squared;      /* tras elevar al cuadrado                          */
    float integrated;   /* tras integrador de ventana móvil (MWI)           */
    float threshold;    /* umbral adaptativo actual (sobre la señal MWI)    */
    bool  beat;         /* true SOLO en la muestra donde se detecta un QRS  */
    float bpm;          /* FC suavizada (a partir del promedio de RR)       */
    float bpm_inst;     /* FC instantánea (último intervalo RR)             */
} PT_Result;

/* Estado completo del detector. Se puede declarar de forma estática o en la
 * pila (no usa malloc), ideal para embebido. */
typedef struct {
    float fs;                 /* frecuencia de muestreo [Hz] */

    /* --- Etapa 1: pasa-banda 5–15 Hz (HP + LP en cascada) --- */
    Biquad hp;                /* pasa-altos ~5 Hz  */
    Biquad lp;                /* pasa-bajos ~15 Hz */

    /* --- Etapa 2: derivada de 5 puntos --- */
    Derivative5 der;          /* derivada de 5 puntos (módulo reutilizable) */

    /* --- Etapa 4: integrador de ventana móvil --- */
    MovingIntegrator mwi;

    /* --- Etapa 5: detección / umbrales adaptativos (señal integrada) --- */
    float spki, npki;         /* estimadores de pico de SEÑAL y de RUIDO */
    float threshold_i1;       /* umbral principal */
    float threshold_i2;       /* umbral reducido (para búsqueda hacia atrás) */

    /* Detección de máximo local sobre la señal integrada (streaming) */
    float prev_int;           /* muestra integrada anterior */
    float peak_val;           /* valor del máximo que se está siguiendo */
    bool  rising;             /* ¿la integrada venía subiendo? */

    /* Pendiente para discriminación de onda T */
    float cur_maxslope;       /* máx |derivada| del latido en curso */
    float last_qrs_slope;     /* pendiente del último QRS aceptado */

    /* Tiempo e intervalos RR (en nº de muestras) */
    uint32_t n;               /* índice global de muestra */
    uint32_t last_qrs_n;      /* muestra del último QRS */
    int   refractory;         /* muestras equivalentes a 200 ms */
    int   twave_win;          /* muestras equivalentes a 360 ms */
    float rr_buf[8];          /* últimos 8 intervalos RR [ms] */
    int   rr_idx, rr_count;
    float rr_med;             /* MEDIANA de los RR [ms]  (robusta) */
    float rr_avg;             /* RR equivalente [muestras], para búsqueda atrás */

    /* --- Base de tiempo --- */
    uint32_t last_qrs_us;     /* marca de tiempo del último QRS [us] */
    bool     use_time;        /* true: los RR se miden con micros() real */

    /* Búsqueda hacia atrás (search-back) */
    float    sb_peak;         /* mejor candidato entre I2 e I1 desde el último QRS */
    uint32_t sb_peak_n;
    float    sb_peak_slope;
    uint32_t sb_peak_us;      /* marca de tiempo del candidato */

    /* Fase de aprendizaje inicial */
    uint32_t learn_settle;    /* muestras de asentamiento de filtros (se descartan) */
    uint32_t learn_samples;   /* muestra en la que termina el aprendizaje */
    float    learn_max;       /* pico máx. de la integrada durante el aprendizaje */
    float    learn_sum;       /* suma para la media */
    uint32_t learn_count;     /* nº de muestras acumuladas en el aprendizaje */
    bool     learning;

    /* Salidas */
    float bpm, bpm_inst;
    uint32_t beats;           /* nº de QRS detectados desde el init/reset */
    bool  initialized;
} PanTompkins;

/* Inicializa el detector para una frecuencia de muestreo dada.
 * Reconstruye filtros y ventanas en función de fs (funciona a 250, 500 Hz...). */
void pt_init(PanTompkins* pt, float fs);

/* Reinicia el estado dinámico (útil al detectar electrodos sueltos). */
void pt_reset(PanTompkins* pt);

/* Procesa UNA muestra de ECG y devuelve todas las señales intermedias,
 * el flag de latido y la FC. Este es el corazón de la librería. */
PT_Result pt_process(PanTompkins* pt, float sample);

PT_Result pt_process_t(PanTompkins* pt, float sample, uint32_t t_us);

/* Devuelve la FC suavizada actual [bpm] (0 si aún no hay estimación). */
float pt_bpm(const PanTompkins* pt);        /* FC suavizada (8 RR)      [bpm] */
float pt_bpm_inst(const PanTompkins* pt);   /* FC del último intervalo  [bpm] */
float pt_rr_ms(const PanTompkins* pt);      /* intervalo RR promedio     [ms] */
uint32_t pt_beats(const PanTompkins* pt);   /* latidos detectados             */


/* ============ Presión arterial por PTT + PIR (Ding et al., 2017) ============
 * Guarda los últimos ~4 s de ECG y PPG crudos y, cada vez que el detector de
 * QRS avisa un latido, calcula el latido ANTERIOR (necesita el R siguiente):
 *  1. Pico R real: máximo del ECG en [aviso - 400 ms, aviso], sin acercarse a
 *     menos de 250 ms del R anterior (corrige el retardo del Pan-Tompkins).
 *  2. PTT: de R al punto de subida más rápida del pulso en [R+100, R+500] ms.
 *  3. PIR = IH / IL: luz máxima entre R y ese punto / luz mínima en los 300 ms
 *     siguientes (el PPG crudo es luz: baja cuando la arteria se llena).
 *  4. Revisión (rangos y artefactos), promedio de BP_NPROM latidos.
 *  5. Calibración con un tensiómetro (SBP0/DBP0 -> PTT0/PIR0) y modelo:
 *       MBP = MBP0·PIR0/PIR,  PP = PP0·(PIR/PIR0)·(PTT0/PTT)²
 *       SBP = MBP + 2·PP/3,   DBP = MBP − PP/3
 * ECG y PPG deben compartir reloj: la muestra n de PPG es simultánea a la
 * muestra n·fs_ecg/fs_ppg de ECG (así llegan las tramas del AFE4950). */
#define BP_ECG_BUF        2048     /* ~4 s @ 500 Hz */
#define BP_PPG_BUF        1024     /* ~4 s @ 250 Hz */
#define BP_NPROM          8        /* latidos promediados */
#define BP_NCAL           30       /* latidos para calibrar */
#define BP_PTT_MIN_MS     150.0f
#define BP_PTT_MAX_MS     450.0f
#define BP_PIR_MAX        1.3f
#define BP_ARTIFACT_FRAC  0.2f     /* salto máximo del PTT respecto al promedio */

/* Motivo por el que un aviso de latido no dio un latido aceptado (BP_Result.reject) */
enum {
    BP_OK = 0,
    BP_REJ_STARTUP,     /* aún no hay 500 ms de ECG */
    BP_REJ_FIRST,       /* primer R: falta el anterior */
    BP_REJ_RR,          /* RR fuera de 333–2000 ms */
    BP_REJ_WINDOW,      /* no hay muestras de PPG en la ventana de búsqueda */
    BP_REJ_SLOPE,       /* el PPG no tuvo subida en la ventana */
    BP_REJ_DC,          /* luz mínima <= 0 (PIR imposible) */
    BP_REJ_PTT,         /* PTT fuera de BP_PTT_MIN_MS..BP_PTT_MAX_MS */
    BP_REJ_PIR,         /* PIR fuera de (1, BP_PIR_MAX] */
    BP_REJ_ARTIFACT,    /* PTT lejos del promedio */
    BP_REJ_COUNT
};

typedef struct {
    bool  beat;           /* true si en este aviso se aceptó un latido (los demás campos son válidos) */
    float ptt_ms;         /* PTT del latido */
    float pir;            /* PIR del latido (1.0 si no se usa PIR) */
    float ptt_avg;        /* promedio de los últimos BP_NPROM latidos aceptados */
    float pir_avg;
    bool  calibrating;    /* acumulando latidos de calibración */
    bool  calibrated;     /* hay PTT0/PIR0: sbp y dbp son válidos */
    bool  cal_done;       /* true SOLO en el latido que termina la calibración */
    float sbp, dbp;       /* presión estimada [mmHg] (si calibrated) */
    uint8_t reject;       /* BP_OK o el motivo BP_REJ_* si beat == false */
} BP_Result;

typedef struct {
    float fs_ecg, fs_ppg;
    int   ecg_sign;       /* +1, o -1 si la onda R sale hacia abajo */
    int   ppg_sign;       /* +1 si el PPG crudo es luz (baja con el pulso), -1 si es volumen */
    bool  use_pir;        /* false: solo PTT (pir = 1) */

    int32_t  ecg[BP_ECG_BUF];
    int32_t  ppg[BP_PPG_BUF];
    uint32_t n_ecg, n_ppg;        /* muestras recibidas de cada señal */

    bool     has_r;               /* ¿hay un R anterior? */
    uint32_t r_prev;              /* índice ECG del R anterior */

    float ptt_hist[BP_NPROM], pir_hist[BP_NPROM];
    int   n_prom, i_prom;
    float ptt_avg, pir_avg;

    bool  calibrating, calibrated;
    float sbp0, dbp0, ptt0, pir0;
    float sum_ptt, sum_pir;
    int   n_cal;
} BpEstimator;

void bp_init(BpEstimator* b, float fs_ecg, float fs_ppg);
void bp_push_ecg(BpEstimator* b, int32_t sample);   /* una muestra cruda de ECG */
void bp_push_ppg(BpEstimator* b, int32_t sample);   /* una muestra cruda de PPG (luz, con DC) */

/* Llamar cuando el detector de QRS marca latido en la ÚLTIMA muestra de ECG
 * entregada con bp_push_ecg. Procesa el latido anterior. */
BP_Result bp_on_beat(BpEstimator* b);

/* Inicia la calibración con la medida del tensiómetro (paciente quieto ~30 s). */
void bp_start_calibration(BpEstimator* b, float sbp, float dbp);

/* Anula la calibración y el promedio (p. ej. al perder la señal) */
void bp_cancel_calibration(BpEstimator* b);

/* Modelo de presión con la calibración actual (útil para probarlo aislado). */
void bp_model(const BpEstimator* b, float ptt_ms, float pir, float* sbp, float* dbp);

/* ================= SpO2 a partir de los parámetros del AFE4950 ==========
 * El AFE entrega una vez por segundo ESTADO y AC/DC de los canales IR y RED.
 *   R    = (AC_RED / DC_RED) / (AC_IR / DC_IR)
 *   SpO2 = SPO2_CAL_A - SPO2_CAL_B * R            (curva empírica del readme)
 * Solo se calcula con ESTADO = SPO2_STATE_VALID. Un resultado bajo
 * SPO2_MIN_PCT se toma como señal mala (0 = sin dato); sobre SPO2_MAX_PCT
 * se acota a SPO2_MAX_PCT. El resultado se redondea a 0.1 %. */
#define SPO2_STATE_VALID  2        /* 0 = sin señal, 1 = estabilizando, 2 = válida */
#define SPO2_CAL_A        110.0f
#define SPO2_CAL_B        25.0f
#define SPO2_MIN_PCT      80.0f    /* por debajo: se descarta (devuelve 0) */
#define SPO2_MAX_PCT      100.0f   /* por encima: se acota */

/* Devuelve la SpO2 [%] o 0 si no es válida (estado distinto de 2, algún AC o
 * DC en 0, o resultado menor que SPO2_MIN_PCT). AC/DC en cuentas (24 bits). */
float spo2_from_params(uint8_t state, uint32_t ac_ir, uint32_t dc_ir,
                       uint32_t ac_red, uint32_t dc_red);

/* ================= Temperatura con termistor YSI 400 =====================
 * El YSI 400 es un NTC de 2252 ohm a 25 °C. Va en un puente de Wheatstone
 * seguido de un amplificador de instrumentación; el ADC mide su salida:
 *          VEXC
 *        ┌──┴──┐
 *       R1     R3
 *   Va ──┤     ├── Vb
 *       R2    NTC
 *        └──┬──┘
 *          GND
 *   Va   = VEXC · R2 / (R1 + R2)            (rama fija)
 *   Vout = G · (Va − Vb) + VREF            (Va a IN+, Vb a IN−: sube con la temperatura)
 * Despeje: Vb = Va − (Vout − VREF)/G,  R_ntc = R3 · Vb / (VEXC − Vb)
 * Curva (Steinhart-Hart normalizada a R25), T en kelvin:
 *   x = ln(R_ntc / R25)
 *   1/T = A + B·x + C·x²
 * Fuera de TEMP_MIN_C..TEMP_MAX_C se toma como sonda suelta o en corto (0 = sin dato). */
#define YSI400_R25_OHM       2252.0f
#define YSI400_A             3.354016e-3f
#define YSI400_B             2.569850e-4f
#define YSI400_C             3.585153e-6f

/* TODO: valores provisorios; reemplazar por los del circuito (PDF) */
#define TEMP_BRIDGE_VEXC_MV  3300.0f   /* excitación del puente */
#define TEMP_BRIDGE_R1_OHM   2252.0f
#define TEMP_BRIDGE_R2_OHM   2252.0f
#define TEMP_BRIDGE_R3_OHM   2252.0f
#define TEMP_AMP_GAIN        5.0f
#define TEMP_AMP_VREF_MV     0.0f      /* tensión del pin REF del amplificador */

#define TEMP_ADC_MIN_MV      50.0f     /* salida pegada a un riel: sonda suelta o en corto */
#define TEMP_ADC_MAX_MV      3100.0f   /* tope del ADC con atenuación de 11 dB */

#define TEMP_MIN_C           25.0f     /* por debajo: sonda suelta o fuera del cuerpo */
#define TEMP_MAX_C           45.0f

float ysi400_r_from_mv(float mv);          /* R del NTC [ohm] a partir de la salida del amplificador; 0 si es imposible o está en un riel */
float ysi400_temp_from_r(float r_ohm);     /* temperatura [°C] sin acotar; NAN si r <= 0 */
float ysi400_temp_from_mv(float mv);       /* temperatura [°C] o 0 si es inválida */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* BIOSIGNALS_H */
