#include <stdio.h>
#include <string.h>

#include "log_format.h"

static const char *const g_names[] = {
    "trace", "debug", "info", "warn", "error", "none",
};

static bool level_is_valid(log_level_t level)
{
    return (unsigned)level < (sizeof(g_names) / sizeof(g_names[0]));
}

const char *log_level_name(log_level_t level)
{
    return level_is_valid(level) ? g_names[level] : "unknown";
}

char log_level_initial(log_level_t level)
{
    /* Uppercase, so the letter stands out from the lowercase message text. */
    static const char initials[] = { 'T', 'D', 'I', 'W', 'E', '-' };
    return level_is_valid(level) ? initials[level] : '?';
}

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

bool log_level_parse(const char *text, log_level_t *level)
{
    if (text == NULL || level == NULL || text[0] == '\0') {
        return false;
    }

    for (unsigned i = 0; i < sizeof(g_names) / sizeof(g_names[0]); i++) {
        /* A single letter is accepted too, so "w" works as well as "warn" —
           worth having on a console where every character is typed. */
        if (text[1] == '\0' && lower(text[0]) == g_names[i][0]) {
            *level = (log_level_t)i;
            return true;
        }

        const char *name = g_names[i];
        size_t at = 0;
        while (text[at] != '\0' && name[at] != '\0' &&
               lower(text[at]) == name[at]) {
            at++;
        }
        if (text[at] == '\0' && name[at] == '\0') {
            *level = (log_level_t)i;
            return true;
        }
    }
    return false;
}

size_t log_format_prefix(char *out, size_t capacity, log_level_t level,
                         uint64_t timestamp_us, const char *tag)
{
    if (out == NULL || capacity == 0) {
        return 0;
    }

    const uint64_t total_ms = timestamp_us / 1000u;
    const uint64_t seconds = total_ms / 1000u;
    const unsigned milliseconds = (unsigned)(total_ms % 1000u);

    /*
     * snprintf rather than hand-rolled formatting: it is already linked in for
     * the message itself, and it truncates rather than overruns. This is called
     * from paths that are already going wrong, so a truncated prefix is a much
     * better failure than a corrupted stack.
     */
    int written;
    if (tag != NULL && tag[0] != '\0') {
        written = snprintf(out, capacity, "[%6llu.%03u] %c %s: ",
                           (unsigned long long)seconds, milliseconds,
                           log_level_initial(level), tag);
    } else {
        written = snprintf(out, capacity, "[%6llu.%03u] %c ",
                           (unsigned long long)seconds, milliseconds,
                           log_level_initial(level));
    }

    if (written < 0) {
        out[0] = '\0';
        return 0;
    }
    /* snprintf reports what it would have written, so clamp to what it did. */
    return ((size_t)written < capacity) ? (size_t)written : capacity - 1u;
}
