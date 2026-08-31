/*
 * log - levelled logging to one or more sinks.
 *
 * What DESIGN_DOC.md section 20 asks for: LOG_INFO and friends, a level that
 * can be raised or lowered, and backends that can be changed or switched off.
 *
 *     #define LOG_TAG "servo"        // before the include, optional
 *     #include "log.h"
 *
 *     LOG_INFO("bus at %lu baud", baud);
 *     LOG_WARN("timeout on id %u", id);
 *
 * Two filters, and the difference matters:
 *
 *   Compile-time  LOG_COMPILE_LEVEL removes calls below it entirely — no code,
 *                 and no format string left in flash. That last part is the
 *                 point on a part where strings are a real cost, and it means
 *                 a release build pays nothing for the trace logging that made
 *                 development bearable.
 *
 *   Runtime       log_set_level() filters what survives compilation, and each
 *                 sink has its own threshold on top. So a console can show
 *                 everything while a slower or scarcer sink takes only errors.
 *
 * Nothing here allocates. A message is formatted into a stack buffer of
 * LOG_LINE_LENGTH bytes and handed to each sink; a longer message is truncated
 * rather than split, since a log line that arrives in two pieces interleaved
 * with another is worse than one that is short.
 */

#ifndef PICO_FRAMEWORK_LOG_H
#define PICO_FRAMEWORK_LOG_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "log_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The compile-time filter has to be a preprocessor number, not the enum: `#if`
 * runs before the enum exists, so an enum name there silently evaluates to 0
 * and compiles in everything. These constants mirror log_level_t and the
 * assertions below stop the two drifting apart.
 */
#define LOG_TRACE_LEVEL 0
#define LOG_DEBUG_LEVEL 1
#define LOG_INFO_LEVEL  2
#define LOG_WARN_LEVEL  3
#define LOG_ERROR_LEVEL 4
#define LOG_NONE_LEVEL  5

/* Calls below this are compiled out. Set it in a profile. */
#ifndef LOG_COMPILE_LEVEL
#define LOG_COMPILE_LEVEL LOG_DEBUG_LEVEL
#endif

/* Longest line, prefix included. */
#ifndef LOG_LINE_LENGTH
#define LOG_LINE_LENGTH 160u
#endif

#ifndef LOG_MAX_SINKS
#define LOG_MAX_SINKS 4u
#endif

/*
 * Where a line goes. The same shape as cli_stream_t's write, so a CLI
 * transport can be used as a sink with no adapter.
 */
typedef void (*log_sink_fn)(void *ctx, const char *text, size_t length);

/* Discard every sink and set the runtime level. */
void log_init(log_level_t level);

/*
 * Add a destination. `min_level` is its own threshold, applied on top of the
 * global one, so a scarce sink can take only errors while a console takes
 * everything.
 *
 * Returns false when LOG_MAX_SINKS are already registered.
 */
bool log_add_sink(log_sink_fn write, void *ctx, log_level_t min_level);

/* Everything the framework's own stdio goes to: USB CDC, the default UART, or
   both, as the build chose. */
bool log_add_stdio_sink(log_level_t min_level);

/*
 * Keep the most recent output in a ring buffer instead of, or as well as,
 * printing it. Worth having on a robot: the interesting lines are the ones just
 * before something went wrong, and they are no use if the console was not
 * being watched. `buffer` is caller-owned.
 */
bool log_add_memory_sink(uint8_t *buffer, size_t capacity, log_level_t min_level);

/* Copy out what the memory sink holds, oldest first. Returns the byte count. */
size_t log_read_memory(char *out, size_t capacity);

/* Discard what the memory sink holds. */
void log_clear_memory(void);

void log_set_level(log_level_t level);
log_level_t log_get_level(void);

/* Emit a line. Prefer the macros, which handle the compile-time filter. */
void log_write(log_level_t level, const char *tag, const char *format, ...)
    __attribute__((format(printf, 3, 4)));

void log_vwrite(log_level_t level, const char *tag, const char *format, va_list args);

/* ---------------------------------------------------------------------------
 * Macros
 *
 * Define LOG_TAG before including this to label a file's output; without one
 * the tag is omitted.
 * -------------------------------------------------------------------------*/

_Static_assert(LOG_TRACE_LEVEL == (int)LOG_LEVEL_TRACE &&
               LOG_DEBUG_LEVEL == (int)LOG_LEVEL_DEBUG &&
               LOG_INFO_LEVEL == (int)LOG_LEVEL_INFO &&
               LOG_WARN_LEVEL == (int)LOG_LEVEL_WARN &&
               LOG_ERROR_LEVEL == (int)LOG_LEVEL_ERROR &&
               LOG_NONE_LEVEL == (int)LOG_LEVEL_NONE,
               "the preprocessor level constants must match log_level_t");

#ifndef LOG_TAG
#define LOG_TAG NULL
#endif

#if LOG_COMPILE_LEVEL <= LOG_TRACE_LEVEL
#define LOG_TRACE(...) log_write(LOG_LEVEL_TRACE, LOG_TAG, __VA_ARGS__)
#else
#define LOG_TRACE(...) ((void)0)
#endif

#if LOG_COMPILE_LEVEL <= LOG_DEBUG_LEVEL
#define LOG_DEBUG(...) log_write(LOG_LEVEL_DEBUG, LOG_TAG, __VA_ARGS__)
#else
#define LOG_DEBUG(...) ((void)0)
#endif

#if LOG_COMPILE_LEVEL <= LOG_INFO_LEVEL
#define LOG_INFO(...) log_write(LOG_LEVEL_INFO, LOG_TAG, __VA_ARGS__)
#else
#define LOG_INFO(...) ((void)0)
#endif

#if LOG_COMPILE_LEVEL <= LOG_WARN_LEVEL
#define LOG_WARN(...) log_write(LOG_LEVEL_WARN, LOG_TAG, __VA_ARGS__)
#else
#define LOG_WARN(...) ((void)0)
#endif

#if LOG_COMPILE_LEVEL <= LOG_ERROR_LEVEL
#define LOG_ERROR(...) log_write(LOG_LEVEL_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOG_ERROR(...) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_LOG_H */
