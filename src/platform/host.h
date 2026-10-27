/* host.h - THPS 2X host platform layer (process, paths, log, crash handling).
 *
 * Everything OS-specific of the game executable lives behind this header
 * (CLAUDE.md, Portability). Windows implementation: host_win32.c.
 */
#ifndef THPS_HOST_H
#define THPS_HOST_H

#include <stddef.h>

#define HOST_PATH_MAX 1024

typedef struct HostOptions {
    char exe_dir[HOST_PATH_MAX];   /* folder of the executable, no trailing separator */
    char game_dir[HOST_PATH_MAX];  /* extracted disc: default.xbe + data folders */
    char save_dir[HOST_PATH_MAX];  /* Xbox U: (and partition images) */
    char data_dir[HOST_PATH_MAX];  /* settings, caches, logs, crash dumps */
    double max_seconds;            /* --max-seconds: exit after this long, 0 = no limit */
} HostOptions;

/* Parse the command line (--max-seconds, --game-dir, --save-dir, --data-dir,
 * --console, --input-script <file>, --input-live <file>), create the layout folders, redirect stdout/stderr
 * into <data>/thps2x.log (previous run kept as thps2x.previous.log), install
 * the crash handler and arm the time limit. Returns 0 on success. */
int host_init(int argc, char **argv, HostOptions *opt);

/* Read a whole file; caller frees with free(). NULL on failure. */
void *host_read_file(const char *path, size_t *out_size);

/* Write a whole file atomically: into pending_dir (created) first, then
 * moved over path in one step (REPLACE_EXISTING | WRITE_THROUGH), like the
 * saves -- an interrupted write leaves the old file. 1 on success. */
int host_write_file_atomic(const char *path, const char *pending_dir, const void *data, size_t n);

/* Set an environment variable unless the user already set it (runtime
 * defaults that a test run can still override). */
void host_setenv_default(const char *name, const char *value);
/* Set (overwrite) a variable; "" removes it. */
void host_setenv(const char *name, const char *value);

/* Debugging aids that need the guest memory mapped (THPS_WATCH_WRITE). */
void host_debug_start(void);

/* End the process now (window closed): log drained, exit code 0. */
void host_quit(void);

/* Start the exe again with the same arguments and the environment it started
 * with, then end this process (RECOMP OPTIONS: RESTART NOW). Returns only
 * when the new process could not be started. */
void host_restart(void);

/* Fatal error: logged and shown to the user. */
void host_fatal(const char *msg);

#endif
