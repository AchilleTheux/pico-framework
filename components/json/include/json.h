/*
 * json - reading and writing small JSON documents, without allocating.
 *
 * What a networked application needs and the framework did not have. `mqtt`
 * moves opaque bytes, which is the right shape for a transport, but almost
 * everything on the other end of a broker speaks JSON: Home Assistant's
 * discovery and light schemas, most REST-shaped APIs, and any device that
 * publishes a reading with a unit attached.
 *
 * SCOPE
 *
 * Deliberately not a general JSON library. There is no document object, no
 * allocation, and no round-tripping: reading is a scan over a buffer the
 * caller already holds, and writing appends text to a buffer the caller
 * already sized. That covers "pull four fields out of a command message" and
 * "build a status document" -- which is the whole of what firmware here does
 * with JSON -- in a couple of hundred bytes of stack and no heap at all.
 *
 * WHY NOT strstr()
 *
 * The obvious shortcut is to search for "\"brightness\"" and read the number
 * after it. It goes wrong quietly in three separate ways, all of which happen
 * in real payloads:
 *
 *   {"effect":"brightness test"}   the key appears inside a *value*
 *   {"color":{"r":10}}             a nested key matches at the top level
 *   {"brightness_scale":100}       one key is a prefix of another
 *
 * json_find() looks only at the keys of the object it is given, at that
 * object's own level, and matches them whole. Nested objects are stepped
 * over, not searched -- to read `color.r`, find "color" and then search the
 * value it returns.
 *
 * No Pico SDK dependency, so all of this is unit-tested on the host
 * (DESIGN_DOC.md section 19).
 */

#ifndef PICO_FRAMEWORK_JSON_H
#define PICO_FRAMEWORK_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * How far objects and arrays may nest before a document is rejected.
 *
 * The scanner walks nested containers to step over them, so an input with
 * thousands of open braces would otherwise be a stack overflow reachable from
 * anything that hands this a message off the network. Sixteen is far past
 * what any payload here has.
 */
#ifndef JSON_MAX_DEPTH
#define JSON_MAX_DEPTH 16u
#endif

typedef enum {
    JSON_TYPE_INVALID = 0,   /* not found, or malformed */
    JSON_TYPE_NULL,
    JSON_TYPE_BOOL,
    JSON_TYPE_NUMBER,
    JSON_TYPE_STRING,
    JSON_TYPE_OBJECT,
    JSON_TYPE_ARRAY,
} json_type_t;

/*
 * A value located inside the caller's buffer. `start` and `length` point into
 * that buffer and are valid exactly as long as it is; nothing is copied.
 *
 * For a string, `start` and `length` cover the text between the quotes, still
 * escaped. Use json_get_string() to get it unescaped into a buffer of your
 * own. For an object or array they cover the whole thing, braces included, so
 * the result can be passed straight back to json_find().
 */
typedef struct {
    json_type_t type;
    const char *start;
    size_t length;
} json_value_t;

/* ---------------------------------------------------------------------------
 * Reading
 * -------------------------------------------------------------------------*/

/*
 * Find `key` among the immediate members of the object at `json`, which need
 * not be zero-terminated if `length` bounds it -- see json_find_n().
 *
 * Returns false, leaving `out` as JSON_TYPE_INVALID, if the input is not an
 * object, is malformed, nests deeper than JSON_MAX_DEPTH, or simply has no
 * such key.
 *
 * The object is checked all the way to its closing brace before any member is
 * returned, so a document cut short in transit yields nothing rather than the
 * fields that happened to arrive first. A caller that wants to tell "absent"
 * from "malformed" apart can ask json_valid() as well; for the usual case --
 * apply the fields that are present, ignore the rest -- the single false is
 * what you want.
 */
bool json_find(const char *json, const char *key, json_value_t *out);

/* json_find() over a buffer that is not zero-terminated. */
bool json_find_n(const char *json, size_t length, const char *key,
                 json_value_t *out);

/* Find a member of an object already located, e.g. a nested "color". */
bool json_find_in(const json_value_t *object, const char *key,
                  json_value_t *out);

/*
 * Whole-document check. Scans one complete value and requires nothing but
 * whitespace after it.
 */
bool json_valid(const char *json);
bool json_valid_n(const char *json, size_t length);

/*
 * Read a number as an integer.
 *
 * JSON has one numeric type and no integers as such, so a fractional or
 * exponent part is accepted and truncated toward zero. Anything that does not
 * fit in an int32_t is rejected rather than wrapped, because a wrapped
 * brightness is worse than a missing one.
 */
bool json_get_int(const json_value_t *value, int32_t *out);

/* Read a boolean. `true` and `false` only -- 0, 1, "on" and "" are numbers or
   strings, and are not silently taken as booleans here. */
bool json_get_bool(const json_value_t *value, bool *out);

/*
 * Copy a string out, resolving the escapes JSON defines, and zero-terminate
 * it.
 *
 * `size` is the whole buffer including the terminator. A value too long to
 * fit is rejected rather than truncated: a half-copied effect name matches
 * nothing anyway, and silently shortening it turns a bug into a mystery.
 * \\uXXXX is resolved for the ASCII range and rejected above it -- this
 * framework has no business guessing an encoding for the caller.
 */
bool json_get_string(const json_value_t *value, char *out, size_t size);

/* True when the value is a string equal to `expected` after unescaping.
   The common case of testing a small enumeration without a buffer. */
bool json_string_equals(const json_value_t *value, const char *expected);

/* How many elements an array has. Zero for anything that is not an array. */
size_t json_array_length(const json_value_t *array);

/* Locate one element by position. */
bool json_array_at(const json_value_t *array, size_t index, json_value_t *out);

/*
 * Read up to `count` integers out of an array, returning how many were
 * written. Fewer than `count` means the array was shorter, or an element was
 * not a number -- the shape "rgb_color":[255,128,0] in one call.
 */
size_t json_array_ints(const json_value_t *array, int32_t *out, size_t count);

/* ---------------------------------------------------------------------------
 * Writing
 * -------------------------------------------------------------------------*/

/*
 * An append-only writer over a caller-owned buffer.
 *
 * Nothing here returns an error. A write that does not fit sets a sticky
 * overflow flag and is dropped, so a document is built as a straight run of
 * calls and checked once at the end with json_writer_finish(). That keeps the
 * usual case -- a discovery payload with twenty fields -- from being twenty
 * ignored return values, which is how truncation gets missed in practice.
 *
 * Every value-writing call takes a `key`: pass the name inside an object, or
 * NULL inside an array or at the root.
 */
typedef struct {
    char *buffer;
    size_t size;         /* including room for the terminator */
    size_t length;       /* written so far, excluding the terminator */
    unsigned depth;
    bool needs_comma;    /* something has already been written at this level */
    bool overflowed;
    bool malformed;      /* a close without a matching open, or too deep */
} json_writer_t;

void json_writer_init(json_writer_t *writer, char *buffer, size_t size);

void json_writer_object_open(json_writer_t *writer, const char *key);
void json_writer_object_close(json_writer_t *writer);
void json_writer_array_open(json_writer_t *writer, const char *key);
void json_writer_array_close(json_writer_t *writer);

void json_writer_int(json_writer_t *writer, const char *key, int32_t value);
void json_writer_bool(json_writer_t *writer, const char *key, bool value);
void json_writer_null(json_writer_t *writer, const char *key);

/* The value is escaped on the way in, so it may contain quotes and
   backslashes. */
void json_writer_string(json_writer_t *writer, const char *key, const char *value);

/*
 * Insert already-formatted JSON verbatim -- a value built elsewhere, or a
 * constant fragment not worth reassembling field by field. Nothing checks
 * it; a caller passing text that is not valid JSON produces a document that
 * is not either.
 */
void json_writer_raw(json_writer_t *writer, const char *key, const char *json);

/*
 * Zero-terminate and report whether the document is sound: everything fit,
 * and every object and array was closed.
 *
 * The buffer is left zero-terminated either way when it has room for a
 * terminator at all, so a failed document can still be logged to see how far
 * it got.
 */
bool json_writer_finish(json_writer_t *writer);

/* Bytes written so far, excluding the terminator. Useful as the length
   argument to mqtt_publish_message() after a successful finish. */
static inline size_t json_writer_length(const json_writer_t *writer)
{
    return writer->length;
}

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_JSON_H */
