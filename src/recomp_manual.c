/**
 * THPS 2X - manual function overrides and ICALL diagnostics.
 *
 * Origin: sp00nznet/xboxrecomp templates/new-game/src/recomp_manual.c
 *         (commit 193e299, MIT), see docs/sources.md.
 *
 * Rule (CLAUDE.md): bugs are fixed in the lifter, never in generated C.
 * Workarounds go here as overrides, each with a comment: why, address,
 * how it was verified.
 *
 * This file provides:
 *   - recomp_lookup_manual()  : intercept specific Xbox VAs with hand-written code
 *   - recomp_icall_fail_log() : log when an indirect call target can't be resolved
 *   - ICALL trace ring buffer  : globals used by the RECOMP_ICALL macro
 *
 * The recomp pipeline generates an auto-dispatch table (recomp_lookup) that
 * resolves most function addresses. recomp_lookup_manual() is called FIRST,
 * giving you a chance to override any function with a custom implementation.
 *
 * Common reasons to add manual overrides:
 *   - Trace a function to understand call flow (wrap the generated version)
 *   - Fix a function the lifter translated incorrectly
 *   - Stub out a function that crashes (return early, set eax to a safe value)
 *   - Redirect a function to a native implementation (e.g., skip CRT init)
 *   - Intercept D3D/audio calls for custom rendering or sound
 */

#include <stdio.h>
#include <stdint.h>

#include "xdk_xinput.h"
#include "xdk_dsound.h"

/* â”€â”€ ICALL trace ring buffer â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

/*
 * These globals are written by the RECOMP_ICALL macro (defined in
 * recomp_types.h) every time an indirect call is dispatched. When a
 * crash occurs, the VEH handler or recomp_icall_fail_log() can dump
 * the last 16 call targets to help you trace what happened.
 *
 * The runtime owns them: xbox_kernel defines all three in
 * src/kernel/xbox_memory_layout.c, and recomp_types.h declares them extern.
 * Declare, do not define -- a definition here as well is a duplicate symbol,
 * and a project copied from this template failed to link on all three:
 *
 *   xbox_memory_layout.obj : error LNK2005: g_icall_count already defined
 *                            in recomp_manual.obj
 */
extern volatile uint32_t g_icall_trace[16];
extern volatile uint32_t g_icall_trace_idx;
extern volatile uint64_t g_icall_count;

typedef void (*recomp_func_t)(void);

/* â”€â”€ Register state (defined in xbox_memory_layout.c) â”€â”€â”€â”€â”€â”€â”€â”€ */

/* Thread-local like the runtime defines them: a plain extern of a TLS
 * variable resolves to the TLS template, not the calling thread's copy (see
 * the toolkit template's main.c). Same spelling as xbox_memory_layout.h. */
#if defined(_MSC_VER)
#  define THPS_TLS __declspec(thread)
#else
#  define THPS_TLS __thread
#endif
#include <stddef.h>
#include <string.h>
#include <stdatomic.h>
extern THPS_TLS uint32_t g_eax, g_esp;
extern ptrdiff_t g_xbox_mem_offset;
#define GUEST32(va) (*(volatile uint32_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))

/* â”€â”€ Manual function overrides â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

/*
 * Return a function pointer to override the given Xbox VA, or NULL
 * to fall through to the auto-generated dispatch table.
 *
 * This is called on every indirect call (RECOMP_ICALL) and every
 * direct call through the dispatch table, so keep it fast. A chain
 * of if-statements on uint32_t compiles to a simple comparison
 * sequence; for large override tables, consider a sorted array
 * with binary search.
 *
 * Examples of common override patterns:
 *
 *   // Trace wrapper: log entry/exit around the generated function
 *   extern void sub_00012345(void);
 *   static void traced_sub_00012345(void) {
 *       fprintf(stderr, "[TRACE] sub_00012345 entered, eax=0x%08X\n", g_eax);
 *       sub_00012345();
 *       fprintf(stderr, "[TRACE] sub_00012345 returned, eax=0x%08X\n", g_eax);
 *   }
 *
 *   // Stub: skip a function entirely (return 0 in eax)
 *   static void stub_00067890(void) {
 *       g_eax = 0;
 *   }
 *
 *   // Fix: replace a broken lifted function with correct C
 *   static void fixed_sub_000ABCDE(void) {
 *       // Read arguments from stack/registers per calling convention
 *       uint32_t arg1 = g_ecx;
 *       uint32_t arg2 = MEM32(g_esp + 4);
 *       // ... correct implementation ...
 *       g_eax = result;
 *   }
 */
recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    /*
     * TODO: Add your overrides here. Examples:
     *
     * if (xbox_va == 0x00012345) return traced_sub_00012345;
     * if (xbox_va == 0x00067890) return stub_00067890;
     * if (xbox_va == 0x000ABCDE) return fixed_sub_000ABCDE;
     */

    (void)xbox_va;
    return (recomp_func_t)0;
}

/* â”€â”€ ICALL failure logging â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

/*
 * Called when RECOMP_ICALL cannot resolve a target address.
 * This usually means one of:
 *   - A vtable dispatch to an address not in the dispatch table
 *   - A function pointer loaded from uninitialized or corrupt memory
 *   - A kernel thunk address that the bridge doesn't handle
 *
 * During early bring-up you will see many of these. Most are harmless
 * (the ICALL macro pops the dummy return address and continues).
 * Focus on the ones that cause crashes or incorrect behavior.
 */
void recomp_icall_fail_log(uint32_t va)
{
    /* Rate-limited per address: a missing vtable target is called every frame
     * (0x0011F760 wrote 850,000 log lines in two minutes). The first three
     * with the call history, then the count at each power of two. */
    static uint32_t seen_va[32];
    static unsigned seen_n[32];
    unsigned k, n = 0;
    for (k = 0; k < 32 && seen_va[k] && seen_va[k] != va; k++)
        ;
    if (k < 32) {
        seen_va[k] = va;
        n = ++seen_n[k];
    }
    if (n > 3) {
        if ((n & (n - 1)) == 0)
            fprintf(stderr, "[ICALL] Failed to resolve VA 0x%08X: %u times so far\n", va, n);
        return;
    }
    fprintf(stderr, "[ICALL] Failed to resolve VA 0x%08X (total calls: %llu)\n",
            va, (unsigned long long)g_icall_count);

    /* Dump last 16 call targets from the ring buffer */
    fprintf(stderr, "  Recent ICALL targets:\n");
    for (int i = 0; i < 16; i++) {
        int idx = (g_icall_trace_idx - 16 + i) & 15;
        if (g_icall_trace[idx])
            fprintf(stderr, "    [%2d] 0x%08X\n", i, g_icall_trace[idx]);
    }
    fflush(stderr);
}

/* An indirect call whose target is not code: a null or wild function pointer.
 *
 * Skipping these is right -- calling a data address is worse -- but skipping
 * them *silently* is not. They almost always arrive inside a loop, so the
 * symptom is a hang with no output rather than a diagnosable null vtable call.
 *
 * Rate-limited per address: a spin can produce millions of these, and the
 * useful information is which addresses occur, not how often.
 */
void recomp_icall_not_code_log(uint32_t va)
{
    enum { SLOTS = 16 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS];
    static int count;
    int i;

    for (i = 0; i < count; i++)
        if (seen[i] == va)
            break;
    if (i == count) {
        if (count == SLOTS)
            return;
        seen[count] = va;
        hits[count] = 0;
        count++;
    }
    hits[i]++;
    /* Report at 1, 10, 100, 1000 ... rather than once. A single line says a
     * wild pointer was skipped; the progression says it is being skipped in a
     * loop, which is the difference between a curiosity and the reason the
     * title is hung. */
    {
        uint64_t n = hits[i];
        while (n >= 10 && n % 10 == 0)
            n /= 10;
        if (n != 1)
            return;
    }
    /* The guest return address is still on top of the guest stack here:
     * RECOMP_ICALL pops it only after this returns. It names the call site,
     * which the target (0 for a null pointer) never does. */
    fprintf(stderr, "[ICALL] target 0x%08X is not code -- skipped %llu time(s) "
                    "(null or wild function pointer, at call #%llu, "
                    "returning to 0x%08X)\n",
            va, (unsigned long long)hits[i],
            (unsigned long long)g_icall_count,
            g_esp ? GUEST32(g_esp) : 0u);
    fflush(stderr);
}

/* â”€â”€ Untranslated instructions â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
 *
 * The lifter emits RECOMP_UNIMPL(text, va) at every instruction it has no
 * translation for, in place of the bare comment it used to leave. The
 * instruction is still a no-op; this only stops the omission being silent.
 * RECOMP_UNIMPL_TRAP=1 aborts at the first hit, at the guest address of the
 * cause rather than wherever the damage surfaces. */
#include <stdlib.h>
#include <math.h>

void recomp_unimpl(const char *text, uint32_t va)
{
    static int printed;
    const char *trap = getenv("RECOMP_UNIMPL_TRAP");
    int stop = trap && *trap && *trap != '0';

    if (printed < 50 || stop) {
        printed++;
        fprintf(stderr,
                "[UNIMPL] untranslated instruction REACHED: `%s` at 0x%08X"
                " (a no-op; set RECOMP_UNIMPL_TRAP=1 to stop here)\n",
                text, va);
        fflush(stderr);
    }
    if (stop) abort();
}

/* â”€â”€ D3D fence wait, sub_00019FE0 (D3D8 3947) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
 *
 * Why: with the toolkit fork's pushbuffer executor (RECOMP_PB_EXEC, M2) the
 * GPU time D3D waits on must be the one the executor writes, once it has
 * executed everything before it -- not the runtime's mirror, which reported
 * every submission consumed at once and let D3D reuse ring space early.
 * Address: sub_00019FE0 spins at 0x0001A094..0x0001A09C until the time at
 * *(dev+0x3F0) reaches the target; dev = g_pDevice at 0x00028030 (see main.c).
 * So the semaphore is the pointer at dev+0x3F0: hand it to the executor
 * (NV2A semaphore releases land there), then run the original wait.
 * Same fix as Burnout 3's BlockOnTime (burnout3-sw 0bf84b5,
 * src/recomp_manual.c).
 * Verified: M2 stage 4 run, docs/status.md.
 *
 * Second part (M3 stage 2): the wait itself, with the executor. The original
 * has two ways to wait (time = [esp+4]): a target close behind the GPU is
 * spun on at 0x0001A094 (semaphore vs. dev+0x1C, unsigned distances); a far
 * one (pushbuffer distance >= 0x8000, sub_00019CD0) pushes WAIT_FOR_IDLE +
 * NOP(0x310), kicks, and sleeps at 0x0001A0B1 on the KEVENT at dev+0x2500
 * until its PGRAPH handler takes the NOP trap and sets it. D3D builds that
 * KEVENT itself (no KeInitializeEvent), so the kernel bridge cannot map it:
 * every wait fails with STATUS_UNSUCCESSFUL and the loop around it never
 * ends. Hit in the front end: Select Player -> A hung in BlockOnResource
 * (sub_0001A2E0, Lock of a vertex buffer, game 0x00180C08) with the GPU idle.
 * RECOMP_TITLE_KEVENTS=1 would map the event but then D3D's init blocks on
 * its vblank event, which nothing sets either (burnout3-sw's D3D handler
 * wrappers are not ported). So: always take the spin path -- same condition,
 * same end state, it only burns the time the original sleeps through. If the
 * fence is the current time it is inserted first (sub_00019F40(0), as the
 * original does at 0x0001A016); otherwise the pushbuffer is kicked
 * (KickOff sub_00019E90, thiscall) so the release is on its way.
 * Verified: M3 stage 2 run (selplayer script), docs/status.md. */
extern void nv2a_pb_set_semaphore_target(uint32_t guest_va);
extern void recomp_spin_wake(void);
extern void xbox_GuestSleepNs(uint64_t ns);
extern void sub_00019FE0_gen(void);
extern void sub_00019F40(void);
extern void sub_00019E90(void);
extern THPS_TLS uint32_t g_ecx;

void sub_00019FE0(void)
{
    static int exec = -1;
    static uint32_t registered;
    uint32_t dev = GUEST32(0x00028030u);
    uint32_t sem = dev ? GUEST32(dev + 0x3F0) : 0;
    uint32_t target = GUEST32(g_esp + 4);

    if (exec < 0) {
        const char *e = getenv("RECOMP_PB_EXEC");
        exec = e && *e && *e != '0';
    }
    if (exec && sem && sem != registered) {
        nv2a_pb_set_semaphore_target(sem);
        fprintf(stderr, "[D3D] GPU semaphore at 0x%08X\n", sem);
        registered = sem;
    }
    if (!exec || !sem || !target) {
        sub_00019FE0_gen();
        return;
    }
    {
        uint32_t entry_esp = g_esp, cur = GUEST32(dev + 0x1C);
        unsigned long long spins = 0;

        if (cur - target < cur - GUEST32(sem)) {
            if (target == cur) {
                g_esp -= 4; GUEST32(g_esp) = 0;              /* flags */
                g_esp -= 4; GUEST32(g_esp) = 0x0001A01Cu;    /* return address */
                sub_00019F40();                              /* ret 4 */
            } else {
                g_ecx = dev;
                g_esp -= 4; GUEST32(g_esp) = 0x0001A01Cu;
                sub_00019E90();                              /* ret */
            }
            g_esp = entry_esp;
            /* The executor thread releases the semaphore; wake it and sleep in
             * 50 us steps instead of spinning (M4, A10: the spin was 18 % of
             * the game thread's busy time in a level, ~0.5 ms a frame). */
            for (;;) {
                cur = GUEST32(dev + 0x1C);
                if (cur - target >= cur - GUEST32(sem))
                    break;
                if (++spins > 64) {
                    recomp_spin_wake();
                    xbox_GuestSleepNs(50000);
                }
                if (spins == 100000ull)
                    fprintf(stderr, "[D3D] fence wait: time %08X still ahead of the GPU "
                            "(semaphore %08X, current %08X)\n", target, GUEST32(sem), cur);
            }
        }
        g_esp = entry_esp + 12;                              /* ret 8 */
    }
}

/* â”€â”€ XInputGetState, sub_0021830B (XPP 3911) â€” diagnostic â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
 *
 * Why: M3 stage 2, the Career sub-menu ignores every press while the main
 * menu takes them. RECOMP_PAD_TRACE=1 logs each change of the state the game
 * gets (packet, buttons, analog A/B/X/Y, LY) with the caller, to tell "the
 * press never reaches the game" from "the menu ignores it".
 * Address: XInputGetState(handle, state) stdcall ret 8 -- copies the packet
 * number from handle+8 and the gamepad from handle+0x14; the only caller is
 * the game's pad poll at 0x001B0FFE. Off by default: a pass-through. */
extern void sub_0021830B_gen(void);

void sub_0021830B(void)
{
    static int on = -1;
    static uint32_t last_buttons = 0xFFFFFFFFu, last_analog, last_rc = 0xFFFFFFFFu;
    uint32_t ret_va = GUEST32(g_esp), state = GUEST32(g_esp + 8);

    if (on < 0) {
        const char *e = getenv("RECOMP_PAD_TRACE");
        on = e && *e && *e != '0';
    }
    if (xdk_xinput_active())
        xdk_XInputGetState();
    else
        sub_0021830B_gen();
    if (on && state) {
        uint32_t buttons = GUEST32(state + 4) & 0xFFFFu;   /* wButtons */
        uint32_t analog = GUEST32(state + 6);              /* A B X Y */
        if (buttons != last_buttons || analog != last_analog || g_eax != last_rc) {
            int i, n = 0;
            fprintf(stderr, "[PADTRACE] rc=%u packet=%u buttons=%04X abxy=%08X ret=0x%08X esp=0x%08X stack:",
                    g_eax, GUEST32(state), buttons, analog, ret_va, g_esp);
            /* Return addresses above the call (FPO frames: a scan). */
            for (i = 0; i < 128 && n < 10; i++) {
                uint32_t v = GUEST32(g_esp + 4 * (uint32_t)i);
                if (v >= 0x00068B40u && v < 0x001F1C50u) {
                    fprintf(stderr, " %08X", v);
                    n++;
                }
            }
            fprintf(stderr, "\n");
            last_buttons = buttons;
            last_rc = g_eax;
            last_analog = analog;
        }
    }
}

/* â”€â”€ D3D swap, sub_00018F90 (D3D8 3947) â€” diagnostic â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
 *
 * Why: M3 stage 2 -- which frame loop runs while a menu ignores the pad.
 * RECOMP_FRAME_STACK=<n>: every n-th swap, log the return addresses on the
 * guest stack (a scan; FPO frames, so stale slots show up too).
 * Address: the swap path's first function, called once per presented frame
 * (waits on the swap throttle at 0x00018FA1). Off by default: a pass-through. */
extern void sub_00018F90_gen(void);
static int thps_fps(void);                    /* level frame limiter, below */
static void thps_clock_tick(void);            /* THPS_CLOCK_LOG (test), below */
static volatile int s_limiter_calls;
static volatile int s_in_level;               /* the last frame ran the level limiter (gameplay) */
static void thps_bind_tick(void);             /* BUTTON MAPPING (end of file) */
static volatile uint64_t s_movie_vbl;         /* the guest vblank of the Bink loop's last pass (thps_movie_wait) */
static volatile int s_menus_stretched;        /* MENUS AT 16:9 = STRETCHED (RECOMP OPTIONS values) */
extern void xbox_GuestSetHz(int hz);
extern int xbox_GuestHz(void);

void sub_00018F90(void)
{
    static int every = -1;
    static unsigned calls;

    if (every < 0) {
        const char *e = getenv("RECOMP_FRAME_STACK");
        every = e ? atoi(e) : 0;
    }
    if (every > 0 && ++calls % (unsigned)every == 0) {
        int i, n = 0;
        fprintf(stderr, "[FRAMESTACK] swap %u esp=0x%08X:", calls, g_esp);
        for (i = 0; i < 400 && n < 16; i++) {
            uint32_t v = GUEST32(g_esp + 4 * (uint32_t)i);
            if (v >= 0x00068B40u && v < 0x001F1C50u) {
                fprintf(stderr, " %08X@%d", v, i * 4);
                n++;
            }
        }
        fprintf(stderr, "\n");
        /* THPS_FRAME_PEEK=<hex va>:<bytes>[,...]: guest memory at the same moment
         * (bytes; the toolkit's RECOMP_PEEK reads dwords at a hang or exit). */
        {
            const char *p = getenv("THPS_FRAME_PEEK");
            while (p && *p) {
                char *end;
                uint32_t va = (uint32_t)strtoul(p, &end, 16), len = 4, k;
                if (*end == ':')
                    len = (uint32_t)strtoul(end + 1, &end, 0);
                if (len > 64) len = 64;
                fprintf(stderr, "[PEEK] %08X:", va);
                for (k = 0; k < len; k++)
                    fprintf(stderr, " %02X", *(volatile uint8_t *)((uintptr_t)g_xbox_mem_offset + va + k));
                fprintf(stderr, "\n");
                p = (*end == ',') ? end + 1 : NULL;
            }
        }
    }
    /* THPS_MEMSCAN=<swap>:<hex value>[,<hex value>...] (debug): at that swap,
     * the addresses in the image (0x00010000..0x00400000) holding each value. */
    {
        static int at = -1;
        static unsigned swaps;
        static uint32_t vals[8];
        static int nv;
        if (at < 0) {
            const char *e = getenv("THPS_MEMSCAN");
            at = 0;
            if (e && *e) {
                char *end;
                at = (int)strtoul(e, &end, 0);
                while (*end == ':' || *end == ',') {
                    if (nv < 8)
                        vals[nv++] = (uint32_t)strtoul(end + 1, &end, 16);
                    else
                        break;
                }
            }
        }
        if (at && ++swaps == (unsigned)at) {
            int k;
            for (k = 0; k < nv; k++) {
                uint32_t va;
                int hits = 0;
                fprintf(stderr, "[MEMSCAN] %08X:", vals[k]);
                for (va = 0x00010000u; va < 0x04000000u && hits < 64; va += 4)
                    if (GUEST32(va) == vals[k]) {
                        fprintf(stderr, " %08X", va);
                        hits++;
                    }
                fprintf(stderr, "\n");
            }
        }
    }
    /* THPS_POKE_AFTER=<swap>:<hex va>:<value> (debug): from that swap on, hold
     * the dword at that value (RECOMP_POKE holds it from the start). */
    {
        static int init;
        static unsigned from, swaps;
        static uint32_t va, val;
        if (!init) {
            const char *e = getenv("THPS_POKE_AFTER");
            init = 1;
            if (e && *e) {
                char *end;
                from = (unsigned)strtoul(e, &end, 0);
                if (*end == ':') va = (uint32_t)strtoul(end + 1, &end, 16);
                if (*end == ':') val = (uint32_t)strtoul(end + 1, &end, 0);
            }
        }
        if (va && ++swaps >= from) {
            if (swaps == from)
                fprintf(stderr, "[POKE] swap %u: %08X = %u (was %u)\n", swaps, va, val, GUEST32(va));
            GUEST32(va) = val;
        }
    }
    /* Level or menu: the limiter above ran since the last swap or not. */
    s_in_level = s_limiter_calls != 0;
    {
        /* Bindings: the lock-out rule (default layout navigates) in menus only. */
        extern void xbox_InputBindSetMenu(int menu);
        xbox_InputBindSetMenu(!s_in_level);
        thps_bind_tick();                          /* BUTTON MAPPING recording a press */
    }
    {
        extern void thps_options_tick(void);       /* RECOMP OPTIONS: render fallback, window rect */
        thps_options_tick();
    }
    {
        /* Widescreen: gameplay at 16:9, menus/movies at 4:3 (see thps_widescreen). */
        extern void xbox_SetPresentWide(int wide);
        static int ws = -1;
        if (ws < 0) {
            const char *e = getenv("THPS_WIDESCREEN");
            ws = e && *e == '1';
        }
        if (ws) {
            extern void thps_ws_apply(void);
            extern uint64_t xbox_GuestVblankCount(void);
            extern void nv2a_vk_set_stretch_43(int on);
            xbox_SetPresentWide(s_in_level);
            thps_ws_apply();
            /* MENUS AT 16:9 stretches menus only, never a movie (maintainer,
             * 2026-10-26): the Bink loop ran within the last half second */
            nv2a_vk_set_stretch_43(s_menus_stretched
                                   && !(s_movie_vbl && xbox_GuestVblankCount() - s_movie_vbl < 30));
        }
    }
    {
        static int menu_hz = -1;
        int fps = thps_fps();
        if (menu_hz < 0)
            menu_hz = xbox_GuestHz();
        /* Also when fps is 60: the 120 setting falls back to 60 when the window
         * leaves a 120 Hz display, mid-level. Same rate = no-op. */
        if (menu_hz > 0)
            xbox_GuestSetHz(s_limiter_calls ? fps : menu_hz);
        if (s_limiter_calls)
            thps_clock_tick();
        s_limiter_calls = 0;
    }
    sub_00018F90_gen();
}

/* â”€â”€ XPP device calls â€” XInput HLE (stage 3, B2) and diagnostic â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
 *
 * Why: the title's XPP library drives the console's USB host controllers;
 * the LLE path emulates those (toolkit src/usb). Stage 3 (toolkit direction
 * 2026-10-14, A10) puts XInput at API level instead: these overrides call the
 * toolkit's XDK HLE layer (src/xdk/xdk_xinput.c), which reads the host input
 * layer directly. RECOMP_XINPUT_HLE=0 runs the title's own code (with
 * RECOMP_USB=1) as before. RECOMP_PAD_TRACE=1 logs opens, closes and
 * device changes either way.
 * Addresses (XPP 3911, identified from the code and the callers in the
 * game's pad init 0x001B08B0 / 0x001B0830 and poll 0x001B0E70):
 *   0x0021782B XInitDevices(n, types) ret 8 (jmp 0x00216E0C)
 *   0x00217830 XGetDevices(type) ret 4        -- title code, record refreshed
 *   0x00217852 XGetDeviceChanges(type, &ins, &rem) ret 0xC -- title code, ditto
 *   0x00218098 XInputOpen(type, port, slot, params) ret 0x10
 *   0x0021810D XInputClose(handle) ret 4
 *   0x00218119 XInputGetCapabilities(handle, caps) ret 8
 *   0x0021830B XInputGetState(handle, state) ret 8 (above)
 *   0x0021837C XInputSetState(handle, feedback) ret 8
 *   0x002183B4 XInputPoll(handle) ret 4
 *   0x00216AA0 XDEVICE_TYPE_GAMEPAD record (bound in input.c)
 * Verified: menu tour and B3 two-pad runs with the HLE on, docs/status.md. */
extern void sub_0021782B_gen(void);
extern void sub_00217830_gen(void);
extern void sub_00217852_gen(void);
extern void sub_00218098_gen(void);
extern void sub_0021810D_gen(void);
extern void sub_00218119_gen(void);
extern void sub_0021837C_gen(void);
extern void sub_002183B4_gen(void);

void sub_0021782B(void)
{
    if (xdk_xinput_active())
        xdk_XInitDevices();
    else
        sub_0021782B_gen();
}

void sub_00217830(void)
{
    if (xdk_xinput_active())
        xdk_UpdateDevices();
    sub_00217830_gen();
}

void sub_00218119(void)
{
    if (xdk_xinput_active())
        xdk_XInputGetCapabilities();
    else
        sub_00218119_gen();
}

void sub_0021837C(void)
{
    if (xdk_xinput_active())
        xdk_XInputSetState();
    else
        sub_0021837C_gen();
}

void sub_002183B4(void)
{
    if (xdk_xinput_active())
        xdk_XInputPoll();
    else
        sub_002183B4_gen();
}

static int pad_trace_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("RECOMP_PAD_TRACE");
        on = e && *e && *e != '0';
    }
    return on;
}

void sub_00217852(void)
{
    uint32_t type = GUEST32(g_esp + 4), pins = GUEST32(g_esp + 8), prem = GUEST32(g_esp + 12);
    uint32_t ret_va = GUEST32(g_esp);
    if (xdk_xinput_active())
        xdk_UpdateDevices();
    sub_00217852_gen();
    if (pad_trace_on() && (GUEST32(pins) || GUEST32(prem)))
        fprintf(stderr, "[PADTRACE] XGetDeviceChanges(type 0x%08X) ins=%X rem=%X ret=0x%08X\n",
                type, GUEST32(pins), GUEST32(prem), ret_va);
}

void sub_00218098(void)
{
    uint32_t type = GUEST32(g_esp + 4), port = GUEST32(g_esp + 8), slot = GUEST32(g_esp + 12);
    uint32_t ret_va = GUEST32(g_esp);
    if (xdk_xinput_active())
        xdk_XInputOpen();
    else
        sub_00218098_gen();
    if (pad_trace_on())
        fprintf(stderr, "[PADTRACE] XInputOpen(type 0x%08X, port %u, slot %u) -> 0x%08X ret=0x%08X\n",
                type, port, slot, g_eax, ret_va);
}

void sub_0021810D(void)
{
    uint32_t h = GUEST32(g_esp + 4), ret_va = GUEST32(g_esp);
    if (pad_trace_on())
        fprintf(stderr, "[PADTRACE] XInputClose(0x%08X) ret=0x%08X\n", h, ret_va);
    if (xdk_xinput_active())
        xdk_XInputClose();
    else
        sub_0021810D_gen();
}

/* â”€â”€ Multiplayer entry with two controllers and no link, sub_001BECBA â”€â”€â”€â”€â”€
 *
 * Why: stage 3, B3. The main menu's Multiplayer entry (check at 0x001238B0)
 * counts the per-player active flags at 0x003054A4 + i*0x18C; at >= 2 it is
 * enabled, otherwise it asks the network link check sub_001BECBA (wraps the
 * PhyGetLinkState thunk 0x0021E7E0) and greys the entry out with no link.
 * After Press Start the game maps only the pad that pressed it (0x001E6A90),
 * so in the main menu the count is always 1 and local split-screen was
 * unreachable without a network cable. The kernel keeps reporting "no link"
 * (System Link stays unavailable); only this one call site, return address
 * 0x001238EC, answers "link" when two or more controllers are connected.
 * Verified: B3 runs with two controllers (docs/status.md). */
extern void sub_001BECBA_gen(void);
extern unsigned xbox_InputConnectedMask(void);

void sub_001BECBA(void)
{
    if (GUEST32(g_esp) == 0x001238ECu) {
        unsigned m = xbox_InputConnectedMask(), n = 0;
        for (; m; m &= m - 1)
            n++;
        if (n >= 2) {
            g_eax = 1;
            g_esp += 4;                                   /* ret */
            return;
        }
    }
    sub_001BECBA_gen();
}

/* â”€â”€ DirectSound â€” HLE (stage 3, B1) and diagnostic â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
 *
 * Why: the title's DSOUND 3925 library drives the MCPX APU, which the LLE path
 * emulates (toolkit src/apu, RECOMP_AC97_READY). Stage 3 (toolkit direction
 * 2026-10-14, A10) puts DirectSound at API level: these overrides call the
 * toolkit's XDK HLE layer (src/xdk/xdk_dsound.c, SDL3 mixer). With the APU
 * absent the library divided by zero in sub_0004C924 once a second pad was
 * connected (stage 3 B3 runs). RECOMP_DSOUND_HLE=0 runs the title's own code
 * (then set RECOMP_AC97_READY=1); RECOMP_DSOUND_TRACE=1 logs every call.
 * Addresses: identified from the code (docs/xbe.md, "XDK entry points"):
 * the C wrappers adjust the interface pointer (IDirectSound this-8,
 * IDirectSoundBuffer this-0x1C, stream this+4); the CDirectSoundStream
 * methods are the XMediaObject vtable at 0x0021EAB0 (second vtable 0x0021EAA4),
 * bound with xdk_dsound_bind() in main.c.
 * Verified: menu tour with RECOMP_DSOUND_WAV recording, docs/status.md. */
extern void sub_0004A19C_gen(void);
extern void sub_0004A278_gen(void);
extern void sub_00049325_gen(void);
extern void sub_000477CE_gen(void);
extern void sub_000477E4_gen(void);
extern void sub_0004A043_gen(void);
extern void sub_0004A067_gen(void);
extern void sub_0004781A_gen(void);
extern void sub_00049ABC_gen(void);
extern void sub_00049AE0_gen(void);
extern void sub_00049137_gen(void);
extern void sub_00049153_gen(void);
extern void sub_00049173_gen(void);
extern void sub_0004918F_gen(void);
extern void sub_00049AF8_gen(void);
extern void sub_00049117_gen(void);
extern void sub_00049A68_gen(void);
extern void sub_00048E9C_gen(void);
extern void sub_000491DF_gen(void);
extern void sub_0004931B_gen(void);
extern void sub_00049972_gen(void);
extern void sub_00049982_gen(void);
extern void sub_000485EF_gen(void);
extern void sub_00048694_gen(void);
extern void sub_000486C9_gen(void);
extern void sub_0004863A_gen(void);
extern void sub_00048667_gen(void);

/* DirectSoundCreate, 3 arg(s) */
void sub_0004A19C(void)
{
    xdk_dsound_trace("DirectSoundCreate", 3);
    if (xdk_dsound_active())
        xdk_DirectSoundCreate();
    else
        sub_0004A19C_gen();
}

/* DirectSoundCreateStream, 2 arg(s) */
void sub_0004A278(void)
{
    xdk_dsound_trace("DirectSoundCreateStream", 2);
    if (xdk_dsound_active())
        xdk_DirectSoundCreateStream();
    else
        sub_0004A278_gen();
}

/* DirectSoundDoWork, 0 arg(s) */
void sub_00049325(void)
{
    xdk_dsound_trace("DirectSoundDoWork", 0);
    if (xdk_dsound_active())
        xdk_DirectSoundDoWork();
    else
        sub_00049325_gen();
}

/* IDirectSound_AddRef, 1 arg(s) */
void sub_000477CE(void)
{
    xdk_dsound_trace("IDirectSound_AddRef", 1);
    if (xdk_dsound_active())
        xdk_IDirectSound_AddRef();
    else
        sub_000477CE_gen();
}

/* IDirectSound_Release, 1 arg(s) */
void sub_000477E4(void)
{
    xdk_dsound_trace("IDirectSound_Release", 1);
    if (xdk_dsound_active())
        xdk_IDirectSound_Release();
    else
        sub_000477E4_gen();
}

/* IDirectSound_CreateSoundBuffer, 4 arg(s) */
void sub_0004A043(void)
{
    xdk_dsound_trace("IDirectSound_CreateSoundBuffer", 4);
    if (xdk_dsound_active())
        xdk_IDirectSound_CreateSoundBuffer();
    else
        sub_0004A043_gen();
}

/* IDirectSound_CreateSoundStream, 4 arg(s) */
void sub_0004A067(void)
{
    xdk_dsound_trace("IDirectSound_CreateSoundStream", 4);
    if (xdk_dsound_active())
        xdk_IDirectSound_CreateSoundStream();
    else
        sub_0004A067_gen();
}

/* IDirectSoundBuffer_Release, 1 arg(s) */
void sub_0004781A(void)
{
    xdk_dsound_trace("IDirectSoundBuffer_Release", 1);
    if (xdk_dsound_active())
        xdk_DSB_Release();
    else
        sub_0004781A_gen();
}

/* IDirectSoundBuffer_Play, 4 arg(s) */
void sub_00049ABC(void)
{
    xdk_dsound_trace("IDirectSoundBuffer_Play", 4);
    if (xdk_dsound_active())
        xdk_DSB_Play();
    else
        sub_00049ABC_gen();
}

/* IDirectSoundBuffer_Stop, 1 arg(s) */
void sub_00049AE0(void)
{
    xdk_dsound_trace("IDirectSoundBuffer_Stop", 1);
    if (xdk_dsound_active())
        xdk_DSB_Stop();
    else
        sub_00049AE0_gen();
}

/* IDirectSoundBuffer_GetStatus, 2 arg(s) */
void sub_00049137(void)
{
    xdk_dsound_trace("IDirectSoundBuffer_GetStatus", 2);
    if (xdk_dsound_active())
        xdk_DSB_GetStatus();
    else
        sub_00049137_gen();
}

/* IDirectSoundBuffer_GetCurrentPosition, 3 arg(s) */
void sub_00049153(void)
{
    xdk_dsound_trace("IDirectSoundBuffer_GetCurrentPosition", 3);
    if (xdk_dsound_active())
        xdk_DSB_GetCurrentPosition();
    else
        sub_00049153_gen();
}

/* IDirectSoundBuffer_SetCurrentPosition, 2 arg(s) */
void sub_00049173(void)
{
    xdk_dsound_trace("IDirectSoundBuffer_SetCurrentPosition", 2);
    if (xdk_dsound_active())
        xdk_DSB_SetCurrentPosition();
    else
        sub_00049173_gen();
}

/* IDirectSoundBuffer_Lock, 8 arg(s) */
void sub_0004918F(void)
{
    xdk_dsound_trace("IDirectSoundBuffer_Lock", 8);
    if (xdk_dsound_active())
        xdk_DSB_Lock();
    else
        sub_0004918F_gen();
}

/* IDirectSoundBuffer_SetBufferData, 3 arg(s) */
void sub_00049AF8(void)
{
    xdk_dsound_trace("IDirectSoundBuffer_SetBufferData", 3);
    if (xdk_dsound_active())
        xdk_DSB_SetBufferData();
    else
        sub_00049AF8_gen();
}

/* IDirectSoundBuffer_SetLoopRegion, 3 arg(s) */
void sub_00049117(void)
{
    xdk_dsound_trace("IDirectSoundBuffer_SetLoopRegion", 3);
    if (xdk_dsound_active())
        xdk_DSB_SetLoopRegion();
    else
        sub_00049117_gen();
}

/* IDirectSoundBuffer_SetFrequency, 2 arg(s) */
void sub_00049A68(void)
{
    xdk_dsound_trace("IDirectSoundBuffer_SetFrequency", 2);
    if (xdk_dsound_active())
        xdk_DSB_SetFrequency();
    else
        sub_00049A68_gen();
}

/* IDirectSoundBuffer_SetVolume, 2 arg(s) */
void sub_00048E9C(void)
{
    xdk_dsound_trace("IDirectSoundBuffer_SetVolume", 2);
    if (xdk_dsound_active())
        xdk_DSB_SetVolume();
    else
        sub_00048E9C_gen();
}

/* IDirectSoundStream_SetVolume, 2 arg(s) */
void sub_000491DF(void)
{
    xdk_dsound_trace("IDirectSoundStream_SetVolume", 2);
    if (xdk_dsound_active())
        xdk_DSS_SetVolume();
    else
        sub_000491DF_gen();
}

/* IDirectSoundStream_Pause, 2 arg(s) */
void sub_0004931B(void)
{
    xdk_dsound_trace("IDirectSoundStream_Pause", 2);
    if (xdk_dsound_active())
        xdk_DSS_Pause();
    else
        sub_0004931B_gen();
}

/* CDirectSoundStream_AddRef, 1 arg(s) */
void sub_00049972(void)
{
    xdk_dsound_trace("CDirectSoundStream_AddRef", 1);
    if (xdk_dsound_active())
        xdk_DSS_AddRef();
    else
        sub_00049972_gen();
}

/* CDirectSoundStream_Release, 1 arg(s) */
void sub_00049982(void)
{
    xdk_dsound_trace("CDirectSoundStream_Release", 1);
    if (xdk_dsound_active())
        xdk_DSS_Release();
    else
        sub_00049982_gen();
}

/* CDirectSoundStream_GetInfo, 2 arg(s) */
void sub_000485EF(void)
{
    xdk_dsound_trace("CDirectSoundStream_GetInfo", 2);
    if (xdk_dsound_active())
        xdk_DSS_GetInfo();
    else
        sub_000485EF_gen();
}

/* CDirectSoundStream_GetStatus, 2 arg(s) */
void sub_00048694(void)
{
    xdk_dsound_trace("CDirectSoundStream_GetStatus", 2);
    if (xdk_dsound_active())
        xdk_DSS_GetStatus();
    else
        sub_00048694_gen();
}

/* CDirectSoundStream_Process, 3 arg(s) */
void sub_000486C9(void)
{
    xdk_dsound_trace("CDirectSoundStream_Process", 3);
    if (xdk_dsound_active())
        xdk_DSS_Process();
    else
        sub_000486C9_gen();
}

/* CDirectSoundStream_Discontinuity, 1 arg(s) */
void sub_0004863A(void)
{
    xdk_dsound_trace("CDirectSoundStream_Discontinuity", 1);
    if (xdk_dsound_active())
        xdk_DSS_Discontinuity();
    else
        sub_0004863A_gen();
}

/* CDirectSoundStream_Flush, 1 arg(s) */
void sub_00048667(void)
{
    xdk_dsound_trace("CDirectSoundStream_Flush", 1);
    if (xdk_dsound_active())
        xdk_DSS_Flush();
    else
        sub_00048667_gen();
}

/* â”€â”€ D3D 3947 spin delay, sub_00019A80 â€” sleep instead of spinning â”€â”€â”€â”€â”€â”€â”€â”€
 *
 * Why: M3.5 reduced 4b (A10: no busy-waiting). The library's GPU waits all
 * spin on this 400-iteration delay loop: the swap throttle (call at
 * 0x00018FB4, waits for "frames completed" dev+0x2518 to catch up), pushbuffer
 * space / BlockUntilIdle (0x0001DE88, 0x0001E010, wait for DMA GET) and
 * BlockOnTime (0x0001FB28, 0x0001FB50, wait for the GPU time). The throttle
 * now waits on the kernel's 60 Hz guest vblank (xbox_GuestFlipWait: retire due
 * flips, else sleep to the next vblank); the others wait for work the
 * executor does synchronously, so a 0.1 ms sleep. No _gen: the delay has no
 * effect to keep. Address: identified from its five callers (docs/d3d8_3947.md).
 * Verified: menu tour at 60 fps, frame-time counter (docs/status.md). */
extern void xbox_GuestFlipWait(void);
extern void xbox_GuestYield(void);

void sub_00019A80(void)
{
    if (GUEST32(g_esp) == 0x00018FB9u)
        xbox_GuestFlipWait();
    else
        xbox_GuestYield();
    g_esp += 4;                                          /* ret */
}

/* â”€â”€ D3D GetDisplayFieldStatus, sub_00013A20 (D3D8 3947) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
 *
 * Why: M4, the Single Session clock stood at 2:00. The game's VSync()
 * (sub_00180AF0, called once per frame by every loop) reads VBlankCount from
 * here ([pStatus+4] = dev+0x2B28 = the miniport's vblank count +0x820), runs
 * its vblank callback (sub_000CDC10, slot 0x00305D88) once per vblank passed
 * (at most 5), and that callback advances the game's time counter
 * 0x0030697C; the level loop adds the per-frame delta (0x003061FC) to the
 * elapsed time 0x00306988 that the HUD clock (sub_000A1060) subtracts from
 * the limit. Only D3D's vblank interrupt handler counts +0x820, and nothing
 * raises that interrupt here, so no vblank ever passed. Answered from the
 * runtime's display clock instead (xbox_DisplayVblankCount: fixed 60 Hz, the
 * same start as the guest vblank, also when the level runs at another rate,
 * THPS_FPS) -- deterministic, no ISR/DPC race. With the interrupt delivered
 * (RECOMP_VBLANK=1, experimental) D3D's own count is used.
 * stdcall ret 4: [esp+4] = D3DFIELD_STATUS* { Field, VBlankCount }.
 * Verified: Single Session clock at 30/60/120 fps (docs/status.md, M4). */
extern void sub_00013A20_gen(void);
extern uint64_t xbox_DisplayVblankCount(void);

void sub_00013A20(void)
{
    static int irq = -1;
    uint32_t status = GUEST32(g_esp + 4);
    if (irq < 0) {
        const char *e = getenv("RECOMP_VBLANK");
        irq = e && strcmp(e, "0");
    }
    sub_00013A20_gen();
    if (!irq && status)
        GUEST32(status + 4) = (uint32_t)xbox_DisplayVblankCount();
}

/* â”€â”€ D3D vblank handler, sub_0001F120 (D3D8 3947) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
 *
 * Why: M4, the Single Session clock stood at 2:00 -- D3D's vblank work
 * (vblank count at block+0x210, the vblank event at +0x1E8, the title's
 * vblank callback at +0x1E4) never ran. With the vblank delivered as an
 * interrupt (toolkit kernel_vblank_tick, RECOMP_VBLANK) the handler ends by
 * writing PCRTC_INTR_0 = 1 and spinning until PMC_INTR_0 bit 24 drops
 * (0x0001F200..0x0001F210); the registers are plain memory here, the
 * write-1-to-clear does nothing and the spin held the timer thread for good
 * (no DPC ran again, the next PGRAPH trap hung the boot). Its callers (the
 * DPC 0x0001F860 via 0x0001F890, D3D's waits) have read the bits already, so
 * clear them first (xbox_Nv2aVblankTaken), as burnout3-sw does for its D3D
 * (src/recomp_manual.c, sub_00356DA0). thiscall: ecx = the miniport block.
 * Verified: Single Session clock, docs/status.md (M4). */
extern void sub_0001F120_gen(void);
extern void xbox_Nv2aVblankTaken(void);

/* One at a time: the DPC (timer thread) and D3D's waits (game thread) both
 * call the handler; run together, both saw the pending flip slot and both
 * counted its retirement (+0x210, which is the swap throttle's "completed"
 * dev+0x2518): completed ran one ahead of submitted and the throttle
 * (submitted - completed >= 2, unsigned) waited for good. On the console the
 * handler runs at DISPATCH on one CPU; burnout3-sw serialises it with its
 * guest lock. Serialised, the second caller finds the slot already cleared. */
static atomic_flag s_vblank_handler_lock = ATOMIC_FLAG_INIT;

static int vblank_trace(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("THPS_VBLANK_TRACE");
        on = e ? atoi(e) : 0;
    }
    return on;
}

void sub_0001F120(void)
{
    uint32_t blk = g_ecx, before;
    while (atomic_flag_test_and_set_explicit(&s_vblank_handler_lock, memory_order_acquire))
        ;                                        /* the handler is short */
    before = GUEST32(blk + 0x210);
    xbox_Nv2aVblankTaken();
    sub_0001F120_gen();
    if (vblank_trace() && GUEST32(blk + 0x210) != before)
        fprintf(stderr, "[VBTRACE] vblank retire %u -> %u (ret 0x%08X)\n", before, GUEST32(blk + 0x210),
                GUEST32(g_esp - 4));
    atomic_flag_clear_explicit(&s_vblank_handler_lock, memory_order_release);
}

/* THPS_VBLANK_TRACE=1: D3D's software-method handler (NOP traps 0x300..0x324),
 * thiscall ecx = block, [esp+4] = method, [esp+8] = data; ret 8. */
extern void sub_0001F300_gen(void);

void sub_0001F300(void)
{
    uint32_t blk = g_ecx, m = GUEST32(g_esp + 4), d = GUEST32(g_esp + 8), before = GUEST32(blk + 0x210);
    sub_0001F300_gen();
    if (vblank_trace())
        fprintf(stderr, "[VBTRACE] swmethod 0x%03X data 0x%08X retire %u -> %u slot %u\n", m, d, before,
                GUEST32(blk + 0x210), GUEST32(blk + 0x1E0));
}

/* â”€â”€ level frame limiter, sub_0015AFA0 â€” sleep instead of spinning â”€â”€â”€â”€â”€â”€â”€â”€
 *
 * Why: M4 (A10: no busy-waiting). The level loop (sub_0015B080) starts each
 * frame here: the original spins on QueryPerformanceCounter (0x001B9DB6,
 * ~9 M calls/s) until 0x4009 us = 16.39 ms -- 1/61 s -- have passed since the
 * last frame's time stamp (low dword at 0x002762DC, ticks -> us factor double
 * at 0x00305168), i.e. one frame per 60 Hz vblank with a margin. It returns
 * the time it waited (us), which the caller turns into a smoothed "fps
 * without the wait" (0x002762D8) and compares with a threshold, so the return
 * value is kept: the whole wait, sleep included. The game logic itself is
 * time based: the caller measures dt (clamped to 83333 us) into a 15-frame
 * ring at 0x002FD540 (preset 16666 us).
 * Here: while the guest vblank runs at the level's rate (default) and the
 * swapchain presents with MAILBOX/IMMEDIATE (toolkit default: mailbox), that
 * vblank is the one pacer -- the swap throttle waits on it -- and the limiter
 * does not wait at all (M5 b, fix B: its sleep to 1.5 ms before the deadline
 * ended ~13 ms into the frame, and the throttle then missed the vblank by a
 * hair: 15-66 doubled frames per 10 min). With FIFO (a device without
 * mailbox, RECOMP_PRESENT=fifo) it keeps that sleep: no wait at all made
 * FIFO's pair mode worse (maintainer's guard, M5). Otherwise -- vblank off
 * (RECOMP_GUEST_HZ=0, the limiter is the pacer) or the one frame after a
 * menu before sub_00018F90 moves the vblank to the level's rate -- it holds
 * the exact deadline with the platform layer's precise wait
 * (host_wait_until_ns: timer, then yields for the last ~1 ms).
 * THPS_LIMITER=0: the original spin, for comparisons.
 * THPS_FPS=<n> (default 60, 10..240): the level's frame rate. The deadline
 * keeps the game's margin (1/(n+1) s), and while the level loop runs the
 * guest vblank -- the one pacer the D3D swap throttle waits on -- runs at
 * n Hz (sub_00018F90 switches it back for the menus, which count frames).
 * Player options: Original (60) and 120 (maintainer 2026-10-24); 30 is a test switch only.
 * Verified: Free Skate level, frame-time counter and process CPU
 * (docs/status.md, M4; fix B: M5). */
#include "platform/host_wait.h"
#include "nv2a_vk.h"                                      /* nv2a_vk_present_fifo */
#include "shader_pack.h"                                  /* SHADERS: the pack index, saved presets */
#include "boot_video.h"                                   /* BOOT ANIMATION: media\boot.mpg */
#include <SDL3/SDL_stdinc.h>                              /* SDL_strcasecmp (SHADERS browser) */
extern void sub_0015AFA0_gen(void);
/* s_limiter_calls (declared above sub_00018F90): limiter runs since the last swap. */

/* ── The 3 s hold after a save is loaded, sub_001205D0 ───────────────────────
 *
 * Why: a busy wait (A10). After reading a career save the game holds its
 * screen for 3000 ms by polling QueryPerformanceCounter in a loop - ~40
 * million kernel calls in 3 s, one core fully loaded (the Steam Deck tester's
 * log: 28 M calls between the intro and Select Player; v0.9.2).
 * Address: sub_001205D0, loop 0x00120B50..0x00120BAD (ebx = 0xBB8 ms), its
 * call of the XAPI wrapper sub_001B9DB6 returns to 0x00120B5A. Found with the
 * toolkit's RECOMP_KCALL_WHERE=126 (KeQueryPerformanceCounter).
 * How: called from there, the wrapper sleeps 1 ms first (no spin); the loop
 * still ends after the game's own 3000 ms, every other time read goes
 * straight through.
 * Verified: docs/status.md (v0.9.2, QPC calls and the [hitch] line with a save). */
extern void sub_001B9DB6_gen(void);
extern void xbox_GuestSleepNs(uint64_t ns);       /* toolkit: the guest waits' high-resolution sleep */
extern void xbox_ft_sleep(double ms);

void sub_001B9DB6(void)
{
    if (GUEST32(g_esp) == 0x00120B5Au) {
        xbox_GuestSleepNs(1000000);
        xbox_ft_sleep(1.0);                       /* the [hitch] line: a sleep, not game work */
    }
    sub_001B9DB6_gen();
}

/* The player's choice, saves\settings.ini frame_rate = original | 120 (the
 * RECOMP OPTIONS FRAME RATE line writes it, M6). 120 on every display at
 * 120 Hz or more (119.88 counts; 144/165 Hz too: 60 judders there just as
 * much, maintainer 2026-10-26); below 120 Hz the level runs at 60 and the log
 * says why, so no frame is rendered that is never shown. The log also says
 * whether 120 divides the display rate evenly. Looked at again every 2 s: a window moved to
 * another display. THPS_FPS=<n> (10..240) forces a rate without that check --
 * the tests on the 60 Hz panel; 30 is a test switch only. */
#include "settings.h"

static int thps_fps_setting(void)
{
    static const char *last;
    float hz = nv2a_vk_display_hz();
    int fps = 60, mult = (int)(hz / 120.0f + 0.5f);
    int even = mult >= 1 && fabsf(hz - 120.0f * (float)mult) <= 0.01f * 120.0f * (float)mult;
    const char *why;
    if (strcmp(settings_get("frame_rate", "original"), "120"))
        why = "original (60)";
    else if (hz <= 0.0f)
        why = "120 asked, display refresh not known -> 60";
    else if (hz < 0.99f * 120.0f)
        why = "120 asked, the display is below 120 Hz -> 60";
    else
        fps = 120, why = "120";
    /* the display's rate only once it is known (v0.9.2: "display 0.00 Hz"
     * at the start confused a tester); again when it becomes known */
    {
        static int known = -1;
        if (why != last || (hz > 0.0f) != known) {
            if (hz <= 0.0f)
                fprintf(stderr, "  frame rate: %s (display refresh not known yet)\n", why);
            else
                fprintf(stderr, "  frame rate: %s (display %.2f Hz, %s)\n", why, hz,
                        even ? "120 divides evenly" : "120 does not divide evenly");
        }
        known = hz > 0.0f;
    }
    last = why;
    return fps;
}

/* THPS_CLOCK_LOG=1 (test, fps120): a [CLOCK] line each time the level's
 * elapsed time (0x00306988, a float of 60 Hz frames; the HUD clock is the limit
 * minus it) passes a whole second -- host ms, that second, the game-time
 * counter 0x0030697C. =2 also the replay recorder / playback calls. */
static volatile uint32_t s_rec_who;           /* the replay recorder's last argument (THPS_CLOCK_LOG=3) */
static unsigned long s_tstep_fixed;           /* level timesteps corrected (thps_timestep_fix) */

static int thps_clock_log(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("THPS_CLOCK_LOG");
        on = e ? atoi(e) : 0;
    }
    return on;
}

static void thps_clock_tick(void)
{
    static int last = -1;
    float el;
    int s;
    if (!thps_clock_log())
        return;
    memcpy(&el, (const void *)((uintptr_t)g_xbox_mem_offset + 0x00306988u), sizeof el);
    s = el >= 0.0f && el < 1e7f ? (int)(el / 60.0f) : -1;
    if (s != last)
        fprintf(stderr, "[CLOCK] t_ms %llu elapsed_s %d frames %.1f tick %u fps %d tstep_fixed %lu\n",
                (unsigned long long)(host_time_ns() / 1000000ull), s, el, GUEST32(0x0030697Cu),
                xbox_GuestHz(), s_tstep_fixed);
    last = s;
    /* =3: the skater's position every level frame (fixed point at +0x56AC of
     * the object the replay recorder gets; physics at 60 / 120). */
    if (thps_clock_log() >= 3 && s_rec_who)
        fprintf(stderr, "[POS] t_us %llu tick %u %d %d %d\n",
                (unsigned long long)(host_time_ns() / 1000ull), GUEST32(0x0030697Cu),
                (int32_t)GUEST32(s_rec_who + 0x56ACu), (int32_t)GUEST32(s_rec_who + 0x56B0u),
                (int32_t)GUEST32(s_rec_who + 0x56B4u));
    /* =4: also velocity (+0x4C), acceleration (+0x58) and the step (part 2). */
    if (thps_clock_log() >= 4 && s_rec_who)
        fprintf(stderr, "[VEL] %d %d %d acc %d %d %d step %d sq %d\n",
                (int32_t)GUEST32(s_rec_who + 0x4Cu), (int32_t)GUEST32(s_rec_who + 0x50u),
                (int32_t)GUEST32(s_rec_who + 0x54u), (int32_t)GUEST32(s_rec_who + 0x58u),
                (int32_t)GUEST32(s_rec_who + 0x5Cu), (int32_t)GUEST32(s_rec_who + 0x60u),
                (int32_t)GUEST32(0x00300850u), (int32_t)GUEST32(0x00300874u));
}

static int thps_replay_playing(void);           /* Watch Replay, below */

/* ── Level timesteps above 60 fps (fps120, part 1) ────────────────────────────
 * Why: the level loop sub_0015B080 turns the frame's time e (us, rounded,
 * at most 83333; the newest entry of the ring 0x2FD540) into integer steps,
 * truncated: the integration step [0x300850] = e*128/16666.6, the timer factor
 * [0x3061F8] = e*2048/16666.6 and the squared step [0x300874] = step^2 >> 8.
 * At 60 the loss is small (127.7 of 128 per frame); at 120 it is 0.7 % for the
 * step and up to 3 % for its square (63^2 >> 8 = 15 for 15.5) -- gravity came
 * out 1.5-2 % weak (docs/status.md, fps120 physics). Here, only while the level
 * runs at a rate other than 60: the same three values exact, the fractions
 * carried from frame to frame, so their sum per second is right. At 60 the
 * game's own truncation stays, bit for bit (maintainer, 2026-10-26). Applied
 * right after sub_0015B080 (its timesteps are its last work; the physics runs
 * in its caller), together with the step clock it accumulates from the step
 * ([0x2FD5C4], [0x2FD504], each player's [+0x5568] in mode 9), and only when
 * the game took the plain path: the value it wrote equals the truncation of e
 * (no pause constant, slow motion 1.0, no x1.25 flag). THPS_TIMESTEP_FIX=0: off.
 * Verified: docs/status.md (fps120 physics: corrections 0 at 60). */
static int thps_fps(void);

static void thps_timestep_fix(void)
{
    static int on = -1;
    static double acc_s, acc_f, acc_q;
    uint32_t idx = GUEST32(0x003061F4u), e, s_game, f_game;
    double xs, xf;
    int32_t s, f, q;
    if (on < 0) {
        const char *v = getenv("THPS_TIMESTEP_FIX");
        on = !(v && *v == '0');
    }
    if (!on || thps_fps() == 60 || idx > 14)
        return;
    e = GUEST32(0x002FD540u + ((idx + 14u) % 15u) * 4u);
    s_game = (uint32_t)((((uint64_t)(e << 8) * 0xFBA92759ull) >> 32) >> 15);
    f_game = (uint32_t)((((uint64_t)(e << 12) * 0xFBA92759ull) >> 32) >> 15);
    if (GUEST32(0x00300850u) != s_game || GUEST32(0x003061F8u) != f_game)
        return;                                              /* pause / slow motion / x1.25: as the game */
    xs = (double)e * 256.0 * (double)0xFBA92759u / 140737488355328.0;   /* 2^47 */
    xf = xs * 16.0;
    acc_s += xs;
    acc_f += xf;
    acc_q += xs * xs / 256.0;
    s = (int32_t)acc_s;
    f = (int32_t)acc_f;
    q = (int32_t)acc_q;
    acc_s -= s;
    acc_f -= f;
    acc_q -= q;
    if (!s)
        s = 1;
    if (!q)
        q = 1;
    {
        /* The loop's step clock: [0x2FD5C4] += 2 * step, [0x2FD504] = that >> 8
         * (60 Hz frames), its change added to each player's [+0x5568] in mode 9. */
        int32_t acc = (int32_t)GUEST32(0x002FD5C4u) + 2 * (s - (int32_t)s_game);
        int32_t t = acc >> 8, d = t - (int32_t)GUEST32(0x002FD504u);
        uint32_t k, skip = GUEST32(0x00306B60u);
        GUEST32(0x002FD5C4u) = (uint32_t)acc;
        GUEST32(0x002FD504u) = (uint32_t)t;
        if (d && GUEST32(0x00276194u) == 9)
            for (k = 0; k < 8; k++)
                if (GUEST32(0x002FD50Cu + k * 4u) && k != skip) {
                    uint32_t pl = GUEST32(0x002F8D94u + k * 4u);
                    GUEST32(pl + 0x5568u) += (uint32_t)d;
                }
    }
    GUEST32(0x00300850u) = (uint32_t)s;
    GUEST32(0x003061F8u) = (uint32_t)f;
    GUEST32(0x00300874u) = (uint32_t)q;
    s_tstep_fixed++;
}

/* The level loop's frame step, sub_0015B080: its timesteps are its last work
 * (after them only the fps text, then ret); the physics runs in its caller. */
extern void sub_0015B080_gen(void);

void sub_0015B080(void)
{
    sub_0015B080_gen();
    thps_timestep_fix();
}

static uint64_t s_fps_next_ns;                /* next display / setting check; 0 = now (FRAME RATE picked) */

static int thps_fps(void)
{
    static int forced = -1, fps = 60;
    uint64_t now;
    if (forced < 0) {
        const char *e = getenv("THPS_FPS");
        forced = 0;
        if (e) {
            forced = atoi(e);
            if (forced < 10) forced = 10;
            if (forced > 240) forced = 240;
            fprintf(stderr, "  THPS_FPS: level at %d fps (forced, no display check)\n", forced);
        }
    }
    if (!forced) {
        now = host_time_ns();
        if (now >= s_fps_next_ns) {
            s_fps_next_ns = now + 2000000000ull;
            fps = thps_fps_setting();
        }
    }
    /* A replay plays at 60: playback takes one recorded frame per frame. */
    if ((forced ? forced : fps) > 60 && thps_replay_playing())
        return 60;
    return forced ? forced : fps;
}

/* The game's time read: QueryPerformanceCounter into a guest buffer (low dword). */
static uint32_t guest_time_lo(void)
{
    uint32_t buf, lo;
    g_esp -= 8;
    buf = g_esp;
    g_esp -= 4;
    GUEST32(g_esp) = buf;
    g_esp -= 4;
    GUEST32(g_esp) = 0;                       /* return address: popped by ret 4 */
    sub_001B9DB6();
    lo = GUEST32(buf);
    g_esp += 8;
    return lo;
}

static double guest_us_per_tick(void)
{
    double d;
    memcpy(&d, (const void *)((uintptr_t)g_xbox_mem_offset + 0x00305168u), sizeof d);
    return d;
}

/* toolkit frame_time.c: the limiter's wait in the [fps] / [hitch] lines */
extern int xbox_ft_on(void);
extern long long xbox_ft_now(void);
extern double xbox_ft_ms(long long since);
extern void xbox_ft_vblank_wait(double ms);

void sub_0015AFA0(void)
{
    double k = guest_us_per_tick();
    uint32_t target = (uint32_t)(1000000 / (thps_fps() + 1));      /* 60: 16393 = 0x4009 */
    uint32_t start = guest_time_lo(), now = start;
    double el;
    long long wait_t0;

    s_limiter_calls++;
    {
        /* THPS_LIMITER=0: the original spin, for comparisons. */
        static int orig = -1;
        if (orig < 0) {
            const char *e = getenv("THPS_LIMITER");
            orig = e && *e == '0';
        }
        if (orig) {
            sub_0015AFA0_gen();
            return;
        }
    }
    if (!(k > 0.0))
        k = 1.0;
    /* the toolkit's [fps] / [hitch] lines: the limiter's wait is waiting, not game work */
    wait_t0 = xbox_ft_on() ? xbox_ft_now() : 0;
    if (xbox_GuestHz() == thps_fps() && nv2a_vk_present_fifo()) {
        /* FIFO (fallback, or RECOMP_PRESENT=fifo): the behaviour before
         * fix B -- a sleep to 1.5 ms before the deadline, no spin. Without
         * any limiter wait FIFO's pair mode got worse (docs/status.md). */
        el = (double)(uint32_t)(now - GUEST32(0x002762DCu)) * k;
        if ((double)target - el > 1500.0)
            host_sleep_ns((uint64_t)(((double)target - el - 1500.0) * 1000.0));
        now = guest_time_lo();
    } else if (xbox_GuestHz() != thps_fps()) {
        /* The limiter is the pacer: hold the exact deadline. The guest time
         * is the host's QueryPerformanceCounter (KeQueryPerformanceCounter),
         * so the deadline is the same instant on host_time_ns. The time
         * reads after it only catch a clock step; they end at once. */
        for (;;) {
            el = (double)(uint32_t)(now - GUEST32(0x002762DCu)) * k;
            if (el >= (double)target)
                break;
            host_wait_until_ns(host_time_ns() + (uint64_t)(((double)target - el) * 1000.0));
            now = guest_time_lo();
        }
    }
    /* else: the guest vblank paces (the swap throttle waits on it) and the
     * display does not (mailbox/immediate): no wait. */
    if (wait_t0)
        xbox_ft_vblank_wait(xbox_ft_ms(wait_t0));
    g_eax = (uint32_t)(int64_t)((double)(uint32_t)(now - start) * k);
    g_esp += 4;                                          /* ret */
}

/* â”€â”€ instruction hooks (config/hooks.toml, M3.5 step 3) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
 *
 * Why: the per-game hook mechanism (toolkit tools/recomp/hooks_config.py) that
 * the D3D8 HLE and later game patches (frame rate, widescreen) build on.
 * thps_hook_mp_check is its self-test: it runs at 0x001238B0, the first
 * instruction of the main menu's Multiplayer check, and only counts.
 * Verified: RECOMP_HOOK_TRACE=1 on the menu tour logs it (docs/status.md). */
void thps_hook_mp_check(void)
{
    static int on = -1;
    static unsigned n;
    if (on < 0) {
        const char *e = getenv("RECOMP_HOOK_TRACE");
        on = e && *e && *e != '0';
    }
    if (on && (++n == 1 || n % 600 == 0))
        fprintf(stderr, "[HOOK] 0x001238B0 Multiplayer check, call %u (esp=0x%08X)\n", n, g_esp);
}

/* â”€â”€ save trace (RECOMP_SAVE_TRACE=1, off by default) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
 * Stage 3, B4: overwriting a save from Options -> Load/Save reports "a file
 * named CAREER A already exists". Log the game's save-list helpers to see
 * whether the old save is deleted before the name check. Addresses read from
 * the code (THPS 2X USA): 0x0019A910 enumerate saves into the list at eax,
 * 0x0019A430 name-exists check (eax = list, [esp+4] = name) ret 4,
 * 0x0019AB90 delete save (eax = index, ecx = list), 0x00146D80 overwrite
 * dialog step (ecx = screen, state at +0x16028). */
extern void sub_0019A910_gen(void);
extern void sub_0019A430_gen(void);
extern void sub_0019AB90_gen(void);
extern void sub_00146D80_gen(void);

static int save_trace_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("RECOMP_SAVE_TRACE");
        on = e && *e && *e != '0';
    }
    return on;
}

static void guest_str(uint32_t va, char *out, size_t n)
{
    size_t i = 0;
    if (va)
        for (; i + 1 < n; i++) {
            char c = *(volatile char *)((uintptr_t)g_xbox_mem_offset + va + (uint32_t)i);
            if (!c) break;
            out[i] = c;
        }
    out[i] = 0;
}

void sub_0019A910(void)
{
    uint32_t list = g_eax, ret_va = GUEST32(g_esp);
    sub_0019A910_gen();
    if (save_trace_on())
        fprintf(stderr, "[SAVETRACE] enumerate list=0x%08X -> %u saves (eax 0x%08X) ret=0x%08X\n",
                list, GUEST32(list + 0x15bac), g_eax, ret_va);
}

void sub_0019A430(void)
{
    uint32_t list = g_eax, ret_va = GUEST32(g_esp);
    char name[64];
    guest_str(GUEST32(g_esp + 4), name, sizeof name);
    sub_0019A430_gen();
    if (save_trace_on())
        fprintf(stderr, "[SAVETRACE] name-exists \"%s\" in list=0x%08X (%u saves) -> %u ret=0x%08X\n",
                name, list, GUEST32(list + 0x15bac), g_eax, ret_va);
}

void sub_0019AB90(void)
{
    uint32_t idx = g_eax, list = g_ecx, ret_va = GUEST32(g_esp);
    if (save_trace_on())
        fprintf(stderr, "[SAVETRACE] delete index %u list=0x%08X ret=0x%08X\n", idx, list, ret_va);
    sub_0019AB90_gen();
    if (save_trace_on())
        fprintf(stderr, "[SAVETRACE] delete -> 0x%08X\n", g_eax);
}

void sub_00146D80(void)
{
    uint32_t self = g_ecx, ret_va = GUEST32(g_esp);
    uint32_t st = GUEST32(self + 0x16028);
    sub_00146D80_gen();
    if (save_trace_on())
        fprintf(stderr, "[SAVETRACE] overwrite dialog 0x%08X state %u -> %u (eax 0x%08X) ret=0x%08X\n",
                self, st, GUEST32(self + 0x16028), g_eax, ret_va);
}

/* Save-screen constructors (stage 3, B4): 0x0010F8B0(index, kind, mode) ret 0xC,
 * 0x0010FB40(?, kind, mode, ...) and 0x0010FC00(index, kind, mode, x) ret 0x10;
 * mode lands in [screen+0x15e24] (0 load, 1 new save, 3 overwrite). */
extern void sub_0010F8B0_gen(void);
extern void sub_0010FB40_gen(void);
extern void sub_0010FC00_gen(void);

static void save_ctor_trace(const char *fn)
{
    if (save_trace_on())
        fprintf(stderr, "[SAVETRACE] %s(0x%X, 0x%X, 0x%X, 0x%X) ret=0x%08X\n", fn,
                GUEST32(g_esp + 4), GUEST32(g_esp + 8), GUEST32(g_esp + 12), GUEST32(g_esp + 16),
                GUEST32(g_esp));
}

void sub_0010F8B0(void) { save_ctor_trace("ctor_10F8B0"); sub_0010F8B0_gen(); }
void sub_0010FB40(void) { save_ctor_trace("ctor_10FB40"); sub_0010FB40_gen(); }
void sub_0010FC00(void) { save_ctor_trace("ctor_10FC00"); sub_0010FC00_gen(); }

/* â”€â”€ movie loop pacing (config/hooks.toml at 0x001B073F) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
 * Why: the Bink loop sub_001B05E0 spins while BinkWait says the next frame is
 * not due: pad poll (0x001B0E70), skip-flag checks, BinkWait again -- about
 * 236,000 times a second, one core busy for every movie (design principle
 * A10: no busy-waiting). The hook sits on the pad-poll call that every pass
 * of the loop makes, and lets one pass per guest vblank through: a second
 * pass in the same vblank sleeps until the next one. Frames are still
 * decoded when BinkWait says so (30 fps < 60 Hz), the pads are read every
 * vblank, and a skip press ends the movie as before.
 * Verified: docs/status.md (M5 hardening, point 6: polls/s and CPU before and
 * after; the intro movies skip on A at the same points). */
extern uint64_t xbox_GuestVblankCount(void);
extern int64_t xbox_GuestNsToNextVblank(void);

void thps_movie_wait(void)
{
    static uint64_t last = (uint64_t)-1;
    uint64_t now = xbox_GuestVblankCount();
    if (now == last) {
        int64_t ns = xbox_GuestNsToNextVblank();
        if (ns <= 0 || ns > 20000000)
            ns = 1000000;             /* vblank off: 1 ms */
        xbox_GuestSleepNs((uint64_t)ns);
        now = xbox_GuestVblankCount();
    }
    last = now;
    s_movie_vbl = now ? now : 1;      /* a movie plays: never stretched (MENUS AT 16:9) */
}

/* â”€â”€ Watch Replay tick field (config/hooks.toml at 0x000D2582 / 0x000D65C5) â”€â”€
 * Why: the replay recorder (sub_000D0FA0) stores the vblanks a frame took as
 * dt - 2 in 2 bits (0x000D2582 `add eax, -2`, dt clamped to 5 above), and
 * playback (sub_000D5FB0, 0x000D65C5 `add eax, 2`) holds each replayed frame
 * that long -- a range of 2..5 vblanks, 30..12 fps. The level runs at 60 fps
 * here, dt = 1: -1 was stored as 3, read back as 5, and the replay played at
 * ~11 fps (measured: 33 presented frames per 3 s; at THPS_FPS=30, dt = 2, it
 * played in real time). Both ends move the bias to 1 instead: 1..4 vblanks,
 * so 60 and 30 fps replay in real time; a frame of 5+ vblanks (a hitch) is
 * held 4. Above 60 fps dt can be 0: the recorder override below writes once
 * per tick, and a replay plays at 60 (fps120). Saved replays (Save Replay) carry the port's encoding and play
 * back right here; a replay saved on a console in the original encoding
 * would be read one vblank short per frame.
 * THPS_REPLAY_TICKS=0: the original encoding, for comparisons.
 * Verified: docs/status.md (2026-10-17, Watch Replay speed: playback at
 * ~59 fps, 775 frames in ~13 s, was ~11 fps). */
static int replay_ticks_fix(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("THPS_REPLAY_TICKS");
        on = !(e && *e == '0');
    }
    return on;
}

/* after 0x000D2582 (recorder): eax = dt - 2 -> clamp(dt - 1, 0, 3) */
void thps_replay_tick_rec(void)
{
    int32_t v = (int32_t)g_eax + 1;
    if (!replay_ticks_fix())
        return;
    if (v < 0) v = 0;
    if (v > 3) v = 3;
    g_eax = (uint32_t)v;
}

/* after 0x000D65C5 (playback): eax = field + 2 -> field + 1 */
void thps_replay_tick_play(void)
{
    if (replay_ticks_fix())
        g_eax -= 1;
}

/* ── Watch Replay recorder, sub_000D0FA0: once per game-time tick ────────────
 * Why: above 60 fps a frame can end inside the same 60 Hz game-time tick
 * (0x0030697C, the vblank callback's counter); the recorder's dt =
 * [0x0030697C] - [rec+0x2D4] is then 0 and stored as 1 tick. So a record call
 * is skipped while the game time has not moved since that skater's last
 * record: at most one record per tick, as on the console at 60 Hz, and the
 * next record carries the whole dt. Playback shows one recorded frame per
 * rendered frame, so a replay plays at 60 (sub_000D5FB0, thps_fps). The
 * argument ([esp+4], the skater's object) tells the players of a split screen
 * apart. stdcall, ret 4. THPS_REPLAY_TICKS=0: the original, every frame.
 * Verified: docs/status.md (fps120, replay length at 60 and 120). */
extern void sub_000D0FA0_gen(void);
extern void sub_000D5FB0_gen(void);
static int thps_clock_log(void);

void sub_000D0FA0(void)
{
    static struct { uint32_t who, tick; } last[8];
    uint32_t who = GUEST32(g_esp + 4), now = GUEST32(0x0030697Cu);
    int i, free_i = -1;
    s_rec_who = who;
    if (thps_clock_log() == 2)
        fprintf(stderr, "[REC] t_ms %llu tick %u who %08X\n",
                (unsigned long long)(host_time_ns() / 1000000ull), now, who);
    if (replay_ticks_fix()) {
        for (i = 0; i < 8; i++) {
            if (last[i].who == who) {
                if (last[i].tick == now) {
                    g_esp += 8;                          /* ret 4: skipped, same tick */
                    return;
                }
                break;
            }
            if (!last[i].who && free_i < 0)
                free_i = i;
        }
        if (i == 8)
            i = free_i >= 0 ? free_i : (int)(who % 8u);
        last[i].who = who;
        last[i].tick = now;
    }
    if (thps_clock_log() == 2)
        fprintf(stderr, "[REC] recorded\n");
    sub_000D0FA0_gen();
}

/* Watch Replay playback, sub_000D5FB0 (the stream reader; called every ~100 ms
 * while a replay plays). Playback consumes one recorded frame per rendered
 * frame -- measured: a 2:00 session recorded once per tick (7240 frames)
 * played in 120.9 s at 60 fps, in 60.2 s at 120 -- so while a replay plays the
 * level runs at 60 (thps_fps): each recorded frame shows for its tick, at any
 * frame rate setting. THPS_CLOCK_LOG=2 logs each call's host time. */
static volatile uint64_t s_replay_play_ns;

static int thps_replay_playing(void)
{
    uint64_t t = s_replay_play_ns;
    return t && host_time_ns() - t < 1000000000ull;
}

void sub_000D5FB0(void)
{
    s_replay_play_ns = host_time_ns();
    if (thps_clock_log() >= 2)
        fprintf(stderr, "[PLAY] t_ms %llu tick %u\n",
                (unsigned long long)(host_time_ns() / 1000000ull), GUEST32(0x0030697Cu));
    sub_000D5FB0_gen();
}

/* â”€â”€ Widescreen: horizontal field of view (config/hooks.toml) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
 * Why: M5 item 3. THPS 2X has no 16:9 mode (no widescreen strings); it
 * renders 640x480 at 4:3. Real 16:9 gameplay (Hor+: the vertical field of view
 * stays, the horizontal one grows) takes two changes in the game's own
 * camera maths, during gameplay only (the level limiter ran last frame):
 * the projection's x scale [0x0030458C] at 3/4 (SetScreenSize, below) gives
 * the 16:9 ratio, and the camera's tan(fov/2) word at 4/3 (sub_00190FB0,
 * below) the wider view. The game renders with vertex shaders from its own composite
 * (SetVertexShaderConstant 0x000171F0), so scaling the D3D projection
 * matrix 0x0030EF18 alone changed nothing on screen. The back end renders
 * square pixels and presents gameplay at 16:9, menus/movies at 4:3
 * pillarboxed (aspect-ratio rule, CLAUDE.md; toolkit xbox_SetPresentWide).
 * THPS_WIDESCREEN=1 turns it on (main.c sets RECOMP_WIDESCREEN); 4:3 is the
 * default. */
static int thps_widescreen(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("THPS_WIDESCREEN");
        on = e && *e == '1';
    }
    return on;
}

/* SetScreenSize sub_00193020(512, 240) (once at start, the PS1 heritage):
 * from the 640x480 display it sets the projection's x and y scale
 * [0x0030458C] / [0x00304588] (4096 = 1.0 each at 4:3). The x scale feeds
 * the projection (0x00191437) and the screen-space maths at 0x0019100C /
 * 0x00191068; the builder keeps m[0][0] and moves m[1][1], so 3/4 of it
 * alone is 16:9 Vert- (measured: m00 1.198, m11 -1.599 -> -2.13). The base
 * is kept here; the swap (sub_00018F90) sets 3/4 of it during gameplay and
 * the base in menus. */
extern void sub_00193020_gen(void);
static uint32_t s_xscale_base;

void sub_00193020(void)
{
    sub_00193020_gen();
    s_xscale_base = GUEST32(0x0030458Cu);
}

static void ws_apply_xscale(void)
{
    if (!thps_widescreen() || !s_xscale_base)
        return;
    GUEST32(0x0030458Cu) = s_in_level ? s_xscale_base * 3u / 4u : s_xscale_base;
}

void thps_ws_apply(void) { ws_apply_xscale(); }

/* Widescreen FOV at the source: the camera modes write the view's tan(fov/2)
 * word [view+0xC] (view = [0x00304584]; 4096 = tan 45 deg, e.g. 0x0B72) every
 * frame, and everything -- the projection builder's 1/tan (k), the screen
 * distance H at 0x00191022 and the culling -- derives from it. 4/3 of it
 * during gameplay is the Hor+ view (with the x scale at 3/4) for the renderer
 * and the culling alike; scaling only k or H let the culling see the old
 * 4:3 view (maintainer: geometry at the side edges popped in and out).
 * s_fov_set remembers the value written here, so a frame where the camera
 * did not rewrite the field is not scaled twice.
 * The 4/3 tan and the 3/4 x scale cancel in the screen distance H, so the
 * visibility frustum built from H stays at the 4:3 width: sub_001603C0
 * below. */
extern void sub_00190FB0_gen(void);
static uint16_t s_fov_set;

void sub_00190FB0(void)
{
    if (thps_widescreen() && s_in_level) {
        uint32_t view = GUEST32(0x00304584u);
        volatile uint16_t *t = (volatile uint16_t *)((uintptr_t)g_xbox_mem_offset + view + 0xCu);
        if (view && *t != s_fov_set) {
            uint32_t v = (uint32_t)*t * 4u / 3u;
            s_fov_set = (uint16_t)(v > 0xFFFFu ? 0xFFFFu : v);
            *t = s_fov_set;
        }
    }
    sub_00190FB0_gen();
}

/* Widescreen visibility frustum: the builder above calls
 * sub_001603C0(out 0x0030D0D8, ?, view) with the field of view in degrees
 * [0x00242564] (sub_0017D3B0 derives it from H: 2 atan(c / H), so it keeps
 * the 4:3 value, 79.8 deg) and the view rectangle; the horizontal extent is
 * tan(fov / 2), the vertical one that times h / w (w = [view] - [view+4]).
 * The game culls whole objects with it, so in 16:9 geometry at the side
 * edges popped in and out (maintainer, single player and split screen). For
 * this call only, the 16:9 field of view 2 atan(tan(fov / 2) * 4/3) and a
 * 4/3 wider rectangle: wider by 4/3, the same height -- Hor+ like the
 * picture, for every viewport (split screen: each view's own rectangle).
 * The projection reads [0x00242564] itself, so both are put back.
 * Verified 2026-10-25 (branch ws-cull, THPS_WS_DIAG): toggled in a paused
 * level the draw count rises (Construction 170 -> 179, Hangar 343 -> 357)
 * with a pixel-identical picture; the wider rectangle alone lowers it (the
 * height follows h / w); maintainer's controller sessions in Hangar: no
 * popping at the left edge (aircraft) with the fix, popping without. */
extern void sub_001603C0_gen(void);

void sub_001603C0(void)
{
    volatile float *fov = (volatile float *)((uintptr_t)g_xbox_mem_offset + 0x00242564u);
    uint32_t view = GUEST32(g_esp + 12u);
    volatile uint16_t *r = (volatile uint16_t *)((uintptr_t)g_xbox_mem_offset + view);
    uint16_t x1;
    float a;

    if (!thps_widescreen() || !s_in_level || !view || r[0] <= r[2]) {
        sub_001603C0_gen();
        return;
    }
    a = *fov;
    x1 = r[0];
    r[0] = (uint16_t)(r[2] + (unsigned)(x1 - r[2]) * 4u / 3u);
    if (a > 1.0f && a < 170.0f)
        *fov = 2.0f * atanf(tanf(a * 0.5f * 3.14159265f / 180.0f) * 4.0f / 3.0f) * 180.0f / 3.14159265f;
    sub_001603C0_gen();
    r[0] = x1;
    *fov = a;
}

/* ── BUTTON CHEATS, the game's code checker sub_00147460 ─────────────────────
 *
 * Why: player request (v0.9.2 item 3): the button cheats from a list instead
 * of typing them with LT held.
 * Address: sub_00147460 (cdecl, one argument, ret 4) runs while player 1 holds
 * LT (0x00305378) on the main menu (sub_00121530, argument 0), in a level's
 * pause (sub_00104210, its own argument) and in one more front-end screen
 * (sub_001232A0, 0). Unless 0x003068C9 is set it takes the first new press of
 * U R D L Y B A X Black(C) White(T) Start(S) - player 1's latches at 0x00305338
 * + 0xA1 / 0x91 / 0xB1 / 0x81 / 0x01 / 0x21 / 0x31 / 0x11 / 0xC1 / 0xD1 / 0xE1,
 * pads 2-4 at + k * 0x18C - appends its letter to the buffer 0x00306218
 * (sub_00147380, at most 0x12 letters; cleared when the timer 0x0030622C runs
 * out) and compares the whole buffer with the 34 code strings at 0x00229A8C ..
 * 0x00229BF4 (repe cmpsb, NUL included); a match does its action (most toggle
 * byte 2 of an entry of the cheat table 0x002FFDBC, 19 entries of 4 bytes,
 * named by sub_000764B0 - the game's own CHEATS screen) and returns 1.
 * How: A on the code's line writes the code but its last letter into the
 * buffer, sets only that last letter's latch of player 1 (all other latches
 * of the four pads cleared for the call, then put back) and calls the checker
 * with the main menu's argument 0 - the same path as typing it there. Front-
 * end Options only (like BUTTON MAPPING). The names come from the game's
 * CHEATS screen where a code toggles one of its entries, otherwise from what
 * the probe saw it change.
 * Verified: THPS_CHEAT_PROBE=1 (v0.9.2 tests, cheat list). */
extern void sub_00147460(void);

/* a code: the list's text (what it does), and where its state is read - a
 * byte or dword at va that means ON when nonzero (CHEAT_NZ), zero (CHEAT_Z)
 * or equal to a value; va 0 = a one-time action (DONE after it) */
#define CHEAT_NZ 0xFFFFFFFFu
#define CHEAT_Z  0xFFFFFFFEu
typedef struct {
    const char *code, *text;
    uint32_t va, size, on;
} Cheat;

/* the probe (2026-10-28, main menu): every code here is taken there;
 * ADDABSURDSCR, BURLYDUDS and STYXDUDS are taken only in a level's pause
 * (argument != 0) and are not listed. The stat codes share 0x00306BD0 (one
 * value: entering one replaces the other, again = off); KID SIZED and BIG
 * HEADS switch each other off (the game's own rule). */
static const Cheat s_cheats[] = {
    { "BURLYSCULLS",   "BIG HEADS",                 0x002FFDEAu, 1, CHEAT_NZ },
    { "RASCALLYBRATS", "KID SIZED SKATER",          0x002FFDCEu, 1, CHEAT_NZ },
    { "STUDLYSTAR",    "SPECIAL ALWAYS FULL",       0x002FFDD6u, 1, CHEAT_NZ },
    { "RULSRAYLS",     "NEVER LOSE BALANCE",        0x002FFDD2u, 1, CHEAT_NZ },
    { "LUSRULS",       "LOW GRAVITY",               0x002FFDF6u, 1, CHEAT_NZ },
    { "YARDSSDARY",    "MIRRORED LEVELS",           0x002FFDFEu, 1, CHEAT_NZ },
    { "RASTABUDDY",    "SLOW MOTION",               0x002FFDE6u, 1, CHEAT_NZ },
    { "CRUSTYSCABS",   "REALISTIC PHYSICS",         0x002FFDEEu, 1, CHEAT_NZ },
    { "ALLSTYX",       "WIREFRAME",                 0x002FFDE2u, 1, CHEAT_NZ },
    { "DULLYARDS",     "SMOOTH",                    0x002FFDF2u, 1, CHEAT_NZ },
    { "STUDLYTUX",     "DISCO MODE",                0x002FFDFAu, 1, CHEAT_NZ },
    { "CUULSTARTS",    "RESTART POINTS IN PAUSE",   0x002FFDCAu, 1, CHEAT_NZ },
    /* the frame time step x 1.25 (sub_0015B080, 0x00300850) */
    { "XTRABUUST",     "GAME RUNS FASTER",          0x003061D0u, 4, CHEAT_NZ },
    { "USTAT",         "ALL STATS AT 5",            0x00306BD0u, 4, 5 },
    { "DSTAT",         "ALL STATS AT 6",            0x00306BD0u, 4, 6 },
    { "LSTAT",         "ALL STATS AT 7",            0x00306BD0u, 4, 7 },
    { "RSTAT",         "ALL STATS AT 8",            0x00306BD0u, 4, 8 },
    { "CSTAT",         "ALL STATS AT 9",            0x00306BD0u, 4, 9 },
    { "STATSTUD",      "ALL STATS AT 10",           0x00306BD0u, 4, 10 },
    { "BURLYSTATS",    "ALL STATS AT 13",           0x00306BD0u, 4, 13 },
    { "RADCUULBABY",   "ALL GAPS FOUND",            0, 0, 0 },         /* one way: fills the gap list */
    /* the official "unlock everything" (tests/input/snippets/unlock_all.txt) */
    { "SATURDAYBABY",  "UNLOCK EVERYTHING",         0, 0, 0 },
    { "BUCUBUX",       "5000 MORE CASH",            0, 0, 0 },         /* the career's cash + 5000 */
    /* not listed until their effect is known (planned for v1.0): XTRALUSRULS
     * (0x00306BC8), RADBLAST (0x00306BCC), XBLURRY (table entry 17 + motion
     * blur), TBUARDS (entry 18 + 0x003061D4), SCABS (0x002761F0),
     * TRYALLYARDS (0x003061BC), RADSCRTCARS, DULLBABY */
};
#define CHEAT_COUNT ((int)(sizeof s_cheats / sizeof s_cheats[0]))

#define GUEST8(va) (*(volatile uint8_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))

/* 1 when code i's state is on (an action: never) */
static int cheat_on(int i)
{
    const Cheat *c = &s_cheats[i];
    uint32_t v;
    if (!c->va)
        return 0;
    v = c->size == 1 ? GUEST8(c->va) : GUEST32(c->va);
    return c->on == CHEAT_NZ ? v != 0 : c->on == CHEAT_Z ? v == 0 : v == c->on;
}

/* the checker's letters and their latches (offset in a pad record) */
static const char s_cheat_letters[] = "URDLYBAXCTS";
static const uint8_t s_cheat_off[] = { 0xA1, 0x91, 0xB1, 0x81, 0x01, 0x21, 0x31, 0x11, 0xC1, 0xD1, 0xE1 };

/* code i through the checker: 1 accepted, 0 refused, -1 the checker is shut */
static int cheat_enter(int i)
{
    uint8_t saved[4][sizeof s_cheat_off];
    const char *c = s_cheats[i].code, *p;
    size_t n = strlen(c), j;
    uint32_t k, last;
    int res;
    p = strchr(s_cheat_letters, c[n - 1]);
    if (GUEST8(0x003068C9u) || !p)
        return -1;
    last = s_cheat_off[p - s_cheat_letters];
    for (k = 0; k < 4; k++)
        for (j = 0; j < sizeof s_cheat_off; j++) {
            saved[k][j] = GUEST8(0x00305338u + k * 0x18Cu + s_cheat_off[j]);
            GUEST8(0x00305338u + k * 0x18Cu + s_cheat_off[j]) = 0;
        }
    for (j = 0; j + 1 < n; j++)
        GUEST8(0x00306218u + j) = (uint8_t)c[j];
    GUEST8(0x00306218u + n - 1) = 0;
    GUEST32(0x0030622Cu) = 0x1E;                  /* the buffer's timer: not run out */
    GUEST8(0x00305338u + last) = 1;
    g_esp -= 4;
    GUEST32(g_esp) = 0;                           /* the main menu's argument */
    g_esp -= 4;
    GUEST32(g_esp) = 0x00121547u;                 /* return address (the main menu's call), popped by ret 4 */
    sub_00147460();
    res = (g_eax & 0xFFu) != 0;
    for (k = 0; k < 4; k++)
        for (j = 0; j < sizeof s_cheat_off; j++)
            GUEST8(0x00305338u + k * 0x18Cu + s_cheat_off[j]) = saved[k][j];
    GUEST8(0x00306218u) = 0;
    return res;
}

/* THPS_CHEAT_PROBE=1 (test, a throw-away layout): every code entered once,
 * each byte of the game's data 0x00270000 .. 0x00320000 it changed logged */
static void cheat_probe(void)
{
    enum { LO = 0x00270000u, HI = 0x00320000u };
    static uint8_t before[HI - LO];
    int i;
    for (i = 0; i < CHEAT_COUNT; i++) {
        uint32_t a, shown = 0;
        int r;
        memcpy(before, (const void *)((uintptr_t)g_xbox_mem_offset + LO), sizeof before);
        r = cheat_enter(i);
        fprintf(stderr, "[CHEAT] %-13s -> %d\n", s_cheats[i].code, r);
        for (a = LO; a < HI; a++)
            if (GUEST8(a) != before[a - LO] && shown++ < 24)
                fprintf(stderr, "[CHEAT]   %08X %02X -> %02X\n", a, before[a - LO], GUEST8(a));
        if (shown > 24)
            fprintf(stderr, "[CHEAT]   ... %u bytes changed\n", shown);
    }
}

/* ── Options menu: CONTROLS and RECOMP OPTIONS lists (M6 groundwork) ─────────
 * Why: maintainer, 2026-10-18 -- tidy the Options list: PLAYER 1-4 CONTROLS
 * move into one CONTROLS entry, and RECOMP OPTIONS holds the port's settings
 * in sub-lists (DISPLAY, GRAPHICS, PERFORMANCE) -- the new entries are
 * placeholders without function for now (the game's font has no hyphen:
 * VSYNC, ANTIALIASING). Vibration is already per player in
 * the game's own PLAYER n CONTROLS screens; button mapping goes there too
 * (each controller has its own settings), not into these lists.
 * The Options screen (vtable 0x0022A2B0, run by sub_0013D050 in front-end
 * state 0x19) is data driven: frontend\OptionMain.PANEL holds the title and
 * 12 lines "cheat line 01..12" with length-prefixed texts; Init (sub_0013D230)
 * gives the menu 12 lines (push 0xC at 0x0013D323) and fills the line -> next
 * state table [screen+0x214] (0x21 chat, 0x22/0x23/0x32/0x33 player 1-4
 * controls, 0x24 sound ... 0x2A general options); A (sub_0013D470) returns
 * that state; B (sub_0013D4D0) makes the run loop give up (-1, main menu);
 * sub_0013D400 greys out lines 1-4 for pads that are not connected.
 * One screen, several lists (s_opt_list): the panel is rewritten in memory
 * right after it is loaded (sub_0017A060) -- the file on disk is untouched --
 * and a list change returns state 0x19, so the Options screen comes back with
 * the other list (after B through sub_0013D050, whose result the run loop
 * sets). THPS_OPTIONS_MENU=0: the original list. */
extern THPS_TLS uint32_t g_edx;

#define OPT_STAY 0x19u                            /* Options again: a list change or a placeholder */
#define OPT_SUB(n) (0x1000u + (n))                /* line opens list n */

typedef struct {
    const char *title;
    const char *line[12];
    uint32_t next[12];                            /* a front-end state, OPT_STAY or OPT_SUB(list) */
    int parent, parent_line;
} OptList;

enum { OPT_MAIN, OPT_CONTROLS, OPT_RECOMP, OPT_FPS, OPT_AA, OPT_BOOT, OPT_RESTART, OPT_SOUND,
       OPT_ALBUM, OPT_PLAYBACK, OPT_BINDKEYS, OPT_SHADERS, OPT_SHBROWSE, OPT_SHPARAMS, OPT_SHRESET,
       OPT_GENERAL, OPT_CHEATS, OPT_LISTS };

/* SHADERS lists (see "SHADERS" below): titles and lines built when shown */
static char s_sh_title[4][40];
static char s_sh_text[4][12][44];
#define SH_LINES(k) { s_sh_text[k][0], s_sh_text[k][1], s_sh_text[k][2], s_sh_text[k][3], s_sh_text[k][4], \
                      s_sh_text[k][5], s_sh_text[k][6], s_sh_text[k][7], s_sh_text[k][8], s_sh_text[k][9], \
                      s_sh_text[k][10], s_sh_text[k][11] }
#define SH_STAY { OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY, \
                  OPT_STAY, OPT_STAY, OPT_STAY }

/* BUTTON MAPPING (rebinding, see "Button mapping" below): one screen -- PLAYER
 * and DEVICE lines, then that device's bindings; texts built when shown (""
 * ends a list). */
static char s_bkeys_title[32] = "BUTTON MAPPING";
static char s_bkeys_text[12][40];

/* RECOMP OPTIONS value lines ("ASPECT RATIO: 16:9"; see "RECOMP OPTIONS
 * values" below): texts built when a list is opened or a value changes. */
#define OPT_RENDER_STEPS 16                       /* 0..16 = 1.00X..5.00X */
static char s_val_text[OPT_LISTS][12][48];
static char s_restart_text[48];                   /* RESTART NEEDED: what changed (opt_restart_pending) */

/* SOUNDTRACK's value line: GAME MUSIC, COMBINED, then every album of the
 * media folder (src/soundtracks.c), picked with left / right like RENDER
 * RESOLUTION; a step saves the choice at once. */
#define OPT_ALBUM_VALUE_LINE 1
static char s_album_text[28] = "GAME MUSIC";
static char s_boot_text[48] = "NEEDS BOOT MPG IN THE MEDIA FOLDER";   /* BOOT ANIMATION's status line (boot_state) */
extern int  thps_st_choices(void);
extern int  thps_st_choice(void);
extern void thps_st_set_choice(int c);
extern void thps_st_choice_text(int c, char *out, size_t n);
extern int  thps_st_mode(void);
extern void thps_st_set_mode(int m);


/* RECOMP OPTIONS: the maintainer's list (2026-10-18; MONITOR and FIELD OF
 * VIEW dropped -- Hor+ widescreen already widens the view; grouped with value
 * lines on 2026-10-26, see "RECOMP OPTIONS values"). BOOT ANIMATION: its
 * status line names media\boot.mpg's state. (The menu font does have a
 * dot -- "2.25X"; an earlier note here said it had none.) */
static const OptList s_opt[OPT_LISTS] = {
    [OPT_MAIN] = { "OPTIONS",
        { "CHAT MESSAGES", "CONTROLS", "SOUND LEVELS", "SOUNDTRACKS", "MOVIES", "MEMORY UNIT", "CHEATS", "HIGH SCORES",
          "GAP CHECKLIST", "GENERAL OPTIONS", "RECOMP OPTIONS" },
        /* GENERAL OPTIONS: our list instead of the game's screen (state 0x2A), see
         * OPT_GENERAL */
        { 0x21, OPT_SUB(OPT_CONTROLS), 0x24, OPT_SUB(OPT_SOUND), 0x25, 0x26, 0x27, 0x28, 0x29, OPT_SUB(OPT_GENERAL),
          OPT_SUB(OPT_RECOMP) },
        -1, 0 },
    /* BUTTON MAPPING here, not in the game's PLAYER n CONTROLS screen: that
     * screen keeps per-line state for three lines only (see thps_ctl_panel) */
    [OPT_CONTROLS] = { "CONTROLS",
        { "PLAYER 1 CONTROLS", "PLAYER 2 CONTROLS", "PLAYER 3 CONTROLS", "PLAYER 4 CONTROLS", "BUTTON MAPPING" },
        { 0x22, 0x23, 0x32, 0x33, OPT_SUB(OPT_BINDKEYS) },
        OPT_MAIN, 1 },
    [OPT_BINDKEYS] = { s_bkeys_title,
        { s_bkeys_text[0], s_bkeys_text[1], s_bkeys_text[2], s_bkeys_text[3], s_bkeys_text[4], s_bkeys_text[5],
          s_bkeys_text[6], s_bkeys_text[7], s_bkeys_text[8], s_bkeys_text[9], s_bkeys_text[10], s_bkeys_text[11] },
        { OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY,
          OPT_STAY, OPT_STAY },
        OPT_CONTROLS, 4 },
    /* one list (maintainer, 2026-10-26: few sub-menus): DISPLAY MODE and the
     * picture values (RENDER RESOLUTION, ASPECT RATIO, MENUS AT 16:9 only with
     * 16:9 saved, ANISOTROPIC FILTER), ANTIALIASING and FRAME RATE as
     * sub-lists (their line: opt_parent_line), BOOT ANIMATION -- built by opt_build (texts, next states and
     * value lines move with the MENUS line); RESOLUTION dropped (2026-10-26) */
    /* all 12 slots: with 16:9 saved the list has 10 lines (the 9 slots it
     * had dropped BOOT ANIMATION once GAMEPLAY OPTIONS came in, v0.9.2) */
    [OPT_RECOMP] = { "RECOMP OPTIONS",
        { s_val_text[OPT_RECOMP][0], s_val_text[OPT_RECOMP][1], s_val_text[OPT_RECOMP][2],
          s_val_text[OPT_RECOMP][3], s_val_text[OPT_RECOMP][4], s_val_text[OPT_RECOMP][5],
          s_val_text[OPT_RECOMP][6], s_val_text[OPT_RECOMP][7], s_val_text[OPT_RECOMP][8],
          s_val_text[OPT_RECOMP][9], s_val_text[OPT_RECOMP][10], s_val_text[OPT_RECOMP][11] },
        { OPT_STAY },                             /* opt_next */
        OPT_MAIN, 10 },
    /* FRAME RATE, VSYNC, FPS COUNTER -- VSYNC and the counter belong with the
     * frame rate (maintainer, 2026-10-26) */
    [OPT_FPS] = { "FRAME RATE",
        { s_val_text[OPT_FPS][0], s_val_text[OPT_FPS][1], s_val_text[OPT_FPS][2] },
        { OPT_STAY, OPT_STAY, OPT_STAY },
        OPT_RECOMP, 0 },
    /* media\boot.mpg (maintainer, 2026-10-27: MPEG-1 + MP2 through pl_mpeg,
     * no H.264 / AAC): the status line (s_boot_text: found / needs the file /
     * convert boot.mp4 or mkv), ON / OFF -- greyed out without the file
     * (sub_0013D400; a sub-menu because the greying reaches lines 1-4 only) */
    [OPT_BOOT] = { "BOOT ANIMATION",
        { s_boot_text, "ON", "OFF" },
        { OPT_STAY, OPT_STAY, OPT_STAY },
        OPT_RECOMP, 0 },
    /* leaving RECOMP OPTIONS with ASPECT RATIO or VSYNC changed (they apply
     * at the next start; maintainer, 2026-10-26: ask once, on leaving);
     * RESTART NOW = host_restart, LATER / B = OPTIONS */
    [OPT_RESTART] = { "RESTART NEEDED",
        { s_restart_text, "RESTART NOW", "LATER" },
        { OPT_STAY, OPT_STAY, OPT_STAY },
        OPT_MAIN, 10 },
    /* a sub-menu with every choice, not a value line (maintainer, 2026-10-26):
     * opens on the one in use, A picks it (saved, at once) and goes back;
     * default FXAA, the picture as before; line order = NV2A_VK_AA_* */
    [OPT_AA] = { "ANTIALIASING",
        { "OFF", "FXAA", "SMAA", "MSAA 2X", "MSAA 4X" },
        { OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY },
        OPT_RECOMP, 0 },
    /* SOUNDTRACKS, right under SOUND LEVELS (maintainer, 2026-10-18: the Sound
     * Levels screen itself stays stock): the soundtrack (GAME MUSIC, COMBINED =
     * game music plus the custom albums, then each album of media/) on a value
     * line, and the 8 playback modes -- A picks one (CLAUDE.md, Custom
     * soundtracks); both kept in saves\settings.ini (src/soundtracks.c) */
    [OPT_SOUND] = { "SOUNDTRACKS",
        { "SOUNDTRACK", "PLAYBACK MODE" },
        { OPT_SUB(OPT_ALBUM), OPT_SUB(OPT_PLAYBACK) },
        OPT_MAIN, 3 },
    [OPT_ALBUM] = { "SOUNDTRACK",
        { "LEFT OR RIGHT CHANGES IT", s_album_text },
        { OPT_STAY, OPT_STAY },
        OPT_SOUND, 0 },
    [OPT_PLAYBACK] = { "PLAYBACK MODE",
        { "PLAY SONGS IN ORDER", "PLAY ALBUM IN ORDER", "SHUFFLE GAME MUSIC", "SHUFFLE ALBUM",
          "SHUFFLE ALL CUSTOM ALBUMS", "SHUFFLE ALL", "PLAY ALL IN ORDER", "PLAY ALL CUSTOM IN ORDER" },
        { OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY },
        OPT_SOUND, 1 },
    /* SHADERS (shader module phase 4): the last line of RECOMP OPTIONS; its
     * lists are built when shown (sh_build) */
    [OPT_SHADERS]  = { s_sh_title[0], SH_LINES(0), SH_STAY, OPT_RECOMP, 0 },
    [OPT_SHBROWSE] = { s_sh_title[1], SH_LINES(1), SH_STAY, OPT_SHADERS, 1 },
    [OPT_SHPARAMS] = { s_sh_title[2], SH_LINES(2), SH_STAY, OPT_SHADERS, 2 },
    [OPT_SHRESET]  = { s_sh_title[3], SH_LINES(3), SH_STAY, OPT_SHADERS, 4 },
    /* GENERAL OPTIONS (maintainer, 2026-10-28): our list in place of the
     * game's screen (vtable 0x0022A810, four fixed lines with ON / OFF
     * elements): the game's four options read and written where its screen
     * keeps them (see "GENERAL OPTIONS values"), then LAYOUT and the v0.9.2
     * gameplay values (default = the original game; a changed one says NOT
     * ORIGINAL), the Y hint last; built by opt_build */
    [OPT_GENERAL] = { "GENERAL OPTIONS",
        { s_val_text[OPT_GENERAL][0], s_val_text[OPT_GENERAL][1], s_val_text[OPT_GENERAL][2],
          s_val_text[OPT_GENERAL][3], s_val_text[OPT_GENERAL][4], s_val_text[OPT_GENERAL][5],
          s_val_text[OPT_GENERAL][6], s_val_text[OPT_GENERAL][7], s_val_text[OPT_GENERAL][8],
          s_val_text[OPT_GENERAL][9], s_val_text[OPT_GENERAL][10], s_val_text[OPT_GENERAL][11] },
        { OPT_STAY },                             /* opt_next */
        OPT_MAIN, 9 },
    /* BUTTON CHEATS (v0.9.2 item 3; maintainer 2026-10-28: every cheat listed,
     * any combination, the text says what it does): the hint, CHEAT_PAGE
     * cheats with ON / OFF (A switches, through the game's checker, see
     * "BUTTON CHEATS" above), NEXT PAGE; built by cheat_build */
    [OPT_CHEATS] = { "BUTTON CHEATS",
        { s_val_text[OPT_CHEATS][0], s_val_text[OPT_CHEATS][1], s_val_text[OPT_CHEATS][2],
          s_val_text[OPT_CHEATS][3], s_val_text[OPT_CHEATS][4], s_val_text[OPT_CHEATS][5],
          s_val_text[OPT_CHEATS][6], s_val_text[OPT_CHEATS][7], s_val_text[OPT_CHEATS][8],
          s_val_text[OPT_CHEATS][9], s_val_text[OPT_CHEATS][10], s_val_text[OPT_CHEATS][11] },
        { OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY, OPT_STAY,
          OPT_STAY, OPT_STAY },
        OPT_RECOMP, 0 },
};

#define CHEAT_PAGE 10                             /* cheats a page: lines 1 .. 10, then NEXT PAGE */
#define CHEAT_PAGES ((CHEAT_COUNT + CHEAT_PAGE - 1) / CHEAT_PAGE)
static int s_cheat_page;
static uint8_t s_cheat_done[CHEAT_COUNT];         /* an action taken in this run (DONE) */
static int8_t s_cheat_refused = -1;               /* the code the game did not take at the last A */

/* the cheats on s_cheat_page (NEXT PAGE is the line after them: an empty
 * line would end the list, opt_lines) */
static int cheat_on_page(void)
{
    int n = CHEAT_COUNT - s_cheat_page * CHEAT_PAGE;
    return n < CHEAT_PAGE ? n : CHEAT_PAGE;
}

/* BUTTON CHEATS' texts for s_cheat_page */
static void cheat_build(void)
{
    int line, n = cheat_on_page();
    memset(s_val_text[OPT_CHEATS], 0, sizeof s_val_text[OPT_CHEATS]);
    snprintf(s_val_text[OPT_CHEATS][0], sizeof s_val_text[OPT_CHEATS][0], "PAGE %d OF %d  Y: LINE OFF",
             s_cheat_page + 1, CHEAT_PAGES);
    for (line = 1; line <= n; line++) {
        int i = s_cheat_page * CHEAT_PAGE + line - 1;
        snprintf(s_val_text[OPT_CHEATS][line], sizeof s_val_text[OPT_CHEATS][line], "%s: %s", s_cheats[i].text,
                 i == s_cheat_refused ? "NOT HERE" : !s_cheats[i].va ? (s_cheat_done[i] ? "DONE" : "DO IT")
                 : cheat_on(i) ? "ON" : "OFF");
    }
    if (CHEAT_PAGES > 1)
        snprintf(s_val_text[OPT_CHEATS][n + 1], sizeof s_val_text[OPT_CHEATS][n + 1], "NEXT PAGE");
}

/* A on BUTTON CHEATS' line: switch its cheat, or the next page; the line the cursor stays on */
static uint32_t cheat_action(uint32_t line)
{
    int i = s_cheat_page * CHEAT_PAGE + (int)line - 1, n = cheat_on_page();
    s_cheat_refused = -1;
    if (CHEAT_PAGES > 1 && line == (uint32_t)n + 1) {
        s_cheat_page = (s_cheat_page + 1) % CHEAT_PAGES;
        line = 1;
    } else if (line >= 1 && line <= (uint32_t)n) {
        int r = cheat_enter(i);
        if (r == 1 && !s_cheats[i].va)
            s_cheat_done[i] = 1;
        else if (r != 1)
            s_cheat_refused = (int8_t)i;
    }
    cheat_build();
    return line;
}

char g_thps_media_dir[512];                       /* set by main.c: <layout>\media (boot player, soundtracks, shaders) */

/* ── BOOT ANIMATION (maintainer, 2026-10-27; toolkit src/bootvideo) ──────────
 * media\boot.mpg (MPEG-1 video + MP2 audio; the README gives the one-time
 * ffmpeg conversion) plays before the title when boot_animation is on (the
 * default; saves\settings.ini, THPS_BOOT_ANIMATION=off); it cannot be skipped
 * (maintainer, 2026-10-27: the one video that is never skippable). A boot.mp4 /
 * boot.mkv without a boot.mpg is only named, in the
 * log and on the menu's status line -- the port never converts or changes
 * anything in media\. */
static int boot_on(void)
{
    return strcmp(settings_get("boot_animation", "on"), "off") != 0;
}

/* the file's state into s_boot_text; 1 when media\boot.mpg is there */
static int boot_state(char *path, size_t n)
{
    int convert, have = boot_video_find(g_thps_media_dir, path, n, &convert);
    snprintf(s_boot_text, sizeof s_boot_text, "%s", have ? "BOOT.MPG FOUND"
             : convert ? "CONVERT BOOT VIDEO TO BOOT.MPG  SEE README" : "NEEDS BOOT MPG IN THE MEDIA FOLDER");
    return have;
}

static int boot_sink(const uint8_t *bgra, int w, int h, void *user)
{
    (void)user;
    return nv2a_vk_boot_frame(bgra, w, h);
}

/* main.c, after the renderer is installed and before the title starts */
void thps_boot_video(void)
{
    char path[700];
    BootVideoSink sink = { boot_sink, NULL };
    if (!boot_state(path, sizeof path)) {
        if (s_boot_text[0] == 'C')
            fprintf(stderr, "[BOOT] boot video: media\\boot.mp4 or boot.mkv found, but no boot.mpg -- convert it once "
                            "(README: ffmpeg -i boot.mp4 -vf scale=-2:720 -r 30 -c:v mpeg1video -q:v 2 -c:a mp2 "
                            "-b:a 192k boot.mpg); "
                            "the port does not convert or change files in media\\\n");
        return;
    }
    if (!boot_on()) {
        fprintf(stderr, "[BOOT] boot video: %s found, BOOT ANIMATION is off\n", path);
        return;
    }
    if (!nv2a_vk_boot_begin())
        return;
    boot_video_play(path, &sink);
    nv2a_vk_boot_end();
}

static int s_opt_list;
static int s_opt_shown = OPT_MAIN;                /* the list the running Options screen was built with */
static uint32_t s_opt_shown_lines;
static int s_opt_back;                            /* B left a sub-list: Options again */
static uint32_t opt_next(int list, uint32_t line); /* RECOMP OPTIONS values, below */
/* SHADERS (below) */
static void sh_build(int list);
static uint32_t sh_action(uint32_t line);
static int sh_lr(uint32_t line, int dir);
static void sh_tick(void);
static void sh_summary(char *out, size_t n);
static int sh_menu_usable(void);
static void sh_ov_load(void);
static void sh_user_scan(void);
static void sh_apply(void);
static char s_sh_saved[40];
static void bind_upper(char *dst, size_t n, const char *src);

static int opt_menu_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("THPS_OPTIONS_MENU");
        on = !(e && *e == '0');
    }
    return on;
}

static uint32_t opt_lines(int list)
{
    uint32_t n = 0;
    while (n < 12 && s_opt[list].line[n] && s_opt[list].line[n][0])
        n++;
    return n;
}

/* the lines the menu navigates: the original pad greying always touches
 * lines 1-4 -- the menu needs entries 0-4 -- so a short list is padded with
 * blank, greyed lines up to 5 */
static uint32_t opt_nav_lines(int list)
{
    uint32_t n = opt_lines(list);
    return n < 5 ? 5 : n;
}

static const char *const s_opt_old[13] = {
    "OPTIONS", "CHAT MESSAGES", "PLAYER 1 CONTROLS", "PLAYER 2 CONTROLS", "PLAYER 3 CONTROLS",
    "PLAYER 4 CONTROLS", "SOUND LEVELS", "MOVIES", "MEMORY UNIT", "CHEATS", "HIGH SCORES",
    "GAP CHECKLIST", "GENERAL OPTIONS" };

/* Rewrite the title and the 12 line texts (u16 length + text, in file order)
 * of a loaded OptionMain.PANEL; the result is never longer than the file
 * (unused lines become " "). */
static uint32_t guest_alloc_like_loader(uint32_t size);
static void guest_free_like_loader(uint32_t buf);

/* Returns the panel buffer: the loaded one, or a bigger one from the
 * loader's allocator when the new texts need more room (PLAYBACK MODE). */
static uint32_t opt_rewrite_panel(uint32_t buf, uint32_t size_ptr)
{
    uint8_t *g = (uint8_t *)((uintptr_t)g_xbox_mem_offset);
    uint32_t size = GUEST32(size_ptr), room, pos = 0, last = 0, out = 0, i, nb = buf;
    const OptList *l = &s_opt[s_opt_list];
    uint8_t *tmp;
    if (size < 16 || size > 65536 || !(tmp = (uint8_t *)malloc(room = size + 1024)))
        return buf;
    for (i = 0; i < 13; i++) {
        const char *nw = i == 0 ? l->title : (l->line[i - 1] && l->line[i - 1][0] ? l->line[i - 1] : " ");
        size_t ol = strlen(s_opt_old[i]), nl = strlen(nw);
        uint32_t p;
        for (p = pos; p + 2 + ol <= size; p++)
            if (g[buf + p] == (uint8_t)ol && g[buf + p + 1] == 0 && !memcmp(g + buf + p + 2, s_opt_old[i], ol))
                break;
        if (p + 2 + ol > size || out + (p - last) + 2 + nl > room) {
            free(tmp);                                 /* not the layout we know: leave it */
            return buf;
        }
        memcpy(tmp + out, g + buf + last, p - last);
        out += p - last;
        tmp[out++] = (uint8_t)nl;
        tmp[out++] = 0;
        memcpy(tmp + out, nw, nl);
        out += (uint32_t)nl;
        last = pos = p + 2 + (uint32_t)ol;
    }
    if (out + (size - last) > room) {
        free(tmp);
        return buf;
    }
    memcpy(tmp + out, g + buf + last, size - last);
    out += size - last;
    if (out > size && !(nb = guest_alloc_like_loader(out))) {
        free(tmp);
        return buf;
    }
    memcpy(g + nb, tmp, out);
    if (nb == buf)
        memset(g + buf + out, 0, size - out);
    else
        guest_free_like_loader(buf);
    GUEST32(size_ptr) = out;
    free(tmp);
    return nb;
}

/* The front end's file loader (ecx = name, edx = &size, 2 stack args). */
extern void sub_0017A060_gen(void);
void sub_0017A060(void)
{
    char name[96];
    uint32_t np = g_ecx, size_ptr = g_edx, k;
    for (k = 0; k + 1 < sizeof name; k++) {
        name[k] = *(volatile char *)((uintptr_t)g_xbox_mem_offset + np + k);
        if (!name[k])
            break;
    }
    name[k] = 0;
    sub_0017A060_gen();
    if (g_eax && opt_menu_on() && k >= 16 && !strcmp(name + k - 16, "OptionMain.PANEL"))
        g_eax = opt_rewrite_panel(g_eax, size_ptr);
    /* the game's PLAYER n CONTROLS screen stays stock since 2026-10-25: BUTTON
     * MAPPING is its own line under CONTROLS (thps_ctl_panel: the former,
     * shown but not selectable fourth line; THPS_CTL_PANEL_LINE=1 shows it) */
    if (g_eax && opt_menu_on() && k >= 25 && !strcmp(name + k - 25, "OptionControlScreen.PANEL")
            && getenv("THPS_CTL_PANEL_LINE") && *getenv("THPS_CTL_PANEL_LINE") == '1') {
        extern uint32_t thps_ctl_panel(uint32_t buf, uint32_t size_ptr);
        g_eax = thps_ctl_panel(g_eax, size_ptr);
    }
}

/* after 0x0013D323 (push 0xC): the number of lines the menu navigates */
void thps_opt_line_count(void)
{
    if (opt_menu_on())
        GUEST32(g_esp) = opt_nav_lines(s_opt_list);
}

/* Init: the line -> next state table for the list shown (sub-lists and
 * placeholders return Options again; A sorts them out). */
extern void sub_0013D230_gen(void);
void sub_0013D230(void)
{
    uint32_t screen = g_ecx, i;
    sub_0013D230_gen();
    s_opt_shown = s_opt_list;
    s_opt_shown_lines = opt_nav_lines(s_opt_list);
    if (!opt_menu_on())
        return;
    for (i = 0; i < 12; i++) {
        uint32_t n = i < opt_lines(s_opt_list) ? opt_next(s_opt_list, i) : OPT_STAY;
        GUEST32(screen + 0x214u + 4 * i) = n >= 0x1000u ? OPT_STAY : n;
    }
}

/* Button mapping (below): the screen, a refresh after a recorded press or a
 * PLAYER / DEVICE step. */
static void bind_open(void);
static uint32_t bind_keys_action(uint32_t line);
static int  bind_lr(uint32_t line, int dir);
static int  s_bind_refresh;                        /* rebuild Options, cursor on s_bind_cursor */
static uint32_t s_bind_cursor;

/* ── RECOMP OPTIONS values (M6; maintainer, 2026-10-26) ──────────────────────
 * Every setting is one line "NAME: VALUE" (maintainer: no sub-list per
 * setting); left / right -- or A, one step on -- changes it, saves it in
 * saves\settings.ini at once and rebuilds the screen with the new text (the
 * A-latch refresh, see opt_lr below). Lines: RECOMP OPTIONS -> DISPLAY MODE,
 * RENDER RESOLUTION, ASPECT RATIO, MENUS AT 16:9 (only with 16:9 saved),
 * ANISOTROPIC FILTER (ANTIALIASING, FRAME RATE and BOOT ANIMATION open lists);
 * FRAME RATE -> FRAME RATE, VSYNC, FPS COUNTER. A hint sits in the value:
 * AFTER RESTART while the saved value is not the one running, NEEDS 120 HZ
 * for 120 on a slower display.
 * settings_get prefers THPS_<KEY> from the environment, so every value can be
 * forced for tests (THPS_FRAME_RATE=120, THPS_ASPECT_RATIO=16:9 ...); the
 * toolkit's own switches (RECOMP_GL_SCALE, RECOMP_VSYNC, RECOMP_FPS_HUD,
 * RECOMP_STRETCH_43, RECOMP_VK_ANISO as a cap, THPS_WIDESCREEN) still win.
 * Applies now: FRAME RATE (thps_fps looks again at once), RENDER RESOLUTION
 * (nv2a_vk_set_render_scale, next flip), MENUS AT 16:9 (nv2a_vk_set_stretch_43
 * per frame, never during a movie), ANISOTROPIC FILTER (nv2a_vk_aniso, the
 * next sampler), FPS COUNTER (nv2a_vk_set_fps_hud), DISPLAY MODE
 * (nv2a_vk_set_display_mode; the windowed rectangle is kept in window_rect,
 * thps_options_tick). After a restart -- the toolkit reads them once at
 * start, main.c turns them into its env defaults: ASPECT RATIO, VSYNC (OFF =
 * the unblocked present modes, RECOMP_VSYNC=0). BOOT ANIMATION: media\boot.mpg,
 * ON / OFF in its sub-list (see "BOOT ANIMATION" above). */
enum { VAL_DISPMODE, VAL_RENDER, VAL_ASPECT, VAL_MENUS, VAL_ANISO, VAL_FRAMERATE, VAL_VSYNC, VAL_COUNTER,
       VAL_TIPS, VAL_SCORE, VAL_CHAT, VAL_BLUR,
       VAL_LAYOUT, VAL_SPINTAPS, VAL_BIGDROP, VAL_COUNT };
#define VAL_FIRST_GAME VAL_TIPS                   /* the game's own GENERAL OPTIONS values .. */
#define VAL_LAST_GAME VAL_BLUR

/* ── GENERAL OPTIONS values: the game's four ─────────────────────────────────
 * The game's screen (init sub_00130D00, exit sub_001314E0) shows the globals
 * below as ON / OFF and, on leaving (unless 0x003068B4 is set), writes them
 * back and calls sub_00073810, which copies every option into the save block
 * (TRICK TIPS -> 0x002FFF20, SCORE DISPLAY -> 0x002FFF24, QUICK CHAT ->
 * 0x002FFF1C, MOTION BLUR -> 0x002FFF60; sub_00073A30 loads them back). Nonzero
 * = ON; the game starts with all four ON (sub_00073D20). Our list does the
 * same at each step: the global, then sub_00073810. */
static const struct { uint32_t va, size; } s_game_opt[VAL_LAST_GAME - VAL_FIRST_GAME + 1] = {
    { 0x002B18DCu, 4 },                           /* TRICK TIPS */
    { 0x002B18E0u, 4 },                           /* SCORE DISPLAY */
    { 0x002B18D8u, 4 },                           /* QUICK CHAT */
    { 0x002B191Cu, 1 },                           /* MOTION BLUR */
};
extern void sub_00073810(void);

static int game_opt_on(int id)
{
    uint32_t va = s_game_opt[id - VAL_FIRST_GAME].va;
    return (s_game_opt[id - VAL_FIRST_GAME].size == 1 ? *(volatile uint8_t *)((uintptr_t)g_xbox_mem_offset + va)
                                                      : GUEST32(va)) != 0;
}

/* set it like the game's screen does on leaving; 0 when the game keeps them now */
static int game_opt_set(int id, int on)
{
    uint32_t va = s_game_opt[id - VAL_FIRST_GAME].va;
    if (GUEST32(0x003068B4u))
        return 0;
    if (s_game_opt[id - VAL_FIRST_GAME].size == 1)
        *(volatile uint8_t *)((uintptr_t)g_xbox_mem_offset + va) = (uint8_t)(on != 0);
    else
        GUEST32(va) = on != 0;
    g_esp -= 4;
    GUEST32(g_esp) = 0;                           /* return address, popped by its ret */
    sub_00073810();
    return 1;
}

typedef struct {
    const char *label, *key, *def;
    const char *val[5], *txt[5];                  /* stored values, shown texts */
    int nval;
} OptVal;

static const OptVal s_val[VAL_COUNT] = {
    /* applies at once (toolkit nv2a_vk_set_display_mode); default BORDERLESS
     * (maintainer, 2026-10-26); FULLSCREEN = exclusive, the native mode;
     * automated runs force windowed (THPS_DISPLAY_MODE=windowed) */
    [VAL_DISPMODE]  = { "DISPLAY MODE", "display_mode", "borderless", { "fullscreen", "borderless", "windowed" },
                        { "FULLSCREEN", "BORDERLESS", "WINDOWED" }, 3 },
    [VAL_RENDER]    = { "RENDER RESOLUTION", NULL, NULL, { NULL }, { NULL }, 0 },   /* opt_render_* */
    [VAL_ASPECT]    = { "ASPECT RATIO", "aspect_ratio", "4:3", { "4:3", "16:9" }, { "4:3", "16:9" }, 2 },
    /* "menus stretched" (aspect-ratio rule, maintainer 2026-10-25): with 16:9
     * on, the 4:3 menu screens pillarboxed or stretched; movies always stay
     * pillarboxed (maintainer, 2026-10-26); gameplay is real 16:9 either way */
    [VAL_MENUS]     = { "MENUS AT 16:9", "menus_stretched", "off", { "off", "on" },
                        { "PILLARBOXED", "STRETCHED" }, 2 },
    [VAL_ANISO]     = { "ANISOTROPIC FILTER", "anisotropic_filter", "16", { "off", "2", "4", "8", "16" },
                        { "OFF", "2X", "4X", "8X", "16X" }, 5 },
    [VAL_FRAMERATE] = { "FRAME RATE", "frame_rate", "original", { "original", "120" }, { "ORIGINAL 60", "120" }, 2 },
    [VAL_VSYNC]     = { "VSYNC", "vsync", "on", { "on", "off" }, { "ON", "OFF" }, 2 },
    /* the counter's two numbers: frames per second, and the longest frame of
     * the last half second in ms (maintainer, 2026-10-26: either or both) */
    [VAL_COUNTER]   = { "FPS COUNTER", "fps_counter", "off", { "off", "fps", "fps_frametime", "frametime" },
                        { "OFF", "FPS", "FPS AND FRAME TIME", "FRAME TIME" }, 4 },
    /* v0.9.2 gameplay options (maintainer, 2026-10-28; player requests):
     * default = the original game; read where they act (ps2_layout,
     * spin_taps_off, ...), so a change applies at once */
    /* the game's own, in its memory (game_opt_*): key NULL */
    [VAL_TIPS]      = { "TRICK TIPS", NULL, NULL, { NULL }, { "ON", "OFF" }, 2 },
    [VAL_SCORE]     = { "SCORE DISPLAY", NULL, NULL, { NULL }, { "ON", "OFF" }, 2 },
    [VAL_CHAT]      = { "QUICK CHAT", NULL, NULL, { NULL }, { "ON", "OFF" }, 2 },
    [VAL_BLUR]      = { "MOTION BLUR", NULL, NULL, { NULL }, { "ON", "OFF" }, 2 },
    /* PLAYSTYLE (maintainer, 2026-10-28; was LAYOUT): the key stays control_layout */
    [VAL_LAYOUT]    = { "PLAYSTYLE", "control_layout", "xbox", { "xbox", "ps2" }, { "XBOX", "PS2" }, 2 },
    [VAL_SPINTAPS]  = { "180 SPIN TAPS", "spin_taps", "on", { "on", "off" }, { "ON", "OFF" }, 2 },
    [VAL_BIGDROP]   = { "BIG DROP", "big_drop", "original", { "original", "no_bail", "off" },
                        { "ORIGINAL", "NO BAIL", "OFF" }, 3 },
};

/* line -> VAL_* + 1, 0 = not a value line (opt_build). Zero means none on
 * purpose: every list opt_build never touched (OPTIONS itself, CONTROLS ...)
 * is all zeros -- a -1 marker left them all looking like RENDER RESOLUTION
 * lines (2026-10-26: A on MEMORY UNIT stepped the render scale; the storage
 * test caught it). */
static signed char s_val_map[OPT_LISTS][12];

static int opt_val_at(int list, uint32_t line)    /* VAL_* or -1 */
{
    return line < 12 ? s_val_map[list][line] - 1 : -1;
}

static int s_vsync_start = -1;                    /* VSYNC as it runs (1 off), thps_options_apply */
static int s_boot_start = -1;                     /* BOOT ANIMATION as at this start (it plays only then) */
/* the render step before the last one (+1, 0 none), its aspect and how many
 * frames thps_options_tick still watches whether the new one allocated */
static int s_render_undo, s_render_undo_ws, s_render_undo_frames;
static int s_aa_undo;                             /* ANTIALIASING before the last pick (+1, 0 none) */
static uint32_t s_line_next[OPT_LISTS][12];       /* opt_build's next states (RECOMP OPTIONS) */
static uint32_t s_sub_line[OPT_LISTS];            /* a sub-list's line in RECOMP OPTIONS (opt_build) */

/* a line's next state: built lists keep theirs in s_line_next */
static uint32_t opt_next(int list, uint32_t line)
{
    if (list == OPT_RECOMP || list == OPT_GENERAL)
        return line < 12 ? s_line_next[list][line] : OPT_STAY;
    return s_opt[list].next[line];
}

/* the line of the parent list a list was opened from */
static uint32_t opt_parent_line(int list)
{
    return s_opt[list].parent == OPT_RECOMP ? s_sub_line[list] : (uint32_t)s_opt[list].parent_line;
}

/* the value in use as an index into val[] (an unknown value: the default) */
static int opt_val_index(int id)
{
    const OptVal *v = &s_val[id];
    const char *cur;
    int i;
    if (id >= VAL_FIRST_GAME && id <= VAL_LAST_GAME)
        return game_opt_on(id) ? 0 : 1;           /* ON, OFF */
    cur = settings_get(v->key, v->def);
    for (i = 0; i < v->nval; i++)
        if (!strcmp(cur, v->val[i]))
            return i;
    for (i = 0; i < v->nval; i++)
        if (!strcmp(v->def, v->val[i]))
            return i;
    return 0;
}

extern volatile int nv2a_vk_aniso;                /* toolkit nv2a_vk.c: 1..16, per sampler */

static int opt_aniso_level(void)
{
    int n = atoi(settings_get("anisotropic_filter", "16"));
    return n == 2 || n == 4 || n == 8 || n == 16 ? n : 1;   /* "off" = 1 */
}

static int opt_hud_mode(void)
{
    const char *v = settings_get("fps_counter", "off");
    return !strcmp(v, "fps") ? NV2A_VK_HUD_FPS : !strcmp(v, "fps_frametime") ? NV2A_VK_HUD_BOTH
         : !strcmp(v, "frametime") ? NV2A_VK_HUD_MS : NV2A_VK_HUD_OFF;
}

static int opt_stretch_on(void)
{
    return !strcmp(settings_get("menus_stretched", "off"), "on");
}

/* ANTIALIASING: saved as off / fxaa / smaa / msaa2 / msaa4 (index = NV2A_VK_AA_*) */
static const char *const s_aa_val[5] = { "off", "fxaa", "smaa", "msaa2", "msaa4" };

static int opt_aa_index(void)
{
    const char *v = settings_get("antialiasing", "fxaa");
    int i;
    for (i = 0; i < 5; i++)
        if (!strcmp(v, s_aa_val[i]))
            return i;
    return NV2A_VK_AA_FXAA;
}

static int opt_display_mode(void)                 /* NV2A_VK_DISPLAY_*, from DISPLAY MODE's index */
{
    static const int m[3] = { NV2A_VK_DISPLAY_FULLSCREEN, NV2A_VK_DISPLAY_BORDERLESS, NV2A_VK_DISPLAY_WINDOWED };
    return m[opt_val_index(VAL_DISPMODE)];
}

/* RENDER RESOLUTION (maintainer, 2026-10-26): at 4:3 the 17 factors
 * 1.00X..5.00X (step 0..16, X = 640 x factor); at 16:9 only the standard
 * sizes (step 0..7: native 854X480, then 720 / 900 / 1080 / 1440 / 1800 /
 * 2160 / 2400 lines, X = lines x 16/9) -- the 0.25 grid gave sizes like
 * 1707X960 there. The factor is vertical either way and each aspect keeps its
 * own (render_scale / render_scale_wide); switching ASPECT RATIO carries the
 * width over (3X 1920X1440 -> 1920X1080), opt_render_carry. A value the device
 * cannot hold is shown, marked, and neither applied nor saved. */
static const int s_wide_lines[] = { 480, 720, 900, 1080, 1440, 1800, 2160, 2400 };
#define OPT_WIDE_STEPS ((int)(sizeof s_wide_lines / sizeof s_wide_lines[0]) - 1)

static int opt_render_last(int ws)
{
    return ws ? OPT_WIDE_STEPS : OPT_RENDER_STEPS;
}

static double opt_render_factor(int s, int ws)
{
    return ws ? s_wide_lines[s] / 480.0 : 1.0 + 0.25 * s;
}

static int opt_render_width(int s, int ws)
{
    return (int)(ws ? s_wide_lines[s] * 16.0 / 9.0 + 0.5 : 640 * opt_render_factor(s, 0) + 0.5);
}

/* the step whose width is closest to w (native stays native) */
static int opt_render_step_for_width(int w, int ws)
{
    int s, best = 0;
    if (w <= (ws ? 640 : 854))
        return 0;
    for (s = 1; s <= opt_render_last(ws); s++)
        if (abs(opt_render_width(s, ws) - w) < abs(opt_render_width(best, ws) - w))
            best = s;
    return best;
}

static const char *opt_render_key(int ws)
{
    return ws ? "render_scale_wide" : "render_scale";
}

/* the saved step for an aspect; without one, the other aspect's by width */
static int opt_render_saved_step_for(int ws, int *saved)
{
    const char *v = settings_get(opt_render_key(ws), NULL), *o;
    int s, best = 0;
    if (saved)
        *saved = v != NULL;
    if (!v) {
        if (!(o = settings_get(opt_render_key(!ws), NULL)))
            return 0;
        if (saved)
            *saved = 1;
        return opt_render_step_for_width(opt_render_width(opt_render_saved_step_for(!ws, NULL), !ws), ws);
    }
    for (s = 1; s <= opt_render_last(ws); s++)
        if (fabs(opt_render_factor(s, ws) - atof(v)) < fabs(opt_render_factor(best, ws) - atof(v)))
            best = s;
    return best;
}

static void opt_render_save(int s, int ws)
{
    char v[12];
    snprintf(v, sizeof v, "%.3f", opt_render_factor(s, ws));
    settings_set(opt_render_key(ws), v);
}

/* ASPECT RATIO picked: the other aspect's render size gets the same width */
static void opt_render_carry(int from_ws, int to_ws)
{
    int saved;
    int s = opt_render_saved_step_for(from_ws, &saved);
    if (from_ws != to_ws && saved)
        opt_render_save(opt_render_step_for_width(opt_render_width(s, from_ws), to_ws), to_ws);
}

/* "RENDER RESOLUTION: 2.25X 1920X1080" */
static void opt_render_text(int s, int ws, char *out, size_t n)
{
    double f = opt_render_factor(s, ws);
    int m = (int)(f * 1000.0 + 0.5);
    snprintf(out, n, m % 10 ? "RENDER RESOLUTION: %d.%03dX %dX%d%s" : "RENDER RESOLUTION: %d.%02dX %dX%d%s", m / 1000,
             m % 10 ? m % 1000 : (m % 1000) / 10, opt_render_width(s, ws), (int)(480 * f + 0.5),
             nv2a_vk_render_scale_fits(f, ws) ? "" : " TOO LARGE");
}

static void opt_shader_apply(void);

/* main.c, right after nv2a_vk_install: the values the renderer takes at run
 * time. No saved render size leaves the toolkit's default (RECOMP_GL_SCALE). */
void thps_options_apply(void)
{
    int saved, s = opt_render_saved_step_for(thps_widescreen(), &saved);
    char rt[48];
    const char *rs = saved ? rt + 19 : NULL;      /* past "RENDER RESOLUTION: " */
    if (saved) {
        nv2a_vk_set_render_scale(opt_render_factor(s, thps_widescreen()), thps_widescreen());
        opt_render_text(s, thps_widescreen(), rt, sizeof rt);
    }
    s_vsync_start = opt_val_index(VAL_VSYNC);
    s_boot_start = boot_on();
    nv2a_vk_set_display_mode(opt_display_mode());
    nv2a_vk_set_antialiasing(opt_aa_index());
    {
        /* the windowed rectangle saved last time (thps_options_tick) */
        int x, y, w, h;
        if (sscanf(settings_get("window_rect", ""), "%d,%d,%d,%d", &x, &y, &w, &h) == 4)
            nv2a_vk_set_window_rect(x, y, w, h);
    }
    nv2a_vk_aniso = opt_aniso_level();
    nv2a_vk_set_fps_hud(opt_hud_mode());
    s_menus_stretched = opt_stretch_on();       /* per frame in sub_00018F90: not during a movie */
    fprintf(stderr, "  options: display %s, render scale %s, anisotropic filter %dx, antialiasing %s, frame rate %s, "
            "aspect %s, menus %s, vsync %s, fps counter %s\n", s_val[VAL_DISPMODE].val[opt_val_index(VAL_DISPMODE)],
            rs ? rs : "default", nv2a_vk_aniso, s_aa_val[opt_aa_index()], settings_get("frame_rate", "original"),
            thps_widescreen() ? "16:9" : "4:3", opt_stretch_on() ? "stretched" : "pillarboxed",
            settings_get("vsync", "on"), settings_get("fps_counter", "off"));
    opt_shader_apply();
}

/* SHADERS at start (docs/shader_module.md): shader_on, shader_preset (a .slangp
 * path, relative to the layout folder -- the one that holds game\ and media\ --
 * or absolute; "" or missing = none) and shader_params from the settings file
 * (sh_apply, see "SHADERS" below). THPS_SHADER_PRESET (read as UTF-8:
 * settings_get's getenv is the ANSI one) forces a preset for tests, without the
 * saved values. The toolkit's RECOMP_SHADER_PRESET wins over both.
 * While the packs are being (re)extracted at start (data\ deleted), a preset in
 * them -- or a saved one whose #reference points there -- cannot load yet:
 * s_sh_retry makes thps_options_tick apply it once more when the extraction
 * has ended and the first try failed (sh_retry_tick). */
static int s_sh_retry;
static void opt_shader_apply(void)
{
    extern const char *xbox_getenv_utf8(const char *name);
    const char *p = xbox_getenv_utf8("THPS_SHADER_PRESET");
    char full[1024];
    size_t n = strlen(g_thps_media_dir);
    s_sh_retry = shader_pack_state() == SHADER_PACK_WORKING;
    sh_ov_load();
    sh_user_scan();
    if (!p || !*p) {
        sh_apply();
        p = settings_get("shader_preset", "");
        if (*p)
            fprintf(stderr, "  options: shader %s, preset %s, values \"%s\"%s\n",
                    strcmp(settings_get("shader_on", "on"), "off") ? "on" : "off", p,
                    settings_get("shader_params", ""),
                    nv2a_vk_shader_library_present() ? "" : " (no librashader next to the exe)");
        return;
    }
    if (*p && !(p[0] == '/' || p[0] == '\\' || (p[0] && p[1] == ':'))) {
        if (n >= 5)
            n -= 5;                                   /* "<layout>\media" -> "<layout>\" */
        snprintf(full, sizeof full, "%.*s%s", (int)n, g_thps_media_dir, p);
        p = full;
    }
    nv2a_vk_set_shader_preset(*p ? p : NULL);
    if (*p)
        fprintf(stderr, "  options: shader preset %s%s\n", p,
                nv2a_vk_shader_library_present() ? "" : " (no librashader next to the exe)");
}

/* thps_options_tick: the extraction at start has ended -- a preset that failed
 * meanwhile is applied once more (one that is still building is waited for) */
static void sh_retry_tick(void)
{
    if (!s_sh_retry || shader_pack_state() == SHADER_PACK_WORKING
        || nv2a_vk_shader_status() == NV2A_VK_SHADER_LOADING)
        return;
    s_sh_retry = 0;
    if (nv2a_vk_shader_status() != NV2A_VK_SHADER_FAILED)
        return;
    fprintf(stderr, "  options: shader packs ready -- the preset that failed at start once more\n");
    nv2a_vk_set_shader_preset(NULL);               /* the same path again is no new request (sp_set_preset) */
    opt_shader_apply();                            /* the SHADERS screen follows by itself (sh_tick) */
}

/* one value line's text */
static void opt_val_text(int id, char *out, size_t n)
{
    const char *hint = "";
    if (id == VAL_RENDER) {
        int ws = thps_widescreen();
        opt_render_text(opt_render_saved_step_for(ws, NULL), ws, out, n);
        return;
    }
    if (id == VAL_ASPECT && opt_val_index(id) != thps_widescreen())
        hint = " AFTER RESTART";
    else if (id == VAL_VSYNC && s_vsync_start >= 0 && opt_val_index(id) != s_vsync_start)
        hint = " AFTER RESTART";
    else if (id == VAL_FRAMERATE && opt_val_index(id) == 1) {
        float hz = nv2a_vk_display_hz();
        if (hz > 0.0f && hz < 0.99f * 120.0f)
            hint = " NEEDS 120 HZ";                /* kept for a 120 Hz display; plays at 60 here */
    }
    /* no NOT ORIGINAL mark any more: Y puts a line back (maintainer, 2026-10-28) */
    snprintf(out, n, "%s: %s%s", s_val[id].label, s_val[id].txt[opt_val_index(id)], hint);
}

/* ASPECT RATIO / VSYNC / BOOT ANIMATION saved but not what this start runs
 * with: the RESTART NEEDED text (BOOT ANIMATION: maintainer, 2026-10-27 - it
 * plays only at a start). The panel font has no comma: "A AND B", "A B AND C". */
static int opt_restart_pending(void)
{
    const char *what[3];
    int n = 0;
    if (opt_val_index(VAL_ASPECT) != thps_widescreen())
        what[n++] = "ASPECT RATIO";
    if (s_vsync_start >= 0 && opt_val_index(VAL_VSYNC) != s_vsync_start)
        what[n++] = "VSYNC";
    if (s_boot_start >= 0 && boot_on() != s_boot_start)
        what[n++] = "BOOT ANIMATION";
    if (n == 1)
        snprintf(s_restart_text, sizeof s_restart_text, "%s CHANGED", what[0]);
    else if (n == 2)
        snprintf(s_restart_text, sizeof s_restart_text, "%s AND %s CHANGED", what[0], what[1]);
    else if (n == 3)
        snprintf(s_restart_text, sizeof s_restart_text, "%s %s AND %s", what[0], what[1], what[2]);
    return n > 0;
}

/* a list's lines: texts, the line -> value map and (RECOMP OPTIONS) the
 * next states; an entry >= OPT_E_SUB opens list (entry - OPT_E_SUB) */
#define OPT_E_SUB 100
#define OPT_E_HINT 99
#define OPT_E_BLANK 98                            /* " ": an empty text would end the list (opt_lines) */
#define OPT_GENERAL_FIRST 2                       /* GENERAL OPTIONS: its first value line */
static void opt_build(int list)
{
    /* SHADERS before BOOT ANIMATION (maintainer, 2026-10-27); BUTTON CHEATS
     * between them (maintainer, 2026-10-28) */
    static const short recomp[] = { VAL_DISPMODE, VAL_RENDER, VAL_ASPECT, VAL_MENUS, VAL_ANISO,
                                    OPT_E_SUB + OPT_AA, OPT_E_SUB + OPT_FPS, OPT_E_SUB + OPT_SHADERS,
                                    OPT_E_SUB + OPT_CHEATS, OPT_E_SUB + OPT_BOOT };
    static const short frame[] = { VAL_FRAMERATE, VAL_VSYNC, VAL_COUNTER };
    /* the game's four, then LAYOUT and the gameplay values (maintainer,
     * 2026-10-28); OPT_E_HINT: a text line without an action */
    /* the Y hint on top, a blank line (greyed: the cursor skips it, see
     * sub_0013D400), then the values (maintainer, 2026-10-28) */
    static const short general[] = { OPT_E_HINT, OPT_E_BLANK, VAL_TIPS, VAL_SCORE, VAL_CHAT, VAL_BLUR,
                                     VAL_LAYOUT, VAL_SPINTAPS, VAL_BIGDROP };
    const short *e = NULL;
    int n = 0, i, line = 0;
    if (list >= OPT_SHADERS && list <= OPT_SHRESET) {
        sh_build(list);
        return;
    }
    if (list == OPT_RECOMP)
        e = recomp, n = (int)(sizeof recomp / sizeof recomp[0]);
    else if (list == OPT_FPS)
        e = frame, n = (int)(sizeof frame / sizeof frame[0]);
    else if (list == OPT_GENERAL)
        e = general, n = (int)(sizeof general / sizeof general[0]);
    if (!e)
        return;
    memset(s_val_map[list], 0, sizeof s_val_map[list]);
    memset(s_val_text[list], 0, sizeof s_val_text[list]);
    for (i = 0; i < 12; i++)
        s_line_next[list][i] = OPT_STAY;
    for (i = 0; i < n; i++) {
        if (e[i] == VAL_MENUS && opt_val_index(VAL_ASPECT) != 1)
            continue;                              /* MENUS AT 16:9: only with 16:9 saved */
        if (e[i] == OPT_E_BLANK) {
            snprintf(s_val_text[list][line], sizeof s_val_text[list][line], " ");
        } else if (e[i] == OPT_E_HINT) {           /* Y resets the line's value (sub_001E66E0) */
            snprintf(s_val_text[list][line], sizeof s_val_text[list][line], "PRESS Y FOR DEFAULT SETTINGS");
        } else if (e[i] >= OPT_E_SUB) {
            int to = e[i] - OPT_E_SUB;
            if (to == OPT_AA)                      /* the sub-menu's title with the choice in use; */
                snprintf(s_val_text[list][line], sizeof s_val_text[list][line], "%s: %s%s", s_opt[to].title,
                         s_opt[OPT_AA].line[opt_aa_index()],    /* a shader chain replaces FXAA (vk_flip) */
                         opt_aa_index() == NV2A_VK_AA_FXAA && (nv2a_vk_shader_status() == NV2A_VK_SHADER_ON
                                                              || nv2a_vk_shader_status() == NV2A_VK_SHADER_LOADING)
                         ? " OFF WITH SHADER" : "");
            else if (to == OPT_SHADERS)            /* "SHADERS: OFF" / "SHADERS: CRT EASYMODE" */
                sh_summary(s_val_text[list][line], sizeof s_val_text[list][line]);
            else if (to == OPT_BOOT) {             /* "BOOT ANIMATION: ON" with media\boot.mpg there */
                char bp[700];
                if (boot_state(bp, sizeof bp))
                    snprintf(s_val_text[list][line], sizeof s_val_text[list][line], "%s: %s", s_opt[to].title,
                             boot_on() ? "ON" : "OFF");
                else
                    snprintf(s_val_text[list][line], sizeof s_val_text[list][line], "%s", s_opt[to].title);
            }
            else
                snprintf(s_val_text[list][line], sizeof s_val_text[list][line], "%s", s_opt[to].title);
            s_line_next[list][line] = OPT_SUB(to);
            s_sub_line[to] = (uint32_t)line;
        } else {
            s_val_map[list][line] = (signed char)(e[i] + 1);
            opt_val_text(e[i], s_val_text[list][line], sizeof s_val_text[list][line]);
        }
        line++;
    }
}

/* one step of a value (dir -1 / +1): saved, applied where it can be; 0 when
 * nothing changed (an end of RENDER RESOLUTION, a factor that failed before) */
static int opt_val_step(int id, int dir)
{
    if (id == VAL_RENDER) {
        int ws = thps_widescreen(), was = opt_render_saved_step_for(ws, NULL), s = was + dir;
        double f;
        if (s < 0 || s > opt_render_last(ws))
            return 0;
        f = opt_render_factor(s, ws);
        if (!nv2a_vk_render_scale_fits(f, ws))
            return 0;                              /* beyond the image limits or failed before: stays */
        opt_render_save(s, ws);
        nv2a_vk_set_render_scale(f, ws);           /* the next flip */
        s_render_undo = was + 1;                   /* opt_render_check: back to it if f fails to allocate */
        s_render_undo_ws = ws;
        s_render_undo_frames = 180;
        return 1;
    }
    if (id >= VAL_FIRST_GAME && id <= VAL_LAST_GAME)
        return game_opt_set(id, !game_opt_on(id));   /* ON <-> OFF, in the game's memory */
    {
        int was = opt_val_index(id), nv = s_val[id].nval;
        int to = (was + dir + nv) % nv;             /* wraps */
        settings_set(s_val[id].key, s_val[id].val[to]);
        if (id == VAL_FRAMERATE)
            s_fps_next_ns = 0;
        else if (id == VAL_ANISO)
            nv2a_vk_aniso = opt_aniso_level();
        else if (id == VAL_COUNTER)
            nv2a_vk_set_fps_hud(opt_hud_mode());
        else if (id == VAL_MENUS)
            s_menus_stretched = opt_stretch_on();
        else if (id == VAL_ASPECT)
            opt_render_carry(was, to);             /* the other aspect's render size, same width */
        else if (id == VAL_DISPMODE)
            nv2a_vk_set_display_mode(opt_display_mode());
    }
    return 1;
}

/* A: a sub-list entry opens its list on its first line; on a value line one
 * step on (opt_val_step). */
static int s_val_refresh;                          /* the A latch set by opt_lr, not a press */

extern void sub_0013D470_gen(void);
void sub_0013D470(void)
{
    uint32_t screen = g_ecx, line, n;
    sub_0013D470_gen();
    if (!opt_menu_on())
        return;
    if (s_bind_refresh) {                          /* set by the button mapping's tick, not a press */
        s_bind_refresh = 0;
        GUEST32(0x00306250u) = s_bind_cursor;
        g_eax = OPT_STAY;
        return;
    }
    if (s_val_refresh) {                           /* a value stepped: the screen again, same line */
        s_val_refresh = 0;
        g_eax = OPT_STAY;
        return;
    }
    line = GUEST32(screen + 0x1B8u);
    if (line >= opt_lines(s_opt_list))
        return;
    n = opt_next(s_opt_list, line);
    if (n >= 0x1000u) {
        int to = (int)(n - 0x1000u);
        if ((to == OPT_BINDKEYS || to == OPT_CHEATS) && s_in_level)
            return;                                /* button mapping, cheats: front-end Options only */
        if (to == OPT_BINDKEYS)
            bind_open();
        s_opt_list = to;
        opt_build(to);
        GUEST32(0x00306250u) = 0;
        if (s_opt_list == OPT_ALBUM) {             /* on the value line, showing the choice */
            thps_st_choice_text(thps_st_choice(), s_album_text, sizeof s_album_text);
            GUEST32(0x00306250u) = OPT_ALBUM_VALUE_LINE;
        } else if (s_opt_list == OPT_PLAYBACK) {   /* on the mode in use */
            GUEST32(0x00306250u) = (uint32_t)thps_st_mode();
        } else if (s_opt_list == OPT_AA) {         /* on the antialiasing in use */
            GUEST32(0x00306250u) = (uint32_t)opt_aa_index();
        } else if (s_opt_list == OPT_BOOT) {       /* the status line fresh; on ON / OFF in use */
            char bp[700];
            if (boot_state(bp, sizeof bp))
                GUEST32(0x00306250u) = boot_on() ? 1u : 2u;
        } else if (s_opt_list == OPT_GENERAL) {    /* on its first value, under the Y hint */
            GUEST32(0x00306250u) = OPT_GENERAL_FIRST;
        } else if (s_opt_list == OPT_CHEATS) {     /* on the first cheat of the first page */
            static int probed;
            const char *e = getenv("THPS_CHEAT_PROBE"), *on = getenv("THPS_CHEATS_ON");
            if (!probed) {
                int i;
                probed = 1;
                if (e && *e == '1')
                    cheat_probe();
                /* THPS_CHEATS_ON=<code>,<code>... (test, cheat_level.txt):
                 * those codes entered at the first opening, as A would */
                for (i = 0; on && i < CHEAT_COUNT; i++) {
                    const char *p = strstr(on, s_cheats[i].code);
                    size_t n = strlen(s_cheats[i].code);
                    if (p && (p == on || p[-1] == ',') && (p[n] == ',' || !p[n]))
                        fprintf(stderr, "[CHEAT] %s (%s) -> %d\n", s_cheats[i].code, s_cheats[i].text,
                                cheat_enter(i));
                }
            }
            s_cheat_page = 0;
            s_cheat_refused = -1;
            cheat_build();
            GUEST32(0x00306250u) = 1;
        }
        g_eax = OPT_STAY;
    } else if (s_opt_list == OPT_CHEATS) {         /* A: a cheat switched through the game's checker */
        GUEST32(0x00306250u) = cheat_action(line);
        g_eax = OPT_STAY;
    } else if (s_opt_list == OPT_BINDKEYS) {       /* record a press, next page or reset */
        GUEST32(0x00306250u) = bind_keys_action(line);
        g_eax = OPT_STAY;
    } else if (s_opt_list == OPT_RESTART) {        /* RESTART NOW, or LATER: back to OPTIONS */
        extern void host_restart(void);            /* platform/host.h */
        if (line == 1)
            host_restart();                        /* returns only if the new process failed */
        s_opt_list = OPT_MAIN;
        GUEST32(0x00306250u) = (uint32_t)s_opt[OPT_RESTART].parent_line;
        g_eax = OPT_STAY;
    } else if (s_opt_list == OPT_AA) {             /* an antialiasing picked: saved, at once, back */
        s_aa_undo = opt_aa_index() + 1;            /* thps_options_tick: back to it if MSAA fails */
        settings_set("antialiasing", s_aa_val[line < 5 ? line : NV2A_VK_AA_FXAA]);
        nv2a_vk_set_antialiasing(opt_aa_index());
        GUEST32(0x00306250u) = opt_parent_line(OPT_AA);
        s_opt_list = OPT_RECOMP;
        opt_build(OPT_RECOMP);
        g_eax = OPT_STAY;
    } else if (s_opt_list == OPT_BOOT) {           /* ON / OFF picked (with the file): saved, back */
        if ((line == 1 || line == 2) && s_boot_text[0] == 'B') {
            settings_set("boot_animation", line == 1 ? "on" : "off");
            GUEST32(0x00306250u) = opt_parent_line(OPT_BOOT);
            s_opt_list = OPT_RECOMP;
            opt_build(OPT_RECOMP);
        }
        g_eax = OPT_STAY;
    } else if (s_opt_list >= OPT_SHADERS && s_opt_list <= OPT_SHRESET) {   /* SHADERS: sh_action */
        GUEST32(0x00306250u) = sh_action(line);
        g_eax = OPT_STAY;
    } else if (opt_val_at(s_opt_list, line) >= 0) { /* a value line: one step on */
        if (opt_val_step(opt_val_at(s_opt_list, line), +1))
            opt_build(s_opt_list);
        g_eax = OPT_STAY;
    } else if (s_opt_list == OPT_PLAYBACK) {       /* a mode picked: saved, back to SOUNDTRACKS */
        thps_st_set_mode((int)line);
        s_opt_list = s_opt[OPT_PLAYBACK].parent;
        GUEST32(0x00306250u) = (uint32_t)s_opt[OPT_PLAYBACK].parent_line;
        g_eax = OPT_STAY;
    }
}

/* B: from a sub-list one level up, on the entry it came from. */
extern void sub_0013D4D0_gen(void);
void sub_0013D4D0(void)
{
    int was = s_opt_list;
    sub_0013D4D0_gen();
    if (opt_menu_on() && was == OPT_RECOMP && opt_restart_pending()) {
        s_opt_list = OPT_RESTART;                  /* ask first, on RESTART NOW */
        s_opt_back = 1;
        GUEST32(0x00306250u) = 1;
    } else if (opt_menu_on() && s_opt[was].parent >= 0) {
        if (was == OPT_SHADERS)
            s_sh_saved[0] = 0;                     /* SAVED AS ...: only until SHADERS is left */
        s_opt_list = s_opt[was].parent;
        opt_build(s_opt_list);
        s_opt_back = 1;
        GUEST32(0x00306250u) = opt_parent_line(was);
    }
}

/* Left / right on a value line (RECOMP OPTIONS values, SOUNDTRACK, BUTTON
 * MAPPING's PLAYER / DEVICE). The Options screen has no left/right of its
 * own -- its vtable (0x0022A2B0) keeps the front-end base handlers at +0x38
 * (left, 0x001E6600) and +0x3C (right, 0x001E65E0), which only clear that
 * button's latch for the 4 pads and return 0 (Sound Levels 0x0022A4D0
 * overrides the same two slots: +0x38 counts down, +0x3C up). The run loop
 * ignores what a left/right handler returns, so a step rewrites the line's
 * text and sets the A latch of the pad that pressed it (0x00305369 + k *
 * 0x18C, cleared by the A handler sub_0013D470): on the next frame A returns
 * OPT_STAY and Options is built again with the new text, the cursor on the
 * same line (0x00306250). Pad k's left / right latches: 0x003053B9 /
 * 0x003053C9 + k * 0x18C. */
#define PAD_LATCH(base, k) (*(volatile uint8_t *)((uintptr_t)g_xbox_mem_offset + (base) + (k) * 0x18Cu))

static int opt_latch_pad(uint32_t base)
{
    int k;
    for (k = 0; k < 4; k++)
        if (PAD_LATCH(base, k))
            return k;
    return 0;                                     /* not latched (repeat): player 1 */
}

static void opt_lr(uint32_t screen, int dir, int pad)
{
    uint32_t line;
    if (pad < 0 || !opt_menu_on() || GUEST32(screen) != 0x0022A2B0u)
        return;
    line = GUEST32(screen + 0x1B8u);
    if (s_opt_shown == OPT_BINDKEYS) {
        if (bind_lr(line, dir))                    /* PLAYER / DEVICE line: rebuilt via A */
            PAD_LATCH(0x00305369u, pad) = 1;
        return;
    }
    if (s_opt_shown >= OPT_SHADERS && s_opt_shown <= OPT_SHRESET) {
        if (sh_lr(line, dir)) {                    /* rebuilt via A, same line */
            s_val_refresh = 1;
            PAD_LATCH(0x00305369u, pad) = 1;
        }
        return;
    }
    if (s_opt_shown == OPT_CHEATS) {               /* the next / previous page, wraps; the cursor stays */
        s_cheat_page = (s_cheat_page + dir + CHEAT_PAGES) % CHEAT_PAGES;
        s_cheat_refused = -1;
        cheat_build();
        if (line > (uint32_t)cheat_on_page() + 1)
            GUEST32(0x00306250u) = (uint32_t)cheat_on_page() + 1;   /* past a short page's end: NEXT PAGE */
        s_val_refresh = 1;
        PAD_LATCH(0x00305369u, pad) = 1;
        return;
    }
    if (s_opt_shown == OPT_ALBUM && line == OPT_ALBUM_VALUE_LINE) {
        int n = thps_st_choices(), c = (thps_st_choice() + dir + n) % n;   /* wraps */
        thps_st_set_choice(c);
        thps_st_choice_text(c, s_album_text, sizeof s_album_text);
        PAD_LATCH(0x00305369u, pad) = 1;
        return;
    }
    if (opt_val_at(s_opt_shown, line) >= 0 && opt_val_step(opt_val_at(s_opt_shown, line), dir)) {
        opt_build(s_opt_shown);
        s_val_refresh = 1;
        PAD_LATCH(0x00305369u, pad) = 1;
    }
}

/* Every swap (sub_00018F90), on the guest thread like every settings_set:
 * (1) a render step whose surfaces failed to allocate -- the toolkit already
 * went back to the step before and marked the factor unfit -- is undone in
 * saves\settings.ini too, and RECOMP OPTIONS shows the old step again; the
 * same for an MSAA count that failed (nv2a_vk_antialiasing_fits): back to the
 * antialiasing picked before;
 * (2) the windowed rectangle (nv2a_vk_window_rect) is saved once it has been
 * still for 60 swaps, as "x,y,w,h" in window_rect. */
void thps_options_tick(void)
{
    static unsigned seen, saved;
    static int still;
    int x, y, w, h;
    unsigned g;
    if (s_render_undo_frames > 0) {
        int ws = s_render_undo_ws, s = opt_render_saved_step_for(ws, NULL);
        s_render_undo_frames--;
        if (s_render_undo && !nv2a_vk_render_scale_fits(opt_render_factor(s, ws), ws)) {
            fprintf(stderr, "  options: render resolution %.3fx failed to allocate -- back to %.3fx\n",
                    opt_render_factor(s, ws), opt_render_factor(s_render_undo - 1, ws));
            opt_render_save(s_render_undo - 1, ws);
            nv2a_vk_set_render_scale(opt_render_factor(s_render_undo - 1, ws), ws);
            s_render_undo = 0;
            s_render_undo_frames = 0;
            if (s_opt_shown == OPT_RECOMP) {       /* the screen again with the old step */
                opt_build(OPT_RECOMP);
                s_val_refresh = 1;
                PAD_LATCH(0x00305369u, 0) = 1;
            }
        }
    }
    if (!nv2a_vk_antialiasing_fits(opt_aa_index())) {
        /* an MSAA count that failed to allocate (or the device lacks): the
         * renderer went back already; the setting goes back too */
        int back = s_aa_undo && nv2a_vk_antialiasing_fits(s_aa_undo - 1) ? s_aa_undo - 1 : NV2A_VK_AA_FXAA;
        fprintf(stderr, "  options: antialiasing %s failed -- back to %s\n", s_aa_val[opt_aa_index()],
                s_aa_val[back]);
        settings_set("antialiasing", s_aa_val[back]);
        nv2a_vk_set_antialiasing(back);
        s_aa_undo = 0;
        if (s_opt_shown == OPT_RECOMP) {
            opt_build(OPT_RECOMP);
            s_val_refresh = 1;
            PAD_LATCH(0x00305369u, 0) = 1;
        }
    }
    sh_retry_tick();
    sh_tick();
    g = nv2a_vk_window_rect(&x, &y, &w, &h);
    if (g != seen) {
        seen = g;
        still = 0;
    } else if (g && g != saved && ++still >= 60 && w > 0 && h > 0) {
        char v[48];
        snprintf(v, sizeof v, "%d,%d,%d,%d", x, y, w, h);
        settings_set("window_rect", v);
        saved = g;
    }
}

extern void sub_001E6600_gen(void);
void sub_001E6600(void)
{
    uint32_t screen = g_ecx;
    int pad = opt_latch_pad(0x003053B9u);
    sub_001E6600_gen();
    opt_lr(screen, -1, pad);
}

extern void sub_001E65E0_gen(void);
void sub_001E65E0(void)
{
    uint32_t screen = g_ecx;
    int pad = opt_latch_pad(0x003053C9u);
    sub_001E65E0_gen();
    opt_lr(screen, +1, pad);
}

/* Y (maintainer, 2026-10-28: only the selected line): in GENERAL OPTIONS
 * the line's value back to its default (the game's four: ON), in BUTTON
 * CHEATS the line's cheat off - entered once more if it is on (the game's
 * checker switches it off; one-time actions cannot be undone). The base
 * front-end handler (vtable +0x1C, 0x001E66E0) only clears the Y latches; the
 * screen is rebuilt through the A latch like a left / right step (opt_lr). */
static int opt_val_default(int id)                /* 1 when it changed */
{
    if (id >= VAL_FIRST_GAME && id <= VAL_LAST_GAME)
        return !game_opt_on(id) && game_opt_set(id, 1);
    if (!s_val[id].key || !strcmp(settings_get(s_val[id].key, s_val[id].def), s_val[id].def))
        return 0;
    settings_set(s_val[id].key, s_val[id].def);
    return 1;
}

extern void sub_001E66E0_gen(void);
void sub_001E66E0(void)
{
    uint32_t screen = g_ecx, line;
    int pad = opt_latch_pad(0x00305339u), changed = 0;
    sub_001E66E0_gen();
    if (!opt_menu_on() || GUEST32(screen) != 0x0022A2B0u)
        return;
    line = GUEST32(screen + 0x1B8u);
    if (s_opt_shown == OPT_GENERAL) {
        int id = opt_val_at(OPT_GENERAL, line);
        if (id < 0)
            return;
        changed = opt_val_default(id);
        opt_build(OPT_GENERAL);
    } else if (s_opt_shown == OPT_CHEATS) {
        int i = s_cheat_page * CHEAT_PAGE + (int)line - 1;
        if (line < 1 || line > (uint32_t)cheat_on_page())
            return;
        changed = cheat_on(i) && cheat_enter(i) == 1;
        s_cheat_refused = -1;
        cheat_build();
    } else
        return;
    fprintf(stderr, "  options: Y in %s line %u - %s\n", s_opt[s_opt_shown].title, line,
            changed ? "back to the default" : "already the default");
    s_val_refresh = 1;
    PAD_LATCH(0x00305369u, pad) = 1;
}

/* The Options screen's runner: after B in a sub-list, Options again. */
extern void sub_0013D050_gen(void);
void sub_0013D050(void)
{
    sub_0013D050_gen();
    if (s_opt_back) {
        s_opt_back = 0;
        g_eax = OPT_STAY;
    }
}

/* Lines greyed out: the original (sub_0013D400) greys line j (1-4) for a
 * missing pad j-1, moves the cursor off greyed lines and hands the colours to
 * the panel (0x001EA250) -- a fix-up after it would never be drawn. It runs
 * on the pad presence words at 0x003054A4 + k * 0x18C, so it gets the ones
 * each list needs for the time of the call: CONTROLS pad j for line j
 * (PLAYER j+1 CONTROLS; PLAYER 1 is line 0, never greyed), and every list
 * the blank lines that pad it to 5.
 * It always touches lines 1-4, so a list never navigates fewer than 5 lines
 * (opt_nav_lines): on a 2- or 3-line list it read menu entries that do not
 * exist (null accesses by the thousand); skipping it froze the screen, and
 * calling its panel update 0x001EA250 from C, or a C rewrite of it, left the
 * screen corrupt (crash in its destructor, 0x0013D1F2; cause not found).
 * The list is the one this screen was built with (s_opt_shown): A / B switch
 * s_opt_list while the old screen still runs a few frames. */
extern void sub_0013D400_gen(void);
void sub_0013D400(void)
{
    uint32_t saved[4], k, real = opt_lines(s_opt_shown);
    if (!opt_menu_on()) {
        sub_0013D400_gen();
        return;
    }
    for (k = 0; k < 4; k++)
        saved[k] = GUEST32(0x003054A4u + k * 0x18Cu);
    for (k = 0; k < 4; k++) {                          /* pad k decides line k + 1 */
        uint32_t line = k + 1, v = 1u;
        if (line >= real)
            v = 0;                                      /* a blank pad line */
        else if (s_opt_shown == OPT_CONTROLS)
            v = line < 4 ? saved[line] : 1u;            /* PLAYER line+1 <- pad line; BUTTON MAPPING always */
        else if (s_opt_shown == OPT_BOOT)
            v = s_boot_text[0] == 'B';                  /* ON / OFF: only with media\boot.mpg (boot_state) */
        else if (s_opt_shown == OPT_SHADERS && !sh_menu_usable())
            v = 0;                                      /* no librashader / no pack: the hint, the rest greyed */
        else if (s_opt_shown == OPT_GENERAL && line < OPT_GENERAL_FIRST)
            v = 0;                                      /* GENERAL OPTIONS: the blank under the Y hint */
        GUEST32(0x003054A4u + k * 0x18Cu) = v;
    }
    sub_0013D400_gen();
    for (k = 0; k < 4; k++)
        GUEST32(0x003054A4u + k * 0x18Cu) = saved[k];
}

/* ── SHADERS (shader module phase 4; docs/shader_module.md) ──────────────────
 * RECOMP OPTIONS -> SHADERS (the line before BOOT ANIMATION, the last one;
 * maintainer, 2026-10-27): SHADER ON / OFF (left / right or A), PRESET (the browser),
 * PARAMETERS (from the preset in place: nv2a_vk_shader_params), SAVE AS PRESET
 * (a RetroArch simple preset in saves\shaders\My Presets: #reference + the
 * changed values, toolkit shader_pack_save_preset), RESET ALL PARAMETERS (with a
 * confirmation). Without librashader (a build without Rust) or without a pack
 * the first line is the hint and the other four are greyed.
 * Browser: the packs (data\shaders\<zip>, folders of media\shaders, MY PRESETS),
 * folders first, 10 entries a page, NEXT PAGE (left / right too), the pass
 * count as the cost hint; A on a folder opens it, on ".." goes up, on a preset
 * picks it (saved, applied at once, back to SHADERS). Parameters: 10 a page,
 * "NAME: VALUE", left / right = one step (no step given: 1/100 of the range;
 * at most 10,000 steps), A = back to the preset's value; min == max = a caption.
 * No shoulder-button big steps (maintainer, 2026-10-27: not needed). Kept in saves\settings.ini: shader_on (on / off),
 * shader_preset (relative to the layout folder when inside it), shader_params
 * ("name=value;..." of the changed ones; dropped when another preset is picked).
 * The panel font: capitals, digits, dot, colon, space (bind_upper). */
#define SH_PAGE 10
#define SH_MAX_PARAMS 1024
static int s_sh_dirpage, s_sh_parpage;
static char s_sh_dir[512];                        /* the browser's folder key: "" = the packs */
static ShaderIndex s_sh_user;                     /* MY PRESETS, listed when the browser opens */
static const ShaderPreset *s_sh_line_preset[12];  /* browser line -> preset (NULL: folder / other) */
static char s_sh_line_folder[12][128];            /* browser line -> folder key */
static Nv2aVkShaderParam *s_sh_par;               /* the preset's parameters (nv2a_vk_shader_params) */
static int s_sh_npar;
static unsigned s_sh_par_gen = ~0u;
static int s_sh_line_par[12];                     /* parameter line -> index, -1 */
static char s_sh_saved[40];                       /* SAVE AS PRESET's result text */
static int s_sh_shown_status = -1, s_sh_shown_pack = -1;
char g_thps_save_dir[512];                        /* set by main.c: <saves> (My Presets) */

/* overrides of the preset in place: name / value, mirrored in shader_params */
typedef struct { char name[64]; float v; } ShOv;
static ShOv *s_sh_ov;
static int s_sh_nov, s_sh_capov;

static const char *sh_layout(char *out, size_t n)  /* "<layout>\" from <layout>\media */
{
    size_t k = strlen(g_thps_media_dir);
    snprintf(out, n, "%.*s", (int)(k >= 5 ? k - 5 : 0), g_thps_media_dir);
    return out;
}

static int sh_ov_grow(void)
{
    if (s_sh_nov == s_sh_capov) {
        int cap = s_sh_capov ? s_sh_capov * 2 : 32;
        ShOv *p = (ShOv *)realloc(s_sh_ov, (size_t)cap * sizeof *p);
        if (!p)
            return 0;
        s_sh_ov = p;
        s_sh_capov = cap;
    }
    return 1;
}

static void sh_ov_load(void)
{
    const char *s = settings_get("shader_params", "");
    s_sh_nov = 0;
    while (*s) {
        const char *eq = strchr(s, '='), *end = strchr(s, ';');
        if (!end)
            end = s + strlen(s);
        if (eq && eq < end && eq - s < 64) {
            if (!sh_ov_grow())
                return;
            snprintf(s_sh_ov[s_sh_nov].name, sizeof s_sh_ov[0].name, "%.*s", (int)(eq - s), s);
            s_sh_ov[s_sh_nov].v = (float)atof(eq + 1);
            s_sh_nov++;
        }
        s = *end ? end + 1 : end;
    }
}

static void sh_ov_save(void)
{
    size_t cap = (size_t)s_sh_nov * 80 + 1, k = 0;
    char *t = (char *)malloc(cap);
    int i;
    if (!t)
        return;
    t[0] = 0;
    for (i = 0; i < s_sh_nov; i++)
        k += (size_t)snprintf(t + k, cap - k, "%s%s=%g", i ? ";" : "", s_sh_ov[i].name, (double)s_sh_ov[i].v);
    settings_set("shader_params", t);
    free(t);
}

static int sh_ov_find(const char *name)
{
    int i;
    for (i = 0; i < s_sh_nov; i++)
        if (!strcmp(s_sh_ov[i].name, name))
            return i;
    return -1;
}

static void sh_ov_set(const char *name, float v, float initial)
{
    int i = sh_ov_find(name);
    if (fabsf(v - initial) < 1e-6f) {                 /* back to the preset's value: no override */
        if (i >= 0)
            s_sh_ov[i] = s_sh_ov[--s_sh_nov];
    } else {
        if (i < 0) {
            if (!sh_ov_grow())
                return;
            i = s_sh_nov++;
            snprintf(s_sh_ov[i].name, sizeof s_sh_ov[i].name, "%s", name);
        }
        s_sh_ov[i].v = v;
    }
    nv2a_vk_set_shader_param(name, v);
    sh_ov_save();
    s_sh_saved[0] = 0;
}

/* the preset path in use (absolute), "" none */
static void sh_preset_path(char *out, size_t n)
{
    const char *p = settings_get("shader_preset", "");
    char lay[512];
    if (*p && !(p[0] == '/' || p[0] == '\\' || (p[0] && p[1] == ':')))
        snprintf(out, n, "%s%s", sh_layout(lay, sizeof lay), p);
    else
        snprintf(out, n, "%s", p);
}

static int sh_on(void)
{
    return strcmp(settings_get("shader_on", "on"), "off") != 0;
}

/* the toolkit gets the preset (or none) and the overrides; any change drops
 * SAVE AS PRESET's result text */
static void sh_apply(void)
{
    char path[1024];
    int i;
    s_sh_saved[0] = 0;
    sh_preset_path(path, sizeof path);
    nv2a_vk_set_shader_preset(sh_on() && path[0] ? path : NULL);
    if (sh_on() && path[0])
        for (i = 0; i < s_sh_nov; i++)
            nv2a_vk_set_shader_param(s_sh_ov[i].name, s_sh_ov[i].v);
}

/* "CRT EASYMODE" from ".../crt-easymode.slangp" */
static void sh_name(const char *file, char *out, size_t n)
{
    const char *b = strrchr(file, '/'), *bs = strrchr(file, '\\');
    char tmp[128];
    size_t l;
    if (bs > b)
        b = bs;
    b = b ? b + 1 : file;
    snprintf(tmp, sizeof tmp, "%s", b);
    l = strlen(tmp);
    if (l > 7 && !SDL_strcasecmp(tmp + l - 7, ".slangp"))
        tmp[l - 7] = 0;
    bind_upper(out, n, tmp);
}

/* the SHADERS lines usable (sub_0013D400 greys them otherwise): librashader
 * next to the exe and a pack (being prepared, or with presets) or own presets */
static int sh_menu_usable(void)
{
    const ShaderIndex *x = shader_pack_index();
    return nv2a_vk_shader_library_present() && (shader_pack_state() == SHADER_PACK_WORKING || (x && x->n > 0)
                                                || s_sh_user.n > 0);
}

static void sh_user_scan(void)
{
    char d[700];
    shader_pack_free(&s_sh_user);
    snprintf(d, sizeof d, "%s\\shaders\\My Presets", g_thps_save_dir);
    shader_pack_list_dir(d, "MY PRESETS", &s_sh_user);
}

/* the SHADERS line in RECOMP OPTIONS: "SHADERS: OFF" / "SHADERS: CRT EASYMODE" /
 * "SHADERS: BROKEN PASS  FAILED" (the preset did not load; the picture is unshaded) */
static void sh_summary(char *out, size_t n)
{
    char path[1024], nm[40];
    sh_preset_path(path, sizeof path);
    if (!sh_on() || !path[0] || !nv2a_vk_shader_library_present())
        snprintf(out, n, "SHADERS: OFF");
    else {
        sh_name(path, nm, 28);
        snprintf(out, n, "SHADERS: %s%s", nm, nv2a_vk_shader_status() == NV2A_VK_SHADER_FAILED ? "  FAILED" : "");
    }
}

static void sh_build_main(void)
{
    char path[1024], nm[40];
    int st = nv2a_vk_shader_status(), pk = shader_pack_state();
    memset(s_sh_text[0], 0, sizeof s_sh_text[0]);
    s_sh_shown_status = st;
    s_sh_shown_pack = pk;
    snprintf(s_sh_title[0], sizeof s_sh_title[0], "SHADERS%s",
             st == NV2A_VK_SHADER_LOADING ? "  LOADING" : st == NV2A_VK_SHADER_FAILED ? "  PRESET FAILED" : "");
    if (!sh_menu_usable()) {
        snprintf(s_sh_text[0][0], sizeof s_sh_text[0][0], "%s", !nv2a_vk_shader_library_present()
                 ? "NEEDS LIBRASHADER IN THE GAME FOLDER" : "NEEDS A SHADER PACK IN MEDIA SHADERS");
        snprintf(s_sh_text[0][1], sizeof s_sh_text[0][1], "SHADER: OFF");
        snprintf(s_sh_text[0][2], sizeof s_sh_text[0][2], "PRESET: NONE");
        snprintf(s_sh_text[0][3], sizeof s_sh_text[0][3], "PARAMETERS");
        snprintf(s_sh_text[0][4], sizeof s_sh_text[0][4], "SAVE AS PRESET");
        return;
    }
    sh_preset_path(path, sizeof path);
    snprintf(s_sh_text[0][0], sizeof s_sh_text[0][0], "SHADER: %s", sh_on() ? "ON" : "OFF");
    if (pk == SHADER_PACK_WORKING)
        snprintf(s_sh_text[0][1], sizeof s_sh_text[0][1], "PRESET: PREPARING PACKS");
    else if (!path[0])
        snprintf(s_sh_text[0][1], sizeof s_sh_text[0][1], "PRESET: NONE");
    else {
        sh_name(path, nm, 30);
        snprintf(s_sh_text[0][1], sizeof s_sh_text[0][1], "PRESET: %s", nm);
    }
    s_sh_npar = nv2a_vk_shader_params(NULL, 0, NULL);
    if (s_sh_npar)
        snprintf(s_sh_text[0][2], sizeof s_sh_text[0][2], "PARAMETERS: %d  %d CHANGED", s_sh_npar, s_sh_nov);
    else
        snprintf(s_sh_text[0][2], sizeof s_sh_text[0][2], "PARAMETERS: NONE");
    snprintf(s_sh_text[0][3], sizeof s_sh_text[0][3], "%s", s_sh_saved[0] ? s_sh_saved : "SAVE AS PRESET");
    snprintf(s_sh_text[0][4], sizeof s_sh_text[0][4], "RESET ALL PARAMETERS");
}

/* every preset known: the index, then MY PRESETS */
static const ShaderPreset *sh_preset_at(int i)
{
    const ShaderIndex *x = shader_pack_index();
    int nx = x ? x->n : 0;
    return i < nx ? &x->p[i] : i - nx < s_sh_user.n ? &s_sh_user.p[i - nx] : NULL;
}

static int sh_presets(void)
{
    const ShaderIndex *x = shader_pack_index();
    return (x ? x->n : 0) + s_sh_user.n;
}

/* entries of folder s_sh_dir: its sub-folders (unique), then its presets */
static int sh_entries(char (*folders)[128], int maxf, int *nf, const ShaderPreset **pres, int maxp, int *np)
{
    size_t dl = strlen(s_sh_dir);
    int i, k;
    *nf = *np = 0;
    for (i = 0; i < sh_presets(); i++) {
        const ShaderPreset *p = sh_preset_at(i);
        const char *rest, *sl;
        if (dl && (SDL_strncasecmp(p->rel, s_sh_dir, dl) || p->rel[dl] != '/'))
            continue;
        rest = p->rel + (dl ? dl + 1 : 0);
        sl = strchr(rest, '/');
        if (sl) {
            char f[128];
            snprintf(f, sizeof f, "%.*s%s%.*s", (int)dl, s_sh_dir, dl ? "/" : "", (int)(sl - rest), rest);
            for (k = 0; k < *nf && SDL_strcasecmp(folders[k], f); k++)
                ;
            if (k == *nf && *nf < maxf)
                snprintf(folders[(*nf)++], 128, "%s", f);
        } else if (*np < maxp) {
            pres[(*np)++] = p;
        }
    }
    return *nf + *np;
}

static void sh_build_browse(void)
{
    static char folders[512][128];
    static const ShaderPreset *pres[4096];
    int nf, np, total, pages, line = 0, e;
    const char *last = strrchr(s_sh_dir, '/');
    char t[40];
    memset(s_sh_text[1], 0, sizeof s_sh_text[1]);
    memset(s_sh_line_preset, 0, sizeof s_sh_line_preset);
    memset(s_sh_line_folder, 0, sizeof s_sh_line_folder);
    total = sh_entries(folders, 512, &nf, pres, 4096, &np);
    pages = total ? (total + SH_PAGE - 1) / SH_PAGE : 1;
    if (s_sh_dirpage >= pages)
        s_sh_dirpage = 0;
    if (s_sh_dirpage < 0)
        s_sh_dirpage = pages - 1;
    bind_upper(t, 22, last ? last + 1 : s_sh_dir[0] ? s_sh_dir : "PRESETS");
    if (pages > 1)
        snprintf(s_sh_title[1], sizeof s_sh_title[1], "%s  %d OF %d", t, s_sh_dirpage + 1, pages);
    else
        snprintf(s_sh_title[1], sizeof s_sh_title[1], "%s", t);
    snprintf(s_sh_text[1][line++], sizeof s_sh_text[1][0], "%s", s_sh_dir[0] ? ".. UP" : "NONE");
    for (e = s_sh_dirpage * SH_PAGE; e < total && e < (s_sh_dirpage + 1) * SH_PAGE; e++, line++) {
        char nm[40];
        if (e < nf) {
            const char *f = strrchr(folders[e], '/');
            bind_upper(nm, 30, f ? f + 1 : folders[e]);
            snprintf(s_sh_text[1][line], sizeof s_sh_text[1][line], "%s ...", nm);
            snprintf(s_sh_line_folder[line], sizeof s_sh_line_folder[line], "%s", folders[e]);
        } else {
            const ShaderPreset *p = pres[e - nf];
            sh_name(p->rel, nm, 28);
            if (p->passes < 0)
                snprintf(s_sh_text[1][line], sizeof s_sh_text[1][line], "%s  BROKEN", nm);
            else
                snprintf(s_sh_text[1][line], sizeof s_sh_text[1][line], "%s  %d %s", nm, p->passes,
                         p->passes == 1 ? "PASS" : "PASSES");
            s_sh_line_preset[line] = p;
        }
    }
    if (pages > 1)
        snprintf(s_sh_text[1][line], sizeof s_sh_text[1][line], "NEXT PAGE");
}

static float sh_step(const Nv2aVkShaderParam *p)
{
    float range = p->maximum - p->minimum, st = p->step;
    if (!(st > 0.0f))
        st = range / 100.0f;
    if (range / st > 10000.0f)
        st = range / 10000.0f;
    return st;
}

static float sh_value(const Nv2aVkShaderParam *p)
{
    int i = sh_ov_find(p->name);
    return i >= 0 ? s_sh_ov[i].v : p->initial;
}

static void sh_params_fetch(void)
{
    unsigned gen;
    int n = nv2a_vk_shader_params(NULL, 0, &gen);
    if (gen == s_sh_par_gen && s_sh_par)
        return;
    free(s_sh_par);
    s_sh_par = n ? (Nv2aVkShaderParam *)calloc((size_t)(n < SH_MAX_PARAMS ? n : SH_MAX_PARAMS), sizeof *s_sh_par)
                 : NULL;
    s_sh_npar = s_sh_par ? nv2a_vk_shader_params(s_sh_par, SH_MAX_PARAMS, &gen) : 0;
    if (s_sh_npar > SH_MAX_PARAMS)
        s_sh_npar = SH_MAX_PARAMS;
    s_sh_par_gen = gen;
}

static void sh_build_params(void)
{
    int pages, line = 0, i;
    memset(s_sh_text[2], 0, sizeof s_sh_text[2]);
    for (i = 0; i < 12; i++)
        s_sh_line_par[i] = -1;
    sh_params_fetch();
    pages = s_sh_npar ? (s_sh_npar + SH_PAGE - 1) / SH_PAGE : 1;
    if (s_sh_parpage >= pages)
        s_sh_parpage = 0;
    if (s_sh_parpage < 0)
        s_sh_parpage = pages - 1;
    snprintf(s_sh_title[2], sizeof s_sh_title[2], "PARAMETERS %d OF %d  %d CHANGED", s_sh_parpage + 1, pages,
             s_sh_nov);
    if (!s_sh_npar) {
        snprintf(s_sh_text[2][0], sizeof s_sh_text[2][0], "NO PARAMETERS");
        return;
    }
    for (i = s_sh_parpage * SH_PAGE; i < s_sh_npar && i < (s_sh_parpage + 1) * SH_PAGE; i++, line++) {
        const Nv2aVkShaderParam *p = &s_sh_par[i];
        char lab[40];
        float v = sh_value(p), st = sh_step(p);
        bind_upper(lab, 26, p->desc[0] ? p->desc : p->name);
        if (!lab[0])
            bind_upper(lab, 26, p->name);
        if (p->maximum <= p->minimum)
            snprintf(s_sh_text[2][line], sizeof s_sh_text[2][line], "%s", lab);       /* a caption */
        else
            snprintf(s_sh_text[2][line], sizeof s_sh_text[2][line], "%s: %.*f", lab,
                     st >= 1.0f ? 0 : st >= 0.1f ? 1 : st >= 0.01f ? 2 : 3, (double)v);
        s_sh_line_par[line] = i;
    }
    if (pages > 1)
        snprintf(s_sh_text[2][line], sizeof s_sh_text[2][line], "NEXT PAGE");
}

static void sh_build_reset(void)
{
    memset(s_sh_text[3], 0, sizeof s_sh_text[3]);
    snprintf(s_sh_title[3], sizeof s_sh_title[3], "RESET ALL PARAMETERS");
    /* the panel font has no parentheses: CHANGE / CHANGES instead of CHANGE(S) */
    snprintf(s_sh_text[3][0], sizeof s_sh_text[3][0], "RESET %d %s TO PRESET VALUES", s_sh_nov,
             s_sh_nov == 1 ? "CHANGE" : "CHANGES");
    snprintf(s_sh_text[3][1], sizeof s_sh_text[3][1], "RESET");
    snprintf(s_sh_text[3][2], sizeof s_sh_text[3][2], "CANCEL");
}

static void sh_build(int list)
{
    if (list == OPT_SHADERS)
        sh_build_main();
    else if (list == OPT_SHBROWSE)
        sh_build_browse();
    else if (list == OPT_SHPARAMS)
        sh_build_params();
    else if (list == OPT_SHRESET)
        sh_build_reset();
}

/* a preset picked in the browser (NULL = none) */
static void sh_pick(const ShaderPreset *p)
{
    char lay[512];
    size_t ll = strlen(sh_layout(lay, sizeof lay));
    if (p && ll && !SDL_strncasecmp(p->path, lay, ll))
        settings_set("shader_preset", p->path + ll);   /* inside the layout: relative (it may move) */
    else
        settings_set("shader_preset", p ? p->path : "");
    s_sh_nov = 0;                                      /* another preset: its own values */
    settings_set("shader_params", "");
    settings_set("shader_on", "on");
    sh_apply();
    fprintf(stderr, "  options: shader preset %s\n", p ? p->rel : "none");
}

static void sh_save(void)
{
    char ref[1024], dir[700], out[1100], nm[40];
    const char **names;
    float *vals;
    int i;
    sh_preset_path(ref, sizeof ref);
    if (!ref[0])
        return;
    names = (const char **)calloc((size_t)(s_sh_nov ? s_sh_nov : 1), sizeof *names);
    vals = (float *)calloc((size_t)(s_sh_nov ? s_sh_nov : 1), sizeof *vals);
    if (!names || !vals) {
        free(names);
        free(vals);
        return;
    }
    for (i = 0; i < s_sh_nov; i++) {
        names[i] = s_sh_ov[i].name;
        vals[i] = s_sh_ov[i].v;
    }
    snprintf(dir, sizeof dir, "%s\\shaders\\My Presets", g_thps_save_dir);
    if (shader_pack_save_preset(dir, "MY PRESET", ref, names, vals, s_sh_nov, out, sizeof out)) {
        ShaderPreset mine;
        sh_name(out, nm, 24);
        fprintf(stderr, "  options: shader preset saved as %s\n", out);
        sh_user_scan();
        mine.path = out;
        mine.rel = out;
        mine.passes = 0;
        sh_pick(&mine);                                /* the saved one is in use now, its values in the file */
        snprintf(s_sh_saved, sizeof s_sh_saved, "SAVED AS %s", nm);
    } else {
        snprintf(s_sh_saved, sizeof s_sh_saved, "SAVE FAILED");
    }
    free(names);
    free(vals);
}

/* A in one of the SHADERS lists; returns the cursor line */
static uint32_t sh_action(uint32_t line)
{
    if (s_opt_list == OPT_SHADERS) {
        if (!sh_menu_usable())
            return line;
        if (line == 0) {
            settings_set("shader_on", sh_on() ? "off" : "on");
            sh_apply();
        } else if (line == 1 && shader_pack_state() != SHADER_PACK_WORKING) {
            char path[1024];
            int i;
            sh_user_scan();
            s_sh_dir[0] = 0;                           /* open on the folder of the preset in use */
            sh_preset_path(path, sizeof path);
            for (i = 0; path[0] && i < sh_presets(); i++)
                if (!SDL_strcasecmp(sh_preset_at(i)->path, path)) {
                    const char *sl = strrchr(sh_preset_at(i)->rel, '/');
                    if (sl)
                        snprintf(s_sh_dir, sizeof s_sh_dir, "%.*s", (int)(sl - sh_preset_at(i)->rel),
                                 sh_preset_at(i)->rel);
                    break;
                }
            s_sh_dirpage = 0;
            s_opt_list = OPT_SHBROWSE;
            sh_build(OPT_SHBROWSE);
            return 0;
        } else if (line == 2 && nv2a_vk_shader_params(NULL, 0, NULL) > 0) {
            s_sh_parpage = 0;
            s_opt_list = OPT_SHPARAMS;
            sh_build(OPT_SHPARAMS);
            return 0;
        } else if (line == 3) {
            sh_save();
        } else if (line == 4 && s_sh_nov) {
            s_opt_list = OPT_SHRESET;
            sh_build(OPT_SHRESET);
            return 2;                                  /* on CANCEL */
        }
        sh_build(OPT_SHADERS);
        return line;
    }
    if (s_opt_list == OPT_SHBROWSE) {
        if (line == 0) {
            char *sl;
            if (!s_sh_dir[0]) {                        /* NONE: no preset */
                sh_pick(NULL);
                s_opt_list = OPT_SHADERS;
                sh_build(OPT_SHADERS);
                return 1;
            }
            sl = strrchr(s_sh_dir, '/');               /* ..: one folder up */
            if (sl)
                *sl = 0;
            else
                s_sh_dir[0] = 0;
            s_sh_dirpage = 0;
        } else if (line < 12 && s_sh_line_folder[line][0]) {
            snprintf(s_sh_dir, sizeof s_sh_dir, "%s", s_sh_line_folder[line]);
            s_sh_dirpage = 0;
        } else if (line < 12 && s_sh_line_preset[line]) {
            sh_pick(s_sh_line_preset[line]);
            s_opt_list = OPT_SHADERS;
            sh_build(OPT_SHADERS);
            return 1;
        } else if (line < 12 && !strcmp(s_sh_text[1][line], "NEXT PAGE")) {
            s_sh_dirpage++;
            sh_build(OPT_SHBROWSE);
            return line;
        }
        sh_build(OPT_SHBROWSE);
        return 0;
    }
    if (s_opt_list == OPT_SHPARAMS) {
        if (line < 12 && s_sh_line_par[line] >= 0) {   /* A: back to the preset's value */
            const Nv2aVkShaderParam *p = &s_sh_par[s_sh_line_par[line]];
            if (p->maximum > p->minimum)
                sh_ov_set(p->name, p->initial, p->initial);
        } else if (line < 12 && !strcmp(s_sh_text[2][line], "NEXT PAGE")) {
            s_sh_parpage++;
        }
        sh_build(OPT_SHPARAMS);
        return line;
    }
    if (s_opt_list == OPT_SHRESET) {
        if (line == 1) {
            int i;
            sh_params_fetch();
            for (i = 0; i < s_sh_npar; i++)
                if (sh_ov_find(s_sh_par[i].name) >= 0)
                    nv2a_vk_set_shader_param(s_sh_par[i].name, s_sh_par[i].initial);
            s_sh_nov = 0;
            sh_ov_save();
            s_sh_saved[0] = 0;
            fprintf(stderr, "  options: shader parameters reset\n");
        }
        s_opt_list = OPT_SHADERS;
        sh_build(OPT_SHADERS);
        return 4;
    }
    return line;
}

/* left / right in the SHADERS lists; 1 when the screen changed */
static int sh_lr(uint32_t line, int dir)
{
    if (s_opt_shown == OPT_SHADERS && line == 0 && sh_menu_usable()) {
        settings_set("shader_on", sh_on() ? "off" : "on");
        sh_apply();
        sh_build(OPT_SHADERS);
        return 1;
    }
    if (s_opt_shown == OPT_SHBROWSE) {
        s_sh_dirpage += dir;
        sh_build(OPT_SHBROWSE);
        return 1;
    }
    if (s_opt_shown == OPT_SHPARAMS && line < 12 && s_sh_line_par[line] >= 0) {
        const Nv2aVkShaderParam *p = &s_sh_par[s_sh_line_par[line]];
        float v, st;
        if (p->maximum <= p->minimum)
            return 0;
        st = sh_step(p);
        v = sh_value(p) + (float)dir * st;
        if (v < p->minimum)
            v = p->minimum;
        if (v > p->maximum)
            v = p->maximum;
        v = p->minimum + st * floorf((v - p->minimum) / st + 0.5f);   /* on the step grid */
        if (v > p->maximum)
            v = p->maximum;
        sh_ov_set(p->name, v, p->initial);
        sh_build(OPT_SHPARAMS);
        return 1;
    }
    if (s_opt_shown == OPT_SHPARAMS && line < 12 && !strcmp(s_sh_text[2][line], "NEXT PAGE")) {
        s_sh_parpage += dir;
        sh_build(OPT_SHPARAMS);
        return 1;
    }
    return 0;
}

/* thps_options_tick: the SHADERS screen follows LOADING -> ON and the packs getting ready */
static void sh_tick(void)
{
    if (s_opt_shown != OPT_SHADERS || s_opt_list != OPT_SHADERS)
        return;
    if (nv2a_vk_shader_status() != s_sh_shown_status || shader_pack_state() != s_sh_shown_pack) {
        sh_build(OPT_SHADERS);
        s_val_refresh = 1;
        PAD_LATCH(0x00305369u, 0) = 1;
    }
}

/* ── PLAYER n CONTROLS: a BUTTON MAPPING line (M6 groundwork) ─────────────────
 * Why: maintainer, 2026-10-18 -- button mapping is per controller, so it goes
 * into the game's own PLAYER n CONTROLS screen as its fourth line
 * (frontend\OptionControlScreen.PANEL: AUTOKICK, VIBRATION, INITIAL SETTINGS
 * and an empty fourth text element "text line 03"). An element gets room for
 * the text the file gives it when the panel is read, so writing text into it
 * later (+0x88) draws nothing; the text has to be in the file data. The
 * loaded file (sub_0017A060: buffer from 0x000988A0, freed by the panel
 * loader with 0x00098660) has no room to grow, so it is copied into a new
 * buffer from the same allocator with "BUTTON MAPPING" as line 03's text.
 * Shown, not selectable yet: giving the vertical menu a fourth line (its
 * count, push 3 at 0x0012F136) wrote through null pointers (0x0012F143 and,
 * when navigating, 0x0012F3F3) -- the screen keeps per-line state for three
 * lines only. */
extern void sub_000988A0(void);
extern void sub_00098660(void);

static uint32_t guest_alloc_like_loader(uint32_t size)
{
    uint32_t saved_ecx = g_ecx, saved_edx = g_edx, p;
    g_esp -= 4; GUEST32(g_esp) = 1;               /* push 1, push 0, push 1 (0x0017A0B7) */
    g_esp -= 4; GUEST32(g_esp) = 0;
    g_esp -= 4; GUEST32(g_esp) = 1;
    g_esp -= 4; GUEST32(g_esp) = 0;               /* return address; ret 0xC pops the args */
    g_ecx = size;
    g_edx = 1;
    sub_000988A0();
    p = g_eax;
    g_ecx = saved_ecx;
    g_edx = saved_edx;
    return p;
}

static void guest_free_like_loader(uint32_t p)
{
    uint32_t saved_ecx = g_ecx, saved_edx = g_edx;
    g_ecx = p - *(volatile uint8_t *)((uintptr_t)g_xbox_mem_offset + p - 1) + 4;   /* as 0x0012F02D */
    g_esp -= 4; GUEST32(g_esp) = 0;
    sub_00098660();
    g_ecx = saved_ecx;
    g_edx = saved_edx;
}

/* returns the buffer to use (the old one, or a new one with the text) */
uint32_t thps_ctl_panel(uint32_t buf, uint32_t size_ptr)
{
    static const char text[] = "BUTTON MAPPING";
    static const uint8_t next_el[] = { 0x96, 0x09, 0x00, '3', 'D', ' ', 'o', 'b', 'j', 'e', 'c', 't' };
    uint8_t *g = (uint8_t *)((uintptr_t)g_xbox_mem_offset);
    uint32_t size = GUEST32(size_ptr), p, nb, n = (uint32_t)sizeof text - 1;
    for (p = 2; p + sizeof next_el <= size; p++)
        if (!memcmp(g + buf + p, next_el, sizeof next_el))
            break;
    if (p + sizeof next_el > size || g[buf + p - 2] || g[buf + p - 1])
        return buf;                                /* not the layout we know: leave it */
    if (!(nb = guest_alloc_like_loader(size + n)))
        return buf;
    memcpy(g + nb, g + buf, p - 2);
    g[nb + p - 2] = (uint8_t)n;
    g[nb + p - 1] = 0;
    memcpy(g + nb + p, text, n);
    memcpy(g + nb + p + n, g + buf + p, size - p);
    GUEST32(size_ptr) = size + n;
    guest_free_like_loader(buf);
    return nb;
}

/* ── Button mapping (rebinding; maintainer, 2026-10-25) ───────────────────────
 * Options -> CONTROLS -> BUTTON MAPPING, one screen (maintainer's layout):
 * PLAYER: n (left / right or A: 1-4), DEVICE: the keyboard (it plays on
 * xbox_InputKeyboardPort, player 1 by default) or the controller on that
 * player's port (left / right or A), RESET TO DEFAULT, then that device's
 * bindings, 8 per page, NEXT PAGE. Bindings stay per device (keyboard / pad GUID,
 * saves\settings.ini; xinput_bind.h, docs/input.md) -- the player only picks
 * which devices are offered. A on a binding records a press: every source
 * released, then the first one pressed on that device (5 s without one
 * cancels); the title sees no input meanwhile (xbox_InputBindSuppress).
 * Duplicates are marked DOUBLE. Front-end Options only: the in-level pause
 * menu has no Options entry, and A refuses the list while a level runs.
 * The panel font has capitals, digits, the dot, the colon and the space. */
#include "xinput_bind.h"

#define BIND_PAGE 8
#define BIND_CAPTURE_FRAMES 300                   /* 5 s at 60 frames a second */

static int s_bind_player, s_bind_dev, s_bind_port = -3, s_bind_page;   /* port: -1 keyboard, -3 none */
static XboxBindMap s_bind_own, s_bind_def;
static uint32_t s_bind_line_ctl[12];              /* line -> control, or one of these */
#define BIND_NEXT   0x100u
#define BIND_RESET  0x101u
#define BIND_PLAYER 0x102u
#define BIND_DEVICE 0x103u
#define BIND_NONE   0x104u
static int s_cap_control = -1, s_cap_phase, s_cap_frames;
static uint32_t s_cap_line;

static const char *const s_bind_ctl_label[XBOX_KEY_CONTROL_COUNT] = {
    "DPAD UP", "DPAD DOWN", "DPAD LEFT", "DPAD RIGHT", "START", "BACK", "L3", "R3",
    "A", "B", "X", "Y", "BLACK", "WHITE", "L TRIGGER", "R TRIGGER",
    "L STICK LEFT", "L STICK RIGHT", "L STICK DOWN", "L STICK UP",
    "R STICK LEFT", "R STICK RIGHT", "R STICK DOWN", "R STICK UP",
};

/* a pad name in the panel font */
static void bind_upper(char *dst, size_t n, const char *src)
{
    size_t i = 0;
    for (; *src && i + 1 < n; src++) {
        char c = *src;
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 32);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '.' || c == ':')
            dst[i++] = c;
        else if (i && dst[i - 1] != ' ')
            dst[i++] = ' ';
    }
    while (i && dst[i - 1] == ' ')
        i--;
    dst[i] = 0;
}

/* a source's name: a fixed text, or written into buf */
static const char *bind_src_label(uint16_t s, char *buf, size_t n)
{
    static const char *const pad[XBOX_PAD_SRC_COUNT] = {
        "NONE", "SOUTH", "EAST", "WEST", "NORTH", "BACK", "START", "L STICK", "R STICK",
        "L SHOULDER", "R SHOULDER", "DPAD UP", "DPAD DOWN", "DPAD LEFT", "DPAD RIGHT", "L TRIGGER", "R TRIGGER",
    };
    const char *t = NULL;
    if (!s)
        return "NONE";
    if (s_bind_port >= 0)
        return s < XBOX_PAD_SRC_COUNT ? pad[s] : "?";
    if (s >= 4 && s <= 29) {
        snprintf(buf, n, "%c", 'A' + (s - 4));
        return buf;
    }
    if (s >= 30 && s <= 38) {
        snprintf(buf, n, "%c", '1' + (s - 30));
        return buf;
    }
    if (s >= 58 && s <= 69) {
        snprintf(buf, n, "F%d", s - 57);
        return buf;
    }
    if (s >= 89 && s <= 97) {
        snprintf(buf, n, "KP %d", s - 88);
        return buf;
    }
    switch (s) {
    case 39: t = "0"; break;                  case 40: t = "ENTER"; break;
    case 41: t = "ESCAPE"; break;             case 42: t = "BACKSPACE"; break;
    case 43: t = "TAB"; break;                case 44: t = "SPACE"; break;
    case 79: t = "RIGHT ARROW"; break;        case 80: t = "LEFT ARROW"; break;
    case 81: t = "DOWN ARROW"; break;         case 82: t = "UP ARROW"; break;
    case 88: t = "KP ENTER"; break;           case 98: t = "KP 0"; break;
    case 224: t = "L CTRL"; break;            case 225: t = "L SHIFT"; break;
    case 226: t = "L ALT"; break;             case 228: t = "R CTRL"; break;
    case 229: t = "R SHIFT"; break;           case 230: t = "R ALT"; break;
    }
    if (t)
        return t;
    snprintf(buf, n, "KEY %u", (unsigned)s);
    return buf;
}

/* the devices of a player: the keyboard when it plays on that port, the
 * controller on that port; returns the count */
static int bind_devices(int player, int port[2])
{
    XboxBindMap o, d;
    char guid[33];
    int n = 0;
    if (xbox_InputKeyboardPort() == player)
        port[n++] = -1;
    if (xbox_InputBindPad(player, guid, &o, &d))
        port[n++] = player;
    return n;
}

/* the current player / device: its bindings */
static void bind_select(void)
{
    int port[2], n = bind_devices(s_bind_player, port);
    char guid[33];
    if (!n) {
        s_bind_port = -3;
        return;
    }
    s_bind_dev = ((s_bind_dev % n) + n) % n;
    s_bind_port = port[s_bind_dev];
    if (s_bind_port < 0)
        xbox_InputBindKeyboard(&s_bind_own, &s_bind_def);
    else if (!xbox_InputBindPad(s_bind_port, guid, &s_bind_own, &s_bind_def))
        s_bind_port = -3;                         /* gone meanwhile */
}

static void bind_keys_build(void)
{
    XboxBindKind kind = s_bind_port >= 0 ? XBOX_BIND_PAD : XBOX_BIND_KEYBOARD;
    uint32_t dup;
    int n, pages, c, line = 0;

    memset(s_bkeys_text, 0, sizeof s_bkeys_text);
    snprintf(s_bkeys_text[line], sizeof s_bkeys_text[line], "PLAYER: %d", s_bind_player + 1);
    s_bind_line_ctl[line++] = BIND_PLAYER;
    if (s_bind_port == -3) {
        snprintf(s_bkeys_title, sizeof s_bkeys_title, "BUTTON MAPPING");
        snprintf(s_bkeys_text[line], sizeof s_bkeys_text[line], "DEVICE: NONE CONNECTED");
        s_bind_line_ctl[line] = BIND_NONE;
        return;
    }
    if (s_bind_port < 0) {
        snprintf(s_bkeys_text[line], sizeof s_bkeys_text[line], "DEVICE: KEYBOARD");
    } else {
        char name[64], up[20];
        xbox_InputPadName(s_bind_port, name, sizeof name);
        bind_upper(up, sizeof up, name);
        snprintf(s_bkeys_text[line], sizeof s_bkeys_text[line], "DEVICE: %s", up[0] ? up : "CONTROLLER");
    }
    s_bind_line_ctl[line++] = BIND_DEVICE;
    snprintf(s_bkeys_text[line], sizeof s_bkeys_text[line], "RESET TO DEFAULT");   /* under DEVICE (maintainer) */
    s_bind_line_ctl[line++] = BIND_RESET;

    dup = xbox_bind_duplicates(kind, &s_bind_own);
    n = xbox_bind_controls(kind);
    pages = (n + BIND_PAGE - 1) / BIND_PAGE;
    if (s_bind_page >= pages)
        s_bind_page = 0;
    snprintf(s_bkeys_title, sizeof s_bkeys_title, "BUTTON MAPPING  %d OF %d", s_bind_page + 1, pages);
    for (c = s_bind_page * BIND_PAGE; c < n && c < (s_bind_page + 1) * BIND_PAGE; c++, line++) {
        char a[16], b[16], *t = s_bkeys_text[line];
        const char *la, *lb;
        size_t sz = sizeof s_bkeys_text[line];
        s_bind_line_ctl[line] = (uint32_t)c;
        if (s_cap_control == c) {
            snprintf(t, sz, "%s: %s", s_bind_ctl_label[c], s_bind_port >= 0 ? "PRESS A BUTTON" : "PRESS A KEY");
            continue;
        }
        la = bind_src_label(s_bind_own.src[c][0], a, sizeof a);
        snprintf(t, sz, "%s: %s", s_bind_ctl_label[c], la);
        if (s_bind_own.src[c][1]) {                /* a second source, when it fits */
            size_t l = strlen(t);
            lb = bind_src_label(s_bind_own.src[c][1], b, sizeof b);
            if (l + 1 + strlen(lb) <= 28)
                snprintf(t + l, sz - l, " %s", lb);
        }
        if (dup & (1u << c)) {
            size_t l = strlen(t);
            snprintf(t + l, sz - l, " DOUBLE");
        }
    }
    if (pages > 1) {
        snprintf(s_bkeys_text[line], sizeof s_bkeys_text[line], "NEXT PAGE");
        s_bind_line_ctl[line] = BIND_NEXT;
    }
}

static void bind_store(void)
{
    if (s_bind_port == -1)
        xbox_InputBindStoreKeyboard(&s_bind_own);
    else if (s_bind_port >= 0)
        xbox_InputBindStorePad(s_bind_port, &s_bind_own);
}

/* CONTROLS -> BUTTON MAPPING: player 1, its first device, page 1 */
static void bind_open(void)
{
    s_bind_player = 0;
    s_bind_dev = 0;
    s_bind_page = 0;
    s_cap_control = -1;
    bind_select();
    bind_keys_build();
}

/* left / right (dir) on the PLAYER or DEVICE line; 1 when the screen changed */
static int bind_lr(uint32_t line, int dir)
{
    uint32_t what = line < 12 ? s_bind_line_ctl[line] : BIND_NONE;
    if (s_cap_control >= 0 || (what != BIND_PLAYER && what != BIND_DEVICE))
        return 0;
    if (what == BIND_PLAYER) {
        s_bind_player = (s_bind_player + dir + 4) % 4;
        s_bind_dev = 0;
    } else {
        s_bind_dev += dir;
    }
    s_bind_page = 0;
    bind_select();
    bind_keys_build();
    s_bind_cursor = line;
    s_bind_refresh = 1;
    return 1;
}

/* A on a line; returns the line the cursor goes to */
static uint32_t bind_keys_action(uint32_t line)
{
    uint32_t what = line < 12 ? s_bind_line_ctl[line] : BIND_NONE;
    if (what == BIND_PLAYER || what == BIND_DEVICE) {   /* A steps forward like right */
        if (what == BIND_PLAYER) {
            s_bind_player = (s_bind_player + 1) % 4;
            s_bind_dev = 0;
        } else {
            s_bind_dev++;
        }
        s_bind_page = 0;
        bind_select();
        bind_keys_build();
        return line;
    }
    if (what == BIND_NONE)
        return line;
    if (what == BIND_NEXT) {
        s_bind_page++;
        bind_keys_build();
        return opt_lines(OPT_BINDKEYS) ? opt_lines(OPT_BINDKEYS) - 1 : 0;   /* on NEXT PAGE again */
    }
    if (what == BIND_RESET) {
        s_bind_own = s_bind_def;
        bind_store();
        fprintf(stderr, "[BIND] %s reset to default\n", s_bind_port >= 0 ? "pad" : "keyboard");
        bind_keys_build();
        return line;
    }
    s_cap_control = (int)what;
    s_cap_line = line;
    s_cap_phase = 0;
    s_cap_frames = BIND_CAPTURE_FRAMES;
    xbox_InputBindSuppress(1);
    bind_keys_build();
    return line;
}

static void bind_refresh(uint32_t cursor)
{
    bind_keys_build();
    s_bind_cursor = cursor;
    s_bind_refresh = 1;
    PAD_LATCH(0x00305369u, 0) = 1;                /* the Options screen's A handler rebuilds it */
}

/* every frame (swap hook): a press being recorded */
static void thps_bind_tick(void)
{
    uint16_t held;
    if (s_cap_control < 0)
        return;
    if (s_in_level || s_opt_shown != OPT_BINDKEYS || s_bind_port == -3) {   /* left the screen: give up */
        s_cap_control = -1;
        xbox_InputBindSuppress(0);
        return;
    }
    held = s_bind_port >= 0 ? xbox_InputBindHeldPad(s_bind_port) : xbox_InputBindHeldKeyboard();
    if (s_cap_phase == 0) {                       /* the A that started it released first */
        if (!held)
            s_cap_phase = 1;
    } else if (s_cap_phase == 1 && held) {
        char a[16];
        xbox_bind_set(&s_bind_own, s_cap_control, held);
        bind_store();
        fprintf(stderr, "[BIND] %s %s = %s\n", s_bind_port >= 0 ? "pad" : "keyboard",
                xbox_bind_control_name(s_cap_control),
                xbox_bind_source_name(s_bind_port >= 0 ? XBOX_BIND_PAD : XBOX_BIND_KEYBOARD, held, a, sizeof a));
        s_cap_phase = 2;
    } else if (s_cap_phase == 2 && !held) {      /* released: the menu may see input again */
        s_cap_control = -1;
        xbox_InputBindSuppress(0);
        bind_refresh(s_cap_line);
        return;
    }
    if (s_cap_phase < 2 && --s_cap_frames <= 0) {
        fprintf(stderr, "[BIND] no press in 5 s - unchanged\n");
        s_cap_control = -1;
        xbox_InputBindSuppress(0);
        bind_refresh(s_cap_line);
    }
}

/* ── PS2-style layout: spins on Black / White, nollie + switch on the triggers ──
 *
 * Why: player request (v0.9.2). The Xbox
 * version put all four PS shoulder roles on the two triggers: spin in the air,
 * nollie / switch on the ground. BUTTON MAPPING cannot split them - both read
 * the same trigger records. The PS2 games had spins on L1 / R1 (Black / White
 * on the Xbox pad, the shoulder buttons on a modern pad) and nollie / switch
 * on L2 / R2.
 * Addresses (runtime trace, report part B): the pad struct per player at
 * 0x305338 + n*0x18C holds one 16-byte record per button - LT +0x40, RT +0x60,
 * Black +0xC0, White +0xD0 (gameplay never reads Black / White). In the air
 * the spin is read by sub_000AC9D0 (held trigger, every frame) and
 * sub_000A85E0 (the per-press impulse); on the ground nollie / switch by
 * sub_000B18A0 / sub_000B2520, which keep the triggers.
 * How: with control_layout = ps2 (saves\settings.ini; THPS_CONTROL_LAYOUT),
 * the two air readers run with the Black / White records swapped into the
 * trigger slots of every player's pad, swapped back after the call - so their
 * latch updates land on Black / White, the triggers stay untouched. Default
 * xbox = the original, both calls straight through.
 * Verified: v0.9.2 tests (PS2 layout test). */
extern void sub_000AC9D0_gen(void);
extern void sub_000A85E0_gen(void);
const char *settings_get(const char *key, const char *def);

static int ps2_layout(void)
{
    static int on = -1;
    static unsigned calls;
    if (on < 0 || ++calls % 600u == 0)          /* the setting re-read every ~10 s of play */
        on = !strcmp(settings_get("control_layout", "xbox"), "ps2");
    return on;
}

static void ps2_swap_shoulders(void)
{
    uint32_t n, k;
    for (n = 0; n < 4; n++) {
        volatile uint8_t *pad = (volatile uint8_t *)((uintptr_t)g_xbox_mem_offset + 0x305338u + n * 0x18Cu);
        for (k = 0; k < 0x10; k++) {
            uint8_t t = pad[0x40 + k]; pad[0x40 + k] = pad[0xC0 + k]; pad[0xC0 + k] = t;
            t = pad[0x60 + k]; pad[0x60 + k] = pad[0xD0 + k]; pad[0xD0 + k] = t;
        }
    }
}

void sub_000AC9D0(void)
{
    /* THPS_LAYOUT_TRACE=1 (test, either layout): player 1's shoulder records
     * and the spin value the reader returned, while any of the four is down. */
    static int tr = -1;
    int ps2 = ps2_layout();
    if (tr < 0) {
        const char *e = getenv("THPS_LAYOUT_TRACE");
        tr = e && *e == '1';
    }
    if (!ps2 && !tr) {
        sub_000AC9D0_gen();
        return;
    }
    if (ps2)
        ps2_swap_shoulders();
    sub_000AC9D0_gen();
    if (ps2)
        ps2_swap_shoulders();
    {
        volatile uint8_t *p = (volatile uint8_t *)((uintptr_t)g_xbox_mem_offset + 0x305338u);
        if (p[0x40] | p[0x60] | p[0xC0] | p[0xD0])
            fprintf(stderr, "[LAYOUT] air spin: LT %u RT %u black %u white %u -> %08X\n",
                    p[0x40], p[0x60], p[0xC0], p[0xD0], g_eax);
    }
}

/* ── 180 spin taps on / off, sub_000A85E0 ────────────────────────────────────
 *
 * Why: player request (v0.9.2 item 1): a toggle for the automatic 180s.
 * Address: sub_000A85E0 (the skater's air update, cdecl, skater at [esp+4]).
 * In the air (state [skater+0x5984] 1 or 2, [skater+0x5520] == 0) a fresh
 * trigger press - the new-press latch, LT record +0x41 / RT +0x61 of the pad at
 * [skater+0x556C] - clears the latch and queues one 180 in [skater+0x572C]
 * (LT -1, RT +1, 0x000A87A1 / 0x000A87CC); the queue turns the skater
 * 0x8C * dt per frame and counts down at each 0x800 (180 of 0x1000) at
 * 0x000A88A2. A held trigger spins through sub_000AC9D0 instead.
 * How: spin_taps = off (saves\settings.ini; THPS_SPIN_TAPS) clears both
 * latches before the call in exactly that state, so nothing is queued; the
 * original clears them there anyway, nothing else reads them in the air. With
 * the PS2 layout the clear comes after the swap, so it hits Black / White.
 * Default on = the original.
 * Verified: v0.9.2 tests (spin taps test). */
static int spin_taps_off(void)
{
    static int off = -1;
    static unsigned calls;
    if (off < 0 || ++calls % 600u == 0)
        off = !strcmp(settings_get("spin_taps", "on"), "off");
    return off;
}

void sub_000A85E0(void)
{
    int ps2 = ps2_layout(), off = spin_taps_off();
    uint32_t sk = GUEST32(g_esp + 4);
    static int tr = -1;

    if (tr < 0) {
        const char *e = getenv("THPS_SPIN_TRACE");
        tr = e && *e == '1';
    }
    if (tr && thps_replay_playing()) {            /* do the gameplay options reach a replay? */
        static unsigned n;
        if (n++ % 120 == 0)
            fprintf(stderr, "[SPINTAP] air update runs during replay playback (%u)\n", n);
    }
    if (!ps2 && !off && !tr) {
        sub_000A85E0_gen();
        return;
    }
    if (ps2)
        ps2_swap_shoulders();
    if (off && sk) {
        uint32_t state = GUEST32(sk + 0x5984), pad = GUEST32(sk + 0x556C);
        if ((state == 1 || state == 2) && GUEST32(sk + 0x5520) == 0 && pad) {
            volatile uint8_t *p = (volatile uint8_t *)((uintptr_t)g_xbox_mem_offset + pad);
            p[0x41] = 0;
            p[0x61] = 0;
        }
    }
    {
        uint32_t q0 = sk ? GUEST32(sk + 0x572C) : 0;
        sub_000A85E0_gen();
        /* THPS_SPIN_TRACE=1 (test): the 180 queue of the skater whenever it changes */
        if (tr && sk && GUEST32(sk + 0x572C) != q0)
            fprintf(stderr, "[SPINTAP] queue %d -> %d (state %u, taps %s)\n", (int)q0,
                    (int)GUEST32(sk + 0x572C), GUEST32(sk + 0x5984), off ? "off" : "on");
    }
    if (ps2)
        ps2_swap_shoulders();
}

/* ── BIG DROP original / no bail / off, sub_000A5970 + sub_000A4C30 ──────────
 *
 * Why: player request (v0.9.2 item 4; maintainer 2026-10-28: all three).
 * Address: the landing code sub_000A6CA0 compares the fall height
 * (([skater+0x57EC] + [skater+0x57E4]) >> 12) with two limits: above
 * [skater+0x57E8] + (11 - [skater+0x5694]) * 80 (when +0x5694 is set) it bails
 * - sub_000A5970 (skater in edi, 2 arguments, ret 8), return address
 * 0x000A7003; above [skater+0x57E8] alone it awards BIG DROP -
 * sub_000A4C30 (skater in eax; "BIG DROP" 0x002353E4, ([skater+0x5694] - 11) *
 * 100, 0, -1; ret 16), return address 0x000A7027. MOON PHYSICS and the codes
 * XTRALUSRULS / RADBLAST (0x00306BC8 / 0x00306BCC) skip both.
 * How: big_drop (saves\settings.ini; THPS_BIG_DROP): original = both calls
 * straight through; no_bail = the bail from 0x000A7003 becomes that award (the
 * drop scores like one under the bail limit); off = neither (the drop lands
 * like any other). sub_000A5970's 32 other callers (every other bail) and
 * sub_000A4C30's other callers go straight through.
 * Verified: v0.9.2 tests (big drop test). */
extern THPS_TLS uint32_t g_edi;
extern void sub_000A5970_gen(void);
extern void sub_000A4C30_gen(void);
void sub_000A4C30(void);

static int big_drop_mode(void)                    /* 0 original, 1 no bail, 2 off */
{
    const char *v = settings_get("big_drop", "original");
    return !strcmp(v, "no_bail") ? 1 : !strcmp(v, "off") ? 2 : 0;
}

void sub_000A5970(void)
{
    static int tr = -1;
    uint32_t sk;
    int mode;
    if (tr < 0) {
        const char *e = getenv("THPS_BAIL_TRACE");
        tr = e && *e == '1';
    }
    if (tr)                                       /* THPS_BAIL_TRACE=1 (test): every bail, where from */
        fprintf(stderr, "[BAIL] from %08X args %08X %08X skater %08X\n", GUEST32(g_esp), GUEST32(g_esp + 4),
                GUEST32(g_esp + 8), g_edi);
    if (GUEST32(g_esp) != 0x000A7003u || (mode = big_drop_mode()) == 0) {
        sub_000A5970_gen();
        return;
    }
    sk = g_edi;
    g_esp += 4 + 8;                               /* the bail's return address and arguments */
    fprintf(stderr, "[BIGDROP] bail -> %s\n", mode == 1 ? "award" : "nothing");
    if (mode == 1) {
        g_esp -= 4; GUEST32(g_esp) = 0xFFFFFFFFu;
        g_esp -= 4; GUEST32(g_esp) = 0;
        g_esp -= 4; GUEST32(g_esp) = (uint32_t)(((int32_t)GUEST32(sk + 0x5694) - 11) * 100);
        g_esp -= 4; GUEST32(g_esp) = 0x002353E4u;      /* "BIG DROP" */
        g_esp -= 4; GUEST32(g_esp) = 0x000A7003u;      /* popped by ret 16 */
        g_eax = sk;
        sub_000A4C30();
    }
}

void sub_000A4C30(void)
{
    if (GUEST32(g_esp) == 0x000A7027u && big_drop_mode() == 2) {
        g_esp += 4 + 16;                          /* no BIG DROP award */
        fprintf(stderr, "[BIGDROP] award -> nothing\n");
        return;
    }
    sub_000A4C30_gen();
}
