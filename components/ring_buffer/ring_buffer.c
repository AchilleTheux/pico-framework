#include <string.h>

#include "ring_buffer.h"

/*
 * Stop the compiler moving the data copy across the index publication.
 *
 * The producer must finish writing the bytes before it advances `head`, and
 * the consumer must finish reading them before it advances `tail`. Both
 * architectures execute in order and do not reorder these stores between
 * themselves, so a compiler barrier is enough; a full data-memory barrier
 * would only be needed across cores, which this buffer does not claim to
 * support.
 */
#if defined(__GNUC__) || defined(__clang__)
#define RING_BUFFER_BARRIER() __asm__ __volatile__("" ::: "memory")
#else
#define RING_BUFFER_BARRIER() ((void)0)
#endif

static size_t advance(const ring_buffer_t *rb, size_t index, size_t by)
{
    index += by;
    while (index >= rb->size) {
        index -= rb->size;
    }
    return index;
}

bool ring_buffer_init(ring_buffer_t *rb, uint8_t *storage, size_t size)
{
    if (rb == NULL || storage == NULL || size < 2) {
        return false;
    }

    rb->storage = storage;
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;
    return true;
}

size_t ring_buffer_count(const ring_buffer_t *rb)
{
    /* Read each index once: the other side may be moving its own. */
    const size_t head = rb->head;
    const size_t tail = rb->tail;

    return (head >= tail) ? (head - tail) : (rb->size - tail + head);
}

size_t ring_buffer_free(const ring_buffer_t *rb)
{
    return ring_buffer_capacity(rb) - ring_buffer_count(rb);
}

/* ---------------------------------------------------------------------------
 * Producer
 * -------------------------------------------------------------------------*/

bool ring_buffer_push(ring_buffer_t *rb, uint8_t byte)
{
    const size_t head = rb->head;
    const size_t next = advance(rb, head, 1);

    if (next == rb->tail) {
        return false; /* full */
    }

    rb->storage[head] = byte;
    RING_BUFFER_BARRIER();
    rb->head = next;
    return true;
}

size_t ring_buffer_write(ring_buffer_t *rb, const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;

    if (rb == NULL || bytes == NULL) {
        return 0;
    }

    size_t writable = ring_buffer_free(rb);
    if (len < writable) {
        writable = len;
    }
    if (writable == 0) {
        return 0;
    }

    /* Copy in at most two runs rather than byte at a time: one to the end of
       the storage, one from the start. */
    const size_t head = rb->head;
    const size_t until_end = rb->size - head;
    const size_t first = (writable < until_end) ? writable : until_end;

    memcpy(&rb->storage[head], bytes, first);
    if (writable > first) {
        memcpy(&rb->storage[0], bytes + first, writable - first);
    }

    RING_BUFFER_BARRIER();
    rb->head = advance(rb, head, writable);
    return writable;
}

/* ---------------------------------------------------------------------------
 * Consumer
 * -------------------------------------------------------------------------*/

bool ring_buffer_peek(const ring_buffer_t *rb, uint8_t *byte)
{
    if (rb == NULL || byte == NULL || ring_buffer_is_empty(rb)) {
        return false;
    }

    *byte = rb->storage[rb->tail];
    return true;
}

bool ring_buffer_pop(ring_buffer_t *rb, uint8_t *byte)
{
    if (rb == NULL || byte == NULL || ring_buffer_is_empty(rb)) {
        return false;
    }

    const size_t tail = rb->tail;
    *byte = rb->storage[tail];

    RING_BUFFER_BARRIER();
    rb->tail = advance(rb, tail, 1);
    return true;
}

size_t ring_buffer_read(ring_buffer_t *rb, void *data, size_t len)
{
    uint8_t *bytes = (uint8_t *)data;

    if (rb == NULL || bytes == NULL) {
        return 0;
    }

    size_t readable = ring_buffer_count(rb);
    if (len < readable) {
        readable = len;
    }
    if (readable == 0) {
        return 0;
    }

    const size_t tail = rb->tail;
    const size_t until_end = rb->size - tail;
    const size_t first = (readable < until_end) ? readable : until_end;

    memcpy(bytes, &rb->storage[tail], first);
    if (readable > first) {
        memcpy(bytes + first, &rb->storage[0], readable - first);
    }

    RING_BUFFER_BARRIER();
    rb->tail = advance(rb, tail, readable);
    return readable;
}

void ring_buffer_clear(ring_buffer_t *rb)
{
    if (rb != NULL) {
        rb->tail = rb->head;
    }
}
