#include "json.h"

/* ---------------------------------------------------------------------------
 * Scanning
 *
 * One cursor walks the buffer. Every helper takes and returns a position, and
 * returns NULL for "this is not what the grammar allows here", so a malformed
 * document falls out of the whole scan rather than being half-interpreted.
 * -------------------------------------------------------------------------*/

typedef struct {
    const char *end;   /* one past the last byte of the document */
} scan_t;

static const char *skip_whitespace(const char *p, const scan_t *s)
{
    while (p < s->end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
        p++;
    }
    return p;
}

/* Past the closing quote of a string whose opening quote is at `p`. */
static const char *skip_string(const char *p, const scan_t *s)
{
    if (p >= s->end || *p != '"') {
        return NULL;
    }
    p++;

    while (p < s->end) {
        if (*p == '\\') {
            /* Whatever follows an escape cannot itself close the string, and
               that includes a \" -- which is the entire reason a scan cannot
               just look for the next quote. */
            p += 2;
            continue;
        }
        if (*p == '"') {
            return p + 1;
        }
        p++;
    }
    return NULL;
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static const char *skip_number(const char *p, const scan_t *s)
{
    const char *start = p;

    if (p < s->end && (*p == '-' || *p == '+')) {
        p++;
    }
    while (p < s->end && is_digit(*p)) {
        p++;
    }
    if (p < s->end && *p == '.') {
        p++;
        while (p < s->end && is_digit(*p)) {
            p++;
        }
    }
    if (p < s->end && (*p == 'e' || *p == 'E')) {
        p++;
        if (p < s->end && (*p == '-' || *p == '+')) {
            p++;
        }
        while (p < s->end && is_digit(*p)) {
            p++;
        }
    }

    /* A lone sign or decimal point is not a number. */
    return (p > start && (is_digit(p[-1]))) ? p : NULL;
}

static const char *skip_literal(const char *p, const scan_t *s, const char *word)
{
    size_t i = 0;

    while (word[i] != '\0') {
        if (p + i >= s->end || p[i] != word[i]) {
            return NULL;
        }
        i++;
    }
    return p + i;
}

static const char *skip_value(const char *p, const scan_t *s, unsigned depth);

/* Past the closing brace or bracket of a container opening at `p`. */
static const char *skip_container(const char *p, const scan_t *s, unsigned depth)
{
    const char open = *p;
    const char close = (open == '{') ? '}' : ']';

    if (depth >= JSON_MAX_DEPTH) {
        return NULL;
    }

    p = skip_whitespace(p + 1, s);
    if (p < s->end && *p == close) {
        return p + 1;   /* empty */
    }

    for (;;) {
        if (open == '{') {
            p = skip_string(skip_whitespace(p, s), s);
            if (p == NULL) {
                return NULL;
            }
            p = skip_whitespace(p, s);
            if (p >= s->end || *p != ':') {
                return NULL;
            }
            p++;
        }

        p = skip_value(skip_whitespace(p, s), s, depth + 1);
        if (p == NULL) {
            return NULL;
        }

        p = skip_whitespace(p, s);
        if (p >= s->end) {
            return NULL;
        }
        if (*p == close) {
            return p + 1;
        }
        if (*p != ',') {
            return NULL;
        }
        p = skip_whitespace(p + 1, s);
    }
}

static const char *skip_value(const char *p, const scan_t *s, unsigned depth)
{
    if (p >= s->end) {
        return NULL;
    }

    switch (*p) {
        case '"':  return skip_string(p, s);
        case '{':  /* fall through */
        case '[':  return skip_container(p, s, depth);
        case 't':  return skip_literal(p, s, "true");
        case 'f':  return skip_literal(p, s, "false");
        case 'n':  return skip_literal(p, s, "null");
        default:   return skip_number(p, s);
    }
}

static json_type_t type_of(char c)
{
    switch (c) {
        case '"':  return JSON_TYPE_STRING;
        case '{':  return JSON_TYPE_OBJECT;
        case '[':  return JSON_TYPE_ARRAY;
        case 't':  /* fall through */
        case 'f':  return JSON_TYPE_BOOL;
        case 'n':  return JSON_TYPE_NULL;
        default:   return JSON_TYPE_NUMBER;
    }
}

static void set_invalid(json_value_t *out)
{
    out->type = JSON_TYPE_INVALID;
    out->start = NULL;
    out->length = 0;
}

/* ---------------------------------------------------------------------------
 * Unescaping
 * -------------------------------------------------------------------------*/

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/*
 * Walk an escaped string body, writing the resolved bytes to `out` when it is
 * non-NULL, and return the unescaped length, or (size_t)-1 if an escape is
 * malformed or outside what this resolves.
 *
 * Passing out = NULL is how json_string_equals() compares without a buffer.
 */
static size_t unescape(const char *p, size_t length, char *out, size_t capacity)
{
    size_t written = 0;
    size_t i = 0;

    while (i < length) {
        char decoded;

        if (p[i] != '\\') {
            decoded = p[i];
            i++;
        } else {
            i++;
            if (i >= length) {
                return (size_t)-1;
            }
            switch (p[i]) {
                case '"':  decoded = '"';  i++; break;
                case '\\': decoded = '\\'; i++; break;
                case '/':  decoded = '/';  i++; break;
                case 'b':  decoded = '\b'; i++; break;
                case 'f':  decoded = '\f'; i++; break;
                case 'n':  decoded = '\n'; i++; break;
                case 'r':  decoded = '\r'; i++; break;
                case 't':  decoded = '\t'; i++; break;
                case 'u': {
                    if (i + 4 >= length) {
                        return (size_t)-1;
                    }
                    int value = 0;
                    for (size_t k = 1; k <= 4; k++) {
                        const int digit = hex_digit(p[i + k]);
                        if (digit < 0) {
                            return (size_t)-1;
                        }
                        value = (value << 4) | digit;
                    }
                    /* Anything above ASCII needs an encoding decision that
                       belongs to the caller, not to a JSON scanner. */
                    if (value > 0x7F) {
                        return (size_t)-1;
                    }
                    decoded = (char)value;
                    i += 5;
                    break;
                }
                default:
                    return (size_t)-1;
            }
        }

        if (out != NULL) {
            if (written >= capacity) {
                return (size_t)-1;
            }
            out[written] = decoded;
        }
        written++;
    }

    return written;
}

/* How many bytes of `p` the escape sequence starting there occupies. */
static size_t escape_width(const char *p, size_t remaining)
{
    if (p[0] != '\\') {
        return 1u;
    }
    if (remaining < 2u) {
        return remaining;   /* truncated; the caller rejects it */
    }
    return (p[1] == 'u') ? 6u : 2u;
}

/*
 * Compare an escaped string body against a plain one, without a buffer.
 *
 * Both key matching and value comparison need exactly this, and neither can
 * afford to size a buffer to the input: a key is whatever the sender chose,
 * and an over-long one should fail to match, not fail to be examined. So the
 * body is unescaped one character at a time into a single byte of scratch.
 */
static bool escaped_equals(const char *body, size_t length, const char *expected,
                           size_t expected_length)
{
    size_t i = 0;
    size_t j = 0;

    while (i < length) {
        const size_t width = escape_width(body + i, length - i);
        char decoded;

        if (width == 0u || i + width > length) {
            return false;
        }
        if (unescape(body + i, width, &decoded, 1) != 1u) {
            return false;
        }
        if (j >= expected_length || expected[j] != decoded) {
            return false;
        }
        i += width;
        j++;
    }

    return j == expected_length;
}

/* ---------------------------------------------------------------------------
 * Reading
 * -------------------------------------------------------------------------*/

static size_t string_length(const char *s)
{
    size_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

bool json_find_n(const char *json, size_t length, const char *key,
                 json_value_t *out)
{
    if (out == NULL) {
        return false;
    }
    set_invalid(out);

    if (json == NULL || key == NULL) {
        return false;
    }

    const scan_t scan = { .end = json + length };
    const char *p = skip_whitespace(json, &scan);

    if (p >= scan.end || *p != '{') {
        return false;
    }

    /*
     * Check the object closes before believing anything inside it.
     *
     * The search below stops at the first matching key, so without this a
     * truncated document -- a message cut short in transit, a buffer that
     * overflowed upstream -- would hand back every field that happened to
     * arrive before the cut and report success. Half a command applied as if
     * it were whole is the precise failure this component exists to avoid, so
     * it costs one extra pass over an object that is at most a few hundred
     * bytes here.
     */
    if (skip_container(p, &scan, 0) == NULL) {
        return false;
    }

    p = skip_whitespace(p + 1, &scan);
    if (p < scan.end && *p == '}') {
        return false;   /* empty object: no keys to match */
    }

    const size_t key_length = string_length(key);

    for (;;) {
        const char *name = skip_whitespace(p, &scan);
        const char *name_end = skip_string(name, &scan);
        if (name_end == NULL) {
            return false;
        }

        p = skip_whitespace(name_end, &scan);
        if (p >= scan.end || *p != ':') {
            return false;
        }

        const char *value = skip_whitespace(p + 1, &scan);
        const char *value_end = skip_value(value, &scan, 1);
        if (value_end == NULL) {
            return false;
        }

        /*
         * Match the key whole and unescaped. Whole is what stops "brightness"
         * from matching "brightness_scale"; only comparing against keys --
         * never against the document as a whole -- is what stops it from
         * matching the word inside somebody's effect name.
         */
        const size_t name_length = (size_t)(name_end - name) - 2u;

        if (escaped_equals(name + 1, name_length, key, key_length)) {
            out->type = type_of(*value);
            out->start = value;
            out->length = (size_t)(value_end - value);
            return true;
        }

        p = skip_whitespace(value_end, &scan);
        if (p >= scan.end || *p == '}') {
            return false;
        }
        if (*p != ',') {
            return false;
        }
        p++;
    }
}

bool json_find(const char *json, const char *key, json_value_t *out)
{
    if (json == NULL) {
        if (out != NULL) {
            set_invalid(out);
        }
        return false;
    }
    return json_find_n(json, string_length(json), key, out);
}

bool json_find_in(const json_value_t *object, const char *key, json_value_t *out)
{
    if (object == NULL || object->type != JSON_TYPE_OBJECT) {
        if (out != NULL) {
            set_invalid(out);
        }
        return false;
    }
    return json_find_n(object->start, object->length, key, out);
}

bool json_valid_n(const char *json, size_t length)
{
    if (json == NULL) {
        return false;
    }

    const scan_t scan = { .end = json + length };
    const char *p = skip_value(skip_whitespace(json, &scan), &scan, 0);

    if (p == NULL) {
        return false;
    }
    return skip_whitespace(p, &scan) == scan.end;
}

bool json_valid(const char *json)
{
    return json != NULL && json_valid_n(json, string_length(json));
}

bool json_get_int(const json_value_t *value, int32_t *out)
{
    if (value == NULL || out == NULL || value->type != JSON_TYPE_NUMBER ||
        value->length == 0) {
        return false;
    }

    const char *p = value->start;
    const char *end = value->start + value->length;
    bool negative = false;

    if (*p == '-') {
        negative = true;
        p++;
    } else if (*p == '+') {
        p++;
    }

    /*
     * Accumulate in 64 bits and bail the moment the magnitude passes what an
     * int32_t holds, rather than wrapping. A wrapped brightness would be
     * applied as if it were a real one.
     */
    int64_t magnitude = 0;
    bool any = false;

    while (p < end && is_digit(*p)) {
        magnitude = magnitude * 10 + (*p - '0');
        if (magnitude > 2147483648LL) {
            return false;
        }
        any = true;
        p++;
    }
    if (!any) {
        return false;
    }

    /* A fractional or exponent part truncates toward zero. Anything else is
       not a number this located in the first place. */
    if (p < end && (*p == '.' || *p == 'e' || *p == 'E')) {
        if (*p == 'e' || *p == 'E') {
            return false;   /* scale would change the value; refuse to guess */
        }
        p = end;
    }
    if (p != end) {
        return false;
    }

    if (negative) {
        if (magnitude > 2147483648LL) {
            return false;
        }
        *out = (int32_t)(-magnitude);
    } else {
        if (magnitude > 2147483647LL) {
            return false;
        }
        *out = (int32_t)magnitude;
    }
    return true;
}

bool json_get_bool(const json_value_t *value, bool *out)
{
    if (value == NULL || out == NULL || value->type != JSON_TYPE_BOOL) {
        return false;
    }
    *out = (value->start[0] == 't');
    return true;
}

bool json_get_string(const json_value_t *value, char *out, size_t size)
{
    if (value == NULL || out == NULL || size == 0 ||
        value->type != JSON_TYPE_STRING || value->length < 2) {
        return false;
    }

    const size_t body = value->length - 2u;
    const size_t written = unescape(value->start + 1, body, out, size - 1u);

    if (written == (size_t)-1) {
        return false;
    }
    out[written] = '\0';
    return true;
}

bool json_string_equals(const json_value_t *value, const char *expected)
{
    if (value == NULL || expected == NULL || value->type != JSON_TYPE_STRING ||
        value->length < 2) {
        return false;
    }

    return escaped_equals(value->start + 1, value->length - 2u, expected,
                          string_length(expected));
}

size_t json_array_length(const json_value_t *array)
{
    if (array == NULL || array->type != JSON_TYPE_ARRAY) {
        return 0;
    }

    const scan_t scan = { .end = array->start + array->length };
    const char *p = skip_whitespace(array->start + 1, &scan);
    size_t count = 0;

    if (p < scan.end && *p == ']') {
        return 0;
    }

    for (;;) {
        const char *value_end = skip_value(p, &scan, 1);
        if (value_end == NULL) {
            return count;
        }
        count++;

        p = skip_whitespace(value_end, &scan);
        if (p >= scan.end || *p != ',') {
            return count;
        }
        p = skip_whitespace(p + 1, &scan);
    }
}

bool json_array_at(const json_value_t *array, size_t index, json_value_t *out)
{
    if (out == NULL) {
        return false;
    }
    set_invalid(out);

    if (array == NULL || array->type != JSON_TYPE_ARRAY) {
        return false;
    }

    const scan_t scan = { .end = array->start + array->length };
    const char *p = skip_whitespace(array->start + 1, &scan);
    size_t position = 0;

    if (p < scan.end && *p == ']') {
        return false;
    }

    for (;;) {
        const char *value_end = skip_value(p, &scan, 1);
        if (value_end == NULL) {
            return false;
        }

        if (position == index) {
            out->type = type_of(*p);
            out->start = p;
            out->length = (size_t)(value_end - p);
            return true;
        }
        position++;

        p = skip_whitespace(value_end, &scan);
        if (p >= scan.end || *p != ',') {
            return false;
        }
        p = skip_whitespace(p + 1, &scan);
    }
}

size_t json_array_ints(const json_value_t *array, int32_t *out, size_t count)
{
    if (out == NULL) {
        return 0;
    }

    for (size_t i = 0; i < count; i++) {
        json_value_t element;

        if (!json_array_at(array, i, &element) ||
            !json_get_int(&element, &out[i])) {
            return i;
        }
    }
    return count;
}

/* ---------------------------------------------------------------------------
 * Writing
 * -------------------------------------------------------------------------*/

void json_writer_init(json_writer_t *writer, char *buffer, size_t size)
{
    if (writer == NULL) {
        return;
    }

    *writer = (json_writer_t){
        .buffer = buffer,
        .size = (buffer != NULL) ? size : 0,
        .length = 0,
        .depth = 0,
        .needs_comma = false,
        .overflowed = (buffer == NULL || size == 0),
        .malformed = false,
    };
}

static void put(json_writer_t *writer, char c)
{
    /* One byte is always held back for the terminator, so json_writer_finish()
       can zero-terminate whatever did fit. */
    if (writer->overflowed || writer->length + 1u >= writer->size) {
        writer->overflowed = true;
        return;
    }
    writer->buffer[writer->length++] = c;
}

static void put_text(json_writer_t *writer, const char *text)
{
    for (size_t i = 0; text[i] != '\0'; i++) {
        put(writer, text[i]);
    }
}

static void put_escaped(json_writer_t *writer, const char *text)
{
    static const char hex[] = "0123456789abcdef";

    for (size_t i = 0; text[i] != '\0'; i++) {
        const unsigned char c = (unsigned char)text[i];

        switch (c) {
            case '"':  put_text(writer, "\\\""); break;
            case '\\': put_text(writer, "\\\\"); break;
            case '\b': put_text(writer, "\\b");  break;
            case '\f': put_text(writer, "\\f");  break;
            case '\n': put_text(writer, "\\n");  break;
            case '\r': put_text(writer, "\\r");  break;
            case '\t': put_text(writer, "\\t");  break;
            default:
                if (c < 0x20u) {
                    /* Control characters are not allowed raw in a JSON
                       string, and a broker or parser on the other end is
                       entitled to reject the whole document over one. */
                    put_text(writer, "\\u00");
                    put(writer, hex[(c >> 4) & 0xFu]);
                    put(writer, hex[c & 0xFu]);
                } else {
                    put(writer, (char)c);
                }
                break;
        }
    }
}

/* Comma if this level already holds something, then the key if there is one. */
static void put_separator(json_writer_t *writer, const char *key)
{
    if (writer->needs_comma) {
        put(writer, ',');
    }
    if (key != NULL) {
        put(writer, '"');
        put_escaped(writer, key);
        put_text(writer, "\":");
    }
    writer->needs_comma = true;
}

static void open_container(json_writer_t *writer, const char *key, char brace)
{
    if (writer == NULL) {
        return;
    }
    if (writer->depth >= JSON_MAX_DEPTH) {
        writer->malformed = true;
        return;
    }
    put_separator(writer, key);
    put(writer, brace);
    writer->depth++;

    /* A freshly opened container has no member yet, so the next thing written
       into it must not be preceded by a comma. */
    writer->needs_comma = false;
}

static void close_container(json_writer_t *writer, char brace)
{
    if (writer == NULL) {
        return;
    }
    if (writer->depth == 0) {
        writer->malformed = true;
        return;
    }
    put(writer, brace);
    writer->depth--;

    /* Whatever follows this container at the parent's level does need one. */
    writer->needs_comma = true;
}

void json_writer_object_open(json_writer_t *writer, const char *key)
{
    open_container(writer, key, '{');
}

void json_writer_object_close(json_writer_t *writer)
{
    close_container(writer, '}');
}

void json_writer_array_open(json_writer_t *writer, const char *key)
{
    open_container(writer, key, '[');
}

void json_writer_array_close(json_writer_t *writer)
{
    close_container(writer, ']');
}

void json_writer_int(json_writer_t *writer, const char *key, int32_t value)
{
    if (writer == NULL) {
        return;
    }
    put_separator(writer, key);

    /* -2147483648 is ten digits, a sign and a terminator. Negating it would
       overflow, so the magnitude is taken in 64 bits. */
    char digits[12];
    size_t n = 0;
    int64_t magnitude = value;

    if (magnitude < 0) {
        put(writer, '-');
        magnitude = -magnitude;
    }
    do {
        digits[n++] = (char)('0' + (magnitude % 10));
        magnitude /= 10;
    } while (magnitude != 0);

    while (n > 0) {
        put(writer, digits[--n]);
    }
}

void json_writer_bool(json_writer_t *writer, const char *key, bool value)
{
    if (writer == NULL) {
        return;
    }
    put_separator(writer, key);
    put_text(writer, value ? "true" : "false");
}

void json_writer_null(json_writer_t *writer, const char *key)
{
    if (writer == NULL) {
        return;
    }
    put_separator(writer, key);
    put_text(writer, "null");
}

void json_writer_string(json_writer_t *writer, const char *key, const char *value)
{
    if (writer == NULL) {
        return;
    }
    put_separator(writer, key);

    if (value == NULL) {
        put_text(writer, "null");
        return;
    }
    put(writer, '"');
    put_escaped(writer, value);
    put(writer, '"');
}

void json_writer_raw(json_writer_t *writer, const char *key, const char *json)
{
    if (writer == NULL) {
        return;
    }
    put_separator(writer, key);
    put_text(writer, (json != NULL) ? json : "null");
}

bool json_writer_finish(json_writer_t *writer)
{
    if (writer == NULL) {
        return false;
    }
    if (writer->buffer != NULL && writer->size > 0) {
        const size_t at = (writer->length < writer->size) ? writer->length
                                                          : writer->size - 1u;
        writer->buffer[at] = '\0';
    }
    return !writer->overflowed && !writer->malformed && writer->depth == 0;
}
