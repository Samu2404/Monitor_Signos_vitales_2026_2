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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* BIOSIGNALS_H */
