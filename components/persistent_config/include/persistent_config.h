/*
 * persistent_config - key/value settings that survive power-off.
 *
 * The format and all the manipulation are in config_store.h, which has no SDK
 * dependency. This adds the flash, and one property worth stating plainly:
 *
 *   A save can be interrupted at any point without losing the previous
 *   configuration.
 *
 * That is what the two slots are for. Each save erases and rewrites the slot
 * that is *not* current, then the newer sequence number makes it current. Until
 * that last write lands the old slot is untouched and still the one that loads,
 * so a battery that sags mid-save costs the change and nothing else.
 *
 * Alternating also halves the wear on either sector, which matters for a part
 * rated around 100,000 erase cycles if anything ever saves in a loop.
 *
 *     +----------------+  data region
 *     |  slot A        |  one sector
 *     +----------------+
 *     |  slot B        |  one sector
 *     +----------------+
 *     |  the rest      |  for logs
 *     +----------------+
 */

#ifndef PICO_FRAMEWORK_PERSISTENT_CONFIG_H
#define PICO_FRAMEWORK_PERSISTENT_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "flash_storage.h"

#include "config_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/* "PCF1", chosen so neither erased flash nor blank flash can be mistaken for
   a slot header. */
#define PERSISTENT_CONFIG_MAGIC 0x31464350u

typedef enum {
    PERSISTENT_CONFIG_OK = 0,
    PERSISTENT_CONFIG_ERR_INVALID_ARG,
    PERSISTENT_CONFIG_ERR_NO_SPACE,     /* the layout has no room for two slots */
    PERSISTENT_CONFIG_ERR_EMPTY,        /* nothing saved yet; defaults apply */
    PERSISTENT_CONFIG_ERR_CORRUPT,      /* both slots failed their checks */
    PERSISTENT_CONFIG_ERR_TOO_LARGE,    /* the set no longer fits in a sector */
    PERSISTENT_CONFIG_ERR_FLASH,
} persistent_config_result_t;

typedef struct {
    config_store_t store;
    flash_region_t slot[2];
    uint8_t current;        /* which slot the loaded data came from */
    uint32_t sequence;      /* of the loaded data */
    bool loaded;
} persistent_config_t;

/*
 * Read the configuration into `buffer`, which the caller owns and which becomes
 * the working copy.
 *
 * Returns PERSISTENT_CONFIG_ERR_EMPTY when nothing has been saved and
 * PERSISTENT_CONFIG_ERR_CORRUPT when both slots fail their checks. Both leave
 * an empty, usable store, so a caller that only wants defaults can ignore the
 * distinction — but they are different situations and only one of them means
 * something went wrong.
 */
persistent_config_result_t persistent_config_load(persistent_config_t *config,
                                                  uint8_t *buffer, size_t capacity);

/*
 * Write the working copy to the slot that is not current.
 *
 * Does not check whether anything changed: a caller that saves on every loop
 * iteration will wear the flash out, and hiding that behind a dirty flag would
 * make it harder to notice rather than less likely.
 */
persistent_config_result_t persistent_config_save(persistent_config_t *config);

/* Erase both slots, so the next load reports EMPTY. */
persistent_config_result_t persistent_config_erase(persistent_config_t *config);

/* The store to pass to config_set() and friends. */
static inline config_store_t *persistent_config_store(persistent_config_t *config)
{
    return &config->store;
}

/* Sequence number of the loaded configuration; increases by one per save. */
static inline uint32_t persistent_config_sequence(const persistent_config_t *config)
{
    return config->sequence;
}

/* Largest configuration that will fit in a slot. */
size_t persistent_config_capacity(void);

const char *persistent_config_result_name(persistent_config_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_PERSISTENT_CONFIG_H */
