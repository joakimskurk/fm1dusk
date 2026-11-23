/* SPDX-License-Identifier: GPL-3.0-only */
/* Dusk: the screen. Three bands, always the same: the cassette (what tape, where in it), the page (the four
 * lanes, or one track's editor, the effects, the song, the shelf of tapes) and the four knobs (what each does here,
 * and its value). Each band is drawn into the canvas and sent in strips; only a band that changed is drawn. */

/* dusk: a dark room, paper, and one colour a track */
#define P_BG RGB(24, 20, 37)
#define P_PANEL RGB(38, 34, 58)
#define P_PANEL2 RGB(50, 46, 76)
#define P_LINE RGB(70, 66, 100)
#define P_DIM RGB(120, 112, 150)
#define P_TXT RGB(234, 212, 170)
#define P_HI RGB(255, 246, 226)
#define P_INK RGB(58, 40, 50)
#define P_INK2 RGB(140, 104, 96)
#define P_PAPER RGB(250, 236, 208)
#define P_SHELL RGB(222, 176, 128)
#define P_SHELL2 RGB(180, 128, 96)
#define P_PINK RGB(236, 110, 140)
#define P_TAPE RGB(92, 60, 56)
static const uint16_t TRK_COL[NTRACK] = {RGB(247, 150, 90), RGB(178, 140, 220), RGB(250, 204, 96), RGB(104, 204, 180)};
static const uint16_t TRK_LOW[NTRACK] = {RGB(110, 62, 44), RGB(74, 58, 104), RGB(108, 86, 40), RGB(40, 88, 84)};
static const char *const TRK_NAME[NTRACK] = {"KEYS", "BASS", "DRUMS", "LEAD"};
static const char *const ROOTN[12] = {"C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

static uint16_t mix565(uint16_t a, uint16_t b, uint32_t t)   /* a .. b, t 0..15 */
{
    uint32_t r = ((a >> 11) * (15u - t) + (b >> 11) * t) / 15u, g = ((a >> 5 & 63u) * (15u - t) + (b >> 5 & 63u) * t) / 15u;
    uint32_t bl = ((a & 31u) * (15u - t) + (b & 31u) * t) / 15u;
    return (uint16_t)(r << 11 | g << 5 | bl);
}

/* the small font: 5 x 7, six pixels a letter */
static int32_t tx(int32_t x, int32_t y, const char *s, uint16_t c)
{
    for (; *s; s++, x += 6) {
        uint32_t ch = (uint8_t)*s, i, j;
        const uint8_t *g;
        if (ch < 32u || ch > 126u) ch = '?';
        g = &FONT5[(ch - 32u) * 5u];
        for (i = 0; i < 5u; i++)
            for (j = 0; j < 7u; j++)
                if (g[i] >> j & 1u)
                    cv_pset(x + (int32_t)i, y + (int32_t)j, c);
    }
    return x;
}
static void txc(int32_t x, int32_t w, int32_t y, const char *s, uint16_t c)   /* centred in x .. x + w */
{
    int32_t n = 0;
    while (s[n]) n++;
    tx(x + (w - n * 6 + 1) / 2, y, s, c);
}
/* the 16 px font on any ground */
static int32_t ts(int32_t x, int32_t y, const char *s, uint16_t fg, uint16_t bg)
{
    const felucca_font_t *f = &FONT_S;
    uint16_t ramp[16];
    uint32_t a;
    for (a = 0; a < 16u; a++)
        ramp[a] = mix565(bg, fg, a);
    for (; *s; s++) {
        uint32_t gi = glyph(f, (uint8_t)*s), gx, gy, w = f->bw[gi], bpr = (w + 1u) / 2u;
        const uint8_t *gd = f->data + f->off[gi];
        for (gy = 0; gy < f->h; gy++)
            for (gx = 0; gx < w; gx++) {
                uint32_t v = gd[gy * bpr + gx / 2u];
                v = (gx & 1u) ? (v & 15u) : (v >> 4);
                if (v)
                    cv_pset(x - f->pad + (int32_t)gx, y + (int32_t)gy, ramp[v]);
            }
        x += f->adv[gi];
    }
    return x;
}
static void tsc(int32_t x, int32_t w, int32_t y, const char *s, uint16_t fg, uint16_t bg)
{
    ts(x + (w - text_w(&FONT_S, s)) / 2, y, s, fg, bg);
}
static void rrect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)     /* a box with its corners off */
{
    if (w < 3 || h < 3) { cv_rect(x, y, w, h, c); return; }
    cv_rect(x + 1, y, w - 2, h, c);
    cv_rect(x, y + 1, w, h - 2, c);
}
static void frame(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    cv_rect(x + 1, y, w - 2, 1, c); cv_rect(x + 1, y + h - 1, w - 2, 1, c);
    cv_rect(x, y + 1, 1, h - 2, c); cv_rect(x + w - 1, y + 1, 1, h - 2, c);
}
static void disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t y, x;
    for (y = -r; y <= r; y++) {
        for (x = 0; x * x + y * y <= r * r + r / 2; x++)
            ;
        cv_rect(cx - x + 1, cy + y, 2 * x - 1, 1, c);
    }
}
static char *put_s(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }
static void scpy(char *d, const char *s, uint32_t max) { while (max > 1u && *s) { *d++ = *s++; max--; } *d = 0; }
static char *put_u(char *d, uint32_t v, uint32_t digits)
{
    char t[10];
    uint32_t n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v || n < digits);
    while (n) *d++ = t[--n];
    *d = 0;
    return d;
}
static void chord_name(uint32_t c, char *d)
{
    if (!c) { put_s(d, "-"); return; }
    d = put_s(d, ROOTN[mod12((int32_t)chord_root(c) + beat.key - 5)]);
    put_s(d, CTYPE[BANK[(c - 1u) % 27u]].name);
}

/* ---- what the screen shows of the panel's state (the app's) */
enum { PG_HOME, PG_KEYS, PG_BASS, PG_DRUMS, PG_LEAD, PG_FX, PG_SONG, PG_TAPES, NPAGE };
static struct {
    uint8_t page, track, slot, step, pos, song, dvoice, btype, fxrow, rec, tape;
    uint32_t toast_t;
    char toast_a[20], toast_b[20];
} ui = {.btype = 1};
static char user_name[8][16];
static uint8_t user_has[8];
static uint32_t reel_a, lamp[NTRACK];
#define PG_TRACK(p) ((uint32_t)(p) - PG_KEYS)   /* a track's page: its track */

static const char *clip_name(uint32_t t, const scene_t *sc)
{
    uint32_t c = sc->clip[t];
    if (!c) return "MINE";
    c--;
    return t == T_KEYS ? PROG_NAME[c % NPROG] : t == T_BASS ? BASSP_NAME[c % NBASSP] :
           t == T_DRUMS ? GROOVE_NAME[c % NGROOVE] : MELC_NAME[c % NMELC];
}
static const char *snd_name(uint32_t t)
{
    return t == T_KEYS ? SND_KEYS[beat.snd[t] % NKEYS].name : t == T_BASS ? SND_BASS[beat.snd[t] % NBASS].name :
           t == T_DRUMS ? KIT[beat.snd[t] % NKIT].name : SND_LEAD[beat.snd[t] % NLEAD].name;
}

/* ---- the cassette: 240 x 56 at the top */
static void draw_head(void)
{
    static const char *const PAGE[NPAGE] = {"HOME", "KEYS", "BASS", "DRUMS", "LEAD", "TAPE FX", "SONG", "TAPES"};
    const scene_t *sc = &beat.sc[beat.scene & 3u];
    uint32_t i, playing = sq_run, sc_now = playing ? sq_scene : beat.scene;
    uint16_t pc = ui.page >= PG_KEYS && ui.page <= PG_LEAD ? TRK_COL[PG_TRACK(ui.page)] : P_INK;
    char s[24], *p;
    int32_t prog = 0;
    rrect(3, 2, 234, 52, P_SHELL2);
    rrect(3, 2, 234, 50, P_SHELL);
    rrect(9, 6, 222, 19, P_PAPER);                              /* the label: the tape's name, its tempo and key */
    ts(14, 7, beat.name, P_INK, P_PAPER);
    p = put_u(s, beat.bpm, 1);
    p = put_s(p, " ");
    put_s(p, ROOTN[mod12(beat.key - 5)]);
    tx(226 - 6 * (int32_t)(p - s + (ROOTN[mod12(beat.key - 5)][1] ? 2 : 1)), 12, s, P_INK2);
    cv_rect(9, 23, 222, 1, TRK_COL[0]);
    cv_rect(9, 24, 222, 1, P_PINK);
    cv_rect(9, 25, 222, 1, TRK_COL[3]);
    rrect(60, 29, 120, 20, P_INK);                              /* the window: two reels, the scenes between */
    if (playing)
        prog = (int32_t)((sq_bar * 16u + sq_step) * 4u / (sc->bars * 16u));
    for (i = 0; i < 2u; i++) {
        int32_t cx = i ? 166 : 74, k;
        disc(cx, 39, i ? 5 + prog : 9 - prog, P_TAPE);
        disc(cx, 39, 4, P_PAPER);
        for (k = 0; k < 3; k++) {
            uint32_t a = (reel_a + (uint32_t)k * 1365u) & 4095u;
            cv_rect(cx + ((LSIN[(a + 1024u) & 4095u] * 3) >> 15), 39 + ((LSIN[a] * 3) >> 15), 1, 1, P_INK);
        }
    }
    for (i = 0; i < 4u; i++) {
        int32_t x = 91 + (int32_t)i * 15;
        char l[2] = {(char)('A' + i), 0};
        if (i == sc_now) rrect(x, 33, 13, 12, playing ? P_PINK : P_TXT);
        else if (i == sq_queue) frame(x, 33, 13, 12, P_PINK);
        tx(x + 4, 36, l, i == sc_now ? P_INK : i == beat.scene ? P_HI : P_DIM);
    }
    tx(11, 30, PAGE[ui.page % NPAGE], P_INK);                   /* left: where you are */
    if (pc != P_INK) { cv_rect(10, 39, 6 * 7 + 3, 3, P_INK); cv_rect(11, 40, 6 * 7 + 1, 1, pc); }
    p = put_s(s, "BAR ");
    p = put_u(p, playing ? sq_bar + 1u : 1u, 1);
    p = put_s(p, "/");
    put_u(p, sc->bars, 1);
    tx(11, 43, s, P_INK2);
    if (beat.song_on) {                                         /* right: the song or the loop; recording */
        p = put_s(s, "SONG ");
        put_u(p, (playing ? sq_slot : ui.song) + 1u, 2);
    } else {
        put_s(s, "LOOP");
    }
    tx(229 - 6 * 7, 30, s, P_INK);
    if (ui.rec) {
        disc(190, 46, 2, P_PINK);
        tx(196, 43, "REC", P_PINK);
    } else {
        tx(187, 43, playing ? "PLAY" : "STOP", P_INK2);
    }
}

/* ---- a lane's little picture of its clip: x, y, w x h */
static void lane_keys(const scene_t *sc, int32_t x, int32_t y, int32_t w, int32_t h, uint16_t on, uint16_t ink)
{
    uint32_t n = sc->bars * 2u, i, j;
    char s[12];
    for (i = 0; i < n; i = j) {
        int32_t x0 = x + (int32_t)(i * (uint32_t)w / n), x1, room;
        uint32_t c = sc_chord_raw(sc, i);
        for (j = i + 1u; j < n && !sc_chord_raw(sc, j); j++)
            ;
        x1 = x + (int32_t)(j * (uint32_t)w / n);
        if (!c && !(c = i ? 0 : sc_chord(sc, 0))) continue;
        rrect(x0, y, x1 - x0 - 1, h, on);
        chord_name(c, s);
        room = (x1 - x0 - 4) / 6;
        if (room < 1) continue;
        if (room < 12) s[room] = 0;
        tx(x0 + 2, y + (h - 7) / 2, s, ink);
    }
}
static void lane_bass(const scene_t *sc, int32_t x, int32_t y, int32_t w, int32_t h, uint16_t on)
{
    uint32_t i, j;
    for (i = 0; i < 16u; i = j) {
        uint32_t t = sc_bass(sc, i);
        for (j = i + 1u; j < 16u && !sc_bass(sc, j); j++)
            ;
        if (!t) continue;
        cv_rect(x + (int32_t)i * w / 16, y + (t == 3u ? 0 : t == 2u ? h / 3 : t == 4u ? h / 2 : h - h / 3),
                (int32_t)(j - i) * w / 16 - 1, h / 3, on);
    }
}
static void lane_drums(const scene_t *sc, int32_t x, int32_t y, int32_t w, int32_t h, uint16_t on, uint16_t low)
{
    uint32_t v, i;
    int32_t rh = h / 4;
    for (v = 0; v < 4u; v++)
        for (i = 0; i < 16u; i++) {
            uint32_t l = sc_drum(sc, v, i);
            int32_t cx = x + (int32_t)i * w / 16, cy = y + (int32_t)v * rh;
            if (l) cv_rect(cx + 1, cy + 1, w / 16 - 2, rh - 2, l == 2u ? on : low);
            else if (!(i & 3u)) cv_rect(cx + w / 32 - 1, cy + rh / 2 - 1, 2, 2, P_LINE);
        }
}
static void lane_lead(const scene_t *sc, int32_t x, int32_t y, int32_t w, int32_t h, uint16_t on)
{
    uint32_t n = sc->bars * 16u, i, j, lo = 127, hi = 0;
    for (i = 0; i < n; i++) {
        uint32_t m = sc_mel(sc, i);
        if (!m || m == MEL_TIE) continue;
        if (m < lo) lo = m;
        if (m > hi) hi = m;
    }
    if (hi < lo) return;
    if (hi - lo < 7u) hi = lo + 7u;
    for (i = 0; i < n; i = j) {
        uint32_t m = sc_mel(sc, i);
        for (j = i + 1u; j < n && sc_mel(sc, j) == MEL_TIE; j++)
            ;
        if (!m || m == MEL_TIE) continue;
        cv_rect(x + (int32_t)(i * (uint32_t)w / n), y + (int32_t)((hi - m) * (uint32_t)(h - 3) / (hi - lo)),
                (int32_t)((j - i) * (uint32_t)w / n) - (n > 64u ? 0 : 1), 3, on);
    }
}

static void title(uint32_t t)                    /* a track's page: its name, its clip, its sound */
{
    const scene_t *sc = &beat.sc[beat.scene & 3u];
    int32_t w = (TRK_NAME[t][4] ? 5 : 4) * 6 + 7;
    rrect(4, 58, w, 13, TRK_COL[t]);
    tx(8, 61, TRK_NAME[t], P_INK);
    tx(w + 10, 61, "CLIP", P_DIM);
    tx(w + 38, 61, clip_name(t, sc), beat.mute >> t & 1u ? P_DIM : P_HI);
    tx(236 - 54 - 24, 61, "SND", P_DIM);
    tx(236 - 54, 61, snd_name(t), P_HI);
}

/* ---- the pages (screen coordinates; the canvas clips) */
static void page_home(void)
{
    const scene_t *sc = &beat.sc[beat.scene & 3u];
    uint32_t t, playing = sq_run && sq_scene == beat.scene;
    for (t = 0; t < NTRACK; t++) {
        int32_t y = 58 + 35 * (int32_t)t;
        uint32_t sel = ui.track == t, muted = beat.mute >> t & 1u;
        uint16_t on = muted ? P_LINE : TRK_COL[t], low = muted ? P_PANEL2 : TRK_LOW[t];
        rrect(4, y, 54, 33, sel ? on : P_PANEL);
        tx(8, y + 4, TRK_NAME[t], sel ? P_INK : on);
        tx(8, y + 14, muted ? "MUTED" : clip_name(t, sc), sel ? P_INK : P_DIM);
        tx(8, y + 23, snd_name(t), sel ? P_INK : P_LINE);
        if (lamp[t]) disc(52, y + 6, 2, sel ? P_HI : on);
        rrect(60, y, 176, 33, P_PANEL);
        if (sel) frame(60, y, 176, 33, on);
        if (t == T_KEYS) lane_keys(sc, 62, y + 4, 172, 25, low, muted ? P_DIM : P_HI);
        else if (t == T_BASS) lane_bass(sc, 62, y + 4, 172, 25, on);
        else if (t == T_DRUMS) lane_drums(sc, 62, y + 2, 172, 29, on, low);
        else lane_lead(sc, 62, y + 4, 172, 25, on);
        if (playing) {
            uint32_t n = sc->bars * 16u, p = sq_bar * 16u + sq_step;
            int32_t x = t == T_BASS || t == T_DRUMS ? (int32_t)(sq_step * 172u / 16u) : (int32_t)(p * 172u / n);
            cv_rect(62 + x, y + 1, 1, 31, P_HI);
        }
    }
}

static void page_keys(void)
{
    const scene_t *sc = &beat.sc[beat.scene & 3u];
    uint32_t n = sc->bars * 2u, rows = (n + 3u) / 4u, i, now = sq_bar * 2u + (sq_step >> 3);
    int32_t rh = rows >= 4u ? 30 : rows == 2u ? 44 : 60, y0 = 78 + (120 - (int32_t)rows * rh) / 2;
    char s[12];
    title(T_KEYS);
    for (i = 0; i < n; i++) {
        int32_t x = 4 + 58 * (int32_t)(i & 3u), y = y0 + rh * (int32_t)(i / 4u);
        uint32_t raw = sc_chord_raw(sc, i), c = sc_chord(sc, i);
        uint32_t live = sq_run && sq_scene == beat.scene && now == i;
        uint16_t bg = raw ? (live ? TRK_COL[T_KEYS] : TRK_LOW[T_KEYS]) : live ? P_PANEL2 : P_PANEL;
        rrect(x, y, 56, rh - 2, bg);
        if (!(i & 1u)) {                                        /* the bar's number in its first half */
            put_u(s, i / 2u + 1u, 1);
            tx(x + 3, y + 3, s, raw ? (live ? P_INK : TRK_COL[T_KEYS]) : P_LINE);
        }
        chord_name(c, s);
        if (raw) tsc(x, 56, y + (rh - 2 - 14) / 2, s, live ? P_INK : P_HI, bg);
        else txc(x, 56, y + (rh - 9) / 2, c ? "- - -" : "", P_DIM);
        if (ui.slot == i) {
            frame(x, y, 56, rh - 2, P_HI);
            frame(x + 1, y + 1, 54, rh - 4, P_HI);
        }
    }
}

static void grid(uint32_t t, int32_t x0, int32_t cw, uint32_t cursor)   /* 16 columns under the title: the beats, the
                                                   cursor, the step that sounds */
{
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        int32_t x = x0 + cw * (int32_t)i;
        uint32_t live = sq_run && sq_scene == beat.scene && sq_step == i;
        cv_rect(x, 80, cw - 1, 100, live ? P_LINE : (i >> 2 & 1u) ? P_PANEL : P_PANEL2);
        if (i == cursor) {
            cv_rect(x, 182, cw - 1, 3, P_HI);
            cv_rect(x, 76, cw - 1, 2, P_HI);
        } else if (!(i & 3u)) {
            cv_rect(x, 182, cw - 1, 1, TRK_COL[t]);
        }
    }
}
static void page_bass(void)
{
    static const char *const ROW[4] = {"8", "5", "R", ">"};
    static const uint8_t ROW_OF[5] = {0, 2, 1, 0, 3};
    const scene_t *sc = &beat.sc[beat.scene & 3u];
    uint32_t i, j;
    title(T_BASS);
    grid(T_BASS, 20, 13, ui.step);
    for (i = 0; i < 4u; i++) {
        uint32_t sel = ROW_OF[ui.btype] == i;
        cv_rect(20, 90 + 25 * (int32_t)i, 207, 1, P_BG);
        if (sel) rrect(4, 84 + 25 * (int32_t)i, 13, 13, TRK_COL[T_BASS]);
        tx(8, 87 + 25 * (int32_t)i, ROW[i], sel ? P_INK : P_DIM);
    }
    for (i = 0; i < 16u; i = j) {
        uint32_t t = sc_bass(sc, i);
        int32_t y;
        for (j = i + 1u; j < 16u && !sc_bass(sc, j); j++)
            ;
        if (!t) continue;
        y = 82 + 25 * (int32_t)ROW_OF[t];
        cv_rect(20 + 13 * (int32_t)i, y + 6, 13 * (int32_t)(j - i) - 1, 5, TRK_LOW[T_BASS]);
        rrect(20 + 13 * (int32_t)i, y, 12, 17, TRK_COL[T_BASS]);
    }
    tx(20, 189, "BLACK KEYS: ROOT 5TH 8VE NEXT", P_DIM);
}
static void page_drums(void)
{
    static const char *const ROW[4] = {"KICK", "SNAR", "HAT", "PERC"};
    const scene_t *sc = &beat.sc[beat.scene & 3u];
    uint32_t v, i;
    title(T_DRUMS);
    grid(T_DRUMS, 34, 12, ui.step);
    for (v = 0; v < 4u; v++) {
        int32_t y = 82 + 25 * (int32_t)v;
        if (ui.dvoice == v) rrect(4, y + 2, 28, 13, TRK_COL[T_DRUMS]);
        tx(6, y + 5, ROW[v], ui.dvoice == v ? P_INK : P_DIM);
        for (i = 0; i < 16u; i++) {
            uint32_t l = sc_drum(sc, v, i);
            int32_t x = 34 + 12 * (int32_t)i;
            if (l == 2u) rrect(x, y, 11, 17, ui.dvoice == v ? TRK_COL[T_DRUMS] : mix565(TRK_LOW[T_DRUMS], TRK_COL[T_DRUMS], 8));
            else if (l) rrect(x + 2, y + 4, 7, 9, TRK_LOW[T_DRUMS]);
            else cv_rect(x + 5, y + 8, 1, 1, P_LINE);
        }
    }
    tx(34, 189, "BLACK KEYS: THE DRUM", P_DIM);
}
static void page_lead(void)
{
    const scene_t *sc = &beat.sc[beat.scene & 3u];
    uint32_t n = sc->bars * 16u, live = sq_run && sq_scene == beat.scene, now = sq_bar * 16u + sq_step;
    uint32_t pg = ((live && !(ui.rec && !sq_run) ? now : ui.pos) / 32u) * 32u, i, j, lo = 127, hi = 0;
    char s[12];
    title(T_LEAD);
    for (i = 0; i < n; i++) {
        uint32_t m = sc_mel(sc, i);
        if (m && m != MEL_TIE) { if (m < lo) lo = m; if (m > hi) hi = m; }
    }
    if (hi < lo) lo = 64;
    lo = lo > 4u ? lo - 4u : 0;
    for (i = 0; i < 25u; i++)                                   /* the roll: a line a semitone, the white keys lighter */
        cv_rect(8, 180 - 4 * (int32_t)i, 224, 4, (0x54Au >> ((lo + i) % 12u) & 1u) ? P_BG : P_PANEL);
    for (i = 0; i < 32u && pg + i < n; i++) {
        int32_t x = 8 + 7 * (int32_t)i;
        if (!(i & 7u)) {                                        /* the chord over each half bar */
            chord_name(sc_chord(sc, (pg + i) / 8u), s);
            tx(x + 1, 75, s, P_DIM);
            cv_rect(x, 84, 1, 100, (i & 15u) ? P_PANEL2 : P_LINE);
        }
        if (pg + i == ui.pos) {
            cv_rect(x, 84, 6, 100, P_PANEL2);
            cv_rect(x, 186, 6, 2, P_HI);
        }
    }
    for (i = pg; i < pg + 32u && i < n; i = j) {
        uint32_t m = sc_mel(sc, i);
        for (j = i + 1u; j < n && sc_mel(sc, j) == MEL_TIE; j++)
            ;
        if (!m || m == MEL_TIE || m < lo || m > lo + 24u) continue;
        rrect(8 + 7 * (int32_t)(i - pg), 180 - 4 * (int32_t)(m - lo), 7 * (int32_t)((j > pg + 32u ? pg + 32u : j) - i) - 1,
              4, TRK_COL[T_LEAD]);
    }
    if (live && now >= pg && now < pg + 32u)
        cv_rect(8 + 7 * (int32_t)(now - pg), 84, 1, 100, P_HI);
    for (i = 0; i < n / 32u + (n < 32u); i++)                   /* which two bars of the scene */
        cv_rect(8 + 12 * (int32_t)i, 192, 10, 3, i == pg / 32u ? TRK_COL[T_LEAD] : P_LINE);
    tx(86, 190, ui.rec ? (sq_run ? "PLAY IT: IT IS KEPT" : "KEY: NOTE  SEL: LONGER") : "REC TO WRITE", ui.rec ? P_PINK : P_DIM);
}

static const char *const FX_NAME[NFX] = {"WOBBLE", "VINYL", "TONE", "DRIVE", "SPACE", "PUMP", "CRUSH", "RAIN"};
static void page_fx(void)
{
    static const uint16_t COL[NFX] = {RGB(247, 150, 90), RGB(250, 204, 96), RGB(234, 212, 170), RGB(236, 110, 140),
                                      RGB(178, 140, 220), RGB(104, 204, 180), RGB(236, 110, 140), RGB(120, 170, 230)};
    uint32_t i;
    tx(6, 61, "THE TAPE AND THE ROOM", P_DIM);
    for (i = 0; i < NFX; i++) {
        int32_t x = 4 + 59 * (int32_t)(i & 3u), y = 76 + 62 * (int32_t)(i >> 2), h;
        uint32_t act = (i >> 2) == ui.fxrow, v = beat.fx[i] > 32u ? 32u : beat.fx[i];
        rrect(x, y, 55, 58, act ? P_PANEL2 : P_PANEL);
        if (act) frame(x, y, 55, 58, COL[i]);
        txc(x, 55, y + 5, FX_NAME[i], act ? P_HI : P_DIM);
        cv_rect(x + 20, y + 16, 15, 34, P_BG);
        h = (int32_t)v;
        cv_rect(x + 21, y + 49 - h, 13, h, act ? COL[i] : mix565(P_PANEL, COL[i], 7));
    }
}

static void page_song(void)
{
    uint32_t i, t;
    tx(6, 61, "THE SONG: SCENES IN ORDER", P_DIM);
    rrect(194, 58, 42, 13, beat.song_on ? P_PINK : P_PANEL2);
    txc(194, 42, 61, beat.song_on ? "ON" : "OFF", beat.song_on ? P_INK : P_DIM);
    for (i = 0; i < 16u; i++) {
        int32_t x = 5 + 29 * (int32_t)(i & 7u), y = 78 + 54 * (int32_t)(i >> 3);
        uint32_t e = beat.chain[i], live = sq_run && beat.song_on && sq_slot == i;
        uint16_t bg = live ? P_TXT : (e & 7u) ? P_PANEL2 : P_PANEL;
        char l[2] = {(e & 7u) ? (char)('A' + (e & 7u) - 1u) : '-', 0};
        rrect(x, y, 27, 48, bg);
        tsc(x, 27, y + 6, l, (e & 7u) ? (live ? P_INK : P_HI) : P_LINE, bg);
        for (t = 0; t < NTRACK && (e & 7u); t++)
            cv_rect(x + 4 + 5 * (int32_t)t, y + 34, 4, 8, (e >> (4u + t) & 1u) ? (live ? P_DIM : P_LINE) : TRK_COL[t]);
        if (i == ui.song) {
            frame(x, y, 27, 48, P_HI);
            frame(x + 1, y + 1, 25, 46, P_HI);
        }
    }
    tx(6, 189, "ALGORITHM: SCENE  SEL: ON/OFF", P_DIM);
}

static void page_tapes(void)
{
    uint32_t top = ui.tape < 3u ? 0 : ui.tape > 12u ? 9u : ui.tape - 3u, i;
    char s[24];
    tx(6, 61, "YOUR TAPES AND THE FACTORY'S", P_DIM);
    for (i = 0; i < 7u; i++) {
        uint32_t k = top + i, sel = k == ui.tape;
        int32_t y = 74 + 18 * (int32_t)i;
        uint16_t bg = sel ? P_TXT : P_PANEL, fg;
        const char *nme = k < 8u ? (user_has[k] ? user_name[k] : "- empty -") : TAPE[(k - 8u) % NTAPE].name;
        fg = sel ? P_INK : k < 8u && !user_has[k] ? P_DIM : P_HI;
        rrect(4, y, 232, 17, bg);
        rrect(7, y + 3, 17, 11, k < 8u ? TRK_COL[3] : TRK_COL[0]);
        put_u(s, k < 8u ? k + 1u : k - 7u, 1);
        txc(7, 17, y + 5, s, P_INK);
        ts(30, y + 1, nme, fg, bg);
        tx(236 - 46, y + 5, k < 8u ? "YOURS" : "FACTORY", sel ? P_INK2 : P_LINE);
    }
}

static void draw_body(void)
{
    switch (ui.page) {
    case PG_KEYS: page_keys(); break;
    case PG_BASS: page_bass(); break;
    case PG_DRUMS: page_drums(); break;
    case PG_LEAD: page_lead(); break;
    case PG_FX: page_fx(); break;
    case PG_SONG: page_song(); break;
    case PG_TAPES: page_tapes(); break;
    default: page_home(); break;
    }
}

/* ---- the knobs: 240 x 40 at the bottom. A knob: its name, its value as text, and as a bar if it is an amount */
typedef struct { const char *name; char val[10]; int8_t bar; uint16_t col; } knob_t;
static knob_t knob[4];
static void draw_knobs(void)
{
    uint32_t i;
    if (ui.page == PG_TAPES && !ui.toast_t) {                   /* (the shelf: no knobs) */
        rrect(4, 202, 232, 36, P_PANEL);
        txc(4, 232, 207, "SELECT: A TAPE     SEL: PLAY IT", P_DIM);
        txc(4, 232, 222, "SAVE AGAIN: KEEP YOURS HERE (1-8)", P_DIM);
        return;
    }
    if (ui.toast_t) {                                           /* or what ALGORITHM, PRESETS or a button just did */
        rrect(4, 202, 232, 36, P_PANEL2);
        txc(4, 232, 206, ui.toast_a, P_DIM);
        tsc(4, 232, 217, ui.toast_b, P_HI, P_PANEL2);
        return;
    }
    for (i = 0; i < 4u; i++) {
        int32_t x = 4 + 59 * (int32_t)i;
        rrect(x, 202, 55, 36, P_PANEL);
        if (!knob[i].name) continue;
        txc(x, 55, 205, knob[i].name, P_DIM);
        tsc(x, 55, 214, knob[i].val, P_HI, P_PANEL);
        if (knob[i].bar >= 0) {
            cv_rect(x + 6, 232, 43, 3, P_BG);
            cv_rect(x + 6, 232, knob[i].bar * 43 / 32, 3, knob[i].col);
        }
    }
}

static void band(uint32_t y0, uint32_t h, void (*fn)(void))
{
    cv_begin(240, h, P_BG);
    cv_oy = -(int32_t)y0;
    fn();
    cv_oy = 0;
    cv_blit(0, y0);
}
