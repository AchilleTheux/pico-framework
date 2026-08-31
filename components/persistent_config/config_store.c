#include <string.h>

#include "config_store.h"

/* Offsets within a record. */
#define KEY_LENGTH_AT 0u
#define VALUE_LENGTH_AT 1u
#define KEY_AT 2u

/* Reports which way a key is unusable, rather than lumping both into
   "invalid argument" — a key one character too long is a different mistake
   from a null one, and the caller can only fix it if told which. */
static config_result_t check_key(const char *key, uint8_t *length_out)
{
    if (key == NULL || key[0] == '\0') {
        return CONFIG_ERR_INVALID_ARG;
    }
    const size_t length = strlen(key);
    if (length > CONFIG_MAX_KEY_LENGTH) {
        return CONFIG_ERR_KEY_TOO_LONG;
    }
    *length_out = (uint8_t)length;
    return CONFIG_OK;
}

/* Walk to the record for `key`, returning its offset or the used size when
   absent. */
static bool find_record(const config_store_t *store, const char *key,
                        uint8_t key_length, size_t *offset_out)
{
    size_t at = 0;
    while (at < store->used) {
        const uint8_t record_key = store->buffer[at + KEY_LENGTH_AT];
        const uint8_t record_value = store->buffer[at + VALUE_LENGTH_AT];

        if (record_key == key_length &&
            memcmp(&store->buffer[at + KEY_AT], key, key_length) == 0) {
            *offset_out = at;
            return true;
        }
        at += CONFIG_RECORD_SIZE(record_key, record_value);
    }
    *offset_out = store->used;
    return false;
}

config_result_t config_store_init(config_store_t *store, uint8_t *buffer,
                                  size_t capacity)
{
    if (store == NULL || buffer == NULL || capacity == 0) {
        return CONFIG_ERR_INVALID_ARG;
    }

    *store = (config_store_t){
        .buffer = buffer,
        .capacity = capacity,
        .used = 0,
        .initialised = true,
    };
    return CONFIG_OK;
}

config_result_t config_store_load(config_store_t *store, uint8_t *buffer,
                                  size_t capacity, size_t used)
{
    const config_result_t started = config_store_init(store, buffer, capacity);
    if (started != CONFIG_OK) {
        return started;
    }
    if (used > capacity) {
        return CONFIG_ERR_CORRUPT;
    }

    /*
     * Every record is walked before any is trusted. A run that does not parse
     * exactly to `used` is rejected outright rather than partly adopted: a
     * truncated final record is precisely what an interrupted write leaves
     * behind, and half a configuration is worse than none.
     */
    size_t at = 0;
    while (at < used) {
        if (used - at < KEY_AT) {
            return CONFIG_ERR_CORRUPT;
        }
        const uint8_t key_length = buffer[at + KEY_LENGTH_AT];
        const uint8_t value_length = buffer[at + VALUE_LENGTH_AT];

        if (key_length == 0 || key_length > CONFIG_MAX_KEY_LENGTH) {
            return CONFIG_ERR_CORRUPT;
        }
        const size_t size = CONFIG_RECORD_SIZE(key_length, value_length);
        if (size > used - at) {
            return CONFIG_ERR_CORRUPT;
        }
        at += size;
    }

    store->used = used;
    return CONFIG_OK;
}

config_result_t config_set(config_store_t *store, const char *key,
                           const void *value, uint8_t length)
{
    if (store == NULL || !store->initialised) {
        return CONFIG_ERR_INVALID_ARG;
    }

    uint8_t key_length;
    const config_result_t key_result = check_key(key, &key_length);
    if (key_result != CONFIG_OK) {
        return key_result;
    }
    if (value == NULL && length > 0) {
        return CONFIG_ERR_INVALID_ARG;
    }

    size_t offset;
    const bool exists = find_record(store, key, key_length, &offset);
    const size_t wanted = CONFIG_RECORD_SIZE(key_length, length);

    if (exists) {
        const size_t existing = CONFIG_RECORD_SIZE(store->buffer[offset + KEY_LENGTH_AT],
                                                   store->buffer[offset + VALUE_LENGTH_AT]);
        /* Room is checked against the difference, so growing a value by one
           byte does not need room for a whole second copy. */
        if (wanted > existing && wanted - existing > config_store_free(store)) {
            return CONFIG_ERR_FULL;
        }

        /* Close the gap or open one, then write the record where it belongs.
           The tail moves rather than the record being appended, so the buffer
           stays a single valid image at all times. */
        const size_t tail_at = offset + existing;
        const size_t tail = store->used - tail_at;
        memmove(&store->buffer[offset + wanted], &store->buffer[tail_at], tail);
        store->used = store->used - existing + wanted;
    } else {
        if (wanted > config_store_free(store)) {
            return CONFIG_ERR_FULL;
        }
        store->used += wanted;
    }

    store->buffer[offset + KEY_LENGTH_AT] = key_length;
    store->buffer[offset + VALUE_LENGTH_AT] = length;
    memcpy(&store->buffer[offset + KEY_AT], key, key_length);
    if (length > 0) {
        memcpy(&store->buffer[offset + KEY_AT + key_length], value, length);
    }

    return CONFIG_OK;
}

config_result_t config_get(config_store_t *store, const char *key,
                          void *value, uint8_t capacity, uint8_t *length)
{
    if (store == NULL || !store->initialised) {
        return CONFIG_ERR_INVALID_ARG;
    }

    uint8_t key_length;
    const config_result_t key_result = check_key(key, &key_length);
    if (key_result != CONFIG_OK) {
        return key_result;
    }

    size_t offset;
    if (!find_record(store, key, key_length, &offset)) {
        if (length != NULL) {
            *length = 0;
        }
        return CONFIG_ERR_NOT_FOUND;
    }

    const uint8_t value_length = store->buffer[offset + VALUE_LENGTH_AT];
    if (length != NULL) {
        *length = value_length;
    }

    /* The length is reported even when the value does not fit, so a caller can
       size a buffer and try again rather than guess. */
    if (value_length > capacity) {
        return CONFIG_ERR_TOO_SMALL;
    }
    if (value != NULL && value_length > 0) {
        memcpy(value, &store->buffer[offset + KEY_AT + key_length], value_length);
    }
    return CONFIG_OK;
}

config_result_t config_remove(config_store_t *store, const char *key)
{
    if (store == NULL || !store->initialised) {
        return CONFIG_ERR_INVALID_ARG;
    }

    uint8_t key_length;
    const config_result_t key_result = check_key(key, &key_length);
    if (key_result != CONFIG_OK) {
        return key_result;
    }

    size_t offset;
    if (!find_record(store, key, key_length, &offset)) {
        return CONFIG_ERR_NOT_FOUND;
    }

    const size_t size = CONFIG_RECORD_SIZE(store->buffer[offset + KEY_LENGTH_AT],
                                            store->buffer[offset + VALUE_LENGTH_AT]);
    const size_t tail = store->used - offset - size;
    memmove(&store->buffer[offset], &store->buffer[offset + size], tail);
    store->used -= size;
    return CONFIG_OK;
}

bool config_has(config_store_t *store, const char *key)
{
    uint8_t length = 0;

    /*
     * A zero capacity, so a value of any length reports TOO_SMALL rather than
     * being copied. Both that and OK mean the key is there; only NOT_FOUND and
     * the argument errors mean it is not.
     */
    const config_result_t result = config_get(store, key, NULL, 0, &length);
    return result == CONFIG_OK || result == CONFIG_ERR_TOO_SMALL;
}

void config_clear(config_store_t *store)
{
    if (store != NULL && store->initialised) {
        store->used = 0;
    }
}

/* ---------------------------------------------------------------------------
 * Typed convenience
 * -------------------------------------------------------------------------*/

config_result_t config_set_u32(config_store_t *store, const char *key, uint32_t value)
{
    /* Little-endian explicitly rather than by memcpy of the native type: the
       stored bytes are a format, and writing it out means a value survives a
       change of compiler or architecture. */
    const uint8_t bytes[4] = {
        (uint8_t)value, (uint8_t)(value >> 8),
        (uint8_t)(value >> 16), (uint8_t)(value >> 24),
    };
    return config_set(store, key, bytes, sizeof(bytes));
}

config_result_t config_get_u32(config_store_t *store, const char *key,
                              uint32_t *value, uint32_t fallback)
{
    if (value == NULL) {
        return CONFIG_ERR_INVALID_ARG;
    }
    *value = fallback;

    /*
     * The length is probed first, with a zero capacity, so that "stored but not
     * as a 32-bit value" is one error whether the value is shorter or longer
     * than four bytes. Reading straight into a four-byte buffer would report a
     * five-byte value as TOO_SMALL and a three-byte one as CORRUPT, which are
     * the same mistake described two ways.
     */
    uint8_t length = 0;
    const config_result_t probe = config_get(store, key, NULL, 0, &length);
    if (probe == CONFIG_ERR_NOT_FOUND || probe == CONFIG_ERR_INVALID_ARG ||
        probe == CONFIG_ERR_KEY_TOO_LONG) {
        return probe;
    }

    uint8_t bytes[4];
    if (length != sizeof(bytes)) {
        /* Reporting the type confusion rather than reassembling whatever
           happens to be there keeps it visible. */
        return CONFIG_ERR_CORRUPT;
    }

    const config_result_t result = config_get(store, key, bytes, sizeof(bytes), &length);
    if (result != CONFIG_OK) {
        return result;
    }

    *value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
             ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    return CONFIG_OK;
}

config_result_t config_set_string(config_store_t *store, const char *key,
                                 const char *value)
{
    if (value == NULL) {
        return CONFIG_ERR_INVALID_ARG;
    }
    const size_t length = strlen(value);
    if (length > CONFIG_MAX_VALUE_LENGTH) {
        return CONFIG_ERR_VALUE_TOO_LONG;
    }
    /* Stored without a terminator: the length is already known, and leaving it
       out means a 255-character string fits. */
    return config_set(store, key, value, (uint8_t)length);
}

config_result_t config_get_string(config_store_t *store, const char *key,
                                 char *value, size_t capacity,
                                 const char *fallback)
{
    if (value == NULL || capacity == 0) {
        return CONFIG_ERR_INVALID_ARG;
    }

    uint8_t length = 0;
    const config_result_t probe = config_get(store, key, NULL, 0, &length);

    if (probe == CONFIG_ERR_NOT_FOUND || (size_t)length + 1u > capacity) {
        /* Fall back rather than truncate: half a WiFi password is worse than
           the default, because it looks like a value. */
        const size_t fallback_length = (fallback != NULL) ? strlen(fallback) : 0;
        const size_t copy = (fallback_length + 1u <= capacity) ? fallback_length
                                                               : capacity - 1u;
        if (copy > 0) {
            memcpy(value, fallback, copy);
        }
        value[copy] = '\0';
        return (probe == CONFIG_ERR_NOT_FOUND) ? CONFIG_ERR_NOT_FOUND
                                               : CONFIG_ERR_TOO_SMALL;
    }

    uint8_t got = 0;
    const config_result_t result = config_get(store, key, value, (uint8_t)(capacity - 1u), &got);
    if (result != CONFIG_OK) {
        value[0] = '\0';
        return result;
    }
    value[got] = '\0';
    return CONFIG_OK;
}

/* ---------------------------------------------------------------------------
 * Enumeration
 * -------------------------------------------------------------------------*/

size_t config_count(config_store_t *store)
{
    if (store == NULL || !store->initialised) {
        return 0;
    }

    size_t count = 0;
    size_t at = 0;
    while (at < store->used) {
        at += CONFIG_RECORD_SIZE(store->buffer[at + KEY_LENGTH_AT],
                                 store->buffer[at + VALUE_LENGTH_AT]);
        count++;
    }
    return count;
}

bool config_key_at(config_store_t *store, size_t index, char *key, size_t capacity)
{
    if (store == NULL || !store->initialised || key == NULL || capacity == 0) {
        return false;
    }

    size_t at = 0;
    size_t seen = 0;
    while (at < store->used) {
        const uint8_t key_length = store->buffer[at + KEY_LENGTH_AT];
        const uint8_t value_length = store->buffer[at + VALUE_LENGTH_AT];

        if (seen == index) {
            if ((size_t)key_length + 1u > capacity) {
                return false;
            }
            memcpy(key, &store->buffer[at + KEY_AT], key_length);
            key[key_length] = '\0';
            return true;
        }
        at += CONFIG_RECORD_SIZE(key_length, value_length);
        seen++;
    }
    return false;
}

const char *config_result_name(config_result_t result)
{
    switch (result) {
        case CONFIG_OK:                  return "ok";
        case CONFIG_ERR_INVALID_ARG:     return "invalid argument";
        case CONFIG_ERR_KEY_TOO_LONG:    return "key too long";
        case CONFIG_ERR_VALUE_TOO_LONG:  return "value too long";
        case CONFIG_ERR_FULL:            return "no room left";
        case CONFIG_ERR_NOT_FOUND:       return "not found";
        case CONFIG_ERR_TOO_SMALL:       return "buffer too small for the value";
        case CONFIG_ERR_CORRUPT:         return "stored records do not parse";
        default:                         return "unknown";
    }
}
