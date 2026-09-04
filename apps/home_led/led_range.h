/*
 * led_range - the contiguous part of the installed strip that is active.
 *
 * The public values are numbered as a person numbers LEDs: first and last are
 * both inclusive and begin at one. Rendering uses the accessors below to get
 * a zero-based, half-open slice. Keeping that conversion here prevents MQTT,
 * the console and effects from each acquiring their own off-by-one rule.
 *
 * NO PICO SDK
 */

#ifndef HOME_LED_LED_RANGE_H
#define HOME_LED_LED_RANGE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t begin;      /* zero-based, inclusive */
    uint16_t end;        /* zero-based, exclusive */
    uint16_t limit;      /* number of installed LEDs */
    uint32_t generation;
} led_range_t;

/* Start with the whole strip selected. A zero-length strip stays empty. */
void led_range_init(led_range_t *range, uint16_t limit);

/* Restore an inclusive, one-based range. Invalid stored endpoints fall back
   to the whole strip rather than leaving a mysteriously dark installation. */
void led_range_restore(led_range_t *range, uint16_t limit,
                       uint16_t first, uint16_t last);

/* Set both endpoints atomically. False means they were outside 1..limit or
   reversed; the existing range is then left alone. */
bool led_range_set(led_range_t *range, uint16_t first, uint16_t last);

/* Set one endpoint as Home Assistant's independent sliders do. Values are
   clamped to the strip. If the changed endpoint crosses the other one, it
   carries that endpoint with it so the range is always non-empty. */
void led_range_set_first(led_range_t *range, uint32_t first);
void led_range_set_last(led_range_t *range, uint32_t last);

uint16_t led_range_first(const led_range_t *range);
uint16_t led_range_last(const led_range_t *range);
uint16_t led_range_begin(const led_range_t *range);
uint16_t led_range_length(const led_range_t *range);

#endif /* HOME_LED_LED_RANGE_H */
