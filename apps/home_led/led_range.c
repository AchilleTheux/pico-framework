#include "led_range.h"

#include <string.h>

static uint16_t clamp_endpoint(const led_range_t *range, uint32_t value)
{
    if (value < 1u) {
        return 1u;
    }
    if (value > range->limit) {
        return range->limit;
    }
    return (uint16_t)value;
}

void led_range_init(led_range_t *range, uint16_t limit)
{
    if (range == NULL) {
        return;
    }

    memset(range, 0, sizeof(*range));
    range->end = limit;
    range->limit = limit;
    range->generation = 1u;
}

void led_range_restore(led_range_t *range, uint16_t limit,
                       uint16_t first, uint16_t last)
{
    led_range_init(range, limit);
    if (range == NULL || limit == 0u) {
        return;
    }

    if (first >= 1u && first <= last && last <= limit) {
        range->begin = (uint16_t)(first - 1u);
        range->end = last;
    }
}

bool led_range_set(led_range_t *range, uint16_t first, uint16_t last)
{
    if (range == NULL || range->limit == 0u || first < 1u || first > last ||
        last > range->limit) {
        return false;
    }

    const uint16_t begin = (uint16_t)(first - 1u);
    if (range->begin != begin || range->end != last) {
        range->begin = begin;
        range->end = last;
        range->generation++;
    }
    return true;
}

void led_range_set_first(led_range_t *range, uint32_t first)
{
    if (range == NULL || range->limit == 0u) {
        return;
    }

    const uint16_t endpoint = clamp_endpoint(range, first);
    const uint16_t begin = (uint16_t)(endpoint - 1u);
    uint16_t end = range->end;

    if (begin >= end) {
        end = endpoint;
    }
    if (range->begin != begin || range->end != end) {
        range->begin = begin;
        range->end = end;
        range->generation++;
    }
}

void led_range_set_last(led_range_t *range, uint32_t last)
{
    if (range == NULL || range->limit == 0u) {
        return;
    }

    const uint16_t endpoint = clamp_endpoint(range, last);
    uint16_t begin = range->begin;

    if (endpoint <= begin) {
        begin = (uint16_t)(endpoint - 1u);
    }
    if (range->begin != begin || range->end != endpoint) {
        range->begin = begin;
        range->end = endpoint;
        range->generation++;
    }
}

uint16_t led_range_first(const led_range_t *range)
{
    return range != NULL && range->limit != 0u ? (uint16_t)(range->begin + 1u) : 0u;
}

uint16_t led_range_last(const led_range_t *range)
{
    return range != NULL ? range->end : 0u;
}

uint16_t led_range_begin(const led_range_t *range)
{
    return range != NULL ? range->begin : 0u;
}

uint16_t led_range_length(const led_range_t *range)
{
    return range != NULL && range->end >= range->begin
        ? (uint16_t)(range->end - range->begin)
        : 0u;
}
