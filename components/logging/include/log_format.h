/*
 * log_format - levels, and the text that goes in front of a message.
 *
 * The decisions a log makes on every call — is this level worth emitting, and
 * what prefix does it get — with no Pico SDK dependency, so they are
 * unit-tested on the host. log.h adds the sinks.
 *
 * A line looks like this:
 *
 *     [    12.345] W servo: timeout on id 3
 *      |           | |      |
 *      |           | |      the message
 *      |           | an optional tag, usually the component
 *      |           the level, one letter
 *      seconds since boot, to the millisecond
 *
 * The timestamp is right-aligned so lines stay in columns for the first eleven
 * hours of running, which is longer than anything this is likely to be watching.
 */

#ifndef PICO_FRAMEWORK_LOG_FORMAT_H
#define PICO_FRAMEWORK_LOG_FORMAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Ordered from most to least verbose, so a threshold is a simple comparison.
 * LOG_LEVEL_NONE is above every message level and therefore silences everything.
 */
typedef enum {
    LOG_LEVEL_TRACE = 0,
    LOG_LEVEL_DEBUG = 1,
    LOG_LEVEL_INFO = 2,
    LOG_LEVEL_WARN = 3,
    LOG_LEVEL_ERROR = 4,
    LOG_LEVEL_NONE = 5,
} log_level_t;

/* Full name, for a command that prints or sets the level. Never NULL. */
const char *log_level_name(log_level_t level);

/* The single letter that goes in a line. '?' for a level out of range. */
char log_level_initial(log_level_t level);

/* Parse a name, case-insensitively; also accepts a single initial. Returns
   false for anything else, leaving `level` untouched. */
bool log_level_parse(const char *text, log_level_t *level);

/* Would a message at `level` be emitted against `threshold`? */
static inline bool log_level_enabled(log_level_t level, log_level_t threshold)
{
    return level >= threshold && level < LOG_LEVEL_NONE;
}

/*
 * Write the prefix into `out` and return its length, not counting the
 * terminator. Always NUL-terminates when capacity is non-zero, and never
 * writes past it — a truncated prefix is better than a corrupted stack, and
 * this runs from paths that are already going wrong.
 *
 * `tag` may be NULL, in which case it and its colon are omitted.
 */
size_t log_format_prefix(char *out, size_t capacity, log_level_t level,
                         uint64_t timestamp_us, const char *tag);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_LOG_FORMAT_H */
