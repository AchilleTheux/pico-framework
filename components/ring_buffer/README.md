# ring_buffer

A byte FIFO over caller-owned storage.

Its job is decoupling a producer from a consumer running at a different rate or
in a different context — bytes arriving from a UART interrupt while the main
loop is busy erasing a flash sector, which is exactly what a firmware update
over serial does.

## Concurrency

Stated precisely, because this is the part that goes wrong:

| | |
|---|---|
| **Safe** | one producer and one consumer, on the same core, no locking. That includes an interrupt handler on one side and the main loop on the other, either way round. |
| **Unsafe** | two producers, two consumers, or use across the RP2040's two cores. |

It works because `head` is written only by the producer and `tail` only by the
consumer, so neither needs a consistent view of the other beyond a single word
read — which is atomic on both architectures. A compiler barrier keeps the data
copy from being moved across the index update. Cross-core would need a real
data-memory barrier and a spin lock; use the SDK's `queue` there instead.

## Capacity

One slot is kept empty so that full and empty are distinguishable without a
shared counter that both sides would have to write — which is what makes the
lock-free case work. Usable capacity is therefore one less than the storage:

```c
uint8_t storage[64];
ring_buffer_t rb;
ring_buffer_init(&rb, storage, sizeof(storage));

ring_buffer_capacity(&rb);   /* 63, not 64 */
```

Read it rather than assuming; `ring_buffer_init()` refuses storage smaller than
two bytes.

## Usage

```c
/* producer, e.g. a UART interrupt */
ring_buffer_push(&rb, byte);              /* false if full */
ring_buffer_write(&rb, chunk, len);       /* returns how much fitted */

/* consumer, e.g. the main loop */
uint8_t byte;
while (ring_buffer_pop(&rb, &byte)) { ... }
size_t got = ring_buffer_read(&rb, buffer, sizeof(buffer));
```

Block transfers are partial by design: `write` stores what fits and reports how
much, rather than failing outright, because partial progress is the normal case
for a stream.

`ring_buffer_clear()` moves `tail`, so it is consumer-side only.

## Testing

`make test` covers ordering, full and empty behaviour, and partial transfers.
The cases that earn their place are the wraparound ones: a ring buffer that
works on the first pass and corrupts data once the indices wrap is the classic
failure, and it only appears after the buffer has been running a while. Several
tests push thousands of bytes through an eight-byte buffer for that reason, and
one interleaves mismatched producer and consumer burst sizes the way an
interrupt and a main loop actually do.
