#include "biosignals.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

void biquad_reset(Biquad* f) {
    f->z1 = 0.0f;
    f->z2 = 0.0f;
}

/* Forma Directa II Transpuesta: robusta y con solo 2 estados. */
float biquad_process(Biquad* f, float x) {
    float y = f->b0 * x + f->z1;
    f->z1   = f->b1 * x - f->a1 * y + f->z2;
    f->z2   = f->b2 * x - f->a2 * y;
    return y;
}

/* Fórmulas del Audio EQ Cookbook (RBJ). Todas normalizan dividiendo por a0. */
void biquad_lowpass(Biquad* f, float fs, float fc, float Q) {
    float w0    = 2.0f * M_PI * fc / fs;
    float cw    = cosf(w0);
    float sw    = sinf(w0);
    float alpha = sw / (2.0f * Q);
    float a0    = 1.0f + alpha;

    f->b0 = ((1.0f - cw) * 0.5f) / a0;
    f->b1 =  (1.0f - cw)         / a0;
    f->b2 = ((1.0f - cw) * 0.5f) / a0;
    f->a1 =  (-2.0f * cw)        / a0;
    f->a2 =  (1.0f - alpha)      / a0;
    biquad_reset(f);
}

void biquad_highpass(Biquad* f, float fs, float fc, float Q) {
    float w0    = 2.0f * M_PI * fc / fs;
    float cw    = cosf(w0);
    float sw    = sinf(w0);
    float alpha = sw / (2.0f * Q);
    float a0    = 1.0f + alpha;

    f->b0 = ((1.0f + cw) * 0.5f) / a0;
    f->b1 = (-(1.0f + cw))       / a0;
    f->b2 = ((1.0f + cw) * 0.5f) / a0;
    f->a1 =  (-2.0f * cw)        / a0;
    f->a2 =  (1.0f - alpha)      / a0;
    biquad_reset(f);
}

/* Pasa-banda con ganancia de pico constante 0 dB. */
void biquad_bandpass(Biquad* f, float fs, float f0, float Q) {
    float w0    = 2.0f * M_PI * f0 / fs;
    float cw    = cosf(w0);
    float sw    = sinf(w0);
    float alpha = sw / (2.0f * Q);
    float a0    = 1.0f + alpha;

    f->b0 =  alpha  / a0;
    f->b1 =  0.0f;
    f->b2 = -alpha  / a0;
    f->a1 = (-2.0f * cw)   / a0;
    f->a2 = (1.0f - alpha) / a0;
    biquad_reset(f);
}

/* Rechaza-banda (notch): útil para 50/60 Hz de red eléctrica. */
void biquad_notch(Biquad* f, float fs, float f0, float Q) {
    float w0    = 2.0f * M_PI * f0 / fs;
    float cw    = cosf(w0);
    float sw    = sinf(w0);
    float alpha = sw / (2.0f * Q);
    float a0    = 1.0f + alpha;

    f->b0 =  1.0f          / a0;
    f->b1 = (-2.0f * cw)   / a0;
    f->b2 =  1.0f          / a0;
    f->a1 = (-2.0f * cw)   / a0;
    f->a2 = (1.0f - alpha) / a0;
    biquad_reset(f);
}

/* Estados de régimen permanente para entrada constante x0 (DF-II T):
 *   y  = G*x0,  G = (b0+b1+b2)/(1+a1+a2)
 *   z2 = b2*x0 - a2*y
 *   z1 = b1*x0 - a1*y + z2                                   */
float biquad_prime(Biquad* f, float x0) {
    float den = 1.0f + f->a1 + f->a2;
    float g   = (den != 0.0f) ? (f->b0 + f->b1 + f->b2) / den : 0.0f;
    float y   = g * x0;
    f->z2 = f->b2 * x0 - f->a2 * y;
    f->z1 = f->b1 * x0 - f->a1 * y + f->z2;
    return y;
}

void chain_init(BiquadChain* c) {
    c->n = 0;
    for (int i = 0; i < BIO_MAX_STAGES; i++) {
        c->stage[i].b0 = 1.0f; c->stage[i].b1 = 0.0f; c->stage[i].b2 = 0.0f;
        c->stage[i].a1 = 0.0f; c->stage[i].a2 = 0.0f;
        biquad_reset(&c->stage[i]);
    }
}

void chain_reset(BiquadChain* c) {
    if (c->n < 0) c->n = 0;
    if (c->n > BIO_MAX_STAGES) c->n = BIO_MAX_STAGES;
    for (int i = 0; i < c->n; i++) biquad_reset(&c->stage[i]);
}

float chain_process(BiquadChain* c, float x) {
    float y = x;
    for (int i = 0; i < c->n; i++) y = biquad_process(&c->stage[i], y);
    return y;
}

void chain_prime(BiquadChain* c, float x0) {
    float x = x0;
    for (int i = 0; i < c->n; i++) x = biquad_prime(&c->stage[i], x);
}

/* ============================ Filtro PPG =============================== */
void ppg_filter_init(PpgFilter* p, float fs, float hp_fc, float lp_fc) {
    chain_init(&p->chain);
    p->fs = fs;

    biquad_highpass(&p->chain.stage[0], fs, hp_fc, PPG_BW_Q);
    p->chain.n = 1;

    if (lp_fc > 0.0f && lp_fc < 0.5f * fs) {
        biquad_lowpass(&p->chain.stage[1], fs, lp_fc, PPG_BW_Q);
        p->chain.n = 2;
    }

    p->primed      = false;
    p->initialized = true;
}

void ppg_filter_reset(PpgFilter* p) {
    chain_reset(&p->chain);
    p->primed = false;
}

float ppg_filter_process(PpgFilter* p, float x) {
    if (!p->primed) {            /* 1ª muestra: arranque sin transitorio de DC */
        chain_prime(&p->chain, x);
        p->primed = true;
    }
    return chain_process(&p->chain, x);
}

/* ======================= Procesador PPG (detector) ====================== */
static void ppg_detector_clear(PpgProcessor* p) {
    p->amp          = 0.0f;
    p->above        = false;
    p->peak_val     = 0.0f;
    p->peak_us      = 0;
    p->has_last     = false;
    p->last_beat_us = 0;
    memset(p->rr, 0, sizeof(p->rr));
    p->rr_idx = 0;
    p->rr_n   = 0;
    p->bpm    = 0.0f;
}

void ppg_init(PpgProcessor* p, float fs) {
    ppg_filter_init(&p->filter, fs, PPG_HP_FC, PPG_LP_FC);
    p->min_amp   = PPG_PK_MIN_AMP;
    p->amp_decay = expf(-1.0f / (PPG_PK_AMP_TAU_S * fs));
    ppg_detector_clear(p);
    p->initialized = true;
}

void ppg_reset(PpgProcessor* p) {
    ppg_filter_reset(&p->filter);
    ppg_detector_clear(p);
}

/* Se llama cuando la señal baja del umbral: decide si el pulso cuyo máximo
 * está en peak_us se acepta. Devuelve true si se aceptó y hay nueva FC. */
static bool ppg_accept_peak(PpgProcessor* p) {
    if (!p->has_last) {
        p->has_last     = true;              /* 1er pulso: solo marca la referencia */
        p->last_beat_us = p->peak_us;
        return false;
    }

    uint32_t rr_ms = (p->peak_us - p->last_beat_us) / 1000u;
    if (rr_ms < PPG_PK_REFRACTORY_MS) {
        return false;                        /* rebote / pico secundario: se ignora sin mover la referencia */
    }
    p->last_beat_us = p->peak_us;
    if (rr_ms > PPG_PK_MAX_RR_MS) {
        p->rr_n   = 0;                       /* se perdieron pulsos: reinicia el promedio */
        p->rr_idx = 0;
        return false;
    }

    p->rr[p->rr_idx] = (float)rr_ms;
    p->rr_idx = (p->rr_idx + 1) % PPG_PK_RR_COUNT;
    if (p->rr_n < PPG_PK_RR_COUNT) p->rr_n++;

    float sum = 0.0f;
    for (int i = 0; i < p->rr_n; i++) sum += p->rr[i];
    p->bpm = 60000.0f * (float)p->rr_n / sum;
    return true;
}

PPG_Result ppg_process(PpgProcessor* p, float sample, uint32_t t_us) {
    PPG_Result r;
    memset(&r, 0, sizeof(r));

    float x = ppg_filter_process(&p->filter, sample);
    r.filtered = x;

    p->amp *= p->amp_decay;
    if (x > p->amp) p->amp = x;
    float thr = p->amp * PPG_PK_THRESH_FRAC;
    r.threshold = thr;

    if (!p->above) {
        if (p->amp >= p->min_amp && x > thr) {   /* empieza un pulso */
            p->above    = true;
            p->peak_val = x;
            p->peak_us  = t_us;
        }
    } else {
        if (x > p->peak_val) {                   /* sigue el máximo */
            p->peak_val = x;
            p->peak_us  = t_us;
        }
        if (x < thr) {                           /* el pulso terminó */
            p->above = false;
            if (ppg_accept_peak(p)) {
                r.beat    = true;
                r.peak_us = p->peak_us;
            }
        }
    }

    /* Sin pulsos durante el timeout: la FC deja de ser válida */
    if (p->has_last && (t_us - p->last_beat_us) > PPG_PK_TIMEOUT_MS * 1000u) {
        p->bpm    = 0.0f;
        p->rr_n   = 0;
        p->rr_idx = 0;
    }

    r.bpm = p->bpm;
    return r;
}

void mwi_init(MovingIntegrator* m, int window) {
    if (window < 1)            window = 1;
    if (window > BIO_MWI_MAX)  window = BIO_MWI_MAX;
    m->size = window;
    mwi_reset(m);
}

void mwi_reset(MovingIntegrator* m) {
    memset(m->buf, 0, sizeof(m->buf));
    m->idx = 0;
    m->acc = 0.0f;
}

/* O(1): resta la muestra que sale, suma la que entra. */
float mwi_process(MovingIntegrator* m, float x) {
    m->acc -= m->buf[m->idx];   /* saca la más antigua */
    m->buf[m->idx] = x;         /* mete la nueva       */
    m->acc += x;
    m->idx = (m->idx + 1) % m->size;
    return m->acc / (float)m->size;
}

void deriv5_init(Derivative5* d)  { memset(d->x, 0, sizeof(d->x)); }
void deriv5_reset(Derivative5* d) { memset(d->x, 0, sizeof(d->x)); }

float deriv5_process(Derivative5* d, float x) {
    d->x[4] = d->x[3];
    d->x[3] = d->x[2];
    d->x[2] = d->x[1];
    d->x[1] = d->x[0];
    d->x[0] = x;
    return 0.125f * (2.0f*d->x[0] + d->x[1] - d->x[3] - 2.0f*d->x[4]);
}


/* --- Parámetros del algoritmo (en ms; se convierten a nº de muestras) --- */
#define PT_HP_FC     5.0f     /* corte pasa-altos del pasa-banda  */
#define PT_LP_FC    15.0f     /* corte pasa-bajos del pasa-banda  */
#define PT_BW_Q      0.707f   /* Q Butterworth                    */
#define PT_MWI_MS    150.0f   /* ancho de ventana de integración  */
#define PT_REFRACT_MS 200.0f  /* periodo refractario              */
#define PT_TWAVE_MS  360.0f   /* ventana de discriminación onda T */
#define PT_SETTLE_S  1.0f     /* asentamiento de filtros (se descarta) */
#define PT_LEARN_S   2.0f     /* fin de la fase de aprendizaje    */

static int ms_to_samples(float ms, float fs) {
    int s = (int)(ms * 0.001f * fs + 0.5f);
    return (s < 1) ? 1 : s;
}

void pt_init(PanTompkins* pt, float fs) {
    memset(pt, 0, sizeof(*pt));
    pt->fs = fs;

    /* Etapa 1: pasa-banda 5–15 Hz = pasa-altos 5 Hz seguido de pasa-bajos 15 Hz */
    biquad_highpass(&pt->hp, fs, PT_HP_FC, PT_BW_Q);
    biquad_lowpass (&pt->lp, fs, PT_LP_FC, PT_BW_Q);

    /* Etapa 4: integrador de ventana móvil (~150 ms) */
    int mwi_win = ms_to_samples(PT_MWI_MS, fs);
    mwi_init(&pt->mwi, mwi_win);

    /* Tiempos del detector */
    pt->refractory    = ms_to_samples(PT_REFRACT_MS, fs);
    pt->twave_win     = ms_to_samples(PT_TWAVE_MS,  fs);
    pt->learn_settle  = (uint32_t)(PT_SETTLE_S * fs);
    pt->learn_samples = (uint32_t)(PT_LEARN_S * fs);

    pt_reset(pt);
    pt->initialized = true;
}

void pt_reset(PanTompkins* pt) {
    biquad_reset(&pt->hp);
    biquad_reset(&pt->lp);
    mwi_reset(&pt->mwi);
    deriv5_reset(&pt->der);

    pt->spki = 0.0f; pt->npki = 0.0f;
    pt->threshold_i1 = 0.0f; pt->threshold_i2 = 0.0f;

    pt->prev_int = 0.0f; pt->peak_val = 0.0f; pt->rising = false;
    pt->cur_maxslope = 0.0f; pt->last_qrs_slope = 0.0f;

    pt->n = 0; pt->last_qrs_n = 0;
    memset(pt->rr_buf, 0, sizeof(pt->rr_buf));
    pt->rr_idx = 0; pt->rr_count = 0; pt->rr_avg = 0.0f; pt->rr_med = 0.0f;
    pt->last_qrs_us = 0; pt->use_time = false;

    pt->sb_peak = 0.0f; pt->sb_peak_n = 0; pt->sb_peak_slope = 0.0f;
    pt->sb_peak_us = 0;

    pt->learn_max = 0.0f; pt->learn_sum = 0.0f; pt->learn_count = 0;
    pt->learning = true;

    pt->bpm = 0.0f; pt->bpm_inst = 0.0f;
    pt->beats = 0;
}

/* Actualiza umbrales a partir de los estimadores de pico de señal/ruido. */
static void pt_update_thresholds(PanTompkins* pt) {
    pt->threshold_i1 = pt->npki + 0.25f * (pt->spki - pt->npki);
    pt->threshold_i2 = 0.5f * pt->threshold_i1;
}

/* Registra un QRS aceptado en la muestra 'qn' con valor de pico 'peak'. */
static void pt_register_qrs(PanTompkins* pt, uint32_t qn, uint32_t qn_us,
                            float peak, float slope, bool searchback) {
    /* Actualiza el estimador de pico de SEÑAL.
     * En búsqueda hacia atrás se usa un coeficiente mayor (0.25) para
     * recuperar la escala más rápido, como en el algoritmo original. */
    if (searchback) pt->spki = 0.25f * peak + 0.75f * pt->spki;
    else            pt->spki = 0.125f * peak + 0.875f * pt->spki;

    /* ---- Intervalo RR y FC ----
     * El RR se guarda SIEMPRE en milisegundos. Si hay base de tiempo real
     * (use_time) se mide con las marcas de micros(); si no, se deduce de la
     * fs declarada contando muestras. */
    bool tengo_previo = (pt->use_time) ? (pt->last_qrs_us != 0)
                                       : (pt->last_qrs_n  != 0);
    if (tengo_previo) {
        float rr_ms;
        if (pt->use_time) {
            /* resta segura ante desbordamiento de micros() (cada ~71 min) */
            uint32_t d = qn_us - pt->last_qrs_us;
            rr_ms = (float)d * 0.001f;
        } else {
            rr_ms = 1000.0f * (float)(qn - pt->last_qrs_n) / pt->fs;
        }

        float bpm = (rr_ms > 0.0f) ? (60000.0f / rr_ms) : 0.0f;

        /* Filtro de plausibilidad fisiológica (~30-220 bpm). */
        if (bpm >= 30.0f && bpm <= 220.0f) {
            pt->bpm_inst = bpm;
            pt->rr_buf[pt->rr_idx] = rr_ms;
            pt->rr_idx = (pt->rr_idx + 1) & 7;             /* módulo 8 */
            if (pt->rr_count < 8) pt->rr_count++;

            /* MEDIANA de los últimos RR (no promedio).
             * Un latido espurio o uno perdido altera mucho el promedio pero
             * casi nada la mediana: es el estimador robusto estándar en
             * monitores de FC. */
            float tmp[8];
            for (int i = 0; i < pt->rr_count; i++) tmp[i] = pt->rr_buf[i];
            for (int i = 1; i < pt->rr_count; i++) {       /* inserción, n<=8 */
                float v = tmp[i]; int j = i - 1;
                while (j >= 0 && tmp[j] > v) { tmp[j+1] = tmp[j]; j--; }
                tmp[j+1] = v;
            }
            int m = pt->rr_count / 2;
            pt->rr_med = (pt->rr_count & 1) ? tmp[m]
                                            : 0.5f * (tmp[m-1] + tmp[m]);

            pt->bpm    = 60000.0f / pt->rr_med;
            pt->rr_avg = pt->rr_med * pt->fs / 1000.0f;   /* en muestras */
        }
    }

    pt->beats++;
    pt->last_qrs_n     = qn;
    pt->last_qrs_us    = qn_us;
    pt->last_qrs_slope = slope;
    pt->sb_peak        = 0.0f;   /* limpia candidato de búsqueda hacia atrás */
    pt_update_thresholds(pt);
}

PT_Result pt_process(PanTompkins* pt, float sample) {
    return pt_process_t(pt, sample, 0);   /* 0 = sin base de tiempo real */
}

PT_Result pt_process_t(PanTompkins* pt, float sample, uint32_t t_us) {
    if (t_us != 0) pt->use_time = true;
    PT_Result r;
    r.raw = sample;
    pt->n++;

    /* ---- Etapa 1: pasa-banda 5–15 Hz ---- */
    float bp = biquad_process(&pt->hp, sample);
    bp       = biquad_process(&pt->lp, bp);
    r.bandpassed = bp;

    /* ---- Etapa 2: derivada de 5 puntos ----
     * y[n] = (1/8)*(2x[n] + x[n-1] - x[n-3] - 2x[n-4])
     * Resalta la pendiente (alta) del complejo QRS. */
    float deriv = deriv5_process(&pt->der, bp);
    r.derivative = deriv;

    /* Seguimiento de la pendiente máxima del latido en curso (para onda T). */
    float aderiv = fabsf(deriv);
    if (aderiv > pt->cur_maxslope) pt->cur_maxslope = aderiv;

    /* ---- Etapa 3: cuadrado ----
     * Hace todo positivo y amplifica no linealmente los picos altos. */
    float sq = deriv * deriv;
    r.squared = sq;

    /* ---- Etapa 4: integrador de ventana móvil ---- */
    float integ = mwi_process(&pt->mwi, sq);
    r.integrated = integ;

    /* ---- Fase de aprendizaje inicial: siembra spki/npki ----
     * Se descarta el primer segundo (asentamiento de los filtros IIR, cuyo
     * transitorio ante el escalón de DC del ADC produce un pico espurio muy
     * grande). Luego, durante ~1 s, se estiman:
     *   spki  = pico máximo de la señal integrada (nivel de SEÑAL/QRS)
     *   npki  = media de la señal integrada       (nivel de RUIDO de fondo) */
    if (pt->learning) {
        if (pt->n > pt->learn_settle) {
            if (integ > pt->learn_max) pt->learn_max = integ;
            pt->learn_sum += integ;
            pt->learn_count++;
        }
        if (pt->n >= pt->learn_samples) {
            float mean = (pt->learn_count > 0)
                       ? pt->learn_sum / (float)pt->learn_count : 0.0f;
            pt->spki = pt->learn_max;                 /* nivel de señal  */
            pt->npki = mean;                          /* nivel de ruido  */
            if (pt->spki <= pt->npki)                 /* guarda de seguridad */
                pt->spki = pt->npki * 2.0f + 1.0f;
            pt_update_thresholds(pt);
            pt->learning = false;
        }
        r.threshold = pt->threshold_i1;
        r.beat      = false;
        r.bpm       = pt->bpm;
        r.bpm_inst  = pt->bpm_inst;
        pt->prev_int = integ;
        return r;   /* aún no detectamos latidos */
    }

    /* ---- Etapa 5: detección con umbral adaptativo ---- */
    bool beat = false;

    /* (a) Detección de máximo local sobre la señal integrada.
     *     Mientras sube, seguimos el máximo; cuando empieza a bajar,
     *     el máximo seguido es un "pico candidato". */
    if (integ > pt->prev_int) {
        pt->rising = true;
        if (integ > pt->peak_val) pt->peak_val = integ;
    } else if (pt->rising && integ < pt->prev_int) {
        /* Se confirmó un pico candidato en la muestra anterior (n-1). */
        float peak   = pt->peak_val;
        uint32_t pn  = pt->n - 1;
        float slope  = pt->cur_maxslope;
        pt->rising   = false;
        pt->peak_val = 0.0f;

        if (peak >= pt->threshold_i1) {
            /* Supera el umbral: posible QRS si pasó el refractario. */
            if ((pn - pt->last_qrs_n) > (uint32_t)pt->refractory) {
                bool isQRS = true;

                /* Discriminación de onda T: si el candidato cae dentro de
                 * los 360 ms del QRS anterior y su pendiente es < 50% de la
                 * del QRS anterior, es una onda T (no un latido). */
                if ((pn - pt->last_qrs_n) < (uint32_t)pt->twave_win
                    && pt->last_qrs_slope > 0.0f) {
                    if (slope < 0.5f * pt->last_qrs_slope) isQRS = false;
                }

                if (isQRS) {
                    pt_register_qrs(pt, pn, t_us, peak, slope, false);
                    beat = true;
                } else {
                    /* Onda T -> se trata como ruido. */
                    pt->npki = 0.125f*peak + 0.875f*pt->npki;
                    pt_update_thresholds(pt);
                }
            }
            /* Si está dentro del refractario, se ignora (mismo QRS). */
        } else {
            /* Por debajo del umbral -> ruido. */
            pt->npki = 0.125f*peak + 0.875f*pt->npki;
            pt_update_thresholds(pt);

            /* Guardar el mejor candidato entre I2 e I1 para búsqueda atrás. */
            if (peak > pt->threshold_i2 && peak > pt->sb_peak) {
                pt->sb_peak       = peak;
                pt->sb_peak_n     = pn;
                pt->sb_peak_us    = t_us;
                pt->sb_peak_slope = slope;
            }
        }
        /* Reiniciar la pendiente para el siguiente segmento/latido. */
        pt->cur_maxslope = 0.0f;
    }

    /* (b) Búsqueda hacia atrás (search-back): si llevamos más de 1.66*RR_prom
     *     sin QRS y guardamos un candidato razonable, lo aceptamos ahora
     *     bajando de hecho el umbral a I2. */
    if (pt->rr_avg > 0.0f && pt->sb_peak > 0.0f) {
        float missed = 1.66f * pt->rr_avg;
        if ((float)(pt->n - pt->last_qrs_n) > missed) {
            pt_register_qrs(pt, pt->sb_peak_n, pt->sb_peak_us, pt->sb_peak,
                            pt->sb_peak_slope, true);
            beat = true;   /* marcador un poco tardío, pero el RR es correcto */
        }
    }

    pt->prev_int = integ;

    r.threshold = pt->threshold_i1;
    r.beat      = beat;
    r.bpm       = pt->bpm;
    r.bpm_inst  = pt->bpm_inst;
    return r;
}

float pt_bpm(const PanTompkins* pt)      { return pt->bpm; }
float pt_bpm_inst(const PanTompkins* pt) { return pt->bpm_inst; }
uint32_t pt_beats(const PanTompkins* pt) { return pt->beats; }

float pt_rr_ms(const PanTompkins* pt) { return pt->rr_med; }

/* ============ Presión arterial por PTT + PIR (Ding et al., 2017) ============ */
#define BP_ECG(b, i)  ((b)->ecg[(i) % BP_ECG_BUF])
#define BP_PPG(b, i)  ((b)->ppg[(i) % BP_PPG_BUF])

void bp_init(BpEstimator* b, float fs_ecg, float fs_ppg) {
    memset(b, 0, sizeof(*b));
    b->fs_ecg   = fs_ecg;
    b->fs_ppg   = fs_ppg;
    b->ecg_sign = 1;
    b->ppg_sign = 1;
    b->use_pir  = true;
}

void bp_push_ecg(BpEstimator* b, int32_t sample) {
    BP_ECG(b, b->n_ecg) = sample * b->ecg_sign;
    b->n_ecg++;
}

void bp_push_ppg(BpEstimator* b, int32_t sample) {
    BP_PPG(b, b->n_ppg) = sample * b->ppg_sign;   /* se guarda como luz: baja con el pulso */
    b->n_ppg++;
}

void bp_start_calibration(BpEstimator* b, float sbp, float dbp) {
    b->sbp0 = sbp;
    b->dbp0 = dbp;
    b->sum_ptt = 0.0f;
    b->sum_pir = 0.0f;
    b->n_cal = 0;
    b->calibrated  = false;
    b->calibrating = true;
}

void bp_model(const BpEstimator* b, float ptt_ms, float pir, float* sbp, float* dbp) {
    float pp0  = b->sbp0 - b->dbp0;            /* presión de pulso */
    float mbp0 = b->dbp0 + pp0 / 3.0f;         /* presión media    */
    float r    = b->ptt0 / ptt_ms;
    float mbp  = mbp0 * (b->pir0 / pir);
    float pp   = pp0 * (pir / b->pir0) * r * r;
    *sbp = mbp + 2.0f * pp / 3.0f;
    *dbp = mbp - pp / 3.0f;
}

/* Índice PPG simultáneo a un índice ECG */
static uint32_t bp_ecg_to_ppg(const BpEstimator* b, uint32_t i_ecg) {
    return (uint32_t)((uint64_t)i_ecg * (uint64_t)(b->fs_ppg + 0.5f) / (uint64_t)(b->fs_ecg + 0.5f));
}

/* Paso 5: PTT y PIR del latido que empieza en R (índice ECG) y termina en r_next.
 * Devuelve false si el latido no sirve. */
static bool bp_measure_beat(const BpEstimator* b, uint32_t R, uint32_t r_next,
                            float* ptt_ms, float* pir) {
    float rr_ms = (float)(r_next - R) * 1000.0f / b->fs_ecg;
    if (rr_ms < 333.0f || rr_ms > 2000.0f) {
        return false;                              /* RR imposible (fuera de 30–180 lpm) */
    }

    /* 5a. Subida más rápida del pulso en [R+100, R+500] ms (sin pasar del R siguiente).
     * El PPG guardado es luz, así que "sube el pulso" = la luz baja más rápido. */
    int      h      = ms_to_samples(10.0f, b->fs_ppg);      /* pendiente en ±10 ms */
    uint32_t p_r    = bp_ecg_to_ppg(b, R);
    uint32_t p_next = bp_ecg_to_ppg(b, r_next);
    uint32_t ini    = p_r + (uint32_t)ms_to_samples(100.0f, b->fs_ppg);
    uint32_t fin    = p_r + (uint32_t)ms_to_samples(500.0f, b->fs_ppg);
    if (fin > p_next)                  fin = p_next;
    if (fin + h >= b->n_ppg)           fin = b->n_ppg - 1 - h;   /* aún no llegan esas muestras */
    if (ini < (uint32_t)h || ini >= fin) {
        return false;
    }

    uint32_t k = ini;
    int64_t  mejor = 0;
    for (uint32_t i = ini; i < fin; i++) {
        int64_t pend = (int64_t)BP_PPG(b, i - h) - (int64_t)BP_PPG(b, i + h);
        if (pend > mejor) { mejor = pend; k = i; }
    }
    if (mejor <= 0) {
        return false;                              /* no hubo subida */
    }

    /* 5b. PTT */
    *ptt_ms = (float)k * 1000.0f / b->fs_ppg - (float)R * 1000.0f / b->fs_ecg;

    /* 5c. PIR = luz máxima entre latidos (antes de k) / luz mínima en el pulso (después de k) */
    *pir = 1.0f;
    if (b->use_pir) {
        int32_t ih = BP_PPG(b, p_r), il = BP_PPG(b, k);
        for (uint32_t i = p_r; i <= k; i++) {
            if (BP_PPG(b, i) > ih) ih = BP_PPG(b, i);
        }
        uint32_t fin_l = k + (uint32_t)ms_to_samples(300.0f, b->fs_ppg);
        if (fin_l > p_next)     fin_l = p_next;
        if (fin_l >= b->n_ppg)  fin_l = b->n_ppg - 1;
        for (uint32_t i = k; i <= fin_l; i++) {
            if (BP_PPG(b, i) < il) il = BP_PPG(b, i);
        }
        if (il <= 0) {
            return false;                          /* sin nivel DC válido */
        }
        *pir = (float)ih / (float)il;
    }
    return true;
}

/* Paso 6: revisar, promediar y (pasos 7 y 8) calibrar o estimar la presión */
static bool bp_review(BpEstimator* b, float ptt, float pir, BP_Result* r) {
    if (ptt < BP_PTT_MIN_MS || ptt > BP_PTT_MAX_MS) return false;          /* fuera de rango */
    if (b->use_pir && (pir <= 1.0f || pir > BP_PIR_MAX)) return false;
    if (b->n_prom == BP_NPROM && fabsf(ptt - b->ptt_avg) > BP_ARTIFACT_FRAC * b->ptt_avg) {
        return false;                                                       /* artefacto */
    }

    b->ptt_hist[b->i_prom] = ptt;
    b->pir_hist[b->i_prom] = pir;
    b->i_prom = (b->i_prom + 1) % BP_NPROM;
    if (b->n_prom < BP_NPROM) b->n_prom++;
    float sp = 0.0f, si = 0.0f;
    for (int j = 0; j < b->n_prom; j++) { sp += b->ptt_hist[j]; si += b->pir_hist[j]; }
    b->ptt_avg = sp / (float)b->n_prom;
    b->pir_avg = si / (float)b->n_prom;

    if (b->calibrating) {
        b->sum_ptt += ptt;
        b->sum_pir += pir;
        if (++b->n_cal >= BP_NCAL) {
            b->ptt0 = b->sum_ptt / (float)b->n_cal;
            b->pir0 = b->sum_pir / (float)b->n_cal;
            b->calibrating = false;
            b->calibrated  = true;
            r->cal_done    = true;
        }
    }
    if (b->calibrated) {
        bp_model(b, b->ptt_avg, b->pir_avg, &r->sbp, &r->dbp);
    }
    return true;
}

BP_Result bp_on_beat(BpEstimator* b) {
    BP_Result r;
    memset(&r, 0, sizeof(r));

    /* Paso 4: el pico R real es el máximo del ECG antes del aviso del Pan-Tompkins */
    uint32_t m = b->n_ecg - 1;
    if (b->n_ecg == 0 || m < (uint32_t)ms_to_samples(500.0f, b->fs_ecg)) {
        return r;                                  /* ignora el arranque */
    }
    uint32_t ini = m - (uint32_t)ms_to_samples(400.0f, b->fs_ecg);
    uint32_t min_ini = b->r_prev + (uint32_t)ms_to_samples(250.0f, b->fs_ecg);
    if (b->has_r && ini < min_ini) ini = min_ini;
    if (ini > m) ini = m;

    uint32_t R = ini;
    for (uint32_t i = ini; i <= m; i++) {
        if (BP_ECG(b, i) > BP_ECG(b, R)) R = i;
    }

    float ptt, pir;
    if (b->has_r && bp_measure_beat(b, b->r_prev, R, &ptt, &pir)) {
        r.ptt_ms = ptt;
        r.pir    = pir;
        r.beat   = bp_review(b, ptt, pir, &r);
    }
    b->r_prev = R;
    b->has_r  = true;

    r.ptt_avg     = b->ptt_avg;
    r.pir_avg     = b->pir_avg;
    r.calibrating = b->calibrating;
    r.calibrated  = b->calibrated;
    return r;
}


