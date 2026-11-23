/* SPDX-License-Identifier: GPL-3.0-only */
/* Dusk: the tape (the song as it is kept), the sequencer and the sound. All of it runs in the audio interrupt,
 * in control blocks of 16 frames: the sequencer's clock, the envelopes and the LFOs once a block, the oscillators
 * and the effects every frame. Integers only (docs/RULES.md).
 *
 *   keys     8 voices, bass 2, topline 3: an FM pair (a modulator on a carrier) and a "tine" partial each
 *   drums    kick, snare, hat, perc: a sine with a falling pitch, noise through a filter, a click
 *   per track   volume, a low-pass (TONE); the keys a tremolo, the bass a drive, the topline a ping-pong echo
 *   the room    the kick ducks the rest (PUMP), a reverb at half the rate (SPACE)
 *   the tape    saturation (DRIVE), wow and flutter and dropouts (WOBBLE), crackle and hiss (VINYL), rain (RAIN),
 *               a low-pass (TONE), a sample-and-hold (CRUSH)
 *
 * The main loop writes the tape (beat) and sends commands (eng_cmd); the interrupt reads them. A byte changed under
 * the sequencer is heard a step later at worst; nothing here can be left half-written in a way that matters. */

enum { T_KEYS, T_BASS, T_DRUMS, T_LEAD, NTRACK };
enum { FX_WOBBLE, FX_VINYL, FX_TONE, FX_DRIVE, FX_SPACE, FX_PUMP, FX_CRUSH, FX_RAIN, NFX };
#define NSCENE 4u
#define BEAT_VER 1u
#define BPM_MIN 60u
#define BPM_MAX 120u
#define MEL_TIE 0x80u

typedef struct {
    uint8_t bars;                               /* 1, 2, 4 or 8 */
    uint8_t clip[NTRACK];                       /* 0: its own (below); 1..: a factory clip */
    uint8_t pad[3];
    uint8_t chord[16];                          /* a half bar each: 0 the chord before goes on, 1..27 that key's */
    uint8_t bass[16];                           /* a 16th each: 0, 1 root, 2 fifth, 3 octave, 4 approach */
    uint8_t drum[4][16];                        /* 0, 1 a ghost, 2 a hit */
    uint8_t mel[128];                           /* a 16th each: 0 a rest, MEL_TIE held on, else the note (MIDI, in C) */
} scene_t;

typedef struct {
    uint8_t ver, bpm, key, swing;               /* key: 0..11, 5 = C; swing: 50..70 % */
    uint8_t snd[NTRACK], vol[NTRACK], tone[NTRACK], k3[NTRACK], k4[NTRACK];   /* 0..32 (k3 of the keys: the rhythm) */
    uint8_t fx[NFX];                            /* 0..32 */
    uint8_t mute, scene, song_on, oct[NTRACK];  /* oct: 0..2, 1 = as written */
    uint8_t chain[16];                          /* the song: 0 nothing; else scene + 1, the muted tracks << 4 */
    char name[16];
    scene_t sc[NSCENE];
} beat_t;
static beat_t beat;

/* ---- reading the tape: a scene's own clips or the factory's */
static uint32_t sc_chord_raw(const scene_t *s, uint32_t slot)
{
    uint32_t c = s->clip[T_KEYS];
    c = c ? PROG[(c - 1u) % NPROG][slot & 15u] : s->chord[slot & 15u];
    return c > 27u ? 0 : c;
}
static uint32_t sc_chord(const scene_t *s, uint32_t slot)   /* the chord that sounds in this half bar */
{
    uint32_t n = s->bars * 2u, i, c;
    if (!n) return 0;
    for (i = 0; i < n; i++)
        if ((c = sc_chord_raw(s, (slot + n - i) % n)) != 0)
            return c;
    return 0;
}
static uint32_t sc_bass(const scene_t *s, uint32_t step)
{
    uint32_t c = s->clip[T_BASS];
    return c ? BASSP[(c - 1u) % NBASSP][step & 15u] : s->bass[step & 15u];
}
static uint32_t sc_drum(const scene_t *s, uint32_t v, uint32_t step)
{
    uint32_t c = s->clip[T_DRUMS];
    return c ? GROOVE[(c - 1u) % NGROOVE][(v & 3u) * 16u + (step & 15u)] : s->drum[v & 3u][step & 15u];
}
static uint32_t sc_mel(const scene_t *s, uint32_t pos)
{
    uint32_t c = s->clip[T_LEAD];
    return c ? MELC[(c - 1u) % NMELC][pos & 127u] : s->mel[pos & 127u];
}
static uint32_t chord_root(uint32_t c) { return (4u + c) % 12u; }   /* key c (1..27; 1 = F3), in C */
static int32_t mod12(int32_t v) { v %= 12; return v < 0 ? v + 12 : v; }

/* the four notes of chord c: each pitch class where it falls in one octave from F3 (so one chord leads into the
 * next by the smallest steps), lowest first */
static void chord_notes(uint32_t c, uint8_t *n)
{
    const ctype_t *t = &CTYPE[BANK[(c - 1u) % 27u]];
    int32_t lo = 53 + 12 * ((int32_t)beat.oct[T_KEYS] - 1), root = (int32_t)chord_root(c) + beat.key - 5;
    uint32_t i, j;
    for (i = 0; i < 4u; i++) {
        uint8_t v = (uint8_t)(lo + mod12(root + t->pc[i] - lo));
        for (j = i; j && n[j - 1u] > v; j--)
            n[j] = n[j - 1u];
        n[j] = v;
    }
}
/* a factory topline's note over chord c: itself, or the nearest that fits */
static uint32_t mel_fit(uint32_t note, uint32_t c)
{
    static const int8_t TRY[5] = {0, -1, 1, -2, 2};
    uint32_t mask, i;
    if (!c) return note;
    mask = CTYPE[BANK[(c - 1u) % 27u]].mask;
    for (i = 0; i < 5u; i++)
        if (mask >> mod12((int32_t)note + TRY[i] - (int32_t)chord_root(c)) & 1u)
            return (uint32_t)((int32_t)note + TRY[i]);
    return note;
}
static uint32_t bass_note(const scene_t *s, uint32_t slot, uint32_t type)
{
    uint32_t c = sc_chord(s, slot);
    int32_t n;
    if (!c) return 0;
    n = 31 + mod12((int32_t)chord_root(c) + beat.key - 5 - 31);     /* the root, G1 .. F#2 */
    if (type == 2u) n += n + 7 > 45 ? -5 : 7;
    else if (type == 3u) n += 12;
    else if (type == 4u) {
        uint32_t c2 = sc_chord(s, (slot + 1u) % (s->bars * 2u));
        if (c2) n = 30 + mod12((int32_t)chord_root(c2) + beat.key - 5 - 31);   /* a semitone under the next root */
    }
    return (uint32_t)(n + 12 * ((int32_t)beat.oct[T_BASS] - 1));
}

/* ---- the engine's state */
#define CH 16u
#define E_ONE (1 << 24)
enum { ST_OFF, ST_ATT, ST_DEC, ST_REL };
typedef struct {
    const snd_t *s;
    uint32_t phc, phm, pht, incc, incm, inct, basec, basem, lfo, gate, age, on;
    int32_t env, idx, idx1, tine, amp, vel, pl, pr;
    uint8_t stage, note, live, track;
} voice_t;
#define NVOICE 13u
static voice_t vc[NVOICE];
static const uint8_t V0[NTRACK] = {0, 8, 0, 10}, VN[NTRACK] = {8, 2, 0, 3};
static const uint16_t TRK_GAIN[NTRACK] = {60, 35, 256, 29};   /* a voice's level, Q8 */

typedef struct {
    const drum_t *d;
    uint32_t ph, inc;
    int32_t pe, se, ne, nt, as, an, cl, lp, bp, gain;
    uint8_t on, att;
} dvoice_t;
static dvoice_t dv[4];

typedef struct { uint16_t delay, gate; uint8_t type, a, b, c; } pev_t;
#define NPEV 48u
static pev_t pev[NPEV];
enum { PE_NONE, PE_NOTE, PE_DRUM };

/* commands from the main loop: a ring, one writer */
enum { C_NOTE_ON = 1, C_NOTE_OFF, C_CHORD_ON, C_CHORD_OFF, C_DRUM, C_PLAY, C_STOP, C_PANIC };
static volatile uint32_t cq[32];
static volatile uint32_t cq_w, cq_r;
static void eng_cmd(uint32_t type, uint32_t a, uint32_t b, uint32_t c)
{
    cq[cq_w & 31u] = type | a << 8 | b << 16 | c << 24;
    cq_w++;
}

/* the sequencer: written here, read by the main loop */
static volatile uint32_t sq_run, sq_ticks, sq_skip;   /* sq_ticks: a step each; sq_skip: (main) pos + 1 not to play */
static volatile uint8_t sq_scene, sq_bar, sq_step, sq_slot, sq_late;
static volatile uint8_t sq_queue = 0xFF;        /* (main) the scene to go to at the next bar */
static volatile uint32_t trk_hits[NTRACK];      /* notes started, a track each (the screen's lamps) */
static volatile uint32_t eng_off, eng_hold;     /* (main) silence: the updater; a save (fades out, eng_held when out) */
static volatile uint32_t eng_held;
static uint32_t sq_acc, sq_half, sq_pair = 4456421u, sq_thr, sq_stepch = 544u, rnd_s = 0x1234ABCDu, nz_s = 0x9E3779B9u;
static uint32_t age_ctr;

static uint32_t rnd(void) { rnd_s ^= rnd_s << 13; rnd_s ^= rnd_s >> 17; rnd_s ^= rnd_s << 5; return rnd_s; }
static inline int32_t noise16(void)
{
    nz_s ^= nz_s << 13; nz_s ^= nz_s >> 17; nz_s ^= nz_s << 5;
    return (int16_t)nz_s;
}
static inline int32_t sclip(int32_t x)          /* a soft clip: 1.5 x - 0.5 x^3, full scale in, full scale out */
{
    if (x > 32767) x = 32767; else if (x < -32767) x = -32767;
    return (3 * x - (((x * x) >> 15) * x >> 15)) >> 1;
}
static inline int32_t sat16(int32_t x) { return x > 32767 ? 32767 : x < -32767 ? -32767 : x; }
static inline int32_t sat17(int32_t x) { return x > 65535 ? 65535 : x < -65535 ? -65535 : x; }
/* a one-pole low-pass: k Q14, x within +-65535 (so the product fits) */
#define LPF(y, x, k) ((y) += (((x) - (y)) * (k)) >> 14)

/* ---- voices */
static void voice_on(uint32_t track, uint32_t note, uint32_t vel, uint32_t gate, int32_t pan, uint32_t live)
{
    const snd_t *s = track == T_KEYS ? &SND_KEYS[beat.snd[T_KEYS] % NKEYS] :
                     track == T_BASS ? &SND_BASS[beat.snd[T_BASS] % NBASS] : &SND_LEAD[beat.snd[T_LEAD] % NLEAD];
    voice_t *v = 0, *p;
    uint32_t i, g;
    if (note > 127u || !VN[track]) return;
    for (i = 0; i < VN[track]; i++) {           /* a free voice, the same note again, the oldest let go, the oldest */
        p = &vc[V0[track] + i];
        if (p->stage != ST_OFF && p->note == note) { v = p; break; }
    }
    for (i = 0; !v && i < VN[track]; i++)
        if (vc[V0[track] + i].stage == ST_OFF) v = &vc[V0[track] + i];
    for (i = 0; !v && i < VN[track]; i++) {
        p = &vc[V0[track] + i];
        if (p->stage == ST_REL && (!v || p->age < v->age)) v = p;
    }
    if (!v) {
        v = &vc[V0[track]];
        for (i = 1; i < VN[track]; i++)
            if (vc[V0[track] + i].age < v->age) v = &vc[V0[track] + i];
    }
    if (vel > 127u) vel = 127u;
    v->s = s;
    v->basec = v->incc = NOTE_INC[note];
    v->basem = v->incm = (uint32_t)(((uint64_t)v->basec * s->rm) >> 8);
    v->inct = (uint32_t)(((uint64_t)v->basec * s->rt) >> 8);
    if (v->stage == ST_OFF) {
        v->phc = v->phm = v->pht = 0;
        v->amp = 0;
        v->env = 0;
    }
    v->vel = (int32_t)(vel * 258u);
    v->idx1 = s->i1;
    v->idx = s->i1 + (s->i0 - s->i1) * (int32_t)(32u + vel) / 128;
    v->tine = (v->inct >> 28) >= 5u ? 0 : (int32_t)(s->tine * (int32_t)vel) >> 7;   /* (not above ~14 kHz) */
    g = (uint32_t)s->gain * TRK_GAIN[track] >> 8;
    v->pl = (int32_t)(g * (uint32_t)(64 - pan)) >> 6;
    v->pr = (int32_t)(g * (uint32_t)(64 + pan)) >> 6;
    v->stage = ST_ATT;
    v->gate = gate;
    v->live = (uint8_t)live;
    v->note = (uint8_t)note;
    v->track = (uint8_t)track;
    v->lfo = 0;
    v->on = 0;
    v->age = ++age_ctr;
    trk_hits[track]++;
}
static void voices_release(uint32_t track, uint32_t live, uint32_t note)   /* note 255: any */
{
    uint32_t i;
    for (i = 0; i < VN[track]; i++) {
        voice_t *v = &vc[V0[track] + i];
        if (v->stage != ST_OFF && v->live == live && (note == 255u || v->note == note)) {
            v->stage = ST_REL;
            v->gate = 0;
        }
    }
}

/* a block of a voice into l (and r, the keys) */
static void voice_run(voice_t *v, int32_t *l, int32_t *r)
{
    const snd_t *s = v->s;
    int32_t tgt, damp, amp = v->amp, tl, pl = v->pl, pr = v->pr, vib;
    uint32_t i, phc = v->phc, phm = v->phm, pht = v->pht, idx, incc, incm, inct = v->inct;
    switch (v->stage) {
    case ST_ATT:
        v->env += s->att;
        if (v->env >= E_ONE) { v->env = E_ONE; v->stage = ST_DEC; }
        break;
    case ST_DEC:
        v->env -= (int32_t)(((int64_t)(v->env - s->sus) * s->dk) >> 16);
        break;
    default:
        v->env -= (int32_t)(((int64_t)v->env * s->rk) >> 16) + 512;
        if (v->env < 0) v->env = 0;
        break;
    }
    v->idx -= (int32_t)(((int64_t)(v->idx - v->idx1) * s->ik) >> 16);
    v->tine -= (int32_t)(((uint32_t)v->tine * s->tk) >> 16);
    if (v->gate && !--v->gate) v->stage = ST_REL;
    v->on++;
    vib = s->vib + (v->track == T_LEAD ? beat.k4[T_LEAD] * 8 : 0);
    if (vib) {                                  /* a vibrato, coming in over the first 300 ms */
        int32_t d = (LSIN[v->lfo >> 20] * vib) >> 15;
        if (v->on < 800u) d = d * (int32_t)v->on / 800;
        v->lfo += s->vrate ? s->vrate : 8103263u;
        v->incc = v->basec + (uint32_t)(((int32_t)(v->basec >> 10) * d) >> 5);
        v->incm = v->basem + (uint32_t)(((int32_t)(v->basem >> 10) * d) >> 5);
    }
    incc = v->incc; incm = v->incm;
    tgt = ((v->env >> 9) * (v->vel >> 1)) >> 14;
    damp = (tgt - amp) / (int32_t)CH;
    tl = (v->tine * (v->env >> 12)) >> 12;
    idx = (uint32_t)v->idx;
    if (r) {
        for (i = 0; i < CH; i++) {
            int32_t o;
            phm += incm;
            phc += incc;
            o = (LSIN[(phc + (uint32_t)LSIN[phm >> 20] * idx) >> 20] * amp) >> 15;
            amp += damp;
            if (tl) { pht += inct; o += (LSIN[pht >> 20] * tl) >> 15; }
            l[i] += (o * pl) >> 8;
            r[i] += (o * pr) >> 8;
        }
    } else {
        pl += pr;
        for (i = 0; i < CH; i++) {
            int32_t o;
            phm += incm;
            phc += incc;
            o = (LSIN[(phc + (uint32_t)LSIN[phm >> 20] * idx) >> 20] * amp) >> 15;
            amp += damp;
            if (tl) { pht += inct; o += (LSIN[pht >> 20] * tl) >> 15; }
            l[i] += (o * pl) >> 9;
        }
    }
    v->phc = phc; v->phm = phm; v->pht = pht; v->amp = amp;
    if (v->stage == ST_REL && !v->env && amp < 24 && amp > -24) {
        v->stage = ST_OFF;
        v->amp = 0;
    }
}

/* ---- drums */
static int32_t duck_e;                          /* the kick's shadow on the rest, Q15 */
static void drum_on(uint32_t i, uint32_t vel)
{
    dvoice_t *v = &dv[i & 3u];
    const drum_t *d = &KIT[beat.snd[T_DRUMS] % NKIT].d[i & 3u];
    static const uint16_t G[4] = {80, 55, 24, 32};
    v->d = d;
    v->ph = 0;
    v->pe = 32767;
    v->se = (d->slvl * (int32_t)vel) >> 7;
    v->nt = (d->nlvl * (int32_t)vel) >> 7;
    v->att = d->natt != 0;
    v->ne = v->att ? 0 : v->nt;
    v->cl = (d->click * (int32_t)vel) >> 7;
    v->gain = G[i & 3u];
    v->on = 1;
    if (!(i & 3u)) duck_e = 32767;
    if ((i & 3u) == 2u && dv[3].on && dv[3].d->nlvl && !dv[3].d->slvl)   /* the hat closes an open one */
        dv[3].ne >>= 3, dv[3].nt = 0, dv[3].att = 0;
    trk_hits[T_DRUMS]++;
}
static void drum_run(dvoice_t *v, int32_t *out)
{
    const drum_t *d = v->d;
    int32_t se, ne, as = v->as, an = v->an, das, dan, lp = v->lp, bp = v->bp, cl = v->cl, ff = d->ff, fq = d->fq;
    int32_t drive = d->drive, gain = v->gain, dinc;
    uint32_t i, ph = v->ph, inc = v->inc, tinc, ft = d->ftype;
    v->pe -= (int32_t)(((uint32_t)v->pe * d->pk) >> 16);
    tinc = d->f1 + (uint32_t)(((int64_t)((int32_t)d->f0 - (int32_t)d->f1) * v->pe) >> 15);
    if (!inc) inc = tinc;
    dinc = ((int32_t)tinc - (int32_t)inc) / (int32_t)CH;
    v->se -= (int32_t)(((uint32_t)v->se * d->sk) >> 16);
    if (v->att) {
        v->ne += (v->nt * (int32_t)d->natt) >> 15;
        if (v->ne >= v->nt) { v->ne = v->nt; v->att = 0; }
    } else {
        v->ne -= (int32_t)(((uint32_t)v->ne * d->nk) >> 16);
    }
    se = v->se; ne = v->ne;
    das = (se - as) / (int32_t)CH;
    dan = (ne - an) / (int32_t)CH;
    for (i = 0; i < CH; i++) {
        int32_t o = 0;
        if (se | as) {
            ph += inc;
            inc += (uint32_t)dinc;
            o = (LSIN[ph >> 20] * as) >> 15;
            as += das;
        }
        if (ne | an) {
            int32_t n = noise16() >> 2, hp;
            lp += (ff * bp) >> 15;
            hp = n - lp - ((fq * bp) >> 15);
            bp += (ff * hp) >> 15;
            o += ((ft == 0 ? lp : ft == 1u ? bp : hp) * an) >> 13;
            an += dan;
        }
        if (cl) {
            o += (noise16() * cl) >> 15;
            cl -= (cl >> 6) + 1;
            if (cl < 0) cl = 0;
        }
        if (drive != 256) o = sclip((o * drive) >> 8);
        out[i] += (o * gain) >> 8;
    }
    v->ph = ph; v->inc = inc; v->as = as; v->an = an; v->lp = lp; v->bp = bp; v->cl = cl;
    if (v->se < 12 && v->ne < 12 && !cl && !v->att && as < 12 && an < 12) {
        v->on = 0;
        v->inc = 0; v->as = v->an = 0; v->lp = v->bp = 0;
    }
}

/* ---- events a little later: the strum, the lazy snare */
static void pend(uint32_t delay, uint32_t type, uint32_t a, uint32_t b, uint32_t c, uint32_t gate)
{
    uint32_t i;
    for (i = 0; i < NPEV; i++)
        if (!pev[i].type) {
            pev[i].delay = (uint16_t)(delay > 65535u ? 65535u : delay);
            pev[i].gate = (uint16_t)(gate > 65535u ? 65535u : gate);
            pev[i].a = (uint8_t)a; pev[i].b = (uint8_t)b; pev[i].c = (uint8_t)c;
            pev[i].type = (uint8_t)type;
            return;
        }
}
static void pend_run(void)
{
    uint32_t i;
    for (i = 0; i < NPEV; i++) {
        pev_t *e = &pev[i];
        if (!e->type) continue;
        if (e->delay) { e->delay--; continue; }
        if (e->type == PE_NOTE)
            voice_on(e->a & 3u, e->b, e->c, e->gate, e->a & 3u ? 0 : ((int32_t)(e->a >> 2) - 2) * 12 + 6,
                     e->gate ? 0 : 2u);
        else
            drum_on(e->a, e->c);
        e->type = PE_NONE;
    }
}

/* a chord, the way kind says: strummed up or down, its upper three, one note of it, its lowest */
enum { K_BLOCK, K_DOWN, K_UPPER, K_ARP, K_LOW };
static void chord_play(uint32_t c, uint32_t len, uint32_t vel, uint32_t kind, uint32_t arg, uint32_t held)
{
    uint8_t n[4];
    uint32_t spread = beat.k4[T_KEYS] * 3u, gate = held ? 0 : len * sq_stepch - sq_stepch / 4u, i, k = 0;
    chord_notes(c, n);
    for (i = 0; i < 4u; i++) {
        uint32_t j = kind == K_DOWN ? 3u - i : i, v;
        if ((kind == K_UPPER && !j) || (kind == K_ARP && j != (arg & 3u)) || (kind == K_LOW && j))
            continue;
        v = vel * (218u + (rnd() & 31u)) >> 8;                  /* each note a little softer or not */
        pend(k * spread + (spread ? rnd() % (spread / 2u + 1u) : 0), PE_NOTE, T_KEYS | j << 2, n[j], v, gate);
        k++;
    }
}

/* ---- the sequencer: a step */
static void sq_fire(void)
{
    const scene_t *sc = &beat.sc[sq_scene & 3u];
    uint32_t step = sq_step, bar = sq_bar, slot = bar * 2u + (step >> 3), i, c, t, v;
    uint32_t mute = beat.mute | (beat.song_on ? beat.chain[sq_slot & 15u] >> 4 : 0);
    sq_ticks++;
    if (!(mute & 1u) && (c = sc_chord(sc, slot)) != 0) {        /* the keys: the rhythm on the chord of this half bar */
        const comp_t *cp = &COMP[beat.k3[T_KEYS] % NCOMP];
        uint32_t changed = (!(step & 7u) && sc_chord_raw(sc, slot)) || (!bar && !step), hit = 0;
        if (changed)
            voices_release(T_KEYS, 0, 255u);
        for (i = 0; i < cp->n; i++)
            if (cp->ev[i][0] == step) {
                chord_play(c, cp->ev[i][1], cp->ev[i][2], cp->ev[i][3], cp->ev[i][4], 0);
                hit = 1;
            }
        if (changed && !hit && cp->implicit) {  /* a new chord between the rhythm's hits: it sounds, until the next */
            uint32_t len = 8u - (step & 7u), s2 = slot + 1u;
            while (s2 < sc->bars * 2u && !sc_chord_raw(sc, s2) && len < 32u) { len += 8u; s2++; }
            chord_play(c, cp->n ? 4u : len, 96, K_BLOCK, 0, 0);
        }
    }
    if (!(mute & 2u) && (t = sc_bass(sc, step)) != 0 && (c = bass_note(sc, slot, t)) != 0) {
        uint32_t len = 1;
        while (len < 16u && !sc_bass(sc, step + len)) len++;
        voices_release(T_BASS, 0, 255u);
        voice_on(T_BASS, c, 92u + (rnd() & 15u), len * sq_stepch * (6u + beat.k3[T_BASS] * 26u / 32u) / 32u, 0, 0);
    }
    if (!(mute & 4u)) {
        uint32_t lazy = beat.k4[T_DRUMS], fill = beat.k3[T_DRUMS];
        uint32_t last = sc->bars >= 4u ? (bar & 3u) == 3u : sc->bars == 2u && bar == 1u;
        for (v = 0; v < 4u; v++) {
            uint32_t lv = sc_drum(sc, v, step), d = 0;
            if (!lv && fill && last && step >= 10u) {           /* the last bar: a few more, as the fill says */
                uint32_t r = rnd() & 255u;
                if (v == 1u && (step & 1u) && r < fill * 3u) lv = 1;
                else if (v == 0u && step == 14u && r < fill * 4u) lv = 1;
                else if (v == 3u && step == 14u && r < fill * 5u) lv = 2;
            }
            if (!lv) continue;
            if (lazy) d = v == 1u ? lazy * 2u : rnd() % (lazy / (v == 2u ? 2u : 6u) + 1u);
            pend(d, PE_DRUM, v, 0, (lv == 2u ? 104u : 44u) * (208u + (rnd() & 47u)) >> 8, 0);
        }
    }
    if (!(mute & 8u)) {
        uint32_t pos = bar * 16u + step, raw = sc_mel(sc, pos), n = raw & 0x7Fu;
        if (sq_skip == pos + 1u) {
            sq_skip = 0;
        } else if (n && !(raw & MEL_TIE)) {
            uint32_t len = 1;
            while (pos + len < sc->bars * 16u && sc_mel(sc, pos + len) == MEL_TIE) len++;
            if (sc->clip[T_LEAD]) n = mel_fit(n, sc_chord(sc, slot));
            voice_on(T_LEAD, (uint32_t)((int32_t)n + beat.key - 5), 84u + (rnd() & 31u), len * sq_stepch - 3u, 6, 0);
        }
    }
}
static void sq_next_slot(void)                  /* the song: the next scene in it (round again after the last) */
{
    uint32_t i, s = sq_slot;
    for (i = 0; i < 16u; i++) {
        s = (s + 1u) & 15u;
        if (beat.chain[s] & 7u) break;
    }
    if (beat.chain[s] & 7u) {
        sq_slot = (uint8_t)s;
        sq_scene = (uint8_t)(((beat.chain[s] & 7u) - 1u) & 3u);
    }
}
static void sq_start(uint32_t slot)
{
    sq_acc = sq_half = 0;
    sq_bar = sq_step = 0;
    sq_queue = 0xFF;
    if (beat.song_on) {
        sq_slot = (uint8_t)((slot + 15u) & 15u);
        sq_next_slot();
    } else {
        sq_scene = beat.scene & 3u;
    }
    sq_run = 1;
    sq_fire();
}
static void sq_clock(void)                      /* a control block on */
{
    uint32_t bars = beat.sc[sq_scene & 3u].bars;
    sq_acc += CH << 8;
    if (!sq_half && sq_acc >= sq_thr) {         /* the off 16th: later by the swing */
        sq_half = 1;
        sq_step++;
        sq_fire();
    }
    if (sq_acc >= sq_pair) {
        sq_acc -= sq_pair;
        sq_half = 0;
        if (sq_step >= 15u) {
            sq_step = 0;
            sq_bar++;
            if (!beat.song_on && sq_queue < NSCENE) {           /* the scene asked for: from its top, on the bar */
                sq_scene = sq_queue;
                sq_queue = 0xFF;
                sq_bar = 0;
            } else if (sq_bar >= bars) {
                sq_bar = 0;
                if (beat.song_on) sq_next_slot();
            }
        } else {
            sq_step++;
        }
        sq_fire();
    }
    sq_late = sq_half ? sq_acc - sq_thr > (sq_pair - sq_thr) / 2u : sq_acc > sq_thr / 2u;
}

static void all_off(void)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (vc[i].stage != ST_OFF) { vc[i].stage = ST_REL; vc[i].gate = 0; }
    for (i = 0; i < NPEV; i++)
        pev[i].type = PE_NONE;
}
static void cmd_run(void)
{
    while (cq_r != cq_w) {
        uint32_t m = cq[cq_r & 31u], a = m >> 8 & 255u, b = m >> 16 & 255u, c = m >> 24;
        cq_r++;
        switch (m & 255u) {
        case C_NOTE_ON: voices_release(a & 3u, 1, b); voice_on(a & 3u, b, c, 0, (a & 3u) == T_LEAD ? 6 : 0, 1); break;
        case C_NOTE_OFF: voices_release(a & 3u, 1, b); break;
        case C_CHORD_ON: voices_release(T_KEYS, 2, 255u); chord_play(b, 0, c, K_BLOCK, 0, 1); break;
        case C_CHORD_OFF: voices_release(T_KEYS, 2, 255u); break;
        case C_DRUM: drum_on(a, c); break;
        case C_PLAY: all_off(); sq_start(a); break;
        case C_STOP:
            sq_run = 0;
            for (a = 0; a < NVOICE; a++)
                if (vc[a].stage != ST_OFF && !vc[a].live) { vc[a].stage = ST_REL; vc[a].gate = 0; }
            for (a = 0; a < NPEV; a++) pev[a].type = PE_NONE;
            break;
        default: all_off(); break;
        }
    }
}

/* ---- the effects' memory (the pool: zero at boot) */
#define ECHO_MAX 16640u                         /* a dotted eighth at 60 BPM, at half the rate */
static int16_t echo_l[ECHO_MAX] __attribute__((section(".pool")));
static int16_t echo_r[ECHO_MAX] __attribute__((section(".pool")));
static int16_t rv_comb[4][968] __attribute__((section(".pool")));
static int16_t rv_ap[4][128] __attribute__((section(".pool")));
static int16_t tape_l[1024] __attribute__((section(".pool")));
static int16_t tape_r[1024] __attribute__((section(".pool")));
static const uint8_t RV_AP[4] = {113, 37, 127, 41};

static struct {
    int32_t k1l, k2l, k1r, k2r, b1, b2, l1, l2, d1, d2, m1l, m2l, m1r, m2r;   /* the low-passes */
    int32_t eol, eor, elp, rvl, rvr, rlp[4], send;
    uint32_t ew, ci[4], ai[4], tw, half;
    uint32_t trem, w1, w2, w3;
    int32_t td, duck, fade;
    int32_t hl, hl2, hr, hr2, c1l, c2l, c1r, c2r;                 /* the record: hiss, crackle */
    int32_t rl, rl2, rr, rr2, rmod, rtgt, pe;                     /* the rain */
    uint32_t pph, pinc, ppan;
    int32_t drop, dtgt;
    uint32_t dcnt, hold;
    int32_t hl_l, hl_r, dcl, dcr;
} fx;

/* the tape's knobs as the frames need them, once an interrupt */
static struct {
    int32_t vol[NTRACK], kt[NTRACK], mt, drv, post, bdrv, trem, emix, efb, space, pump, vinyl, rain;
    uint32_t elen, wob, crush, cshift, crk;
} ep;
static void eng_params(void)
{
    uint32_t i, bpm = beat.bpm < BPM_MIN ? BPM_MIN : beat.bpm > BPM_MAX ? BPM_MAX : beat.bpm, sw = beat.swing;
    uint32_t f;
    if (sw < 50u) sw = 50u; else if (sw > 70u) sw = 70u;
    sq_pair = 338688000u / bpm;                 /* two 16ths, in frames, Q8 */
    sq_thr = sq_pair / 100u * sw;
    sq_stepch = 661500u / bpm / CH;
    ep.elen = 992250u / bpm;
    if (ep.elen >= ECHO_MAX) ep.elen = ECHO_MAX - 1u;
    for (i = 0; i < NTRACK; i++) {
        uint32_t v = beat.vol[i] > 32u ? 32u : beat.vol[i];
        ep.vol[i] = (int32_t)(v * v * 4u);                      /* Q12 */
        ep.kt[i] = TONE_K[beat.tone[i] > 32u ? 32u : beat.tone[i]] >> 1;   /* Q14 */
    }
    f = beat.fx[FX_TONE] > 32u ? 32u : beat.fx[FX_TONE];
    ep.mt = TONE_K[f] >> 1;
    f = beat.fx[FX_DRIVE] > 32u ? 32u : beat.fx[FX_DRIVE];
    ep.drv = (int32_t)(3072u + f * 320u);                       /* into the tape: x 0.75 .. 3.25, Q12 */
    ep.post = (int32_t)(30000u - f * 260u);                     /* and out of it: the louder in, the less out */
    ep.bdrv = (int32_t)(256u + beat.k4[T_BASS] * 40u);
    ep.trem = SND_KEYS[beat.snd[T_KEYS] % NKEYS].trem;
    ep.emix = (int32_t)(beat.k3[T_LEAD] * 700u);
    ep.efb = (int32_t)(9000u + beat.k3[T_LEAD] * 330u);
    ep.space = (int32_t)(beat.fx[FX_SPACE] * 20u);
    ep.pump = (int32_t)(beat.fx[FX_PUMP] * 660u);
    ep.vinyl = beat.fx[FX_VINYL];
    ep.rain = beat.fx[FX_RAIN];
    ep.wob = beat.fx[FX_WOBBLE];
    ep.crush = 1u + beat.fx[FX_CRUSH] / 4u;
    ep.cshift = beat.fx[FX_CRUSH] / 7u;
    ep.crk = beat.fx[FX_VINYL] * 2u / 3u;
}

/* the reverb, a frame at half the rate: four combs, two all-passes a side */
static inline void reverb(int32_t in)
{
    int32_t y[4], a, b, z, v;
    uint32_t c;
    for (c = 0; c < 4u; c++) {
        y[c] = rv_comb[c][fx.ci[c]];
        fx.rlp[c] += ((y[c] - fx.rlp[c]) * 13000) >> 15;
        rv_comb[c][fx.ci[c]] = (int16_t)sat16(in + ((fx.rlp[c] * (int32_t)RV_FB[c]) >> 15));
        if (++fx.ci[c] >= RV_LEN[c]) fx.ci[c] = 0;
    }
    a = y[0] + y[2] + (y[1] >> 1);
    b = y[1] + y[3] + (y[2] >> 1);
    for (c = 0; c < 4u; c++) {
        int32_t *x = c < 2u ? &a : &b;
        z = rv_ap[c][fx.ai[c]];
        v = *x + (z >> 1);
        rv_ap[c][fx.ai[c]] = (int16_t)sat16(v);
        *x = z - (v >> 1);
        if (++fx.ai[c] >= RV_AP[c]) fx.ai[c] = 0;
    }
    fx.rvl = a;
    fx.rvr = b;
}

/* ---- a control block: 16 frames into out (L R L R) */
static void eng_chunk(int32_t *out)
{
    int32_t kl[CH], kr[CH], bs[CH], ld[CH], dr[CH];
    int32_t trl, trr, duck, dduck, td, dtd, drop, ddrop, g;
    uint32_t i;
    for (i = 0; i < CH; i++)
        kl[i] = kr[i] = bs[i] = ld[i] = dr[i] = 0;
    cmd_run();
    if (sq_run)
        sq_clock();
    pend_run();
    for (i = 0; i < NVOICE; i++)
        if (vc[i].stage != ST_OFF)
            voice_run(&vc[i], i < 8u ? kl : i < 10u ? bs : ld, i < 8u ? kr : 0);
    for (i = 0; i < 4u; i++)
        if (dv[i].on)
            drum_run(&dv[i], dr);

    /* the block's slow things: the tremolo, the kick's shadow, the tape's speed, its dropouts, the rain */
    fx.trem += SND_KEYS[beat.snd[T_KEYS] % NKEYS].trate;
    g = (LSIN[fx.trem >> 20] * ep.trem) >> 15;
    trl = 32767 - ep.trem + g;
    trr = 32767 - ep.trem - g;
    duck_e -= (duck_e * 170) >> 16;                             /* ~140 ms */
    g = 32767 - ((duck_e * ep.pump) >> 15);
    duck = fx.duck ? fx.duck : 32767;
    g = duck + ((g - duck) >> 2);                               /* comes down in a few ms, not at once */
    dduck = (g - duck) / (int32_t)CH;
    fx.duck = g;
    fx.w1 += 514245u; fx.w2 += 1714151u; fx.w3 += 10596400u;    /* 0.33, 1.1, 6.8 Hz */
    g = (LSIN[fx.w1 >> 20] * 110 + LSIN[fx.w2 >> 20] * 20 + LSIN[fx.w3 >> 20]) >> 7;   /* +-33500: frames, Q8 */
    g = (300 << 8) + g * (int32_t)ep.wob / 20;                  /* at most +-210 frames: 1 % of the pitch */
    td = fx.td ? fx.td : g;
    dtd = (g - td) / (int32_t)CH;
    fx.td = td + dtd * (int32_t)CH;
    if (!fx.dcnt--) {                                           /* a dropout now and then, deeper with WOBBLE */
        uint32_t r = rnd();
        fx.dcnt = 60u + (r & 255u);
        fx.dtgt = ep.wob > 6u && (r >> 8 & 7u) < 3u ? (int32_t)((r >> 16 & 255u) * (ep.wob - 6u)) : 0;
    }
    drop = fx.drop;
    fx.drop += (fx.dtgt - fx.drop) >> 4;
    ddrop = (fx.drop - drop) / (int32_t)CH;
    drop = 32767 - drop;
    if (ep.rain) {
        if (!(rnd() & 63u)) fx.rtgt = 12000 + (int32_t)(rnd() & 16383u);
        fx.rmod += (fx.rtgt - fx.rmod) >> 6;
        if (!fx.pe && (rnd() & 255u) < 2u + ep.rain / 4u) {      /* a drop on the sill */
            uint32_t r = rnd();
            fx.pe = 1500 + (int32_t)(r & 4095u);
            fx.pinc = 146000000u + (r >> 4 & 0x0FFFFFFFu);      /* 1.5 .. 4.3 kHz */
            fx.ppan = r >> 12 & 255u;
            fx.pph = 0;
        }
    }
    if (eng_hold || eng_off) {
        fx.fade -= 1400;
        if (fx.fade <= 0) { fx.fade = 0; eng_held = 1; }
    } else {
        eng_held = 0;
        if (fx.fade < 32767) fx.fade += 1400;
        if (fx.fade > 32767) fx.fade = 32767;
    }

    for (i = 0; i < CH; i++) {
        int32_t xl, xr, b, l, d, ll, lr, ml, mr, n, p;
        uint32_t pos, i0;
        /* the keys: tone, tremolo */
        LPF(fx.k1l, sat17(kl[i]), ep.kt[T_KEYS]); LPF(fx.k2l, fx.k1l, ep.kt[T_KEYS]);
        LPF(fx.k1r, sat17(kr[i]), ep.kt[T_KEYS]); LPF(fx.k2r, fx.k1r, ep.kt[T_KEYS]);
        xl = (((fx.k2l * trl) >> 15) * ep.vol[T_KEYS]) >> 12;
        xr = (((fx.k2r * trr) >> 15) * ep.vol[T_KEYS]) >> 12;
        /* the bass: tone, drive */
        LPF(fx.b1, sat17(bs[i]), ep.kt[T_BASS]); LPF(fx.b2, fx.b1, ep.kt[T_BASS]);
        b = (sclip((fx.b2 * ep.bdrv) >> 8) * ep.vol[T_BASS]) >> 12;
        /* the topline: tone, echo from side to side (at half the rate) */
        LPF(fx.l1, sat17(ld[i]), ep.kt[T_LEAD]); LPF(fx.l2, fx.l1, ep.kt[T_LEAD]);
        l = (fx.l2 * ep.vol[T_LEAD]) >> 12;
        if (i & 1u) {
            uint32_t rp = fx.ew >= ep.elen ? fx.ew - ep.elen : fx.ew + ECHO_MAX - ep.elen;
            fx.eol = echo_l[rp];
            fx.eor = echo_r[rp];
            fx.elp += ((sat16(l) + ((fx.eor * ep.efb) >> 15) - fx.elp) * 14000) >> 15;
            echo_l[fx.ew] = (int16_t)sat16(fx.elp);
            echo_r[fx.ew] = (int16_t)fx.eol;
            if (++fx.ew >= ECHO_MAX) fx.ew = 0;
        }
        ll = l + ((fx.eol * ep.emix) >> 15);
        lr = l + ((fx.eor * ep.emix) >> 15);
        /* the drums: tone, a soft clip */
        LPF(fx.d1, sat17(dr[i]), ep.kt[T_DRUMS]); LPF(fx.d2, fx.d1, ep.kt[T_DRUMS]);
        d = (sclip(fx.d2) * ep.vol[T_DRUMS]) >> 12;
        /* together: the rest under the kick, the room */
        ml = (((xl + b + ll) * duck) >> 15) + d;
        mr = (((xr + b + lr) * duck) >> 15) + d;
        duck += dduck;
        if (i & 1u)
            reverb(((xl + xr + ll + lr + (ll + lr) / 2 + d / 2) * ep.space) >> 11);
        ml += fx.rvl;
        mr += fx.rvr;
        /* onto the tape: saturation, then the tape read back where its speed puts it */
        ml = sclip((sat17(ml) * ep.drv) >> 12);
        mr = sclip((sat17(mr) * ep.drv) >> 12);
        tape_l[fx.tw] = (int16_t)ml;
        tape_r[fx.tw] = (int16_t)mr;
        pos = (fx.tw << 8) - (uint32_t)td;
        td += dtd;
        i0 = pos >> 8 & 1023u;
        p = (int32_t)(pos & 255u);
        ml = tape_l[i0] + (((tape_l[(i0 + 1u) & 1023u] - tape_l[i0]) * p) >> 8);
        mr = tape_r[i0] + (((tape_r[(i0 + 1u) & 1023u] - tape_r[i0]) * p) >> 8);
        fx.tw = (fx.tw + 1u) & 1023u;
        ml = (((ml * drop) >> 15) * ep.post) >> 15;
        mr = (((mr * drop) >> 15) * ep.post) >> 15;
        drop -= ddrop;
        /* the record: hiss, crackle; the window: rain */
        if (ep.vinyl | ep.rain) {
            int32_t nl, nr;
            n = noise16();
            nl = n;
            nr = (int16_t)(nz_s >> 16);
            if (ep.vinyl) {
                fx.hl += (nl - fx.hl) >> 2; fx.hl2 += (fx.hl - fx.hl2) >> 5;
                fx.hr += (nr - fx.hr) >> 2; fx.hr2 += (fx.hr - fx.hr2) >> 5;
                ml += ((fx.hl - fx.hl2) * ep.vinyl) >> 10;
                mr += ((fx.hr - fx.hr2) * ep.vinyl) >> 10;
                p = 0; g = 0;
                if ((nz_s >> 9 & 0xFFFFu) < ep.crk) {           /* a tick: an impulse into a short ring */
                    int32_t a = (int32_t)((nz_s & 63u) * (nz_s >> 6 & 63u) * (nz_s >> 12 & 31u)) >> 4;
                    a = (a * ep.vinyl) >> 5;
                    if (nz_s & 0x40000000u) p = a; else g = a;
                    if (!(nz_s & 0x30000000u)) p = g = a;
                }
                n = ((27640 * fx.c1l) >> 14) - ((14075 * fx.c2l) >> 14) + p;
                fx.c2l = fx.c1l; fx.c1l = n; ml += n;
                n = ((27640 * fx.c1r) >> 14) - ((14075 * fx.c2r) >> 14) + g;
                fx.c2r = fx.c1r; fx.c1r = n; mr += n;
            }
            if (ep.rain) {
                fx.rl += ((nl - fx.rl) * 3) >> 3; fx.rl2 += (fx.rl - fx.rl2) >> 3;
                fx.rr += ((nr - fx.rr) * 3) >> 3; fx.rr2 += (fx.rr - fx.rr2) >> 3;
                g = (fx.rmod * (int32_t)ep.rain) >> 7;
                ml += ((fx.rl - fx.rl2) * g) >> 15;
                mr += ((fx.rr - fx.rr2) * g) >> 15;
                if (fx.pe) {
                    fx.pph += fx.pinc;
                    n = (((LSIN[fx.pph >> 20] * fx.pe) >> 15) * (int32_t)ep.rain) >> 5;
                    ml += (n * (int32_t)(255u - fx.ppan)) >> 8;
                    mr += (n * (int32_t)fx.ppan) >> 8;
                    fx.pe -= (fx.pe >> 7) + 1;
                    if (fx.pe < 0) fx.pe = 0;
                }
            }
        }
        /* the tape's tone; what an old converter keeps of it */
        LPF(fx.m1l, sat17(ml), ep.mt); LPF(fx.m2l, fx.m1l, ep.mt);
        LPF(fx.m1r, sat17(mr), ep.mt); LPF(fx.m2r, fx.m1r, ep.mt);
        fx.dcl += fx.m2l - (fx.dcl >> 10);                         /* (no DC: the shifts round down) */
        fx.dcr += fx.m2r - (fx.dcr >> 10);
        if (++fx.hold >= ep.crush) {
            fx.hold = 0;
            fx.hl_l = (fx.m2l - (fx.dcl >> 10)) & ~(int32_t)((1u << ep.cshift) - 1u);
            fx.hl_r = (fx.m2r - (fx.dcr >> 10)) & ~(int32_t)((1u << ep.cshift) - 1u);
        }
        out[2u * i] += (fx.hl_l * fx.fade) >> 15;
        out[2u * i + 1u] += (fx.hl_r * fx.fade) >> 15;
    }
}

static void eng_render(int32_t *out, uint32_t n)
{
    uint32_t i;
    eng_params();
    for (i = 0; i + CH <= n; i += CH)
        eng_chunk(out + 2u * i);
}
