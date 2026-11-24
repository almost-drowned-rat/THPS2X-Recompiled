/* host_linux.c - THPS 2X host platform layer, Linux implementation (2.0, AppImage).
 *
 * Model: Panzer Dragoon Orta Recompiled src/platform/host_linux.c (same author, option-c v1.1.0 Linux port), with
 * THPS 2X's own parts of host_win32.c: page tracking and RECOMP_RT_WATCH forwarded from the fault handler (Orta's
 * Linux host has neither), APU / USB traps only in a THPS_LLE build, host_renderer_failed, RECOMP_NO_DIALOGS for
 * the fatal box, THPS_LOG_RAW, thps2x.log.
 *
 * The same contract as host_win32.c (host.h): layout folders beside the executable - or beside the AppImage file
 * when run from one (toolkit xbox_host_root, $APPIMAGE), so game, saves and data stay portable next to it -, the log
 * through a pipe with the same repeat collapsing, the time limit, restart with the start environment, atomic writes.
 *
 * Crash reports and device traps: one vectored exception handler on the toolkit's signal-backed list (option-c
 * win32_compat AddVectoredExceptionHandler: CONTEXT from the ucontext, write flag from the page-fault error code).
 * Windows-only diagnostics stay on Windows: the DR0-3 write watchpoint (THPS_WATCH_WRITE) and the sampling profiler
 * (RECOMP_SAMPLE); a Linux run uses perf for the latter.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <SDL3/SDL.h>

#include "kernel.h"
#include "xbox_memory_layout.h"

#include "host.h"
#include "platform/host_paths.h"     /* xbox_host_root / xbox_host_self (option-c) */
#include "recomp_icall_feedback.h"   /* no-op unless RECOMP_ICALL_FEEDBACK */

extern ptrdiff_t g_xbox_mem_offset;
extern char **environ;

static void host_log_drain(void);

/* ── crash report ────────────────────────────────────────────────── */

/* The faulting function by name (dladdr; the exe is linked with -rdynamic, CMakeLists.txt) and the guest stack
 * scanned for return addresses (FPO frames: a scan, not a walk, and it over-reports) - as host_win32.c. */
static void print_guest_context(RECOMP_PARAMS, void *rip)
{
    Dl_info di;
    (void)base;
    if (dladdr(rip, &di) && di.dli_sname)
        fprintf(stderr, "  in %s+0x%llX\n", di.dli_sname,
                (unsigned long long)((uintptr_t)rip - (uintptr_t)di.dli_saddr));
    if (g_xbox_mem_offset && g_esp) {
        const uint32_t *sp = (const uint32_t *)((uintptr_t)g_xbox_mem_offset + g_esp);
        int shown = 0, i;
        fprintf(stderr, "  guest stack (return addresses, innermost first):\n");
        for (i = 0; i < 256 && shown < 24; i++) {
            uint32_t v = sp[i];
            if (v > g_xbox_code_lo && v < g_xbox_code_hi) {
                fprintf(stderr, "    [esp+%-4d] 0x%08X\n", i * 4, v);
                shown++;
            }
        }
        if (!shown) {
            extern volatile uint32_t g_icall_trace[16];
            extern volatile uint32_t g_icall_trace_idx;
            uint32_t k;
            fprintf(stderr, "    (no return addresses in range)\n  recent ICALL targets:");
            for (k = 0; k < 16; k++)
                fprintf(stderr, " %08X", g_icall_trace[(g_icall_trace_idx + k) & 15]);
            fprintf(stderr, "\n");
            for (i = 0; i < 20; i++)
                fprintf(stderr, "    raw[esp+%-4d] 0x%08X\n", i * 4, sp[i]);
        }
    }
}

/* A vectored exception handler has a fixed signature: no register context is handed in. Diagnostics only, so it
 * takes the faulting thread's registered set (xbox_diag_ctx) - or an all-zero one on a host thread that runs no
 * guest code - and only READS it (as host_win32.c). */
#define HOST_DIAG_CTX()                                                   \
    static X86Ctx diag_none_;                                             \
    X86Ctx *ctx = xbox_diag_ctx() ? xbox_diag_ctx() : &diag_none_;        \
    uint8_t *base = RECOMP_GUEST_BASE;                                    \
    (void)base

#if defined(__x86_64__)
#if defined(THPS_LLE)
extern bool apu_hook_handle_mmio(PCONTEXT ctx, uintptr_t fault_addr, uint32_t xbox_va, int is_write);
extern int xbox_OhciOwnsAddress(uint32_t xbox_va);
extern int xbox_OhciHandleMmio(void *ctx, uint32_t xbox_va);
#define APU_TRAP_BASE 0xFE800000u
#define APU_TRAP_END  0xFE880000u
#endif

static LONG veh_handler(PEXCEPTION_POINTERS ep)
{
    HOST_DIAG_CTX();    /* ctx_rewrite: keep (VEH signature) */
    DWORD code = ep->ExceptionRecord->ExceptionCode;

    if (code == EXCEPTION_ACCESS_VIOLATION) {
        uintptr_t fault_addr = ep->ExceptionRecord->ExceptionInformation[1];
        uint32_t xbox_va = (uint32_t)(fault_addr - (uintptr_t)g_xbox_mem_offset);

        /* Page tracking (toolkit page_track.c, POSIX too): a guest write to a page a texture or vertex buffer was
         * read from -- noted and resumed. */
        if (xbox_PageTrackFault(fault_addr))
            return EXCEPTION_CONTINUE_EXECUTION;

        /* RECOMP_RT_WATCH (debug): a CPU touch of a render target the renderer armed -- logged and resumed. */
        if (xbox_RtWatchFault(fault_addr, ep->ExceptionRecord->ExceptionInformation[0] ? 1 : 0,
                              (uintptr_t)ep->ContextRecord->Rip)) {
            static int shown;
            if (shown++ < 8)
                print_guest_context(RECOMP_ARGS, (void *)(uintptr_t)ep->ContextRecord->Rip);
            return EXCEPTION_CONTINUE_EXECUTION;
        }

#if defined(THPS_LLE)
        if (xbox_va >= APU_TRAP_BASE && xbox_va < APU_TRAP_END
                && apu_hook_handle_mmio(ep->ContextRecord, fault_addr, xbox_va,
                                        ep->ExceptionRecord->ExceptionInformation[0] ? 1 : 0))
            return EXCEPTION_CONTINUE_EXECUTION;
        if (xbox_OhciOwnsAddress(xbox_va) && xbox_OhciHandleMmio(ep->ContextRecord, xbox_va))
            return EXCEPTION_CONTINUE_EXECUTION;
#endif

        fprintf(stderr, "[CRASH] Access violation at RIP=0x%llX, fault addr=0x%llX (%s)\n",
                (unsigned long long)ep->ContextRecord->Rip, (unsigned long long)fault_addr,
                ep->ExceptionRecord->ExceptionInformation[0] ? "write" : "read");
        fprintf(stderr, "  Xbox VA of fault: 0x%08X\n", xbox_va);
    } else if (code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_INT_DIVIDE_BY_ZERO) {
        fprintf(stderr, "[CRASH] exception 0x%08lX at RIP=0x%llX\n",
                (unsigned long)code, (unsigned long long)ep->ContextRecord->Rip);
    } else {
        return EXCEPTION_CONTINUE_SEARCH;   /* SIGTRAP (debugger), FPU signals: not ours */
    }

    fprintf(stderr, "  Xbox regs: eax=0x%08X ecx=0x%08X edx=0x%08X esp=0x%08X\n", g_eax, g_ecx, g_edx, g_esp);
    fprintf(stderr, "  Xbox regs: ebx=0x%08X esi=0x%08X edi=0x%08X\n", g_ebx, g_esi, g_edi);
    print_guest_context(RECOMP_ARGS, (void *)(uintptr_t)ep->ContextRecord->Rip);
    RECOMP_ICALL_FEEDBACK_DUMP();   /* a title that dies mid-boot is the one whose targets we want */
    host_log_drain();               /* the process is about to die */
    return EXCEPTION_CONTINUE_SEARCH;   /* the toolkit's chain ends in the default action (core / exit) */
}

static void install_crash_handlers(void)
{
    /* Last in the list (as on Windows): the runtime's own traps (AC97, null guard) see a fault first. */
    if (!AddVectoredExceptionHandler(0, veh_handler))
        fprintf(stderr, "[HOST] AddVectoredExceptionHandler failed -- no crash report, no page tracking\n");
    else
        xbox_PageTrackEnable();      /* the handler above forwards its faults */
}
#else
static void install_crash_handlers(void)
{
    /* THPS 2X 2.0 ships x86-64 only; no page tracking without the fault handler */
    fprintf(stderr, "[HOST] no vectored exception handlers on this CPU -- no crash report, no page tracking\n");
}
#endif

void host_debug_start(void)
{
    /* THPS_WATCH_WRITE (debug registers DR0-3) is Windows only */
    if (getenv("THPS_WATCH_WRITE"))
        fprintf(stderr, "[WATCH] THPS_WATCH_WRITE is Windows only -- ignored\n");
}

/* ── time limit / quit ───────────────────────────────────────────── */

static void host_terminate(int code)
{
    RECOMP_ICALL_FEEDBACK_DUMP();   /* _exit skips atexit */
    host_log_drain();
    _exit(code);
}

static void *time_limit_thread(void *arg)
{
    double secs = *(double *)arg;
    struct timespec ts;
    ts.tv_sec = (time_t)secs;
    ts.tv_nsec = (long)((secs - (double)ts.tv_sec) * 1e9);
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
        ;
    fprintf(stderr, "[HOST] --max-seconds %.0f reached, exiting\n", secs);
    host_terminate(0);
    return NULL;
}

/* The window was closed. The guest threads never return on their own, so this ends the process the way the time
 * limit does. */
void host_quit(void)
{
    fprintf(stderr, "[HOST] window closed, exiting\n");
    host_terminate(0);
}

void host_renderer_failed(void)
{
    fprintf(stderr, "[HOST] the renderer cannot start, exiting\n");
    host_terminate(3);
}

/* ── paths ───────────────────────────────────────────────────────── */

static void join(char *out, const char *dir, const char *name)
{
    snprintf(out, HOST_PATH_MAX, "%s/%s", dir, name);
}

static void mkdir_p(const char *path)
{
    char tmp[HOST_PATH_MAX];
    size_t i;
    snprintf(tmp, sizeof tmp, "%s", path);
    for (i = 1; tmp[i]; i++)
        if (tmp[i] == '/') {
            tmp[i] = 0;
            mkdir(tmp, 0755);
            tmp[i] = '/';
        }
    mkdir(tmp, 0755);
}

/* The folder the player sees: the toolkit's writable install root = beside the AppImage file when run from one
 * ($APPIMAGE, believed only when the exe lies under $APPDIR), else beside the executable. Without its trailing
 * separator (join() adds one). */
static void root_dir(char *out)
{
    const char *r = xbox_host_root();
    size_t n;
    snprintf(out, HOST_PATH_MAX, "%s", (r && *r) ? r : ".");
    n = strlen(out);
    if (n > 1 && out[n - 1] == '/')
        out[n - 1] = 0;
}

/* ── log file with repeat collapsing (as host_win32.c) ─────────────
 *
 * stdout and stderr go through a pipe; a reader thread writes the log: a run of lines that differ only in their
 * numbers keeps its first HOST_LOG_KEEP lines and is then summarised as one line with the count and the last line.
 * Crash and watchdog reports are written verbatim. */

#define HOST_LOG_KEEP      3
#define HOST_LOG_LINE_MAX  1024
#define HOST_LOG_VERBATIM  400   /* lines after a [CRASH]/[WATCHDOG] header */

static FILE     *s_log;
static int       s_log_pipe_w = -1;
static int       s_log_pipe_r = -1;
static pthread_t s_log_thread;
static int       s_log_thread_on;
static char      s_prev_key[HOST_LOG_LINE_MAX];
static char      s_last_line[HOST_LOG_LINE_MAX];
static unsigned long s_repeats;
static int       s_verbatim;

/* The line with every run of hex/decimal digits replaced by '#'. */
static void log_key(const char *line, char *key, size_t n)
{
    size_t k = 0;
    while (*line && k + 1 < n) {
        if (isxdigit((unsigned char)*line) && (isdigit((unsigned char)*line)
                || (line[1] && isxdigit((unsigned char)line[1])))) {
            while (*line && (isxdigit((unsigned char)*line) || *line == 'x'))
                line++;
            key[k++] = '#';
        } else {
            key[k++] = *line++;
        }
    }
    key[k] = '\0';
}

static void log_flush_run(void)
{
    if (s_repeats > HOST_LOG_KEEP - 1) {
        unsigned long hidden = s_repeats - (HOST_LOG_KEEP - 1);
        fprintf(s_log, "  [log] ... %lu more similar line(s), last: %s", hidden, s_last_line);
    }
    s_repeats = 0;
}

static void log_line(const char *line)
{
    char key[HOST_LOG_LINE_MAX];
    static int raw = -1;

    if (!strncmp(line, "[CRASH]", 7) || !strncmp(line, "[WATCHDOG]", 10)) {
        log_flush_run();
        s_prev_key[0] = '\0';
        s_verbatim = HOST_LOG_VERBATIM;
    }
    if (s_verbatim > 0) {
        s_verbatim--;
        fputs(line, s_log);
        return;
    }
    /* THPS_LOG_RAW=1: no collapsing at all (register traces). A frame trace is one line per draw: never collapsed. */
    if (raw < 0) {
        const char *e = getenv("THPS_LOG_RAW");
        raw = e && *e && *e != '0';
    }
    if (raw) {
        fputs(line, s_log);
        return;
    }
    /* [DRAW] (RECOMP_VK_DRAW_TRACE) uncollapsed like [FTRACE] (as host_win32.c, 11-19) */
    if (!strncmp(line, "[FTRACE]", 8) || !strncmp(line, "  [DRAW]", 8)) {
        log_flush_run();
        s_prev_key[0] = '\0';
        fputs(line, s_log);
        return;
    }
    log_key(line, key, sizeof(key));
    if (s_prev_key[0] && !strcmp(key, s_prev_key)) {
        s_repeats++;
        if (s_repeats < HOST_LOG_KEEP)
            fputs(line, s_log);
        else
            snprintf(s_last_line, sizeof(s_last_line), "%s", line);
        return;
    }
    log_flush_run();
    snprintf(s_prev_key, sizeof(s_prev_key), "%s", key);
    fputs(line, s_log);
}

static void *log_thread(void *unused)
{
    char buf[4096], line[HOST_LOG_LINE_MAX];
    size_t len = 0;
    ssize_t n, i;
    (void)unused;
    while ((n = read(s_log_pipe_r, buf, sizeof(buf))) > 0 || (n < 0 && errno == EINTR)) {
        for (i = 0; i < n; i++) {
            if (len + 2 < sizeof(line))
                line[len++] = buf[i];
            if (buf[i] == '\n' || len + 2 >= sizeof(line)) {
                if (line[len - 1] != '\n')
                    line[len++] = '\n';
                line[len] = '\0';
                log_line(line);
                len = 0;
            }
        }
        fflush(s_log);
    }
    if (len) {
        line[len] = '\0';
        log_line(line);
    }
    log_flush_run();
    fflush(s_log);
    return NULL;
}

/* Close the write side and wait (at most 5 s, as on Windows) for the reader to empty the pipe: the run is over
 * (time limit, crash, exit) and nothing printed so far may be lost. Output written after this is dropped. */
static void host_log_drain(void)
{
    if (s_log_pipe_w < 0)
        return;
    fflush(stdout);
    fflush(stderr);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    close(s_log_pipe_w);
    s_log_pipe_w = -1;
    if (s_log_thread_on) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 5;
        pthread_timedjoin_np(s_log_thread, NULL, &ts);
        s_log_thread_on = 0;
    }
}

static void open_log(const char *data_dir)
{
    char log[HOST_PATH_MAX], prev[HOST_PATH_MAX];
    int fds[2];
    join(log, data_dir, "thps2x.log");
    join(prev, data_dir, "thps2x.previous.log");
    rename(log, prev);
    s_log = fopen(log, "w");
    if (!s_log)
        return;
    if (pipe(fds) != 0) {
        /* No filter then, but still a log. */
        fclose(s_log);
        if (freopen(log, "w", stdout))
            dup2(STDOUT_FILENO, STDERR_FILENO);
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
        return;
    }
    s_log_pipe_r = fds[0];
    s_log_pipe_w = fds[1];
    fcntl(s_log_pipe_r, F_SETFD, FD_CLOEXEC);   /* a restarted process must not hold the old pipe open */
    fcntl(s_log_pipe_w, F_SETFD, FD_CLOEXEC);
    dup2(s_log_pipe_w, STDOUT_FILENO);
    dup2(s_log_pipe_w, STDERR_FILENO);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    if (pthread_create(&s_log_thread, NULL, log_thread, NULL) == 0)
        s_log_thread_on = 1;
    atexit(host_log_drain);
}

/* ── restart (RECOMP OPTIONS: RESTART NOW) ──────────────────────────
 * A new process with the same arguments (minus the input files, which are timed from the start) plus
 * --restart-wait <pid>, and the environment this process STARTED with (main.c turns settings into env defaults;
 * inheriting those would keep the old values). From an AppImage the AppImage file is started again, not the
 * mounted copy (the mount goes away with this process). */
static char **s_env0;
static int    s_argc;
static char **s_argv;

static void keep_start_env(int argc, char **argv)
{
    int n = 0, i;
    while (environ && environ[n]) n++;
    s_env0 = (char **)calloc((size_t)n + 1, sizeof *s_env0);
    for (i = 0; s_env0 && i < n; i++)
        s_env0[i] = strdup(environ[i]);
    s_argc = argc;
    s_argv = argv;
}

void host_restart(void)
{
    char exe[HOST_PATH_MAX], pidbuf[32];
    char **args = (char **)calloc((size_t)s_argc + 4, sizeof *args);
    int i, k = 0;
    pid_t child;
    if (!args)
        return;
    snprintf(exe, sizeof exe, "%s", xbox_host_self());   /* $APPIMAGE inside an AppImage, else this exe */
    if (!exe[0]) {
        free(args);
        fprintf(stderr, "[HOST] restart: own file unknown -- the settings apply at the next start\n");
        return;
    }
    snprintf(pidbuf, sizeof pidbuf, "%lu", (unsigned long)getpid());
    args[k++] = exe;
    args[k++] = "--restart-wait";
    args[k++] = pidbuf;
    for (i = 1; i < s_argc; i++) {
        if (!strcmp(s_argv[i], "--input-script") || !strcmp(s_argv[i], "--input-live") ||
            !strcmp(s_argv[i], "--restart-wait")) {
            i++;                         /* and its value */
            continue;
        }
        args[k++] = s_argv[i];
    }
    args[k] = NULL;
    fprintf(stderr, "[HOST] restarting to apply the settings\n");
    child = fork();
    if (child == 0) {
        setsid();
        execve(exe, args, s_env0 ? s_env0 : environ);
        _exit(127);
    }
    free(args);
    if (child < 0) {
        fprintf(stderr, "[HOST] restart failed (errno %d) -- the settings apply at the next start\n", errno);
        return;
    }
    host_terminate(0);
}

/* --input-script / --input-live: the toolkit's pad script, made absolute before the working directory moves to
 * data/. The command line wins over the environment. */
static void set_input_file(const char *var, const char *prefix, const char *path)
{
    char full[PATH_MAX], val[PATH_MAX + 2];
    if (!realpath(path, full))
        snprintf(full, sizeof full, "%s", path);
    snprintf(val, sizeof val, "%s%s", prefix, full);
    setenv(var, val, 1);
}

/* ── init ────────────────────────────────────────────────────────── */

int host_init(int argc, char **argv, HostOptions *opt)
{
    static double s_limit;
    int console = 0, i;
    unsigned long wait_pid = 0;

    keep_start_env(argc, argv);          /* before anything sets an env default */
    memset(opt, 0, sizeof(*opt));
    root_dir(opt->exe_dir);
    join(opt->game_dir, opt->exe_dir, "game");
    join(opt->save_dir, opt->exe_dir, "saves");
    join(opt->data_dir, opt->exe_dir, "data");

    for (i = 1; i < argc; i++) {
        const char *a = argv[i], *v = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (!strcmp(a, "--console")) { console = 1; continue; }
        if (!v) break;
        if (!strcmp(a, "--max-seconds")) opt->max_seconds = atof(v);
        else if (!strcmp(a, "--game-dir")) snprintf(opt->game_dir, HOST_PATH_MAX, "%s", v);
        else if (!strcmp(a, "--save-dir")) snprintf(opt->save_dir, HOST_PATH_MAX, "%s", v);
        else if (!strcmp(a, "--data-dir")) snprintf(opt->data_dir, HOST_PATH_MAX, "%s", v);
        else if (!strcmp(a, "--input-script")) set_input_file("RECOMP_PAD_SCRIPT", "@", v);
        else if (!strcmp(a, "--input-live")) set_input_file("RECOMP_PAD_LIVE", "", v);
        else if (!strcmp(a, "--restart-wait")) wait_pid = strtoul(v, NULL, 10);
        else continue;
        i++;
    }
    if (wait_pid) {                      /* host_restart: the old process leaves first (at most 10 s) */
        int t;
        for (t = 0; t < 100 && kill((pid_t)wait_pid, 0) == 0; t++)
            usleep(100000);
    }

    mkdir_p(opt->save_dir);
    mkdir_p(opt->data_dir);
    if (!console) open_log(opt->data_dir);
    else { setvbuf(stdout, NULL, _IONBF, 0); setvbuf(stderr, NULL, _IONBF, 0); }
    /* Nothing outside the folder (portable layout): the GPU drivers' shader caches go into data/. Mesa + NVIDIA. */
    {
        char cache[HOST_PATH_MAX];
        join(cache, opt->data_dir, "shadercache");
        mkdir_p(cache);
        host_setenv_default("MESA_SHADER_CACHE_DIR", cache);
        host_setenv_default("__GL_SHADER_DISK_CACHE_PATH", cache);
        host_setenv_default("__GL_SHADER_DISK_CACHE_SKIP_CLEANUP", "1");
    }
    /* Every game path is absolute; the working directory only catches what the runtime writes relative to it
     * (xbox_kernel.log, icall_targets.dump), and that belongs in data/. */
    if (chdir(opt->data_dir) != 0)
        fprintf(stderr, "[HOST] chdir %s failed (errno %d)\n", opt->data_dir, errno);
    RECOMP_ICALL_FEEDBACK_INIT();
    install_crash_handlers();
    if (getenv("RECOMP_SAMPLE"))
        fprintf(stderr, "[SAMPLE] RECOMP_SAMPLE is Windows only -- use perf on Linux\n");

    if (opt->max_seconds > 0) {
        pthread_t t;
        s_limit = opt->max_seconds;
        if (pthread_create(&t, NULL, time_limit_thread, &s_limit) == 0)
            pthread_detach(t);
    }
    return 0;
}

void host_setenv_default(const char *name, const char *value)
{
    if (!getenv(name))
        setenv(name, value, 1);
}

void host_setenv(const char *name, const char *value)
{
    if (value && *value) setenv(name, value, 1);
    else unsetenv(name);
}

void *host_read_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    long size;
    void *data;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = size > 0 ? malloc((size_t)size) : NULL;
    if (data && fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        data = NULL;
    }
    fclose(f);
    if (data) *out_size = (size_t)size;
    return data;
}

/* Into pending_dir first, synced, then renamed over path in one step (rename is atomic within one file system;
 * pending_dir lies beside the target) - an interrupted write leaves the old file. */
int host_write_file_atomic(const char *path, const char *pending_dir, const void *data, size_t n)
{
    char tmp[HOST_PATH_MAX];
    const char *base = path + strlen(path);
    FILE *f;
    int ok;
    while (base > path && base[-1] != '/' && base[-1] != '\\')
        base--;
    mkdir_p(pending_dir);
    join(tmp, pending_dir, base);
    if (!(f = fopen(tmp, "wb")))
        return 0;
    ok = fwrite(data, 1, n, f) == n;
    ok = fflush(f) == 0 && ok;
    ok = fsync(fileno(f)) == 0 && ok;
    ok = fclose(f) == 0 && ok;
    if (!ok)
        return 0;
    return rename(tmp, path) == 0;
}

void host_fatal(const char *msg)
{
    const char *nd = getenv("RECOMP_NO_DIALOGS");
    fprintf(stderr, "[FATAL] %s\n", msg);
    fflush(stderr);
    /* RECOMP_NO_DIALOGS=1 (automated tests): the log line only -- a box would wait for a click until the run's
     * time limit. */
    if (nd && *nd == '1')
        return;
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "THPS 2X Recomp", msg, NULL);
}
