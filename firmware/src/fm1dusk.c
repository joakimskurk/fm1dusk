/* SPDX-License-Identifier: GPL-3.0-only */
/* Dusk: a lofi beat maker for the M-VAVE FM-1. A tape is four scenes (A B C D); a scene is one clip on each of
 * four tracks: KEYS (a chord progression and how it is played), BASS (a line under the chords' roots), DRUMS (a
 * groove of kick, snare, hat and perc) and LEAD (a topline). Pick clips from the bank and you have a beat; change
 * one and it becomes your own. The song is scenes in order. It all goes through a tired tape: wobble, vinyl, a
 * low-pass, rain on the window.
 *
 * Three knobs mean the same on every page:
 *   SELECT       the cursor (the lane, the chord, the step, the song's place, the tape on the shelf)
 *   ALGORITHM    the clip of this track (0 "MINE": your own; turning back to it brings it back)
 *   PRESETS      the sound of this track
 * The pages (the button of the page you are on is lit):
 *   HOME         the four lanes. Keys: F G A B the scenes A B C D (hold one, press another: a copy), C D E F the
 *                tracks on / off, the keys above play the lead. SEL: this lane on / off.
 *                KNOBS  tempo, key, swing, bars
 *   ARP          KEYS: every key is a chord (the lower octave the key's own, the upper their restless relatives).
 *                REC lit: the chord goes where the cursor is (playing: where the tape is). SEL: hold the chord before.
 *                KNOBS  volume, tone, rhythm, strum
 *   LFO          BASS: the white keys are the 16 steps of the bar; the first black keys choose what a step plays
 *                (root, fifth, octave, an approach to the next chord).   KNOBS  volume, tone, length, drive
 *   SEQ          DRUMS: the white keys are the 16 steps (hit, ghost, nothing), the first black keys the drum.
 *                KNOBS  volume, tone, fill, lazy
 *   EDIT         LEAD: the keys play, the white ones always in key. REC lit and playing: what you play is kept, on
 *                the 16ths. REC lit and stopped: a key writes an eighth at the cursor; SEL makes it longer, or
 *                takes a note away.   KNOBS  volume, tone, echo, vibrato
 *   FX           the tape and the room; FX again for the other four.
 *                KNOBS  wobble, vinyl, tone, drive / space, pump, crush, rain
 *   GLO          the SONG: 16 places (the white keys, or SELECT); ALGORITHM puts a scene there, the knobs switch its
 *                tracks, SEL turns the song on (PLAY then plays it from the cursor) or back to looping the scene.
 *   SAVE         the TAPES: eight of your own and the factory's. SEL loads, SAVE again keeps yours here.
 * Everywhere:
 *   PLAY         start / stop          REC   write what the keys play (KEYS, LEAD)
 *   ENV          tap the tempo         OCT   this track an octave down / up
 *   SEL held     (a track's page) empty this track's own clip
 *   MIDI in      notes play the lead; start, continue and stop are PLAY
 *
 * The tape as it is, is kept in flash when it has been still for two seconds and nothing plays. The sound, the
 * sequencer and the tape's format: lofi_engine.h; the screen: lofi_draw.h; the music: tools/gen_lofi.py. */
#define APP_NAME "DUSK"
#define STORE_MAGIC 0x4B535544u                 /* "DUSK": this firmware's own data (store.h) */
#define CV_MAX (240u * 72u)
#include "platform.c"
#include "lofi_gen.h"
#include "lofi_engine.h"
#include "lofi_draw.h"

static void app_audio(int32_t *out, uint32_t n)
{
    if (eng_off && eng_held)                    /* the updater has the chip */
        return;
    eng_render(out, n);
}

/* ---- tapes */
static uint32_t dirty, t_change, rd_head = 1, rd_body = 1, rd_knob = 1;
#define CHG() (dirty = 1, t_change = fm1_ms)

static void toast(const char *a, const char *b)
{
    scpy(ui.toast_a, a, sizeof ui.toast_a);
    scpy(ui.toast_b, b, sizeof ui.toast_b);
    ui.toast_t = fm1_ms | 1u;
    rd_knob = 1;
}

static void beat_fix(beat_t *b)                 /* whatever was read: every value where the engine expects it */
{
    static const uint8_t NCLIP[NTRACK] = {NPROG, NBASSP, NGROOVE, NMELC};
    static const uint8_t NSND[NTRACK] = {NKEYS, NBASS, NKIT, NLEAD};
    uint32_t i, t, k;
    b->ver = BEAT_VER;
    if (b->bpm < BPM_MIN) b->bpm = BPM_MIN; else if (b->bpm > BPM_MAX) b->bpm = BPM_MAX;
    if (b->key > 11u) b->key = 5;
    if (b->swing < 50u) b->swing = 50; else if (b->swing > 70u) b->swing = 70;
    for (t = 0; t < NTRACK; t++) {
        b->snd[t] %= NSND[t];
        if (b->vol[t] > 32u) b->vol[t] = 32;
        if (b->tone[t] > 32u) b->tone[t] = 32;
        if (b->k3[t] > 32u) b->k3[t] = 32;
        if (b->k4[t] > 32u) b->k4[t] = 32;
        if (b->oct[t] > 2u) b->oct[t] = 1;
    }
    b->k3[T_KEYS] %= NCOMP;
    for (i = 0; i < NFX; i++)
        if (b->fx[i] > 32u) b->fx[i] = 32;
    b->mute &= 15u;
    b->scene &= 3u;
    b->song_on = b->song_on != 0;
    for (i = 0; i < 16u; i++)
        if ((b->chain[i] & 7u) > NSCENE) b->chain[i] = 0;
    b->name[15] = 0;
    for (i = 0; i < NSCENE; i++) {
        scene_t *s = &b->sc[i];
        if (s->bars != 1u && s->bars != 2u && s->bars != 4u && s->bars != 8u) s->bars = 4;
        for (t = 0; t < NTRACK; t++)
            if (s->clip[t] > NCLIP[t]) s->clip[t] = 0;
        for (k = 0; k < 16u; k++) {
            if (s->chord[k] > 27u) s->chord[k] = 0;
            if (s->bass[k] > 4u) s->bass[k] = 0;
            for (t = 0; t < 4u; t++)
                if (s->drum[t][k] > 2u) s->drum[t][k] = 0;
        }
    }
}
static beat_t tmp_beat;
static void beat_take(void)                     /* tmp_beat becomes the tape, between two interrupts */
{
    beat_fix(&tmp_beat);
    fm1_irq_off();
    memcpy(&beat, &tmp_beat, sizeof beat);
    fm1_irq_on();
    sq_queue = sq_run && !beat.song_on ? beat.scene : 0xFF;     /* (playing: its scene, from the next bar) */
    ui.slot = ui.step = ui.pos = 0;
    rd_head = rd_body = rd_knob = 1;
}
static void tape_load(uint32_t i)               /* a factory tape */
{
    const tape_t *t = &TAPE[i % NTAPE];
    uint32_t k, s;
    memset(&tmp_beat, 0, sizeof tmp_beat);
    tmp_beat.bpm = t->bpm; tmp_beat.key = t->key; tmp_beat.swing = t->swing;
    for (k = 0; k < NTRACK; k++) {
        tmp_beat.snd[k] = t->snd[k]; tmp_beat.vol[k] = t->vol[k]; tmp_beat.tone[k] = t->tone[k];
        tmp_beat.k3[k] = t->k3[k]; tmp_beat.k4[k] = t->k4[k]; tmp_beat.oct[k] = 1;
    }
    memcpy(tmp_beat.fx, t->fx, NFX);
    memcpy(tmp_beat.chain, t->chain, 16);
    scpy(tmp_beat.name, t->name, sizeof tmp_beat.name);
    for (s = 0; s < NSCENE; s++) {
        tmp_beat.sc[s].bars = t->sc[s][0];
        for (k = 0; k < NTRACK; k++)
            tmp_beat.sc[s].clip[k] = t->sc[s][1u + k];
    }
    beat_take();
}
static void quiet(void)                         /* (an erase stops the interrupts: the sound down first) */
{
    uint32_t t0 = fm1_ms;
    eng_hold = 1;
    while (!eng_held && fm1_ms - t0 < 100u)
        platform_poll();
}

/* a factory clip of track t becomes the scene's own: for the first change to it */
static void fork_clip(uint32_t t)
{
    scene_t *sc = &beat.sc[beat.scene & 3u];
    uint32_t i, v;
    if (!sc->clip[t]) return;
    if (t == T_KEYS) {
        for (i = 0; i < 16u; i++) sc->chord[i] = (uint8_t)sc_chord_raw(sc, i);
    } else if (t == T_BASS) {
        for (i = 0; i < 16u; i++) sc->bass[i] = (uint8_t)sc_bass(sc, i);
    } else if (t == T_DRUMS) {
        for (v = 0; v < 4u; v++)
            for (i = 0; i < 16u; i++) sc->drum[v][i] = (uint8_t)sc_drum(sc, v, i);
    } else {
        for (i = 0; i < 128u; i++) {            /* (as it sounds over this scene's chords) */
            uint32_t m = sc_mel(sc, i);
            if (m && m != MEL_TIE) m = mel_fit(m, sc_chord(sc, (i / 8u) % (sc->bars * 2u)));
            sc->mel[i] = (uint8_t)m;
        }
    }
    sc->clip[t] = 0;
}
static void mel_put(scene_t *sc, uint32_t pos, uint32_t v)   /* a note (or a rest, or held on) at pos: what was
                                                   held on from there ends */
{
    uint32_t p;
    if (pos >= 128u) return;
    sc->mel[pos] = (uint8_t)v;
    for (p = pos + 1u; p < 128u && sc->mel[p] == MEL_TIE; p++)
        sc->mel[p] = 0;
}

/* ---- the panel */
static uint8_t widx[27], bidx[27], wkey[16], bkey[11], held_note[27];
static uint32_t keys_prev, chord_key = 99, aud_key = 99, aud_note, rec_key = 99, rec_pos, t_sel, sel_done, t_tap, n_tap;
static uint32_t t_head, ticks_seen, was_run, keys_was, hits_seen[NTRACK], lamps_shown;
static int is_black(uint32_t k) { return (0x54Au >> ((53u + k) % 12u)) & 1u; }
static uint32_t cur_track(void) { return ui.page >= PG_KEYS && ui.page <= PG_LEAD ? PG_TRACK(ui.page) : ui.track; }

static void adj(uint8_t *p, int32_t e, int32_t lo, int32_t hi)
{
    int32_t v = (int32_t)*p + e;
    *p = (uint8_t)(v < lo ? lo : v > hi ? hi : v);
    CHG();
}
static void wrap(uint8_t *p, int32_t e, int32_t n)
{
    int32_t v = ((int32_t)*p + e) % n;
    *p = (uint8_t)(v < 0 ? v + n : v);
}
static void knobs_fill(void)
{
    static const char *const K3[NTRACK] = {"RHYTHM", "LENGTH", "FILL", "ECHO"};
    static const char *const K4[NTRACK] = {"STRUM", "DRIVE", "LAZY", "VIBRATO"};
    uint32_t i, t;
    for (i = 0; i < 4u; i++) {
        knob[i].name = 0; knob[i].bar = -1; knob[i].val[0] = 0; knob[i].col = P_TXT;
    }
#define KV(i, n, v) (knob[i].name = (n), knob[i].bar = (int8_t)(v), put_u(knob[i].val, (v), 1))
    switch (ui.page) {
    case PG_HOME:
        knob[0].name = "TEMPO"; put_u(knob[0].val, beat.bpm, 1);
        knob[1].name = "KEY"; put_s(knob[1].val, ROOTN[mod12(beat.key - 5)]);
        knob[2].name = "SWING"; put_s(put_u(knob[2].val, beat.swing, 1), "%");
        knob[3].name = "BARS"; put_u(knob[3].val, beat.sc[beat.scene & 3u].bars, 1);
        break;
    case PG_KEYS: case PG_BASS: case PG_DRUMS: case PG_LEAD:
        t = PG_TRACK(ui.page);
        KV(0, "VOLUME", beat.vol[t]); KV(1, "TONE", beat.tone[t]); KV(2, K3[t], beat.k3[t]); KV(3, K4[t], beat.k4[t]);
        if (t == T_KEYS) { knob[2].bar = -1; put_s(knob[2].val, COMP[beat.k3[t] % NCOMP].name); }
        for (i = 0; i < 4u; i++) knob[i].col = TRK_COL[t];
        break;
    case PG_FX:
        for (i = 0; i < 4u; i++) KV(i, FX_NAME[ui.fxrow * 4u + i], beat.fx[ui.fxrow * 4u + i]);
        break;
    case PG_SONG:
        for (i = 0; i < 4u; i++) {
            uint32_t e = beat.chain[ui.song & 15u];
            knob[i].name = TRK_NAME[i];
            put_s(knob[i].val, !(e & 7u) ? "-" : (e >> (4u + i) & 1u) ? "OFF" : "ON");
        }
        break;
    default: break;
    }
#undef KV
}
static void knob_turn(uint32_t i, int32_t e)
{
    uint32_t t;
    scene_t *sc = &beat.sc[beat.scene & 3u];
    switch (ui.page) {
    case PG_HOME:
        if (i == 0) adj(&beat.bpm, e, BPM_MIN, BPM_MAX);
        else if (i == 1u) adj(&beat.key, e, 0, 11);
        else if (i == 2u) adj(&beat.swing, e, 50, 70);
        else {
            uint32_t b = sc->bars;
            b = e > 0 ? (b < 8u ? b * 2u : 8u) : (b > 1u ? b / 2u : 1u);
            sc->bars = (uint8_t)b;
            if (ui.slot >= b * 2u) ui.slot = 0;
            if (ui.pos >= b * 16u) ui.pos = 0;
            CHG();
            rd_body = 1;
        }
        rd_head = 1;
        break;
    case PG_KEYS: case PG_BASS: case PG_DRUMS: case PG_LEAD:
        t = PG_TRACK(ui.page);
        if (i == 2u && t == T_KEYS) { wrap(&beat.k3[t], e, NCOMP); CHG(); }
        else adj(i == 0 ? &beat.vol[t] : i == 1u ? &beat.tone[t] : i == 2u ? &beat.k3[t] : &beat.k4[t], e, 0, 32);
        break;
    case PG_FX:
        adj(&beat.fx[ui.fxrow * 4u + i], e, 0, 32);
        rd_body = 1;
        break;
    case PG_SONG:
        if (beat.chain[ui.song & 15u] & 7u) {
            if (e > 0) beat.chain[ui.song & 15u] &= (uint8_t)~(16u << i);
            else beat.chain[ui.song & 15u] |= (uint8_t)(16u << i);
            CHG();
            rd_body = 1;
        }
        break;
    default: break;
    }
    ui.toast_t = 0;
    rd_knob = 1;
}

static void lead_on(uint32_t k, uint32_t note)  /* key k plays the lead: note as written (in C) */
{
    int32_t n = (int32_t)note + beat.key - 5;
    if (n < 0 || n > 127) return;
    if (held_note[k]) eng_cmd(C_NOTE_OFF, T_LEAD, held_note[k], 0);
    held_note[k] = (uint8_t)n;
    eng_cmd(C_NOTE_ON, T_LEAD, (uint32_t)n, 100);
}
static void step_toggle(uint32_t t, uint32_t step)
{
    scene_t *sc = &beat.sc[beat.scene & 3u];
    fork_clip(t);
    if (t == T_BASS) {
        sc->bass[step] = sc->bass[step] == ui.btype ? 0 : ui.btype;
        if (sc->bass[step] && !sq_run) {
            uint32_t n = bass_note(sc, ui.slot % (sc->bars * 2u), ui.btype);
            if (n) eng_cmd(C_NOTE_HIT, T_BASS, n, 100);
        }
    } else {
        uint8_t *p = &sc->drum[ui.dvoice & 3u][step];
        *p = *p == 0 ? 2 : *p == 2u ? 1 : 0;
        if (*p && !sq_run) eng_cmd(C_DRUM, ui.dvoice, 0, *p == 2u ? 104 : 44);
    }
    ui.step = (uint8_t)step;
    CHG();
}

static void key_down(uint32_t k)
{
    scene_t *sc = &beat.sc[beat.scene & 3u];
    uint32_t w = widx[k], b = bidx[k], i;
    rd_body = 1;
    switch (ui.page) {
    case PG_HOME:
        if (w < 4u) {
            for (i = 0; i < 4u; i++)
                if (i != w && (keys_was >> wkey[i] & 1u)) break;
            if (i < 4u) {                       /* another scene's key is held: a copy of it here */
                char s[8] = {(char)('A' + i), ' ', '>', ' ', (char)('A' + w), 0};
                fm1_irq_off();
                memcpy(&beat.sc[w], &beat.sc[i], sizeof(scene_t));
                fm1_irq_on();
                toast("SCENE COPIED", s);
                CHG();
            }
            beat.scene = (uint8_t)w;
            if (sq_run && !beat.song_on) sq_queue = (uint8_t)w;
            ui.slot = ui.step = ui.pos = 0;
            rd_head = rd_knob = 1;
            return;
        }
        if (w < 8u) {
            beat.mute ^= (uint8_t)(1u << (w - 4u));
            CHG();
            return;
        }
        break;
    case PG_KEYS:
        eng_cmd(C_CHORD_ON, 0, k + 1u, 96);
        chord_key = k;
        if (ui.rec) {
            uint32_t n = sc->bars * 2u, slot = ui.slot % n;
            fork_clip(T_KEYS);
            if (sq_run && sq_scene == beat.scene) {             /* where the tape is (the next, from its last 8th) */
                slot = (sq_bar * 2u + (sq_step >> 3) + ((sq_step & 7u) >= 6u)) % n;
                ui.slot = (uint8_t)slot;
            } else {
                ui.slot = (uint8_t)((slot + 1u) % n);
            }
            sc->chord[slot] = (uint8_t)(k + 1u);
            CHG();
        }
        return;
    case PG_BASS:
        if (w < 16u) { step_toggle(T_BASS, w); return; }
        if (b < 4u) {
            uint32_t n = bass_note(sc, ui.slot % (sc->bars * 2u), b + 1u);
            ui.btype = (uint8_t)(b + 1u);
            if (aud_key != 99u) eng_cmd(C_NOTE_OFF, T_BASS, aud_note, 0);
            if (n) { eng_cmd(C_NOTE_ON, T_BASS, n, 100); aud_key = k; aud_note = n; }
        }
        return;
    case PG_DRUMS:
        if (w < 16u) { step_toggle(T_DRUMS, w); return; }
        if (b < 4u) {
            ui.dvoice = (uint8_t)b;
            eng_cmd(C_DRUM, b, 0, 104);
        }
        return;
    case PG_LEAD: {
        uint32_t note = 53u + k + 12u * beat.oct[T_LEAD] - 12u, n = sc->bars * 16u;
        lead_on(k, note);
        if (!ui.rec) return;
        fork_clip(T_LEAD);
        if (sq_run && sq_scene == beat.scene) {                 /* kept on the nearest 16th */
            uint32_t pos = (sq_bar * 16u + sq_step) % n;
            if (sq_late) {
                pos = (pos + 1u) % n;
                sq_skip = pos + 1u;             /* (it sounds now: not again when the tape gets there) */
            }
            mel_put(sc, pos, note);
            rec_key = k;
            rec_pos = pos;
        } else {                                /* stopped: an eighth at the cursor, the cursor on */
            uint32_t pos = ui.pos % n;
            mel_put(sc, pos, note);
            if (pos + 1u < n) mel_put(sc, pos + 1u, MEL_TIE);
            ui.pos = (uint8_t)((pos + 2u) % n);
        }
        CHG();
        return;
    }
    case PG_SONG:
        if (w < 16u) { ui.song = (uint8_t)w; rd_knob = 1; return; }
        break;
    default: break;
    }
    lead_on(k, 53u + k + 12u * beat.oct[T_LEAD] - 12u);        /* a key with nothing else to do plays the lead */
}
static void key_up(uint32_t k)
{
    if (held_note[k]) {
        eng_cmd(C_NOTE_OFF, T_LEAD, held_note[k], 0);
        held_note[k] = 0;
    }
    if (k == chord_key) { eng_cmd(C_CHORD_OFF, 0, 0, 0); chord_key = 99; }
    if (k == aud_key) {
        eng_cmd(C_NOTE_OFF, T_BASS, aud_note, 0);
        aud_key = 99;
    }
    if (k == rec_key) rec_key = 99;
}

static void clip_turn(int32_t e)                /* ALGORITHM */
{
    static const uint8_t NCLIP[NTRACK] = {NPROG, NBASSP, NGROOVE, NMELC};
    scene_t *sc = &beat.sc[beat.scene & 3u];
    uint32_t t = cur_track();
    char a[20], b[20], *p;
    if (ui.page == PG_FX || ui.page == PG_TAPES) return;
    if (ui.page == PG_SONG) {                   /* the scene at this place of the song (as it is, all tracks on) */
        uint8_t s = beat.chain[ui.song & 15u] & 7u;
        wrap(&s, e, NSCENE + 1);
        beat.chain[ui.song & 15u] = (uint8_t)(s | (s ? beat.chain[ui.song & 15u] & 0xF0u : 0));
        CHG();
        rd_body = rd_knob = 1;
        return;
    }
    wrap(&sc->clip[t], e, NCLIP[t] + 1);
    p = put_s(a, TRK_NAME[t]);
    put_s(p, " CLIP");
    p = put_u(b, sc->clip[t], 2);
    p = put_s(p, " ");
    put_s(p, clip_name(t, sc));
    toast(a, b);
    CHG();
    rd_body = 1;
}
static void snd_turn(int32_t e)                 /* PRESETS */
{
    static const uint8_t NSND[NTRACK] = {NKEYS, NBASS, NKIT, NLEAD};
    uint32_t t = cur_track();
    char a[20], *p;
    if (ui.page >= PG_FX) return;
    wrap(&beat.snd[t], e, NSND[t]);
    p = put_s(a, TRK_NAME[t]);
    put_s(p, t == T_DRUMS ? " KIT" : " SOUND");
    toast(a, snd_name(t));
    CHG();
    rd_body = 1;
}
static void select_turn(int32_t e)
{
    const scene_t *sc = &beat.sc[beat.scene & 3u];
    switch (ui.page) {
    case PG_HOME: wrap(&ui.track, e, NTRACK); break;
    case PG_KEYS: wrap(&ui.slot, e, sc->bars * 2); break;
    case PG_BASS: case PG_DRUMS: wrap(&ui.step, e, 16); break;
    case PG_LEAD: wrap(&ui.pos, e, sc->bars * 16); break;
    case PG_FX: ui.fxrow = e > 0; break;
    case PG_SONG: wrap(&ui.song, e, 16); break;
    default: wrap(&ui.tape, e, 8 + NTAPE); break;
    }
    rd_body = rd_knob = 1;
}
static void sel_press(void)
{
    scene_t *sc = &beat.sc[beat.scene & 3u];
    uint32_t n;
    rd_body = 1;
    switch (ui.page) {
    case PG_HOME: beat.mute ^= (uint8_t)(1u << ui.track); CHG(); break;
    case PG_KEYS:                               /* the chord before goes on through here; again: it starts anew */
        fork_clip(T_KEYS);
        n = ui.slot % (sc->bars * 2u);
        sc->chord[n] = sc->chord[n] ? 0 : (uint8_t)sc_chord(sc, n);
        CHG();
        break;
    case PG_BASS: step_toggle(T_BASS, ui.step & 15u); break;
    case PG_DRUMS: step_toggle(T_DRUMS, ui.step & 15u); break;
    case PG_LEAD: {
        uint32_t pos, m;
        fork_clip(T_LEAD);
        n = sc->bars * 16u;
        pos = ui.pos % n;
        m = sc->mel[pos];
        if (m && m != MEL_TIE) {                /* a note starts here: away with it */
            mel_put(sc, pos, 0);
        } else if (m == MEL_TIE) {              /* a note is held through here: it ends here */
            mel_put(sc, pos, 0);
        } else {                                /* nothing: the note before an eighth longer (or a rest) */
            if (pos && sc->mel[pos - 1u]) {
                sc->mel[pos] = MEL_TIE;
                if (pos + 1u < n && !sc->mel[pos + 1u]) sc->mel[pos + 1u] = MEL_TIE;
            }
            ui.pos = (uint8_t)((pos + 2u) % n);
        }
        CHG();
        break;
    }
    case PG_FX: ui.fxrow ^= 1u; rd_knob = 1; break;
    case PG_SONG:
        beat.song_on ^= 1u;
        toast("THE SONG", beat.song_on ? "ON" : "OFF: THE SCENE LOOPS");
        CHG();
        rd_head = 1;
        break;
    case PG_TAPES:
        if (ui.tape < 8u) {
            if (!user_has[ui.tape] || blob_load(ui.tape, &tmp_beat, sizeof tmp_beat)) { toast("TAPES", "NOTHING THERE"); break; }
            beat_take();
        } else {
            tape_load(ui.tape - 8u);
        }
        toast("LOADED", beat.name);
        CHG();
        break;
    default: break;
    }
}
static void page_go(uint32_t p)
{
    if (ui.page == p) {
        if (p == PG_FX) ui.fxrow ^= 1u;
        else if (p == PG_TAPES) {               /* SAVE again: keep the tape here */
            if (ui.tape < 8u) {
                char s[20], *q = put_s(s, "KEPT IN ");
                put_u(q, ui.tape + 1u, 1);
                int bad;
                quiet();
                bad = blob_save(ui.tape, &beat, sizeof beat);
                eng_hold = 0;
                if (bad) { toast("TAPES", "COULD NOT KEEP IT"); }
                else {
                    user_has[ui.tape] = 1;
                    scpy(user_name[ui.tape], beat.name, sizeof user_name[0]);
                    toast(s, beat.name);
                }
            } else {
                toast("A FACTORY TAPE", "PICK 1-8 TO KEEP");
            }
        }
    } else {
        if (ui.page == PG_HOME && p >= PG_KEYS && p <= PG_LEAD) ui.track = (uint8_t)PG_TRACK(p);
        if (ui.page >= PG_KEYS && ui.page <= PG_LEAD) ui.track = (uint8_t)PG_TRACK(ui.page);
        ui.page = (uint8_t)p;
        ui.toast_t = 0;
        t_sel = 0;                              /* (SEL held from another page empties nothing here) */
    }
    rd_head = rd_body = rd_knob = 1;
}

static void midi_in(void)
{
    uint8_t st, d1, d2;
    while (midi_get(&st, &d1, &d2)) {
        if (st == 0xFAu || st == 0xFBu) { if (!sq_run) eng_cmd(C_PLAY, 0, 0, 0); }
        else if (st == 0xFCu) eng_cmd(C_STOP, 0, 0, 0);
        else if ((st & 0xF0u) == 0x90u && d2) eng_cmd(C_NOTE_ON, T_LEAD, d1 & 127u, d2 & 127u);
        else if ((st & 0xF0u) == 0x80u || ((st & 0xF0u) == 0x90u && !d2)) eng_cmd(C_NOTE_OFF, T_LEAD, d1 & 127u, 0);
    }
}

/* the lights: the page's button, PLAY on the beat, REC; the keys show what they are on this page */
static void leds(void)
{
    static const uint8_t PAGE_BTN[NPAGE] = {BTN_HOME, BTN_ARP, BTN_LFO, BTN_SEQ, BTN_EDIT, BTN_FX, BTN_GLO, BTN_SAVE};
    static uint32_t lit_s, glow_s, btn_s, all = ~0u;            /* (all: the first time, every light as it should be) */
    const scene_t *sc = &beat.sc[beat.scene & 3u];
    uint32_t lit = 0, glow = 0, btn, i, live = sq_run && sq_scene == beat.scene;
    btn = 1u << PAGE_BTN[ui.page % NPAGE];
    if (sq_run && (sq_step & 3u) < 2u) btn |= 1u << BTN_PLAY;
    if (ui.rec) btn |= 1u << BTN_REC;
    if (beat.song_on) btn |= 1u << BTN_GLO;
    switch (ui.page) {
    case PG_HOME:
        for (i = 0; i < 4u; i++) {
            if (i == beat.scene) lit |= 1u << wkey[i]; else glow |= 1u << wkey[i];
            if (beat.mute >> i & 1u) glow |= 1u << wkey[4u + i]; else lit |= 1u << wkey[4u + i];
        }
        break;
    case PG_KEYS:
        i = sc_chord(sc, live ? sq_bar * 2u + (sq_step >> 3) : ui.slot % (sc->bars * 2u));
        if (i) { if (live) lit |= 1u << (i - 1u); else glow |= 1u << (i - 1u); }
        break;
    case PG_BASS: case PG_DRUMS:
        for (i = 0; i < 16u; i++) {
            uint32_t l = ui.page == PG_BASS ? (sc_bass(sc, i) ? 2u : 0) : sc_drum(sc, ui.dvoice, i);
            if (live && sq_step == i) l = l ? 0 : 2u;           /* the step that sounds: the other way round */
            if (l == 2u) lit |= 1u << wkey[i]; else if (l) glow |= 1u << wkey[i];
        }
        for (i = 0; i < 4u; i++) {
            if (i == (ui.page == PG_BASS ? ui.btype - 1u : ui.dvoice)) lit |= 1u << bkey[i];
            else glow |= 1u << bkey[i];
        }
        break;
    case PG_SONG:
        for (i = 0; i < 16u; i++) {
            if (i == (sq_run && beat.song_on ? sq_slot : ui.song)) lit |= 1u << wkey[i];
            else if (beat.chain[i] & 7u) glow |= 1u << wkey[i];
        }
        break;
    default: break;
    }
    lit |= fm1_in.notes & (ui.page == PG_LEAD || ui.page >= PG_FX ? 0x7FFFFFFu : 0);
    for (i = 0; i < 27u; i++) {
        if (((lit ^ lit_s) | all) >> i & 1u) fm1_led_key(KEY_LED(i), lit >> i & 1u);
        if (((glow ^ glow_s) | all) >> i & 1u) fm1_led_key_glow(KEY_LED(i), glow >> i & 1u);
    }
    for (i = 0; i < (BOARD_SMK37 ? 18u : 14u); i++)             /* (the SMK-37's PLAY, REC, FX, SEQ: ids 14..17) */
        if (((btn ^ btn_s) | all) >> i & 1u) fm1_led_key(i, btn >> i & 1u);
    lit_s = lit; glow_s = glow; btn_s = btn;
    all = 0;
}

static void app_init(void)
{
    uint32_t k, nw = 0, nb = 0;
    for (k = 0; k < 27u; k++) {
        widx[k] = bidx[k] = 255;
        if (is_black(k)) { if (nb < 11u) { bidx[k] = (uint8_t)nb; bkey[nb++] = (uint8_t)k; } }
        else if (nw < 16u) { widx[k] = (uint8_t)nw; wkey[nw++] = (uint8_t)k; }
    }
    for (k = 0; k < 8u; k++)                    /* the shelf: the names of your tapes */
        if (!blob_load(k, &tmp_beat, sizeof tmp_beat)) {
            user_has[k] = 1;
            tmp_beat.name[15] = 0;
            scpy(user_name[k], tmp_beat.name, sizeof user_name[0]);
        }
    if (settings_load(&tmp_beat, sizeof tmp_beat) || tmp_beat.ver != BEAT_VER)
        tape_load(0);
    else
        beat_take();
    lcd_fill(0, 0, 240, 240, P_BG);
}

static void app_pass(void)                      /* the main loop, once round */
{
    {
        uint32_t rel, b, keys, down, up, now, k;
        int32_t e;
#ifndef DINGHY_HOST
        if (usb.ota_req)
            eng_off = 1;                        /* the updater gets the whole chip */
#endif
        platform_poll();
        midi_in();
        now = fm1_ms;

        if ((e = fm1_enc_take(ENC_SELECT)) != 0) select_turn(e);
        if ((e = fm1_enc_take(ENC_ALGORITHM)) != 0) clip_turn(e);
        if ((e = fm1_enc_take(ENC_PRESETS)) != 0) snd_turn(e);
        for (k = 0; k < 4u; k++)
            if ((e = fm1_enc_take(ENC_KNOB1 + k)) != 0) knob_turn(k, e);

        b = fm1_input_edges(&rel);
        if (b & (1u << BTN_PLAY)) {
            if (sq_run) eng_cmd(C_STOP, 0, 0, 0);
            else eng_cmd(C_PLAY, ui.page == PG_SONG ? ui.song : 0, 0, 0);
            rd_head = rd_body = 1;
        }
        if (b & (1u << BTN_REC)) {
            ui.rec ^= 1u;
            if (ui.rec && ui.page != PG_KEYS && ui.page != PG_LEAD)
                toast("REC", "WRITES ON KEYS AND LEAD");
            rd_head = rd_body = 1;
        }
        if (b & (1u << BTN_HOME)) page_go(PG_HOME);
        if (b & (1u << BTN_ARP)) page_go(PG_KEYS);
        if (b & (1u << BTN_LFO)) page_go(PG_BASS);
        if (b & (1u << BTN_SEQ)) page_go(PG_DRUMS);
        if (b & (1u << BTN_EDIT)) page_go(PG_LEAD);
        if (b & (1u << BTN_FX)) page_go(PG_FX);
        if (b & (1u << BTN_GLO)) page_go(PG_SONG);
        if (b & (1u << BTN_SAVE)) page_go(PG_TAPES);
        if (b & (1u << BTN_ENV)) {              /* the tempo, tapped: the mean of these taps */
            static uint32_t t_first;
            if (!n_tap || now - t_tap > 2000u) { n_tap = 0; t_first = now; }
            else if (now > t_first) {
                uint32_t bpm = 60000u * n_tap / (now - t_first);
                char s[12];
                beat.bpm = (uint8_t)(bpm < BPM_MIN ? BPM_MIN : bpm > BPM_MAX ? BPM_MAX : bpm);
                put_s(put_u(s, beat.bpm, 1), " BPM");
                toast("TEMPO", s);
                CHG();
                rd_head = 1;
            }
            n_tap++;
            t_tap = now;
        }
        if (b & ((1u << BTN_OCT_DOWN) | (1u << BTN_OCT_UP))) {
            uint32_t t = cur_track();
            static const char *const O[3] = {"-1", "0", "+1"};
            char a[20];
            if (t != T_DRUMS) {
                adj(&beat.oct[t], b & (1u << BTN_OCT_UP) ? 1 : -1, 0, 2);
                put_s(put_s(a, TRK_NAME[t]), " OCTAVE");
                toast(a, O[beat.oct[t]]);
            }
        }
        if (b & (1u << BTN_SEL)) { t_sel = now | 1u; sel_done = 0; }
        if (t_sel && !sel_done && ui.page >= PG_KEYS && ui.page <= PG_LEAD && (fm1_in.buttons >> BTN_SEL & 1u) &&
            (int32_t)(now - t_sel) > 900) {               /* held: this track's own clip, empty */
            scene_t *sc = &beat.sc[beat.scene & 3u];
            uint32_t t = PG_TRACK(ui.page);
            char a[20];
            sc->clip[t] = 0;
            if (t == T_KEYS) memset(sc->chord, 0, 16);
            else if (t == T_BASS) memset(sc->bass, 0, 16);
            else if (t == T_DRUMS) memset(sc->drum, 0, 64);
            else memset(sc->mel, 0, 128);
            put_s(put_s(a, TRK_NAME[t]), " CLIP");
            toast(a, "EMPTY");
            sel_done = 1;
            CHG();
            rd_body = 1;
        }
        if (rel & (1u << BTN_SEL)) {
            if (t_sel && !sel_done) sel_press();
            t_sel = 0;
        }

        keys = fm1_in.notes & 0x7FFFFFFu;
#ifdef DINGHY_HOST
        down = keys & ~keys_prev;
#else
        down = fm1_input_note_edges() & 0x7FFFFFFu;
#endif
        up = keys_prev & ~keys;
        keys_was = keys_prev;
        keys_prev = keys | down;
        for (k = 0; k < 27u; k++) {
            if (down >> k & 1u) key_down(k);
            if (up >> k & 1u) key_up(k);
        }

        if (sq_ticks != ticks_seen) {           /* the tape moved a step */
            ticks_seen = sq_ticks;
            if (beat.song_on && sq_run && beat.scene != sq_scene) {   /* the song: the screen goes with it */
                beat.scene = sq_scene;
                rd_knob = 1;
            }
            if (rec_key != 99u && ui.rec && sq_run && sq_scene == beat.scene) {   /* a key held: its note goes on */
                uint32_t pos = sq_bar * 16u + sq_step;
                if (pos > rec_pos) mel_put(&beat.sc[beat.scene & 3u], pos, MEL_TIE);
                else if (pos < rec_pos) rec_key = 99;           /* (round the end of the scene: it ends there) */
            }
            rd_body = 1;
            if (!sq_step) rd_head = 1;
        }
        if (was_run != sq_run) { was_run = sq_run; rd_head = rd_body = 1; }   /* started, stopped */
        for (k = 0; k < NTRACK; k++)            /* the lanes' lamps: a note started */
            if (trk_hits[k] != hits_seen[k]) { hits_seen[k] = trk_hits[k]; lamp[k] = now | 1u; }
        if (ui.page == PG_HOME) {
            uint32_t m = 0;
            for (k = 0; k < NTRACK; k++) {
                if (lamp[k] && (int32_t)(now - lamp[k]) > 90) lamp[k] = 0;
                if (lamp[k]) m |= 1u << k;
            }
            if (m != lamps_shown) { lamps_shown = m; rd_body = 1; }
        }
        if (sq_run && now - t_head > 90u) {     /* the reels turn */
            reel_a += 150u;
            rd_head = 1;
        }
        if (ui.toast_t && (int32_t)(now - ui.toast_t) > 1400) { ui.toast_t = 0; rd_knob = 1; }

        if (rd_head) { rd_head = 0; t_head = now; band(0, 56, draw_head); }
        else if (rd_body) { rd_body = 0; band(56, 72, draw_body); band(128, 72, draw_body); }
        else if (rd_knob) { rd_knob = 0; knobs_fill(); band(200, 40, draw_knobs); }
        leds();

        /* the tape as it is: kept when it has been still for 2 s and nothing plays (an erase stops the sound) */
        if (dirty && !sq_run && now - t_change > 2000u && !fm1_in.notes) {
            quiet();
            settings_save(&beat, sizeof beat);
            eng_hold = 0;
            dirty = 0;
        }
    }
}

static void app_main(void)
{
#ifdef DINGHY_WEB
    static int started;                         /* in a browser (web/app/): a pass a call */
    if (!started) {
        started = 1;
        app_init();
    }
    app_pass();
#else
    app_init();
    for (;;)
        app_pass();
#endif
}
