/*
 * config_store - a set of key/value pairs, and the record format they take on
 * flash.
 *
 * The whole of the format and all of the manipulation, with no Pico SDK
 * dependency, so it is unit-tested on the host. persistent_config.h adds the
 * flash on either side.
 *
 * Values live in one caller-owned buffer as a run of records:
 *
 *     [key length][value length][key bytes][value bytes] ...
 *
 * Setting a key rewrites its record in place when the new value is the same
 * length and shuffles the buffer when it is not, so the buffer is always a
 * valid image of the whole set and can be written to flash as one blob. That
 * is deliberate: an append-only log would need less shuffling but would make
 * every read a scan and every recovery a replay, and there is no shortage of
 * time here — a configuration is saved when something changes, not in a loop.
 */

#ifndef PICO_FRAMEWORK_CONFIG_STORE_H
#define PICO_FRAMEWORK_CONFIG_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Both lengths are one byte, which bounds these. */
#define CONFIG_MAX_KEY_LENGTH 31u
#define CONFIG_MAX_VALUE_LENGTH 255u

/* Bytes a record occupies for a given key and value. */
#define CONFIG_RECORD_SIZE(key_len, value_len) ((size_t)(key_len) + (value_len) + 2u)

typedef enum {
    CONFIG_OK = 0,
    CONFIG_ERR_INVALID_ARG,
    CONFIG_ERR_KEY_TOO_LONG,
    CONFIG_ERR_VALUE_TOO_LONG,
    CONFIG_ERR_FULL,         /* no room in the buffer for the new value */
    CONFIG_ERR_NOT_FOUND,
    CONFIG_ERR_TOO_SMALL,    /* the caller's buffer cannot hold the value */
    CONFIG_ERR_CORRUPT,      /* the records do not parse */
} config_result_t;

typedef struct {
    uint8_t *buffer;
    size_t capacity;
    size_t used;
    bool initialised;
} config_store_t;

/*
 * Start an empty set over caller-owned storage. The buffer must outlive the
 * store and is where the whole configuration lives; nothing is allocated.
 */
config_result_t config_store_init(config_store_t *store, uint8_t *buffer,
                                  size_t capacity);

/*
 * Adopt `used` bytes of existing records, as read back from flash.
 *
 * Every record is checked, and a run that does not parse is rejected rather
 * than partly adopted — a truncated final record is exactly what an
 * interrupted write leaves, and half a configuration is worse than none.
 */
config_result_t config_store_load(config_store_t *store, uint8_t *buffer,
                                  size_t capacity, size_t used);

/* Bytes currently occupied, which is what needs writing to flash. */
static inline size_t config_store_used(const config_store_t *store)
{
    return store->used;
}

static inline size_t config_store_free(const config_store_t *store)
{
    return store->capacity - store->used;
}

/* ---------------------------------------------------------------------------
 * Values
 * -------------------------------------------------------------------------*/

/* Replaces any existing value for the key. A zero-length value is allowed and
   is distinct from the key being absent. */
config_result_t config_set(config_store_t *store, const char *key,
                           const void *value, uint8_t length);

/*
 * Copy the value out. `length` receives the true length and may be NULL.
 * Returns CONFIG_ERR_TOO_SMALL without copying if the value does not fit,
 * still setting `length` so the caller can size a buffer and retry.
 */
config_result_t config_get(config_store_t *store, const char *key,
                          void *value, uint8_t capacity, uint8_t *length);

config_result_t config_remove(config_store_t *store, const char *key);

bool config_has(config_store_t *store, const char *key);

void config_clear(config_store_t *store);

/* ---------------------------------------------------------------------------
 * Typed convenience
 *
 * Stored little-endian, which is the byte order of both architectures, so a
 * value written by one build reads the same in another.
 * -------------------------------------------------------------------------*/

config_result_t config_set_u32(config_store_t *store, const char *key, uint32_t value);
config_result_t config_get_u32(config_store_t *store, const char *key,
                              uint32_t *value, uint32_t fallback);

config_result_t config_set_string(config_store_t *store, const char *key,
                                 const char *value);

/* Always NUL-terminates when capacity is non-zero. */
config_result_t config_get_string(config_store_t *store, const char *key,
                                 char *value, size_t capacity,
                                 const char *fallback);

/* ---------------------------------------------------------------------------
 * Enumeration, for a command that lists what is stored
 * -------------------------------------------------------------------------*/

size_t config_count(config_store_t *store);

/*
 * The key at `index`, in storage order. Returns false past the end. Order is
 * insertion order until a key is removed or resized, and is not otherwise
 * meaningful.
 */
bool config_key_at(config_store_t *store, size_t index, char *key, size_t capacity);

const char *config_result_name(config_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_CONFIG_STORE_H */
