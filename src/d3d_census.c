/* d3d_census.c - calls per second of every D3D8 3947 function the game calls
 * directly (M3.5 step 4a inventory). Built only with -DTHPS_D3D_CENSUS=ON.
 *
 * Generated (scratch tool gen_census.py) from the direct-call scan of the
 * game's .text into the D3D section: 65 targets, minus the two recomp_manual.c
 * already wraps (0x00018F90 swap, 0x00019FE0 fence; the swap is counted there).
 * Each is a strong override of the lifter's weak sub_X (--weak-functions), so
 * this file needs no re-lift -- it is also the test of that mechanism.
 * RECOMP_D3D_CENSUS=<seconds> prints count/inclusive time (outermost D3D call,
 * game thread) of each window to stderr; the
 * guest is paced at 60 Hz, so calls/frame = calls/s / 60. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include <time.h>

static unsigned long now_ms(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (unsigned long)ts.tv_sec * 1000ul + (unsigned long)(ts.tv_nsec / 1000000);
}
#define N_FUNCS 64
static const uint32_t s_va[N_FUNCS] = {
    0x000146A0u, /* 330 call sites */
    0x000116F0u, /* 107 call sites */
    0x000145E0u, /* 93 call sites */
    0x00014610u, /* 34 call sites */
    0x000116B0u, /* 32 call sites */
    0x00011BF0u, /* 28 call sites */
    0x000171F0u, /* 27 call sites */
    0x000131F0u, /* 22 call sites */
    0x00013650u, /* 14 call sites */
    0x00014B70u, /* 13 call sites */
    0x000111B0u, /* 10 call sites */
    0x00014A40u, /* 10 call sites */
    0x00017A40u, /* 9 call sites */
    0x00011150u, /* 7 call sites */
    0x00014D70u, /* 7 call sites */
    0x00017110u, /* 6 call sites */
    0x00011000u, /* 5 call sites */
    0x00015520u, /* 5 call sites */
    0x00014580u, /* 5 call sites */
    0x00014960u, /* 4 call sites */
    0x00013870u, /* 4 call sites */
    0x00013530u, /* 4 call sites */
    0x00016BF0u, /* 4 call sites */
    0x00014EB0u, /* 4 call sites */
    0x00016890u, /* 4 call sites */
    0x00018090u, /* 4 call sites */
    0x00017E10u, /* 4 call sites */
    0x00017E70u, /* 3 call sites */
    0x00015AD0u, /* 3 call sites */
    0x00011F70u, /* 3 call sites */
    0x00014C40u, /* 3 call sites */
    0x00016FF0u, /* 3 call sites */
    0x000128F0u, /* 2 call sites */
    0x00014D30u, /* 2 call sites */
    0x00013350u, /* 2 call sites */
    0x000129B0u, /* 2 call sites */
    0x00018490u, /* 2 call sites */
    0x00011B00u, /* 2 call sites */
    0x00014240u, /* 2 call sites */
    0x00012CA0u, /* 1 call sites */
    0x00014260u, /* 1 call sites */
    0x00013990u, /* 1 call sites */
    0x000139C0u, /* 1 call sites */
    0x00012050u, /* 1 call sites */
    0x00015DF0u, /* 1 call sites */
    0x00015D50u, /* 1 call sites */
    0x00013A20u, /* 1 call sites */
    0x00013A60u, /* 1 call sites */
    0x00017E20u, /* 1 call sites */
    0x00014910u, /* 1 call sites */
    0x000180A0u, /* 1 call sites */
    0x00018330u, /* 1 call sites */
    0x00018590u, /* 1 call sites */
    0x00012240u, /* 1 call sites */
    0x00012260u, /* 1 call sites */
    0x000131A0u, /* 1 call sites */
    0x000131C0u, /* 1 call sites */
    0x00011DA0u, /* 1 call sites */
    0x00014A10u, /* 1 call sites */
    0x00011F50u, /* 1 call sites */
    0x00011BB0u, /* 1 call sites */
    0x00017EB0u, /* 1 call sites */
    0x00019630u, /* 1 call sites */
    0x00015590u, /* 1 call sites */
};
static volatile unsigned s_n[N_FUNCS];
static volatile unsigned long long s_ns[N_FUNCS];
static _Thread_local int s_depth;   /* nested D3D calls count once, at the outermost */

#include <SDL3/SDL_timer.h>
/* SDL's counter: QueryPerformanceCounter on Windows (timespec_get there is
 * too coarse to time a call). */
static unsigned long long now_ns(void) { return SDL_GetTicksNS(); }

static void census_report(void)
{
    static int every = -1;
    static unsigned long t0;
    unsigned long now;
    int i;
    if (every < 0) {
        const char *e = getenv("RECOMP_D3D_CENSUS");
        every = e ? atoi(e) : 0;
    }
    if (every <= 0)
        return;
    now = now_ms();
    if (!t0)
        t0 = now;
    if (now - t0 < (unsigned long)every * 1000ul)
        return;
    fprintf(stderr, "[D3DCENSUS] window %lu ms (depth %d):", now - t0, s_depth);
    for (i = 0; i < N_FUNCS; i++)
        if (s_n[i])
            fprintf(stderr, " %08X=%u/%lluus", s_va[i], s_n[i], s_ns[i] / 1000ull);
    fprintf(stderr, "\n");
    memset((void *)s_n, 0, sizeof s_n);
    memset((void *)s_ns, 0, sizeof s_ns);
    t0 = now;
}

static void hit(int i)
{
    s_n[i]++;
    if (i == 0)
        census_report();
}

/* Readback check (M3.5 reduced 4b): the 5-argument lock candidates log their
 * arguments and whether the locked resource's data (D3D resource +4) lies in
 * a surface the renderer drew into -- a CPU read there would see stale memory.
 * Rate-limited: the first 32 calls of each. */
#if defined(_MSC_VER)
#  define CENSUS_TLS __declspec(thread)
#else
#  define CENSUS_TLS __thread
#endif
extern CENSUS_TLS uint32_t g_esp;
extern ptrdiff_t g_xbox_mem_offset;
extern int nv2a_vk_render_target_at(uint32_t va);
static uint32_t g32(uint32_t va) { return *(volatile uint32_t *)((uintptr_t)va + g_xbox_mem_offset); }
static void lock_log(uint32_t fn, int *n)
{
    uint32_t a0 = g32(g_esp + 4), data;
    if (*n >= 32 || !a0)
        return;
    (*n)++;
    data = g32(a0 + 4);
    fprintf(stderr, "[D3DLOCK] %08X(res %08X, %08X, %08X, %08X, %08X) data %08X%s ret %08X\n", fn, a0,
            g32(g_esp + 8), g32(g_esp + 12), g32(g_esp + 16), g32(g_esp + 20), data,
            nv2a_vk_render_target_at(data) ? " RENDER TARGET" : "", g32(g_esp));
}

extern void sub_000146A0_gen(void);
void sub_000146A0(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(0);
    sub_000146A0_gen();
    if (!--s_depth) s_ns[0] += now_ns() - t0;
}
extern void sub_000116F0_gen(void);
void sub_000116F0(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(1);
    sub_000116F0_gen();
    if (!--s_depth) s_ns[1] += now_ns() - t0;
}
extern void sub_000145E0_gen(void);
void sub_000145E0(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(2);
    sub_000145E0_gen();
    if (!--s_depth) s_ns[2] += now_ns() - t0;
}
extern void sub_00014610_gen(void);
void sub_00014610(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(3);
    sub_00014610_gen();
    if (!--s_depth) s_ns[3] += now_ns() - t0;
}
extern void sub_000116B0_gen(void);
void sub_000116B0(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(4);
    sub_000116B0_gen();
    if (!--s_depth) s_ns[4] += now_ns() - t0;
}
extern void sub_00011BF0_gen(void);
void sub_00011BF0(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(5);
    sub_00011BF0_gen();
    if (!--s_depth) s_ns[5] += now_ns() - t0;
}
extern void sub_000171F0_gen(void);
void sub_000171F0(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(6);
    sub_000171F0_gen();
    if (!--s_depth) s_ns[6] += now_ns() - t0;
}
extern void sub_000131F0_gen(void);
void sub_000131F0(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(7);
    sub_000131F0_gen();
    if (!--s_depth) s_ns[7] += now_ns() - t0;
}
extern void sub_00013650_gen(void);
void sub_00013650(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(8);
    sub_00013650_gen();
    if (!--s_depth) s_ns[8] += now_ns() - t0;
}
extern void sub_00014B70_gen(void);
void sub_00014B70(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(9);
    sub_00014B70_gen();
    if (!--s_depth) s_ns[9] += now_ns() - t0;
}
extern void sub_000111B0_gen(void);
void sub_000111B0(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    { static int n_; lock_log(0x000111B0u, &n_); }
    hit(10);
    sub_000111B0_gen();
    if (!--s_depth) s_ns[10] += now_ns() - t0;
}
extern void sub_00014A40_gen(void);
void sub_00014A40(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(11);
    sub_00014A40_gen();
    if (!--s_depth) s_ns[11] += now_ns() - t0;
}
extern void sub_00017A40_gen(void);
void sub_00017A40(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(12);
    sub_00017A40_gen();
    if (!--s_depth) s_ns[12] += now_ns() - t0;
}
extern void sub_00011150_gen(void);
void sub_00011150(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    { static int n_; lock_log(0x00011150u, &n_); }
    hit(13);
    sub_00011150_gen();
    if (!--s_depth) s_ns[13] += now_ns() - t0;
}
extern void sub_00014D70_gen(void);
void sub_00014D70(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(14);
    sub_00014D70_gen();
    if (!--s_depth) s_ns[14] += now_ns() - t0;
}
extern void sub_00017110_gen(void);
void sub_00017110(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(15);
    sub_00017110_gen();
    if (!--s_depth) s_ns[15] += now_ns() - t0;
}
extern void sub_00011000_gen(void);
void sub_00011000(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    { static int n_; lock_log(0x00011000u, &n_); }
    hit(16);
    sub_00011000_gen();
    if (!--s_depth) s_ns[16] += now_ns() - t0;
}
extern void sub_00015520_gen(void);
void sub_00015520(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(17);
    sub_00015520_gen();
    if (!--s_depth) s_ns[17] += now_ns() - t0;
}
extern void sub_00014580_gen(void);
void sub_00014580(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(18);
    sub_00014580_gen();
    if (!--s_depth) s_ns[18] += now_ns() - t0;
}
extern void sub_00014960_gen(void);
void sub_00014960(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(19);
    sub_00014960_gen();
    if (!--s_depth) s_ns[19] += now_ns() - t0;
}
extern void sub_00013870_gen(void);
void sub_00013870(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(20);
    sub_00013870_gen();
    if (!--s_depth) s_ns[20] += now_ns() - t0;
}
extern void sub_00013530_gen(void);
void sub_00013530(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(21);
    sub_00013530_gen();
    if (!--s_depth) s_ns[21] += now_ns() - t0;
}
extern void sub_00016BF0_gen(void);
void sub_00016BF0(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(22);
    sub_00016BF0_gen();
    if (!--s_depth) s_ns[22] += now_ns() - t0;
}
extern void sub_00014EB0_gen(void);
void sub_00014EB0(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(23);
    sub_00014EB0_gen();
    if (!--s_depth) s_ns[23] += now_ns() - t0;
}
extern void sub_00016890_gen(void);
void sub_00016890(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(24);
    sub_00016890_gen();
    if (!--s_depth) s_ns[24] += now_ns() - t0;
}
extern void sub_00018090_gen(void);
void sub_00018090(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(25);
    sub_00018090_gen();
    if (!--s_depth) s_ns[25] += now_ns() - t0;
}
extern void sub_00017E10_gen(void);
void sub_00017E10(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(26);
    sub_00017E10_gen();
    if (!--s_depth) s_ns[26] += now_ns() - t0;
}
extern void sub_00017E70_gen(void);
void sub_00017E70(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(27);
    sub_00017E70_gen();
    if (!--s_depth) s_ns[27] += now_ns() - t0;
}
extern void sub_00015AD0_gen(void);
void sub_00015AD0(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(28);
    sub_00015AD0_gen();
    if (!--s_depth) s_ns[28] += now_ns() - t0;
}
extern void sub_00011F70_gen(void);
void sub_00011F70(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(29);
    sub_00011F70_gen();
    if (!--s_depth) s_ns[29] += now_ns() - t0;
}
extern void sub_00014C40_gen(void);
void sub_00014C40(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(30);
    sub_00014C40_gen();
    if (!--s_depth) s_ns[30] += now_ns() - t0;
}
extern void sub_00016FF0_gen(void);
void sub_00016FF0(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(31);
    sub_00016FF0_gen();
    if (!--s_depth) s_ns[31] += now_ns() - t0;
}
extern void sub_000128F0_gen(void);
void sub_000128F0(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(32);
    sub_000128F0_gen();
    if (!--s_depth) s_ns[32] += now_ns() - t0;
}
extern void sub_00014D30_gen(void);
void sub_00014D30(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(33);
    sub_00014D30_gen();
    if (!--s_depth) s_ns[33] += now_ns() - t0;
}
extern void sub_00013350_gen(void);
void sub_00013350(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(34);
    sub_00013350_gen();
    if (!--s_depth) s_ns[34] += now_ns() - t0;
}
extern void sub_000129B0_gen(void);
void sub_000129B0(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(35);
    sub_000129B0_gen();
    if (!--s_depth) s_ns[35] += now_ns() - t0;
}
extern void sub_00018490_gen(void);
void sub_00018490(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(36);
    sub_00018490_gen();
    if (!--s_depth) s_ns[36] += now_ns() - t0;
}
extern void sub_00011B00_gen(void);
void sub_00011B00(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(37);
    sub_00011B00_gen();
    if (!--s_depth) s_ns[37] += now_ns() - t0;
}
extern void sub_00014240_gen(void);
void sub_00014240(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(38);
    sub_00014240_gen();
    if (!--s_depth) s_ns[38] += now_ns() - t0;
}
extern void sub_00012CA0_gen(void);
void sub_00012CA0(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(39);
    sub_00012CA0_gen();
    if (!--s_depth) s_ns[39] += now_ns() - t0;
}
extern void sub_00014260_gen(void);
void sub_00014260(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(40);
    sub_00014260_gen();
    if (!--s_depth) s_ns[40] += now_ns() - t0;
}
extern void sub_00013990_gen(void);
void sub_00013990(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(41);
    sub_00013990_gen();
    if (!--s_depth) s_ns[41] += now_ns() - t0;
}
extern void sub_000139C0_gen(void);
void sub_000139C0(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(42);
    sub_000139C0_gen();
    if (!--s_depth) s_ns[42] += now_ns() - t0;
}
extern void sub_00012050_gen(void);
void sub_00012050(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(43);
    sub_00012050_gen();
    if (!--s_depth) s_ns[43] += now_ns() - t0;
}
extern void sub_00015DF0_gen(void);
void sub_00015DF0(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(44);
    sub_00015DF0_gen();
    if (!--s_depth) s_ns[44] += now_ns() - t0;
}
extern void sub_00015D50_gen(void);
void sub_00015D50(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(45);
    sub_00015D50_gen();
    if (!--s_depth) s_ns[45] += now_ns() - t0;
}
extern void sub_00013A20_gen(void);
void sub_00013A20(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(46);
    sub_00013A20_gen();
    if (!--s_depth) s_ns[46] += now_ns() - t0;
}
extern void sub_00013A60_gen(void);
void sub_00013A60(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(47);
    sub_00013A60_gen();
    if (!--s_depth) s_ns[47] += now_ns() - t0;
}
extern void sub_00017E20_gen(void);
void sub_00017E20(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(48);
    sub_00017E20_gen();
    if (!--s_depth) s_ns[48] += now_ns() - t0;
}
extern void sub_00014910_gen(void);
void sub_00014910(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(49);
    sub_00014910_gen();
    if (!--s_depth) s_ns[49] += now_ns() - t0;
}
extern void sub_000180A0_gen(void);
void sub_000180A0(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(50);
    sub_000180A0_gen();
    if (!--s_depth) s_ns[50] += now_ns() - t0;
}
extern void sub_00018330_gen(void);
void sub_00018330(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(51);
    sub_00018330_gen();
    if (!--s_depth) s_ns[51] += now_ns() - t0;
}
extern void sub_00018590_gen(void);
void sub_00018590(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(52);
    sub_00018590_gen();
    if (!--s_depth) s_ns[52] += now_ns() - t0;
}
extern void sub_00012240_gen(void);
void sub_00012240(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(53);
    sub_00012240_gen();
    if (!--s_depth) s_ns[53] += now_ns() - t0;
}
extern void sub_00012260_gen(void);
void sub_00012260(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(54);
    sub_00012260_gen();
    if (!--s_depth) s_ns[54] += now_ns() - t0;
}
extern void sub_000131A0_gen(void);
void sub_000131A0(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(55);
    sub_000131A0_gen();
    if (!--s_depth) s_ns[55] += now_ns() - t0;
}
extern void sub_000131C0_gen(void);
void sub_000131C0(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(56);
    sub_000131C0_gen();
    if (!--s_depth) s_ns[56] += now_ns() - t0;
}
extern void sub_00011DA0_gen(void);
void sub_00011DA0(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(57);
    sub_00011DA0_gen();
    if (!--s_depth) s_ns[57] += now_ns() - t0;
}
extern void sub_00014A10_gen(void);
void sub_00014A10(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(58);
    sub_00014A10_gen();
    if (!--s_depth) s_ns[58] += now_ns() - t0;
}
extern void sub_00011F50_gen(void);
void sub_00011F50(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(59);
    sub_00011F50_gen();
    if (!--s_depth) s_ns[59] += now_ns() - t0;
}
extern void sub_00011BB0_gen(void);
void sub_00011BB0(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(60);
    sub_00011BB0_gen();
    if (!--s_depth) s_ns[60] += now_ns() - t0;
}
extern void sub_00017EB0_gen(void);
void sub_00017EB0(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(61);
    sub_00017EB0_gen();
    if (!--s_depth) s_ns[61] += now_ns() - t0;
}
extern void sub_00019630_gen(void);
void sub_00019630(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(62);
    sub_00019630_gen();
    if (!--s_depth) s_ns[62] += now_ns() - t0;
}
extern void sub_00015590_gen(void);
void sub_00015590(void)
{
    unsigned long long t0 = s_depth++ ? 0 : now_ns();
    hit(63);
    sub_00015590_gen();
    if (!--s_depth) s_ns[63] += now_ns() - t0;
}

/* M4 level probe: the game's time read 0x001B9DB6 (KeQueryPerformanceCounter
 * wrapper, 65 callers) runs ~70 M times a second in a level -- a spin. Counts
 * its return addresses so the spinning loop can be found; printed with the
 * census window. */
#define QPC_SLOTS 64
static uint32_t s_qpc_ra[QPC_SLOTS], s_qpc_n[QPC_SLOTS];
static unsigned long s_qpc_t0;
extern void sub_001B9DB6_gen(void);
void sub_001B9DB6(void)
{
    uint32_t ra = g32(g_esp);
    unsigned i = (ra * 2654435761u) >> 26, k;
    unsigned long now;
    for (k = 0; k < QPC_SLOTS; k++, i = (i + 1) % QPC_SLOTS) {
        if (s_qpc_ra[i] == ra || !s_qpc_ra[i]) {
            s_qpc_ra[i] = ra;
            s_qpc_n[i]++;
            break;
        }
    }
    if ((s_qpc_n[i] & 0xFFFFF) == 0) {
        now = now_ms();
        if (!s_qpc_t0)
            s_qpc_t0 = now;
        if (now - s_qpc_t0 >= 5000) {
            fprintf(stderr, "[QPCPROBE] %lu ms:", now - s_qpc_t0);
            for (k = 0; k < QPC_SLOTS; k++)
                if (s_qpc_n[k] > 1000)
                    fprintf(stderr, " %08X=%u", s_qpc_ra[k], s_qpc_n[k]);
            fprintf(stderr, "\n");
            memset(s_qpc_n, 0, sizeof s_qpc_n);
            s_qpc_t0 = now;
        }
    }
    sub_001B9DB6_gen();
}

/* The spinning caller: 0x0015AFA0, the level loop's frame limiter (spins on
 * the time read until 0x4009 us = 16.39 ms have passed since the last frame).
 * Its inclusive time per window = spin time, to separate it from game logic. */
static unsigned long long s_lim_ns;
static unsigned s_lim_n;
static unsigned long s_lim_t0;
extern void sub_0015AFA0_gen(void);
void sub_0015AFA0(void)
{
    unsigned long long t0 = now_ns();
    unsigned long now;
    sub_0015AFA0_gen();
    s_lim_ns += now_ns() - t0;
    s_lim_n++;
    now = now_ms();
    if (!s_lim_t0)
        s_lim_t0 = now;
    if (now - s_lim_t0 >= 5000) {
        fprintf(stderr, "[LIMITER] %lu ms: %u calls, %.3f ms spin per call\n", now - s_lim_t0, s_lim_n,
                s_lim_n ? (double)s_lim_ns / 1e6 / s_lim_n : 0.0);
        s_lim_ns = 0;
        s_lim_n = 0;
        s_lim_t0 = now;
    }
}
