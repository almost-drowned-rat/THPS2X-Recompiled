/* settings.c -- saves\settings.ini (see settings.h). */

#include "settings.h"

#include "platform/host.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINES 256
#define MAX_LINE  1024

static char  s_path[HOST_PATH_MAX], s_pending_dir[HOST_PATH_MAX];
static char *s_line[MAX_LINES];       /* the file, line by line (key=value or kept as is) */
static int   s_n;

static char *dupn(const char *s, size_t n)
{
    char *d = (char *)malloc(n + 1);
    if (d) {
        memcpy(d, s, n);
        d[n] = 0;
    }
    return d;
}

void settings_init(const char *save_dir)
{
    size_t size = 0, i, start;
    char *text;
    snprintf(s_path, sizeof s_path, "%s/settings.ini", save_dir);
    snprintf(s_pending_dir, sizeof s_pending_dir, "%s/.pending", save_dir);
    while (s_n)
        free(s_line[--s_n]);
    if (!(text = (char *)host_read_file(s_path, &size)))
        return;
    start = size >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB
            && (unsigned char)text[2] == 0xBF ? 3 : 0;
    for (i = start; i <= size && s_n < MAX_LINES; i++) {
        if (i == size || text[i] == '\n') {
            size_t e = i;
            while (e > start && (text[e - 1] == '\r'))
                e--;
            if (!(i == size && e == start) && (s_line[s_n] = dupn(text + start, e - start)) != NULL)
                s_n++;
            start = i + 1;
        }
    }
    free(text);
    fprintf(stderr, "[SETTINGS] %s: %d line(s)\n", s_path, s_n);
}

/* The line holding key, or -1. */
static int find(const char *key)
{
    size_t kl = strlen(key);
    int i;
    for (i = 0; i < s_n; i++) {
        const char *l = s_line[i];
        while (*l == ' ' || *l == '\t')
            l++;
        if (!strncmp(l, key, kl)) {
            const char *r = l + kl;
            while (*r == ' ' || *r == '\t')
                r++;
            if (*r == '=')
                return i;
        }
    }
    return -1;
}

static const char *value_of(int i)
{
    const char *v = strchr(s_line[i], '=') + 1;
    while (*v == ' ' || *v == '\t')
        v++;
    return v;
}

const char *settings_get(const char *key, const char *def)
{
    char env[128];
    const char *e;
    size_t i;
    int k;
    snprintf(env, sizeof env, "THPS_%s", key);
    for (i = 5; env[i]; i++)
        env[i] = (char)toupper((unsigned char)env[i]);
    if ((e = getenv(env)) != NULL && *e)
        return e;
    k = find(key);
    return k >= 0 ? value_of(k) : def;
}

static void write_file(void)
{
    size_t n = 0, at = 0;
    char *buf;
    int i;
    for (i = 0; i < s_n; i++)
        n += strlen(s_line[i]) + 1;
    if (!(buf = (char *)malloc(n + 1)))
        return;
    for (i = 0; i < s_n; i++) {
        size_t l = strlen(s_line[i]);
        memcpy(buf + at, s_line[i], l);
        at += l;
        buf[at++] = '\n';
    }
    if (!host_write_file_atomic(s_path, s_pending_dir, buf, at))
        fprintf(stderr, "[SETTINGS] cannot write %s\n", s_path);
    free(buf);
}

void settings_set(const char *key, const char *value)
{
    char buf[MAX_LINE];
    int k = find(key);
    char *d;
    if (k >= 0 && !strcmp(value_of(k), value))
        return;
    snprintf(buf, sizeof buf, "%s=%s", key, value);
    if (!(d = dupn(buf, strlen(buf))))
        return;
    if (k >= 0) {
        free(s_line[k]);
        s_line[k] = d;
    } else if (s_n < MAX_LINES) {
        s_line[s_n++] = d;
    } else {
        free(d);
        return;
    }
    write_file();
    fprintf(stderr, "[SETTINGS] %s=%s\n", key, value);
}
