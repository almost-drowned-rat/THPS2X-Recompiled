/* ── Custom soundtracks (M5 item c; CLAUDE.md → Custom soundtracks) ──────────
 *
 * Why: THPS 2X plays the dashboard's custom soundtracks through XAPI and its
 * WMA decoder; on the PC the soundtracks are the albums below media\. The
 * toolkit's src/xdk/xdk_soundtrack.c does the generic part (catalog over
 * media\, host music media object, MP3/FLAC/WAV/Vorbis); this file binds it
 * to the game's addresses and adds the playback modes and the settings.
 *
 * Addresses (XAPI 3911 and WMADECXM, identified from the code and the
 * callers in the game's music module 0x001A95D0..0x001A9C50, 2026-10-24):
 *   0x001B9A4E XFindFirstSoundtrack(data) ret 4     (enumerator magic 0xBE4BEA00)
 *   0x001B9A2F XFindNextSoundtrack(h, data) ret 8
 *   0x001B8D5C XFindClose(h) ret 4                   (also used for saved games)
 *   0x001B9AFD XGetSoundtrackSongInfo(st, i, &id, &ms, name, cch) ret 0x18
 *   0x001B9D26 XOpenSoundtrackSong(id, async) ret 8  ("%s%04x\%08x.WMA")
 *   0x00067740 WmaCreateDecoderEx(name, hFile, async, lookahead, packets, yield,
 *              pwfx, ppObj) ret 0x20, called by the stream open 0x001AE9B0
 *   0x0021EAE4 the WMA file media object vtable (11 methods, bound below)
 * The game's music state: 0x00305ABC the soundtrack (0 = TONY HAWK, k =
 * custom soundtrack k, records of 0x844C bytes at [0x00305AC4] + k * 0x844C
 * filled by 0x001A9B30), 0x00305AA4 its song count, 0x00305AAC the song,
 * 0x00305AA8 the number of level songs (0x00304940: 64-byte names from
 * songs\songs.txt; the 16th, spit8, only in level 0x13 = [0x00276198]).
 * 0x001A9CE0 (eax = k) sets the soundtrack -- the pause menu's SOUNDTRACK
 * picker; 0x001A95D0 sets the music up for a level (the catalog, soundtrack
 * TONY HAWK); 0x001A98B0 starts the next song: 0x001B4220 opens a level song
 * as "D:\songs\<name>", 0x001B4340 a custom song by its handle. At a song's
 * end the stream service (0x001AEDC0) gets a short packet and calls Flush:
 * on the console the song loops and the next track comes on Retry or the
 * next level.
 *
 * Since 2026-11-05 (maintainer) there are two choices only: the SOURCE
 * (GAME MUSIC, ALL CUSTOM SOUNDTRACKS, COMBINED = game music + every album,
 * or one album) and the PLAYBACK MODE (PLAY IN ORDER or SHUFFLE). Every
 * combination follows a playlist (src/media/media_playlist.h) through one
 * host media object, so the stream never sees a song end: at a song's end the
 * next one plays (no in-level loop any more), and a new stream (level
 * select, Retry) goes on with the song after the last one played, as the
 * console's own music goes on to its next track -- never back to the first.
 * Game tracks inside a playlist are the game's own WMA decoder, driven from
 * here. Music in the menus (0x00240F78 table) and ambience stay the game's own.
 *
 * Settings (saves\settings.ini): soundtrack_album = game | custom | combined |
 * <album path below media\>; soundtrack_mode = order | shuffle (the older
 * eight mode keys are read and mapped). A choice made in the pause menu's own
 * picker is saved too. RECOMP_SOUNDTRACK_HLE=0: off.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "settings.h"
#include "media_decode.h"
#include "xdk_soundtrack.h"

#if defined(_MSC_VER)
#  define THPS_TLS __declspec(thread)
#else
#  define THPS_TLS __thread
#endif
extern THPS_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
extern ptrdiff_t g_xbox_mem_offset;
#define GUEST32(va) (*(volatile uint32_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define GUEST16(va) (*(volatile uint16_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define GPTR(va)    ((uint8_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define ARG(n)      GUEST32(g_esp + 4u + 4u * (n))

typedef void (*recomp_func_t)(void);
recomp_func_t recomp_lookup(uint32_t xbox_va);
uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);
void xbox_HeapFree(uint32_t xbox_va);

#define ST_CHOSEN    0x00305ABCu
#define ST_RECORDS   0x00305AC4u
#define ST_COUNT     0x00305AC0u
#define GAME_COUNT   0x00305AA8u
#define LEVEL        0x00276198u
#define LEVEL_SONGS  0x00304940u
#define SONG_NAME    64u
#define MAX_GAME     32
#define WMA_VTABLE   0x0021EAE4u
#define MAX_SONGS    128                    /* the game's soundtrack screen: 0x80 songs per soundtrack */

#define SEL_CUSTOM   (-3)                   /* the source: every custom album (MEDIA_SEL_* are -1 / -2) */
#define CHOICE_FIRST_ALBUM 3                /* menu choices: GAME MUSIC, ALL CUSTOM, COMBINED, the albums */

static int s_on;
static int s_sel = MEDIA_SEL_GAME;          /* MEDIA_SEL_*, SEL_CUSTOM or an album index */
static int s_shuffle;                       /* PLAYBACK MODE: 0 PLAY IN ORDER, 1 SHUFFLE */
static int s_req_game = -1;                 /* the level song 0x001B4220 is opening */
static int s_pending = 1;                   /* the saved choice still has to go into the game */
static int s_user_pick;                     /* the request comes from the pause menu's sound options */
static uint32_t s_seed;
static MediaSong s_last;                    /* the song played last (any stream), for "go on with the next" */
static int s_have_last;

/* source x mode -> the toolkit's playlist mode (its choice argument is s_sel) */
static MediaPlayMode play_mode(void)
{
    if (s_sel == SEL_CUSTOM)
        return s_shuffle ? MEDIA_CUSTOM_SHUFFLE : MEDIA_CUSTOM_ORDER;
    if (s_sel == MEDIA_SEL_COMBINED)
        return s_shuffle ? MEDIA_ALL_SHUFFLE : MEDIA_ALL_ORDER;
    if (s_sel == MEDIA_SEL_GAME)
        return s_shuffle ? MEDIA_GAME_SHUFFLE : MEDIA_ALBUM_ORDER;
    return s_shuffle ? MEDIA_ALBUM_SHUFFLE : MEDIA_ALBUM_ORDER;
}

/* ── settings ─────────────────────────────────────────────────────────── */

static void save_sel(void)
{
    const MediaLibrary *lib = xdk_soundtrack_library();
    settings_set("soundtrack_album", s_sel == MEDIA_SEL_COMBINED ? "combined" : s_sel == SEL_CUSTOM ? "custom"
                                     : s_sel >= 0 ? lib->albums[s_sel].rel : "game");
}

static void load_settings(void)
{
    /* the eight mode keys before 2026-11-05: order or shuffle, and the
     * source some of them carried (custom / all) */
    static const struct { const char *key; int shuffle, sel; } old[] = {
        { "game_order", 0, 0 }, { "album_order", 0, 0 }, { "game_shuffle", 1, MEDIA_SEL_GAME },
        { "album_shuffle", 1, 0 }, { "custom_shuffle", 1, SEL_CUSTOM }, { "all_shuffle", 1, MEDIA_SEL_COMBINED },
        { "all_order", 0, MEDIA_SEL_COMBINED }, { "custom_order", 0, SEL_CUSTOM },
    };
    const MediaLibrary *lib = xdk_soundtrack_library();
    const char *a = settings_get("soundtrack_album", "game"), *m = settings_get("soundtrack_mode", "order");
    size_t i;
    if (!strcmp(a, "game"))
        s_sel = MEDIA_SEL_GAME;
    else if (!strcmp(a, "custom"))
        s_sel = SEL_CUSTOM;
    else if (!strcmp(a, "combined"))
        s_sel = MEDIA_SEL_COMBINED;
    else if ((s_sel = media_find_album(lib, a)) < 0) {
        fprintf(stderr, "[MUSIC] soundtrack \"%s\" not found in media\\ -- game music\n", a);
        s_sel = MEDIA_SEL_GAME;
    }
    s_shuffle = !strcmp(m, "shuffle");
    for (i = 0; i < sizeof old / sizeof old[0]; i++)
        if (!strcmp(m, old[i].key)) {
            s_shuffle = old[i].shuffle;
            if (old[i].sel)
                s_sel = old[i].sel;
            save_sel();
            settings_set("soundtrack_mode", s_shuffle ? "shuffle" : "order");
            fprintf(stderr, "[MUSIC] old soundtrack_mode \"%s\" converted\n", old[i].key);
            break;
        }
    fprintf(stderr, "[MUSIC] soundtrack: %s, mode: %s\n",
            s_sel == MEDIA_SEL_COMBINED ? "combined" : s_sel == SEL_CUSTOM ? "all custom"
            : s_sel >= 0 ? lib->albums[s_sel].name : "game music", s_shuffle ? "shuffle" : "in order");
}

/* GENERAL OPTIONS -> SOUNDTRACKS (recomp_manual.c): choice 0 game music,
 * 1 ALL CUSTOM SOUNDTRACKS, 2 COMBINED, 3 + k album k. */
int thps_st_choices(void) { return CHOICE_FIRST_ALBUM + xdk_soundtrack_library()->nalbums; }
int thps_st_choice(void)
{
    return s_sel == MEDIA_SEL_GAME ? 0 : s_sel == SEL_CUSTOM ? 1 : s_sel == MEDIA_SEL_COMBINED ? 2
         : CHOICE_FIRST_ALBUM + s_sel;
}
int thps_st_mode(void) { return s_shuffle; }

void thps_st_set_choice(int c)
{
    if (c < 0 || c >= thps_st_choices())
        return;
    s_sel = c == 0 ? MEDIA_SEL_GAME : c == 1 ? SEL_CUSTOM : c == 2 ? MEDIA_SEL_COMBINED : c - CHOICE_FIRST_ALBUM;
    save_sel();
    s_pending = 1;                          /* into the game's own state at the next song */
}

void thps_st_set_mode(int m)
{
    if (m < 0 || m > 1)
        return;
    s_shuffle = m;
    settings_set("soundtrack_mode", m ? "shuffle" : "order");
}

/* The menu text: upper case, only what the game's font draws (letters,
 * digits, space, dot, colon -- no hyphen), at most n - 1 characters. */
void thps_st_choice_text(int c, char *out, size_t n)
{
    const char *s = c == 0 ? "GAME MUSIC" : c == 1 ? "ALL CUSTOM SOUNDTRACKS" : c == 2 ? "COMBINED"
                  : c - CHOICE_FIRST_ALBUM < xdk_soundtrack_library()->nalbums
                  ? xdk_soundtrack_library()->albums[c - CHOICE_FIRST_ALBUM].name : "?";
    size_t k = 0;
    for (; *s && k + 1 < n; s++) {
        unsigned char ch = (unsigned char)*s;
        char o = ch < 0x80 && (isalnum(ch) || ch == '.' || ch == ':') ? (char)toupper(ch) : ' ';
        if (o == ' ' && (!k || out[k - 1] == ' '))
            continue;
        out[k++] = o;
    }
    while (k && out[k - 1] == ' ')
        k--;
    out[k] = 0;
}

/* ── the game's level songs as a music source ─────────────────────────── */

/* The level songs the game plays here: all but the 16th (spit8), which
 * 0x001A98B0 plays only in level 0x13. */
static int game_songs(void)
{
    int n = (int)GUEST32(GAME_COUNT);
    if (n > MAX_GAME)
        n = MAX_GAME;
    if (n > 15 && GUEST32(LEVEL) != 0x13u)
        n = 15;
    return n < 0 ? 0 : n;
}

/* A guest stdcall function with n stack arguments; returns eax. */
static uint32_t guest_call(uint32_t fn_va, recomp_func_t fn, int n, const uint32_t *args)
{
    uint32_t esp = g_esp, eax = g_eax, ecx = g_ecx, edx = g_edx, r;
    int i;
    if (!fn && !(fn = recomp_lookup(fn_va)))
        return 0x80004005u;
    g_esp -= 4u * (uint32_t)(n + 1);
    GUEST32(g_esp) = 0;                     /* return address: the callee only pops it */
    for (i = 0; i < n; i++)
        GUEST32(g_esp + 4u + 4u * (uint32_t)i) = args[i];
    fn();
    r = g_eax;
    g_esp = esp; g_eax = eax; g_ecx = ecx; g_edx = edx;
    return r;
}

/* scratch block per source: +0 name, +0x80 wfx, +0x98 ppObj, +0x9C completed,
 * +0xA0 status, +0xA4 packet, +0x100 PCM */
#define GS_BLOCK 0x2100u
#define GS_PCM   0x2000u

typedef struct {
    uint32_t blk, obj;
    int      eof;
    long     have, at;                      /* frames left in pcm[] from the last packet */
    int16_t  pcm[GS_PCM / 2];
} GameSrc;

static uint32_t method(uint32_t obj, int slot) { return GUEST32(GUEST32(obj) + 4u * (uint32_t)slot); }

/* The game's decoder always gets whole 0x2000-byte packets, as the game's
 * own stream asks for them (a shorter one in the middle of a song broke its
 * state: a crash in WMADEC after a custom song ended mid-packet); what the
 * caller does not take now waits in pcm[]. */
static long gs_read(void *c, int16_t *out, long frames)
{
    GameSrc *g = (GameSrc *)c;
    long done = 0;
    while (done < frames) {
        uint32_t got, a[3];
        long n;
        if (g->at < g->have) {
            n = g->have - g->at < frames - done ? g->have - g->at : frames - done;
            memcpy(out + 2 * done, g->pcm + 2 * g->at, (size_t)n * 4u);
            g->at += n;
            done += n;
            continue;
        }
        if (g->eof)
            break;
        GUEST32(g->blk + 0x9C) = 0;
        GUEST32(g->blk + 0xA4) = g->blk + 0x100;    /* pvBuffer */
        GUEST32(g->blk + 0xA8) = GS_PCM;            /* dwMaxSize */
        GUEST32(g->blk + 0xAC) = g->blk + 0x9C;     /* pdwCompletedSize */
        GUEST32(g->blk + 0xB0) = g->blk + 0xA0;     /* pdwStatus */
        GUEST32(g->blk + 0xB4) = 0;
        GUEST32(g->blk + 0xB8) = 0;
        a[0] = g->obj; a[1] = 0; a[2] = g->blk + 0xA4;
        if (guest_call(method(g->obj, 4), NULL, 3, a) & 0x80000000u)
            return done ? done : -1;
        got = GUEST32(g->blk + 0x9C) & ~3u;
        if (got > GS_PCM)
            got = GS_PCM;
        memcpy(g->pcm, GPTR(g->blk + 0x100), got);
        g->have = (long)(got / 4u);
        g->at = 0;
        if (got < GS_PCM)
            g->eof = 1;
    }
    return done;
}

static int gs_rewind(void *c)
{
    GameSrc *g = (GameSrc *)c;
    uint32_t a = g->obj;
    guest_call(method(g->obj, 6), NULL, 1, &a);   /* Flush */
    g->eof = 0;
    g->have = g->at = 0;
    return 1;
}

static void gs_close(void *c)
{
    GameSrc *g = (GameSrc *)c;
    uint32_t a = g->obj;
    if (g->obj)
        guest_call(method(g->obj, 1), NULL, 1, &a);   /* Release */
    xbox_HeapFree(g->blk);
    free(g);
}

extern void sub_00067740_gen(void);

static int open_game_track(int track, XdkMusicSource *src)
{
    GameSrc *g;
    uint32_t a[8], r;
    char name[SONG_NAME + 16];
    if (track < 0 || track >= game_songs())
        return 0;
    g = (GameSrc *)calloc(1, sizeof *g);
    if (!g || !(g->blk = xbox_HeapAlloc(GS_BLOCK, 16))) {
        free(g);
        return 0;
    }
    memset(GPTR(g->blk), 0, GS_BLOCK);
    snprintf(name, sizeof name, "D:\\songs\\%.*s", (int)SONG_NAME - 1,
             (const char *)GPTR(LEVEL_SONGS + (uint32_t)track * SONG_NAME));
    memcpy(GPTR(g->blk), name, strlen(name) + 1);
    a[0] = g->blk; a[1] = 0; a[2] = 0; a[3] = 0x10000; a[4] = 8; a[5] = 1;
    a[6] = g->blk + 0x80; a[7] = g->blk + 0x98;
    r = guest_call(0x00067740u, sub_00067740_gen, 8, a);
    g->obj = GUEST32(g->blk + 0x98);
    if (r || !g->obj || GUEST16(g->blk + 0x80) != 1 || GUEST16(g->blk + 0x82) != 2
            || GUEST32(g->blk + 0x84) != MEDIA_RATE || GUEST16(g->blk + 0x8E) != 16) {
        fprintf(stderr, "[MUSIC] game track %d (%s): no 44.1 kHz stereo decoder (0x%08X)\n", track + 1, name, r);
        if (!g->obj)
            g->obj = 0;
        gs_close(g);
        return 0;
    }
    src->ctx = g;
    src->read = gs_read;
    src->rewind = gs_rewind;
    src->close = gs_close;
    return 1;
}

/* ── init ─────────────────────────────────────────────────────────────── */

void thps_soundtrack_init(const char *media_dir)
{
    const char *e = getenv("RECOMP_SOUNDTRACK_HLE");
    s_on = !(e && *e == '0');
    if (!s_on)
        return;
    xdk_soundtrack_init(media_dir, MAX_SONGS);
    xdk_music_bind(WMA_VTABLE, open_game_track);
    load_settings();
}

static int on(void) { return s_on && xdk_soundtrack_active(); }

/* ── XAPI soundtrack functions ─────────────────────────────────────────── */

extern void sub_001B9A4E_gen(void);
extern void sub_001B9A2F_gen(void);
extern void sub_001B8D5C_gen(void);
extern void sub_001B9AFD_gen(void);
extern void sub_001B9D26_gen(void);

void sub_001B9A4E(void) { if (on()) xdk_XFindFirstSoundtrack(); else sub_001B9A4E_gen(); }
void sub_001B9A2F(void) { if (on()) xdk_XFindNextSoundtrack(); else sub_001B9A2F_gen(); }
void sub_001B8D5C(void) { if (!on() || !xdk_XFindClose_ours()) sub_001B8D5C_gen(); }
void sub_001B9AFD(void) { if (on()) xdk_XGetSoundtrackSongInfo(); else sub_001B9AFD_gen(); }
void sub_001B9D26(void) { if (on()) xdk_XOpenSoundtrackSong(); else sub_001B9D26_gen(); }

/* ── which song the game asks for ──────────────────────────────────────── */

/* The saved choice into the game: 0x001A9CE0 with eax = k (album k - 1),
 * or 0 for game music and COMBINED (that is TONY HAWK there). Only once the
 * level's catalog exists. */
extern void sub_001A9CE0_gen(void);
static void apply_choice(void)
{
    uint32_t k = s_sel >= 0 ? (uint32_t)s_sel + 1u : 0u, eax = g_eax;
    if (!s_pending || !GUEST32(ST_RECORDS) || k > GUEST32(ST_COUNT))
        return;
    g_eax = k;
    g_esp -= 4;
    GUEST32(g_esp) = 0;                     /* plain ret pops it */
    sub_001A9CE0_gen();
    g_eax = eax;
    s_pending = 0;
}

/* 0x001A95D0: the level's music set-up resets the soundtrack to TONY HAWK. */
extern void sub_001A95D0_gen(void);
void sub_001A95D0(void)
{
    sub_001A95D0_gen();
    if (on()) {
        s_pending = 1;
        apply_choice();
    }
}

/* 0x00097160 loads songs\<name>.bet for each level song i < [0x00305AA4],
 * the names from 0x00304940. On the console the soundtrack is always TONY
 * HAWK when it runs (0x001A95D0 resets it); with the saved album set there,
 * 0x00305AA4 is that album's song count and a 20-song album read past the
 * 16 level names: "songs\.bet" not found, then a null read in 0x00097070
 * (maintainer's listening test, 2026-10-25). It sees the level song count. */
#define SONG_COUNT   0x00305AA4u
extern void sub_00097160_gen(void);
void sub_00097160(void)
{
    uint32_t n = GUEST32(SONG_COUNT);
    int swap = on() && GUEST32(ST_CHOSEN) != 0;
    if (swap)
        GUEST32(SONG_COUNT) = GUEST32(GAME_COUNT);
    sub_00097160_gen();
    if (swap)
        GUEST32(SONG_COUNT) = n;
}

/* 0x001A9CE0 from the pause menu: the player's own pick, saved (only a
 * change -- with COMBINED or ALL CUSTOM the game shows TONY HAWK, and keeping
 * that is no choice). */
void sub_001A9CE0(void)
{
    uint32_t before = GUEST32(ST_CHOSEN);
    sub_001A9CE0_gen();
    if (on() && GUEST32(ST_CHOSEN) != before) {
        uint32_t k = GUEST32(ST_CHOSEN);
        fprintf(stderr, "[MUSIC] pause-menu soundtrack %u: saved\n", k);
        thps_st_set_choice(k ? (int)k - 1 + CHOICE_FIRST_ALBUM : 0);
        s_pending = 0;
    }
}

/* 0x001A98B0 starts the next song: a choice made in Options goes in first. */
extern void sub_001A98B0_gen(void);
void sub_001A98B0(void)
{
    if (on())
        apply_choice();
    sub_001A98B0_gen();
}

/* 0x001A99E0: the pause menu's sound options restart the music after a
 * change (SOUNDTRACK or SELECT TRACK) -- the song asked for then is the
 * player's pick, which a shuffle mode starts from too. */
extern void sub_001A99E0_gen(void);
void sub_001A99E0(void)
{
    s_user_pick = 1;
    sub_001A99E0_gen();
    s_user_pick = 0;
}

/* 0x001B4220(name, x) opens a file stream "D:\songs\<name>"; a name from the
 * level-song table is game track (name - table) / 64. */
extern void sub_001B4220_gen(void);
void sub_001B4220(void)
{
    uint32_t name = ARG(0);
    int saved = s_req_game;
    if (name >= LEVEL_SONGS && name < LEVEL_SONGS + MAX_GAME * SONG_NAME && !((name - LEVEL_SONGS) % SONG_NAME))
        s_req_game = (int)((name - LEVEL_SONGS) / SONG_NAME);
    sub_001B4220_gen();
    s_req_game = saved;
}

/* The song playing now (the newest host stream), kept for the next stream. */
static void remember_current(void)
{
    MediaSong cur;
    int pl = 0;
    if (xdk_music_current(&cur, &pl) && pl) {
        s_last = cur;
        s_have_last = 1;
    }
}

/* WmaCreateDecoderEx: a custom song's handle or a level song (every source
 * and mode has a playlist since 2026-11-05) gets a host media object; the
 * playlist's inner game tracks the game's own decoder. */
void sub_00067740(void)
{
    MediaSong req;
    int custom = 0, game = 0;
    uint32_t pwfx = ARG(6), pp = ARG(7), obj = 0;
    MediaPlaylist pl;
    const MediaSong *c;
    if (on()) {
        custom = xdk_soundtrack_song_of_handle(ARG(1), &req);
        if (!custom && s_req_game >= 0) {
            req.album = -1;
            req.track = s_req_game;
            game = 1;
            s_req_game = -1;                    /* the inner decoders of the playlist are the game's */
        }
    }
    if (!custom && !game) {
        sub_00067740_gen();
        return;
    }
    remember_current();                         /* a stream still open: its song is the last one */
    if (!s_seed)
        s_seed = (uint32_t)time(NULL) ^ (uint32_t)clock() ^ 0x2545F491u;
    media_playlist_build(&pl, xdk_soundtrack_library(), game_songs(), play_mode(), s_sel, s_seed++);
    if (s_user_pick) {
        c = media_playlist_seek(&pl, req);      /* the pause menu's pick plays */
    } else if (s_have_last && !pl.shuffle) {
        /* PLAY IN ORDER (maintainer, 2026-11-05): a new stream (level select,
         * Retry) goes on with the song after the last one played, as the
         * console's music does -- never back to the first; a song that is not
         * in this list (the source changed) starts the list */
        int found = 0, i;
        for (i = 0; i < pl.n; i++)
            if (pl.items[i].album == s_last.album && pl.items[i].track == s_last.track)
                found = 1;
        if (found) {
            media_playlist_seek(&pl, s_last);
            c = media_playlist_next(&pl);
        } else {
            c = media_playlist_current(&pl);
        }
    } else if (pl.shuffle) {
        c = media_playlist_current(&pl);        /* a fresh shuffled pass: its first song */
    } else {
        c = media_playlist_seek(&pl, req);      /* the session's first stream: the game's own pick */
    }
    if (c && (c->album != req.album || c->track != req.track))
        req = *c;
    obj = xdk_music_create(req, &pl, pwfx);
    if (obj) {
        s_last = req;
        s_have_last = 1;
    }
    if (pp)
        GUEST32(pp) = obj;
    g_eax = obj ? 0u : 0x80004005u;
    g_esp += 4u + 4u * 8u;
}

/* ── Pause menu -> SOUND OPTIONS: a NEXT SONG line and the album shown ─────
 *
 * Why: maintainer, 2026-10-24 -- SELECT TRACK steps only inside the chosen
 * soundtrack, so in "shuffle all" there was no way to the next song; and the
 * SOUNDTRACK line should name the album that is playing.
 * The in-game menus (built by 0x000FFE00 at a level start) are objects from
 * 0x0015A8F0 + 0x000FF540: +6 the selected line (byte), +9 the line count
 * (byte), +0x10 / +0x14 layout, lines of 0x18 bytes from +0x18 (+0 text,
 * +0xC height, +0xD shown, +0xE selectable); 0x000FF600 lays a menu out (ecx).
 * SOUND OPTIONS is [0x00306914]: SFX LEVEL, MUSIC LEVEL, SELECT TRACK,
 * SOUNDTRACK, BACK -- the pause UI (0x00104210, once per frame from
 * 0x001AF080; state [0x003068D4] = 0x2F while SOUND OPTIONS is open) tells
 * the lines apart by their text (0x000FF880 compares strings), so a line
 * added before BACK moves nothing for it. NEXT SONG: A on it moves a
 * playlist on (toolkit xdk_music_skip), in the original mode it asks the
 * game for its next track (0x001A98B0(1), what Retry does). The SOUNDTRACK
 * line prints the name 0x001A9C50 (ecx = soundtrack, 0 = TONY HAWK) gives;
 * while a playlist plays, it names the album of the song that is playing,
 * and SELECT TRACK shows that song's number (maintainer, 2026-10-25).
 * Verified: pause-menu runs, docs/status.md. */
#define SND_MENU    0x00306914u
#define UI_STATE    0x003068D4u
#define ST_LINE     0x002F97ECu                     /* "SOUNDTRACK: <name>", 28 bytes */
#define TRACK_LINE  0x002F97D0u                     /* "SELECT TRACK: <n>", 28 bytes (sub_001DBF50) */
#define PAD_A(k)    (0x00305369u + (uint32_t)(k) * 0x18Cu)

static uint32_t s_next_text;                       /* guest "NEXT SONG" */
static uint32_t s_mode_text;                       /* guest "PLAYBACK MODE: IN ORDER" / ": SHUFFLE" (32 bytes) */

static void mode_text(void)
{
    if (s_mode_text)
        snprintf((char *)GPTR(s_mode_text), 32, "PLAYBACK MODE: %s", s_shuffle ? "SHUFFLE" : "IN ORDER");
}

/* SOUND OPTIONS in the maintainer's order (2026-11-05): SOUNDTRACK, PLAYBACK
 * MODE, SFX LEVEL, MUSIC LEVEL, SELECT TRACK, NEXT TRACK, BACK (no blank line:
 * maintainer, same day). The game builds SFX LEVEL, MUSIC LEVEL, SELECT
 * TRACK, SOUNDTRACK, BACK; its UI tells the lines apart by their text, so the
 * line records (0x18 bytes) can be put in any order; ours take BACK's record
 * as the template. Once per build: only the game's five lines are reordered. */
static void sound_menu_order(uint32_t menu)
{
    uint8_t rec[7][0x18];
    uint32_t n = *GPTR(menu + 9), i;
    if (!s_next_text || !s_mode_text || n != 5)
        return;                                     /* not the game's five lines (or ours already) */
    if (GUEST32(menu + 0x18u + 2u * 0x18u) != TRACK_LINE || GUEST32(menu + 0x18u + 3u * 0x18u) != ST_LINE)
        return;                                     /* another layout than the one checked: left as it is */
#define REC(k) GPTR(menu + 0x18u + (uint32_t)(k) * 0x18u)
    memcpy(rec[0], REC(3), 0x18);                   /* SOUNDTRACK */
    memcpy(rec[1], REC(4), 0x18);                   /* PLAYBACK MODE (BACK's record) */
    memcpy(rec[2], REC(0), 0x18);                   /* SFX LEVEL */
    memcpy(rec[3], REC(1), 0x18);                   /* MUSIC LEVEL */
    memcpy(rec[4], REC(2), 0x18);                   /* SELECT TRACK */
    memcpy(rec[5], REC(4), 0x18);                   /* NEXT TRACK */
    memcpy(rec[6], REC(4), 0x18);                   /* BACK */
    memcpy(rec[1], &s_mode_text, 4);
    memcpy(rec[5], &s_next_text, 4);
    for (i = 0; i < 7; i++)
        memcpy(REC(i), rec[i], 0x18);
#undef REC
    *GPTR(menu + 9) = 7;
    *GPTR(menu + 6) = 0;                            /* the cursor on SOUNDTRACK */
}

extern void sub_000FF600_gen(void);
void pause_quit_line(uint32_t menu);               /* recomp_manual.c: QUIT GAME in the pause menu */
void sub_000FF600(void)
{
    pause_quit_line(g_ecx);
    if (on() && g_ecx && g_ecx == GUEST32(SND_MENU)) {
        if (!s_next_text && (s_next_text = xbox_HeapAlloc(16, 4)) != 0)
            memcpy(GPTR(s_next_text), "NEXT TRACK", 11);
        if (!s_mode_text)
            s_mode_text = xbox_HeapAlloc(32, 4);
        mode_text();
        sound_menu_order(g_ecx);
    }
    sub_000FF600_gen();
}

static uint32_t s_name_buf;

/* 0x001A9C50 (ecx = soundtrack; returns eax = the name, plain ret): the name
 * for the SOUNDTRACK line. Since 2026-11-05 (maintainer) always the source
 * chosen in GENERAL OPTIONS -> SOUNDTRACKS, upper case: the album's own
 * spelling ("Nu Med") came out in the font's small lower-case letters, a
 * smaller line than the others; and the line is the source picker now. */
extern void sub_001A9C50_gen(void);
void sub_001A9C50(void)
{
    if (on()) {
        if (!s_name_buf)
            s_name_buf = xbox_HeapAlloc(64, 4);
        if (s_name_buf) {
            thps_st_choice_text(thps_st_choice(), (char *)GPTR(s_name_buf), 64);
            g_eax = s_name_buf;
            g_esp += 4;                             /* ret */
            return;
        }
    }
    sub_001A9C50_gen();
}

/* The SOUNDTRACK line (the game prints it when the screen opens; rewritten
 * every frame while SOUND OPTIONS is open): since 2026-11-05 (maintainer) the
 * full source choice of GENERAL OPTIONS -> SOUNDTRACKS -- "SOUNDTRACK: <name>",
 * a name over 10 characters cut to 10 plus "..", as 0x00101790 does. */
static void refresh_st_line(void)
{
    char line[28], name[64];
    const char *label = (const char *)GPTR(GUEST32(0x00289A70u));
    thps_st_choice_text(thps_st_choice(), name, sizeof name);
    if (strlen(name) > 10) {
        name[10] = '.';
        name[11] = '.';
        name[12] = 0;
    }
    snprintf(line, sizeof line, "%.12s: %s", label, name);
    memcpy(GPTR(ST_LINE), line, strlen(line) + 1);
}

/* the music again from the next song in the source / mode just picked: the
 * game's next track (0x001A98B0(1), as Retry), which takes the choice first */
static void restart_music(void)
{
    uint32_t one = 1;
    guest_call(0x001A98B0u, sub_001A98B0, 1, &one);
}

/* 0x00101790 prints the in-game menu texts every frame and formats the
 * SELECT TRACK line itself just before (sprintf 0x001DBF50 at 0x00103360 /
 * 0x00103380, destination 0x002F97D0), so the line is rewritten after that
 * sprintf. While a playlist plays, SELECT TRACK shows the song's number in
 * the album that is playing (a game track: its number in the game's list),
 * like the SOUNDTRACK line names that album. */
extern void sub_001DBF50_gen(void);
void sub_001DBF50(void)
{
    MediaSong cur;
    int pl = 0;
    uint32_t dst = ARG(0);
    sub_001DBF50_gen();
    if (dst == TRACK_LINE && on() && xdk_music_current(&cur, &pl) && pl) {
        char line[28];
        snprintf(line, sizeof line, "%s: %d", (const char *)GPTR(GUEST32(0x00289A6Cu)), cur.track + 1);
        memcpy(GPTR(TRACK_LINE), line, strlen(line) + 1);
    }
}

/* Left / right on SELECT TRACK while a playlist plays: the song before /
 * after in the album that is playing (game music: the level songs), wraps;
 * the playlist goes on from there in its mode. NEXT SONG is the playlist's
 * next (maintainer, 2026-10-25: two functions -- SELECT TRACK picks a song of
 * the album, NEXT SONG follows the mode, in a shuffle the next shuffled one).
 * The game's own step (inside the chosen soundtrack) does not run: the
 * latches are cleared first. Pad k's left / right latches: 0x003053B9 /
 * 0x003053C9 + k * 0x18C, as in Options. */
#define PAD_LEFT(k)  (0x003053B9u + (uint32_t)(k) * 0x18Cu)
#define PAD_RIGHT(k) (0x003053C9u + (uint32_t)(k) * 0x18Cu)

static void select_track_step(void)
{
    MediaSong cur;
    int pl = 0, dir = 0, n;
    uint32_t k;
    for (k = 0; k < 4; k++) {
        if (*GPTR(PAD_LEFT(k)))
            dir = -1;
        if (*GPTR(PAD_RIGHT(k)))
            dir = 1;
    }
    if (!dir || !xdk_music_current(&cur, &pl) || !pl)
        return;
    for (k = 0; k < 4; k++)
        *GPTR(PAD_LEFT(k)) = *GPTR(PAD_RIGHT(k)) = 0;
    n = cur.album >= 0 ? xdk_soundtrack_library()->albums[cur.album].ntracks : game_songs();
    if (n <= 0)
        return;
    cur.track = (cur.track + dir + n) % n;
    xdk_music_jump(cur);
    fprintf(stderr, "[MUSIC] SELECT TRACK %d\n", cur.track + 1);
}

/* the pads' left / right latches this frame: -1, +1 or 0; cleared, so the
 * game's own step does not run */
static int take_lr(void)
{
    int dir = 0;
    uint32_t k;
    for (k = 0; k < 4; k++) {
        if (*GPTR(PAD_LEFT(k)))
            dir = -1;
        if (*GPTR(PAD_RIGHT(k)))
            dir = 1;
    }
    if (dir)
        for (k = 0; k < 4; k++)
            *GPTR(PAD_LEFT(k)) = *GPTR(PAD_RIGHT(k)) = 0;
    return dir;
}

static int take_a(void)
{
    uint32_t k, a = 0;
    for (k = 0; k < 4; k++)
        a |= *GPTR(PAD_A(k));
    if (a)
        for (k = 0; k < 4; k++)
            *GPTR(PAD_A(k)) = 0;
    return a != 0;
}

/* The pause menu's SOUND OPTIONS while open (before the game's frame UI
 * reads the pads; it ignores A on a line it does not know):
 * - NEXT SONG: A = the playlist's next song;
 * - SOUNDTRACK (maintainer, 2026-11-05): left / right step through the full
 *   source list of GENERAL OPTIONS -> SOUNDTRACKS (GAME MUSIC, ALL CUSTOM
 *   SOUNDTRACKS, COMBINED, each album) instead of the game's own list (TONY
 *   HAWK + the albums); saved, the music goes on in the new source at once;
 * - PLAYBACK MODE (maintainer, 2026-11-05; under NEXT SONG): left / right or
 *   A switch IN ORDER / SHUFFLE; saved, the music goes on at once. */
extern void sub_00104210_gen(void);
void classic_frame(void);                         /* recomp_manual.c: classic career screens */
int pause_camera_frame(void);                     /* recomp_manual.c: CAMERA OPTIONS in this screen */
void sub_00104210(void)
{
    uint32_t menu = GUEST32(SND_MENU);
    classic_frame();
    if (pause_camera_frame()) {                   /* our camera lines are open: not the sound lines */
        sub_00104210_gen();
        return;
    }
    if (on() && menu && s_next_text && GUEST32(UI_STATE) == 0x2Fu) {
        uint32_t sel = *GPTR(menu + 6), text = GUEST32(menu + 0x18u + sel * 0x18u);
        int dir;
        if (text == TRACK_LINE)
            select_track_step();
        if (text == s_next_text && take_a()) {
            xdk_music_skip();
            fprintf(stderr, "[MUSIC] NEXT TRACK\n");
        }
        if (text == ST_LINE && (dir = take_lr()) != 0) {
            int n = thps_st_choices();
            thps_st_set_choice((thps_st_choice() + dir + n) % n);
            fprintf(stderr, "[MUSIC] pause SOUNDTRACK -> choice %d\n", thps_st_choice());
            restart_music();
        }
        if (text == s_mode_text && s_mode_text && ((dir = take_lr()) != 0 || take_a())) {
            thps_st_set_mode(!s_shuffle);
            mode_text();
            fprintf(stderr, "[MUSIC] pause PLAYBACK MODE -> %s\n", s_shuffle ? "shuffle" : "in order");
            restart_music();
        }
        refresh_st_line();
    }
    sub_00104210_gen();
}

/* ── the WMA media object's methods: host objects go to the toolkit ────── */

#define BIND(addr, fn) \
    extern void sub_##addr##_gen(void); \
    void sub_##addr(void) { if (xdk_music_owns(ARG(0))) fn(); else sub_##addr##_gen(); }

BIND(00067900, xdk_music_AddRef)
/* Release: the song that played to the end of the stream (a playlist moves on
 * inside the object) is the one PLAY IN ORDER goes on from */
extern void sub_00067940_gen(void);
void sub_00067940(void)
{
    if (xdk_music_owns(ARG(0))) {
        remember_current();
        xdk_music_Release();
    } else {
        sub_00067940_gen();
    }
}
BIND(000688B0, xdk_music_GetInfo)
BIND(000688F0, xdk_music_GetStatus)
BIND(000682A0, xdk_music_Process)
BIND(000688E0, xdk_music_Discontinuity)
BIND(00067F80, xdk_music_Flush)
BIND(00068A10, xdk_music_Seek)
BIND(00068A40, xdk_music_GetLength)
BIND(000679B0, xdk_music_GetFormat)
BIND(00068950, xdk_music_DoWork)
