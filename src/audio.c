/* audio.c - dzwiek bezposrednio na Pauli (4 kanaly DMA, bez audio.device).
 *
 *   kanal 0 (L): silnik lidera          - petla 8 zaplonow, okres z obrotow
 *   kanal 1 (P): silnik drugiego        - jw.
 *   kanal 2 (P): gwar kibicow           - petla, glosnosc plynnie do celu
 *   kanal 3 (L): efekty jednorazowe     - brzeczyk, gwizdek
 *
 * Jednorazowy efekt na Pauli: start DMA laduje (latch) LC/LEN do licznikow,
 * wiec w nastepnej klatce wpisujemy do LC/LEN 2-slowowa cisze - po koncu
 * probki Paula "zapetli" juz tylko cisze.
 * Probki generuje tools/genaudio.py; bez plikow gra jest po prostu cicha.
 *
 * Muzyka "Night Grid" (plansza tytulowa i ustawienia): gotowy miks PCM signed 8 bit mono,
 * ~16 kHz (PAL, okres 222), data/music.raw, 428 KiB. Zrodlo wczytywane przed przejeciem
 * sprzetu do dowolnej pamieci (Fast RAM, jesli jest), a Paula gra z dwoch buforow po
 * 8 KiB w Chip RAM. Para kanalow silnikow (0 lewy + 1 prawy, te same dane, start jednym
 * zapisem do DMACON) = mono na srodku; w menu silniki milcza.
 * Kolejka: po zaladowaniu LC/LEN Paula ustawia AUD0 w INTREQ (przerwanie wylaczone -
 * tylko flaga). Wtedy drugi bufor juz nie gra: wypelniamy go kolejnym kawalkiem utworu
 * (kursor zawija do bajtu 0) i wpisujemy jego adres do LC. Bufor trwa ~0.5 s, a flage
 * sprawdzamy co klatke (20 ms). */
#include <exec/memory.h>
#include <proto/exec.h>

#include "game.h"
#include "hw.h"

#define R_ADKCON     0x09E
#define AUD(ch, off) (0x0A0 + (ch) * 0x10 + (off))
#define A_LC  0x0
#define A_LEN 0x4
#define A_PER 0x6
#define A_VOL 0x8

#define CH_ENGINE0   0
#define CH_ENGINE1   1
#define CH_CROWD     2
#define CH_SFX       3

#define ENGINE_CYCLE 48                   /* probek na zaplon (genaudio.py) */
#define RPM_TO_PER   8867237UL            /* 3546895 * 120 / ENGINE_CYCLE: 4-suw, 1 cylinder */
#define PER_CROWD    322                  /* 11025 Hz */
#define PER_BUZZER   322
#define PER_WHISTLE  161                  /* 22050 Hz */
#define PER_MUSIC    222                  /* 3546895 / 222 = 15977 Hz (PAL) */
#define MUSIC_VOL    48
#define MUSIC_BUF    8192UL               /* bajtow na bufor DMA = 4096 slow w AUDxLEN */
#define MUSIC_FADE   4                    /* wyciszanie: glosnosc w dol o 4 na klatke */
#define INTF_AUD0    0x0080
#define INTF_AUD1    0x0100

typedef struct { BYTE *data; ULONG len; } Sample;
enum { S_ENGINE = 0, S_CROWD, S_BUZZER, S_WHISTLE, S_COUNT };
static const char *files[S_COUNT] = {
    "PROGDIR:data/engine.raw", "PROGDIR:data/crowd.raw",
    "PROGDIR:data/buzzer.raw", "PROGDIR:data/whistle.raw"
};
static Sample smp[S_COUNT];
static Sample music;                      /* data/music.raw, Fast albo Chip RAM */
static UBYTE *music_buf[2];               /* bufory DMA w Chip RAM */
static BOOL   music_on;
static UBYTE  music_vol;
static UBYTE  music_queued;               /* bufor wpisany do LC (gra albo zaraz zagra) */
static BOOL   music_dirty[2];             /* bufor odegrany - do ponownego wypelnienia */
static ULONG  music_cursor;               /* nastepny bajt utworu */
BOOL g_music_enabled = TRUE;              /* opcja MUZYKA w ustawieniach */
#ifdef SHOW_LAG
ULONG g_music_fills, g_music_loops, g_chip_free;   /* diagnostyka na ekranie ustawien */
ULONG g_kb_bytes;                                  /* bajty z klawiatury (input.c) */
UBYTE g_kb_last;
#endif
static UWORD *silence;                    /* 2 slowa zer w Chip RAM */
static BOOL   running;
static UBYTE  sfx_relatch;                /* klatki do podmiany LC/LEN na cisze */

static UWORD eng_rpm[2], eng_vol[2];
static UBYTE crowd_vol, crowd_target, crowd_boost;
static UBYTE last_leader = 0xFF, last_finished;

BOOL audio_init(void)
{
    UBYTE i;
    silence = AllocMem(4, MEMF_CHIP | MEMF_CLEAR);
    if (!silence) return FALSE;
    for (i = 0; i < S_COUNT; i++)
        smp[i].data = (BYTE *)asset_load_alloc(files[i], &smp[i].len, MEMF_CHIP);
    /* muzyka: brak pliku albo brak Chip RAM -> menu po prostu bez muzyki */
    music.data = (BYTE *)asset_load_alloc("PROGDIR:data/music.raw", &music.len, MEMF_ANY);
    music_buf[0] = AllocMem(MUSIC_BUF * 2, MEMF_CHIP);
    music_buf[1] = music_buf[0] ? music_buf[0] + MUSIC_BUF : 0;
    if (music.data && (music.len < 4 || (music.len & 1) || !music_buf[0])) {
        FreeMem(music.data, music.len);
        music.data = 0;
    }
    return TRUE;
}

void audio_free(void)
{
    UBYTE i;
    for (i = 0; i < S_COUNT; i++)
        if (smp[i].data) FreeMem(smp[i].data, smp[i].len);
    if (music.data) FreeMem(music.data, music.len);     /* audio_stop wylaczyl juz DMA */
    if (music_buf[0]) FreeMem(music_buf[0], MUSIC_BUF * 2);
    if (silence) FreeMem(silence, 4);
}

static void set_ptr(UBYTE ch, const void *p, ULONG bytes)
{
    REG32(AUD(ch, A_LC)) = (ULONG)p;
    REG16(AUD(ch, A_LEN)) = (UWORD)(bytes >> 1);
}

static void wait_lines(UWORD n) { hw_wait_lines(n); }

static void start_loop(UBYTE ch, const Sample *s, UWORD per, UBYTE vol)
{
    if (s->data) set_ptr(ch, s->data, s->len);
    else         set_ptr(ch, silence, 4);
    REG16(AUD(ch, A_PER)) = per;
    REG16(AUD(ch, A_VOL)) = vol;
    REG16(R_DMACON) = (UWORD)(DMAF_SETCLR | (1 << ch));
}

void audio_start(void)
{
    REG16(R_ADKCON) = 0x00FF;             /* bez modulacji kanalow */
    REG16(R_DMACON) = 0x000F;
    wait_lines(3);
    start_loop(CH_ENGINE0, &smp[S_ENGINE], 2000, 0);
    start_loop(CH_ENGINE1, &smp[S_ENGINE], 2000, 0);
    start_loop(CH_CROWD,   &smp[S_CROWD], PER_CROWD, 0);
    set_ptr(CH_SFX, silence, 4);
    REG16(AUD(CH_SFX, A_VOL)) = 0;
    music_on = FALSE;
    running = TRUE;
}

void audio_stop(void)
{
    UBYTE ch;
    if (!running) return;
    for (ch = 0; ch < 4; ch++) REG16(AUD(ch, A_VOL)) = 0;
    REG16(R_DMACON) = 0x000F;
    running = FALSE;
}

static void play_sfx(UBYTE id, UWORD per, UBYTE vol)
{
    if (!running || !smp[id].data) return;
    REG16(R_DMACON) = 1 << CH_SFX;
    wait_lines(3);                        /* Paula musi zauwazyc wylaczenie kanalu */
    set_ptr(CH_SFX, smp[id].data, smp[id].len);
    REG16(AUD(CH_SFX, A_PER)) = per;
    REG16(AUD(CH_SFX, A_VOL)) = vol;
    REG16(R_DMACON) = (UWORD)(DMAF_SETCLR | (1 << CH_SFX));
    sfx_relatch = 1;
}

void sfx_buzzer(void)  { play_sfx(S_BUZZER, PER_BUZZER, 64); crowd_boost = 100; }
void sfx_whistle(void) { play_sfx(S_WHISTLE, PER_WHISTLE, 56); }

/* ---------------------------- muzyka --------------------------------- */

/* kolejne MUSIC_BUF bajtow utworu do bufora, przez koniec petli (dlugosci parzyste) */
static void music_fill(UBYTE i)
{
    UWORD *d = (UWORD *)music_buf[i];
    ULONG left = MUSIC_BUF;
    while (left) {
        ULONG n = music.len - music_cursor;
        const UWORD *src = (const UWORD *)(music.data + music_cursor);
        if (n > left) n = left;
        left -= n;
        music_cursor += n;
        if (music_cursor >= music.len) {
            music_cursor = 0;
#ifdef SHOW_LAG
            g_music_loops++;
#endif
        }
        for (n >>= 1; n; n--) *d++ = *src++;
    }
    music_dirty[i] = FALSE;
#ifdef SHOW_LAG
    g_music_fills++;
#endif
}

static void music_queue(UBYTE i)
{
    set_ptr(CH_ENGINE0, music_buf[i], MUSIC_BUF);
    set_ptr(CH_ENGINE1, music_buf[i], MUSIC_BUF);
    music_queued = i;
}

/* zawsze od poczatku utworu; oba kanaly startuja jednym zapisem do DMACON (synchronicznie) */
static void music_start(void)
{
    REG16(R_DMACON) = (1 << CH_ENGINE0) | (1 << CH_ENGINE1);
    wait_lines(3);
    REG16(R_INTREQ) = INTF_AUD0 | INTF_AUD1;
    music_cursor = 0;
    music_fill(0);
    music_fill(1);
    music_queue(0);
    music_vol = MUSIC_VOL;
    REG16(AUD(CH_ENGINE0, A_PER)) = PER_MUSIC;
    REG16(AUD(CH_ENGINE1, A_PER)) = PER_MUSIC;
    REG16(AUD(CH_ENGINE0, A_VOL)) = music_vol;
    REG16(AUD(CH_ENGINE1, A_VOL)) = music_vol;
    REG16(R_DMACON) = (UWORD)(DMAF_SETCLR | (1 << CH_ENGINE0) | (1 << CH_ENGINE1));
    music_on = TRUE;
}

/* Paula zaladowala bufor z LC (flaga AUD0): drugi bufor skonczyl grac -> wypelnij go
 * i wpisz do LC; zagra bez przerwy po koncu biezacego. */
static void music_feed(void)
{
    UBYTE cur, nxt;
    if (!(REG16(R_INTREQR) & INTF_AUD0)) return;
    REG16(R_INTREQ) = INTF_AUD0 | INTF_AUD1;
    cur = music_queued;
    nxt = (UBYTE)(cur ^ 1);
    if (music_dirty[nxt]) music_fill(nxt);
    music_queue(nxt);
    music_dirty[cur] = TRUE;
}

static void music_stop(void)
{
    REG16(AUD(CH_ENGINE0, A_VOL)) = 0;
    REG16(AUD(CH_ENGINE1, A_VOL)) = 0;
    REG16(R_DMACON) = (1 << CH_ENGINE0) | (1 << CH_ENGINE1);
    wait_lines(3);
    REG16(R_INTREQ) = INTF_AUD0 | INTF_AUD1;
    eng_vol[0] = eng_vol[1] = 0;
    eng_rpm[0] = eng_rpm[1] = 0;
    start_loop(CH_ENGINE0, &smp[S_ENGINE], 2000, 0);
    start_loop(CH_ENGINE1, &smp[S_ENGINE], 2000, 0);
    music_on = FALSE;
}

static BOOL music_wanted(void)
{
    return (BOOL)(music.data && g_music_enabled && (g_tour.state == GS_TITLE || g_tour.state == GS_SETUP));
}

/* start przy wejsciu do menu (raz), przy wyjsciu krotkie wyciszenie i stop */
static void update_music(void)
{
    BOOL want = music_wanted();
    if (!music_on) {
        if (want) music_start();
        return;
    }
    music_feed();
    if (!want) {
        music_vol = (UBYTE)(music_vol > MUSIC_FADE ? music_vol - MUSIC_FADE : 0);
        REG16(AUD(CH_ENGINE0, A_VOL)) = music_vol;
        REG16(AUD(CH_ENGINE1, A_VOL)) = music_vol;
        if (!music_vol) music_stop();
    }
}

/* ---------------------------- silniki -------------------------------- */

/* obroty zaleza od fazy: pod tasma zawodnicy "kreca" silniki, w biegu - predkosc + uslizg */
static UWORD bike_rpm(const Bike *b)
{
    HeatPhase ph = g_tour.heat.phase;
    if (b->state == BIKE_EXCLUDED) return 0;
    if (b->state == BIKE_WHEELSPIN) return (UWORD)(13000 + (rng_next() & 1023));
    if (b->state == BIKE_AT_GATE) {
        if (ph == HEAT_PRE) return 3500;
        return (UWORD)(8000 + (rng_next() & 2047) + ((rng_next() & 31) == 0 ? 3000 : 0));  /* blipy gazu */
    }
    {
        /* 6000 + do 8000 z predkosci; uslizg = kolo sie slizga -> wyzsze obroty */
        ULONG frac = (ULONG)(b->speed >> 8) * 8000UL / (ULONG)(BIKE_MAX_SPEED >> 8);
        UWORD rpm = (UWORD)(6000 + (frac > 8000 ? 8000 : frac));
        rpm += (UWORD)((b->drift > 0 ? b->drift : 0) * 30);
        if (b->state == BIKE_FINISHED) rpm = (UWORD)(rpm * 3 / 4);
        return rpm;
    }
}

static void pick_engines(UBYTE out[2])
{
    UBYTE i, n = 0;
    out[0] = out[1] = 0xFF;
    /* pod tasma: pola A i D (szeroko w stereo); w biegu: dwaj pierwsi */
    if (g_tour.heat.phase < HEAT_TAPE_UP) { out[0] = 0; out[1] = 3; return; }
    for (i = 0; i < MAX_RIDERS && n < 2; i++) {
        UBYTE k;
        for (k = 0; k < MAX_RIDERS; k++)
            if (g_tour.bikes[k].position == i && g_tour.bikes[k].state != BIKE_EXCLUDED) out[n++] = k;
    }
}

static void update_engines(void)
{
    UBYTE pick[2], c;
    UWORD target_rpm[2] = { 0, 0 }, target_vol[2] = { 0, 0 };

    if (g_paused) {
        /* pauza: silniki cichna */
    } else if (g_tour.state == GS_HEAT && g_tour.heat.phase != HEAT_FALSE_START) {
        pick_engines(pick);
        for (c = 0; c < 2; c++)
            if (pick[c] != 0xFF) {
                target_rpm[c] = bike_rpm(&g_tour.bikes[pick[c]]);
                target_vol[c] = target_rpm[c] ? (c == 0 ? 44 : 34) : 0;
            }
    } else if (g_tour.state == GS_TRACK_GRADING) {
        target_rpm[0] = 2400; target_vol[0] = 30;       /* traktor */
        target_rpm[1] = 2100; target_vol[1] = 22;
    }

    for (c = 0; c < 2; c++) {
        /* wygladzanie: obroty 1/4 roznicy na klatke, glosnosc o 2 */
        if (target_rpm[c]) {
            if (!eng_rpm[c]) eng_rpm[c] = target_rpm[c];
            eng_rpm[c] = (UWORD)(eng_rpm[c] + ((WORD)(target_rpm[c] - eng_rpm[c]) >> 2));
        }
        if (eng_vol[c] < target_vol[c]) eng_vol[c] = (UWORD)(eng_vol[c] + 2 > target_vol[c] ? target_vol[c] : eng_vol[c] + 2);
        else if (eng_vol[c] > target_vol[c]) eng_vol[c] = (UWORD)(eng_vol[c] < target_vol[c] + 2 ? target_vol[c] : eng_vol[c] - 2);
        if (eng_rpm[c] >= 1000)
            REG16(AUD(c, A_PER)) = (UWORD)(RPM_TO_PER / eng_rpm[c]);
        REG16(AUD(c, A_VOL)) = eng_vol[c];
    }
}

/* ---------------------------- kibice --------------------------------- */

static void update_crowd(void)
{
    GameState s = g_tour.state;
    UBYTE i;

    if (s == GS_HEAT) {
        Heat *h = &g_tour.heat;
        crowd_target = 14;
        if (h->phase == HEAT_RED_LIGHT) crowd_target = 8;               /* cisza przed startem */
        for (i = 0; i < MAX_RIDERS; i++)                                /* zmiana lidera */
            if (g_tour.bikes[i].position == 0 && h->phase == HEAT_RACING) {
                if (last_leader != 0xFF && last_leader != i && h->race_frames > 3 * FPS) crowd_boost = 60;
                last_leader = i;
            }
        if (h->finished_count != last_finished) {                       /* meta */
            if (h->finished_count == 1) crowd_boost = 150;
            last_finished = h->finished_count;
        }
    } else {
        crowd_target = (s == GS_HEAT_RESULT || s == GS_TRACK_GRADING || s == GS_TABLE) ? 10 : 0;
        last_leader = 0xFF;
        last_finished = 0;
    }
    if (crowd_boost) { crowd_boost--; if (crowd_target < 48) crowd_target = 48; }
    if (g_paused) crowd_target = 4;

    if (crowd_vol < crowd_target) crowd_vol += 2;
    else if (crowd_vol > crowd_target) crowd_vol--;
    if (crowd_vol > 64) crowd_vol = 64;
    REG16(AUD(CH_CROWD, A_VOL)) = crowd_vol;
    /* lekkie "falowanie" gwaru */
    REG16(AUD(CH_CROWD, A_PER)) = (UWORD)(PER_CROWD - 8 + (rng_next() & 15));
}

void audio_update(void)
{
    if (!running) return;
    if (sfx_relatch && !--sfx_relatch)
        set_ptr(CH_SFX, silence, 4);          /* po koncu efektu Paula gra juz cisze */
    update_music();
    if (!music_on) update_engines();      /* kanaly 0/1 zajete przez muzyke */
    update_crowd();
}
