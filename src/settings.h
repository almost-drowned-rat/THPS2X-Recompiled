/*
 * settings.h -- the port's settings file, saves\settings.ini (CLAUDE.md:
 * "one settings file in saves/", shared by every M6 option).
 *
 * Format: one "key=value" per line, UTF-8; lines starting with # or ; and
 * unknown keys are kept as they are. Written atomically like the saves: the
 * new file goes to saves\.pending\ first and replaces the old one in one
 * move (MoveFileEx REPLACE_EXISTING | WRITE_THROUGH), so an interrupted
 * write leaves the previous file. Every setting can be overridden for one
 * run with THPS_<KEY> (key upper-cased), e.g. THPS_SOUNDTRACK_MODE; an
 * override is not written back.
 */
#ifndef THPS_SETTINGS_H
#define THPS_SETTINGS_H

void        settings_init(const char *save_dir);
/* The value (env override first), or def when unset. */
const char *settings_get(const char *key, const char *def);
/* Sets and writes the file at once (no-op when the value is unchanged). */
void        settings_set(const char *key, const char *value);

#endif
