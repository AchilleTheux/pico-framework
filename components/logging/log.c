#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "ring_buffer.h"

#include "log.h"

typedef struct {
    log_sink_fn write;
    void *ctx;
    log_level_t min_level;
} sink_t;

static struct {
    sink_t sinks[LOG_MAX_SINKS];
    size_t sink_count;
    log_level_t level;
    bool initialised;

    ring_buffer_t memory;
    bool memory_ready;
} g_log;

void log_init(log_level_t level)
{
    memset(&g_log, 0, sizeof(g_log));
    g_log.level = level;
    g_log.initialised = true;
}

bool log_add_sink(log_sink_fn write, void *ctx, log_level_t min_level)
{
    if (!g_log.initialised || write == NULL || g_log.sink_count >= LOG_MAX_SINKS) {
        return false;
    }

    g_log.sinks[g_log.sink_count++] = (sink_t){
        .write = write,
        .ctx = ctx,
        .min_level = min_level,
    };
    return true;
}

void log_set_level(log_level_t level)
{
    g_log.level = level;
}

log_level_t log_get_level(void)
{
    return g_log.level;
}

/* ---------------------------------------------------------------------------
 * Sinks
 * -------------------------------------------------------------------------*/

static void stdio_sink(void *ctx, const char *text, size_t length)
{
    (void)ctx;

    /* Not fwrite or puts: the text is not NUL-terminated at `length` and may
       contain any byte a format string produced. */
    for (size_t i = 0; i < length; i++) {
        putchar_raw(text[i]);
    }
}

bool log_add_stdio_sink(log_level_t min_level)
{
    return log_add_sink(stdio_sink, NULL, min_level);
}

static void memory_sink(void *ctx, const char *text, size_t length)
{
    (void)ctx;

    /*
     * Oldest output is dropped to make room, rather than the newest being
     * refused. A log that stops recording once it fills would keep the least
     * interesting lines and lose the ones just before the fault.
     */
    while (ring_buffer_free(&g_log.memory) < length) {
        uint8_t discard;
        if (!ring_buffer_pop(&g_log.memory, &discard)) {
            break;
        }
    }
    ring_buffer_write(&g_log.memory, text, length);
}

bool log_add_memory_sink(uint8_t *buffer, size_t capacity, log_level_t min_level)
{
    if (!ring_buffer_init(&g_log.memory, buffer, capacity)) {
        return false;
    }
    if (!log_add_sink(memory_sink, NULL, min_level)) {
        return false;
    }
    g_log.memory_ready = true;
    return true;
}

size_t log_read_memory(char *out, size_t capacity)
{
    if (!g_log.memory_ready || out == NULL || capacity == 0) {
        return 0;
    }
    return ring_buffer_read(&g_log.memory, out, capacity);
}

void log_clear_memory(void)
{
    if (g_log.memory_ready) {
        ring_buffer_clear(&g_log.memory);
    }
}

/* ---------------------------------------------------------------------------
 * Emitting
 * -------------------------------------------------------------------------*/

void log_vwrite(log_level_t level, const char *tag, const char *format, va_list args)
{
    if (!g_log.initialised || g_log.sink_count == 0) {
        return;
    }
    if (!log_level_enabled(level, g_log.level)) {
        return;
    }

    char line[LOG_LINE_LENGTH];
    size_t at = log_format_prefix(line, sizeof(line), level, time_us_64(), tag);

    const int written = vsnprintf(&line[at], sizeof(line) - at, format, args);
    if (written > 0) {
        /* vsnprintf reports what it would have written, so a long message is
           truncated here rather than being allowed to run past the buffer. */
        const size_t room = sizeof(line) - at - 1u;
        at += ((size_t)written < room) ? (size_t)written : room;
    }

    /* Every line ends CRLF, so output is readable on a terminal that does not
       translate. */
    if (at + 2u < sizeof(line)) {
        line[at++] = '\r';
        line[at++] = '\n';
    }

    for (size_t i = 0; i < g_log.sink_count; i++) {
        if (level >= g_log.sinks[i].min_level) {
            g_log.sinks[i].write(g_log.sinks[i].ctx, line, at);
        }
    }
}

void log_write(log_level_t level, const char *tag, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_vwrite(level, tag, format, args);
    va_end(args);
}
