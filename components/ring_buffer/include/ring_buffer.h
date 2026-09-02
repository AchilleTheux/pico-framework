/*
 * ring_buffer - a byte FIFO over caller-owned storage.
 *
 * The point of it is decoupling a producer from a consumer that run at
 * different rates or in different contexts: bytes arriving from a UART
 * interrupt while the main loop is busy erasing a flash sector, for instance,
 * which is exactly what a firmware update over serial needs.
 *
 * Concurrency, stated precisely because this is the part that goes wrong:
 *
 *   Safe    one producer and one consumer, on the same core, with no locking.
 *           That includes an interrupt handler on one side and the main loop
 *           on the other, either way round. `head` is written only by the
 *           producer and `tail` only by the consumer, so neither needs to see
 *           a consistent view of the other beyond a single word read, which is
 *           atomic on both architectures.
 *
 *   Unsafe  two producers, two consumers, or use across the RP2040's two
 *           cores. Cross-core needs real barriers and a spin lock; wrap it or
 *           use the SDK's own queue instead.
 *
 * No Pico SDK dependency, so this is unit-tested on the host.
 */

#ifndef PICO_FRAMEWORK_RING_BUFFER_H
#define PICO_FRAMEWORK_RING_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *storage;
    size_t size;

    /* Written by the producer, read by both. */
    volatile size_t head;

    /* Written by the consumer, read by both. */
    volatile size_t tail;
} ring_buffer_t;

/*
 * `storage` must hold `size` bytes and outlive the buffer.
 *
 * One slot is kept empty so that a full buffer is distinguishable from an
 * empty one without a shared count that both sides would have to write — which
 * is what makes the lock-free single-producer case work. Usable capacity is
 * therefore `size - 1`; ring_buffer_capacity() reports it, so callers need not
 * remember. `size` must be at least 2.
 *
 * Returns false, leaving the buffer unusable, on a null pointer or size < 2.
 */
bool ring_buffer_init(ring_buffer_t *rb, uint8_t *storage, size_t size);

/* Usable capacity: one less than the storage given to init. */
static inline size_t ring_buffer_capacity(const ring_buffer_t *rb)
{
    return rb->size - 1u;
}

size_t ring_buffer_count(const ring_buffer_t *rb);
size_t ring_buffer_free(const ring_buffer_t *rb);

static inline bool ring_buffer_is_empty(const ring_buffer_t *rb)
{
    return rb->head == rb->tail;
}

static inline bool ring_buffer_is_full(const ring_buffer_t *rb)
{
    return ring_buffer_free(rb) == 0;
}

/* ---------------------------------------------------------------------------
 * Producer side
 * -------------------------------------------------------------------------*/

/* False when the buffer is full; the byte is not stored. */
bool ring_buffer_push(ring_buffer_t *rb, uint8_t byte);

/*
 * Writes as much as fits and returns how much that was, which may be less than
 * `len` and may be zero. Partial writes are the normal case for a stream, so
 * this never fails outright.
 */
size_t ring_buffer_write(ring_buffer_t *rb, const void *data, size_t len);

/* ---------------------------------------------------------------------------
 * Consumer side
 * -------------------------------------------------------------------------*/

/* False when the buffer is empty. */
bool ring_buffer_pop(ring_buffer_t *rb, uint8_t *byte);

/* Reads up to `len` bytes and returns how many. */
size_t ring_buffer_read(ring_buffer_t *rb, void *data, size_t len);

/* Like pop, but leaves the byte in place. */
bool ring_buffer_peek(const ring_buffer_t *rb, uint8_t *byte);

/*
 * Like read, but leaves the bytes in place: copies up to `len` bytes from the
 * front and returns how many, without moving `tail`.
 *
 * For a consumer that has to hand the bytes to something which may take fewer
 * than it was offered. Reading them out first and pushing the remainder back
 * is not equivalent — the remainder would go behind whatever else is queued,
 * turning a partial acceptance into reordered output. Peek, offer, then
 * ring_buffer_discard() exactly what was taken.
 */
size_t ring_buffer_peek_bytes(const ring_buffer_t *rb, void *data, size_t len);

/*
 * Drop up to `len` bytes from the front without copying them, and return how
 * many were actually dropped. The consumer half of ring_buffer_peek_bytes().
 */
size_t ring_buffer_discard(ring_buffer_t *rb, size_t len);

/*
 * Discard everything currently buffered.
 *
 * Consumer-side only: it moves `tail`. Calling it from the producer while a
 * consumer is running is a data race.
 */
void ring_buffer_clear(ring_buffer_t *rb);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_RING_BUFFER_H */
