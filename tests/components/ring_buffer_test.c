/*
 * Host-side tests for the byte FIFO.
 *
 * The interesting cases are all about the wrap: a ring buffer that works for
 * the first pass and corrupts data after the indices wrap around is the
 * classic failure, and it does not show up until the buffer has been running
 * for a while — on hardware, usually during the one transfer that mattered.
 * Several tests below deliberately run many times the buffer's length through
 * it for that reason.
 */

#include <string.h>

#include "test.h"

#include "ring_buffer.h"

TEST(a_fresh_buffer_is_empty)
{
    uint8_t storage[8];
    ring_buffer_t rb;

    CHECK(ring_buffer_init(&rb, storage, sizeof(storage)));
    CHECK(ring_buffer_is_empty(&rb));
    CHECK(!ring_buffer_is_full(&rb));
    CHECK_EQ_INT(ring_buffer_count(&rb), 0);
}

TEST(usable_capacity_is_one_less_than_the_storage)
{
    /* One slot is kept empty so full and empty are distinguishable without a
       shared counter. Callers should read this rather than assume. */
    uint8_t storage[8];
    ring_buffer_t rb;
    ring_buffer_init(&rb, storage, sizeof(storage));

    CHECK_EQ_INT(ring_buffer_capacity(&rb), 7);
    CHECK_EQ_INT(ring_buffer_free(&rb), 7);
}

TEST(init_rejects_storage_too_small_to_hold_anything)
{
    uint8_t storage[4];
    ring_buffer_t rb;

    CHECK(!ring_buffer_init(&rb, storage, 1)); /* capacity would be 0 */
    CHECK(!ring_buffer_init(&rb, storage, 0));
    CHECK(!ring_buffer_init(&rb, NULL, 8));
    CHECK(ring_buffer_init(&rb, storage, 2)); /* the smallest useful size */
    CHECK_EQ_INT(ring_buffer_capacity(&rb), 1);
}

TEST(a_byte_comes_back_out)
{
    uint8_t storage[8];
    ring_buffer_t rb;
    ring_buffer_init(&rb, storage, sizeof(storage));

    CHECK(ring_buffer_push(&rb, 0xA5));
    CHECK_EQ_INT(ring_buffer_count(&rb), 1);

    uint8_t byte = 0;
    CHECK(ring_buffer_pop(&rb, &byte));
    CHECK_EQ_INT(byte, 0xA5);
    CHECK(ring_buffer_is_empty(&rb));
}

TEST(bytes_come_back_in_order)
{
    uint8_t storage[8];
    ring_buffer_t rb;
    ring_buffer_init(&rb, storage, sizeof(storage));

    for (uint8_t i = 0; i < 7; i++) {
        CHECK(ring_buffer_push(&rb, (uint8_t)(i + 1)));
    }

    for (uint8_t i = 0; i < 7; i++) {
        uint8_t byte = 0;
        CHECK(ring_buffer_pop(&rb, &byte));
        CHECK_EQ_INT(byte, i + 1);
    }
}

TEST(pushing_into_a_full_buffer_fails_without_losing_data)
{
    uint8_t storage[4]; /* capacity 3 */
    ring_buffer_t rb;
    ring_buffer_init(&rb, storage, sizeof(storage));

    CHECK(ring_buffer_push(&rb, 1));
    CHECK(ring_buffer_push(&rb, 2));
    CHECK(ring_buffer_push(&rb, 3));
    CHECK(ring_buffer_is_full(&rb));

    /* The rejected byte must not overwrite the oldest one. */
    CHECK(!ring_buffer_push(&rb, 4));
    CHECK_EQ_INT(ring_buffer_count(&rb), 3);

    uint8_t byte = 0;
    ring_buffer_pop(&rb, &byte);
    CHECK_EQ_INT(byte, 1);
}

TEST(popping_an_empty_buffer_fails)
{
    uint8_t storage[8];
    ring_buffer_t rb;
    ring_buffer_init(&rb, storage, sizeof(storage));

    uint8_t byte = 0xEE;
    CHECK(!ring_buffer_pop(&rb, &byte));
    CHECK_EQ_INT(byte, 0xEE); /* left untouched */
}

TEST(peek_does_not_consume)
{
    uint8_t storage[8];
    ring_buffer_t rb;
    ring_buffer_init(&rb, storage, sizeof(storage));
    ring_buffer_push(&rb, 0x42);

    uint8_t byte = 0;
    CHECK(ring_buffer_peek(&rb, &byte));
    CHECK_EQ_INT(byte, 0x42);
    CHECK_EQ_INT(ring_buffer_count(&rb), 1);

    byte = 0;
    CHECK(ring_buffer_pop(&rb, &byte));
    CHECK_EQ_INT(byte, 0x42);
}

/* ---------------------------------------------------------------------------
 * Wraparound
 * -------------------------------------------------------------------------*/

TEST(data_survives_many_wraps)
{
    /* Push and pop far more than the buffer holds, so the indices wrap dozens
       of times. A wrap bug shows up here and almost nowhere else. */
    uint8_t storage[8];
    ring_buffer_t rb;
    ring_buffer_init(&rb, storage, sizeof(storage));

    for (unsigned i = 0; i < 1000; i++) {
        const uint8_t expected = (uint8_t)(i * 7u + 1u);
        CHECK(ring_buffer_push(&rb, expected));

        uint8_t byte = 0;
        if (!ring_buffer_pop(&rb, &byte) || byte != expected) {
            printf("    iteration %u: expected 0x%02X, got 0x%02X\n", i,
                   expected, byte);
            CHECK(false);
            return;
        }
    }
}

TEST(a_block_write_that_wraps_reads_back_intact)
{
    /* The two-memcpy path: part of the data goes to the end of the storage and
       the rest to the start. */
    uint8_t storage[16];
    ring_buffer_t rb;
    ring_buffer_init(&rb, storage, sizeof(storage));

    /* Advance the indices so the next write straddles the end. */
    uint8_t scratch[10];
    memset(scratch, 0xCC, sizeof(scratch));
    ring_buffer_write(&rb, scratch, sizeof(scratch));
    ring_buffer_read(&rb, scratch, sizeof(scratch));

    static const uint8_t payload[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
    CHECK_EQ_INT(ring_buffer_write(&rb, payload, sizeof(payload)), sizeof(payload));

    uint8_t received[sizeof(payload)];
    memset(received, 0, sizeof(received));
    CHECK_EQ_INT(ring_buffer_read(&rb, received, sizeof(received)), sizeof(payload));
    CHECK(memcmp(payload, received, sizeof(payload)) == 0);
}

TEST(block_transfers_survive_many_wraps)
{
    uint8_t storage[16];
    ring_buffer_t rb;
    ring_buffer_init(&rb, storage, sizeof(storage));

    uint8_t counter = 0;
    for (unsigned round = 0; round < 500; round++) {
        uint8_t out[5];
        for (unsigned i = 0; i < sizeof(out); i++) {
            out[i] = counter++;
        }

        CHECK_EQ_INT(ring_buffer_write(&rb, out, sizeof(out)), sizeof(out));

        uint8_t in[5];
        memset(in, 0, sizeof(in));
        CHECK_EQ_INT(ring_buffer_read(&rb, in, sizeof(in)), sizeof(out));

        if (memcmp(out, in, sizeof(out)) != 0) {
            printf("    round %u corrupted\n", round);
            CHECK(false);
            return;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Partial transfers
 * -------------------------------------------------------------------------*/

TEST(a_block_write_larger_than_the_space_writes_what_fits)
{
    uint8_t storage[8]; /* capacity 7 */
    ring_buffer_t rb;
    ring_buffer_init(&rb, storage, sizeof(storage));

    static const uint8_t payload[20] = { 0 };
    CHECK_EQ_INT(ring_buffer_write(&rb, payload, sizeof(payload)), 7);
    CHECK(ring_buffer_is_full(&rb));

    /* A further write adds nothing rather than overwriting. */
    CHECK_EQ_INT(ring_buffer_write(&rb, payload, sizeof(payload)), 0);
}

TEST(a_block_read_larger_than_the_contents_reads_what_is_there)
{
    uint8_t storage[8];
    ring_buffer_t rb;
    ring_buffer_init(&rb, storage, sizeof(storage));

    static const uint8_t payload[] = { 1, 2, 3 };
    ring_buffer_write(&rb, payload, sizeof(payload));

    uint8_t received[20];
    memset(received, 0xFF, sizeof(received));
    CHECK_EQ_INT(ring_buffer_read(&rb, received, sizeof(received)), 3);
    CHECK(memcmp(payload, received, sizeof(payload)) == 0);
    CHECK_EQ_INT(received[3], 0xFF); /* nothing written past what was there */
}

TEST(count_and_free_always_add_up_to_capacity)
{
    /* Checked at every fill level and at several wrap offsets, because the
       count calculation is where the wrap arithmetic usually goes wrong. */
    uint8_t storage[8];
    ring_buffer_t rb;
    ring_buffer_init(&rb, storage, sizeof(storage));

    for (unsigned offset = 0; offset < 20; offset++) {
        for (unsigned fill = 0; fill <= ring_buffer_capacity(&rb); fill++) {
            for (unsigned i = 0; i < fill; i++) {
                ring_buffer_push(&rb, (uint8_t)i);
            }

            const size_t count = ring_buffer_count(&rb);
            const size_t space = ring_buffer_free(&rb);
            if (count != fill || count + space != ring_buffer_capacity(&rb)) {
                printf("    offset %u fill %u: count %u free %u\n", offset, fill,
                       (unsigned)count, (unsigned)space);
                CHECK(false);
                return;
            }

            uint8_t byte;
            while (ring_buffer_pop(&rb, &byte)) { }
        }

        /* Shift the indices along so the next round starts at a new offset. */
        ring_buffer_push(&rb, 0);
        uint8_t byte;
        ring_buffer_pop(&rb, &byte);
    }
}

TEST(clear_discards_everything)
{
    uint8_t storage[8];
    ring_buffer_t rb;
    ring_buffer_init(&rb, storage, sizeof(storage));

    ring_buffer_push(&rb, 1);
    ring_buffer_push(&rb, 2);
    ring_buffer_clear(&rb);

    CHECK(ring_buffer_is_empty(&rb));
    CHECK_EQ_INT(ring_buffer_count(&rb), 0);
    CHECK_EQ_INT(ring_buffer_free(&rb), ring_buffer_capacity(&rb));

    /* And the buffer still works afterwards. */
    CHECK(ring_buffer_push(&rb, 3));
    uint8_t byte = 0;
    CHECK(ring_buffer_pop(&rb, &byte));
    CHECK_EQ_INT(byte, 3);
}

TEST(interleaved_partial_transfers_preserve_stream_order)
{
    /*
     * Closest thing to the real use: a producer writing in bursts of one size
     * while a consumer drains in bursts of another, which is what a UART
     * interrupt and a flash-writing main loop actually do to each other.
     */
    uint8_t storage[32];
    ring_buffer_t rb;
    ring_buffer_init(&rb, storage, sizeof(storage));

    uint8_t next_written = 0;
    uint8_t next_expected = 0;
    unsigned produced = 0;

    for (unsigned step = 0; step < 2000; step++) {
        /* Produce a burst of 1..9 bytes, as much as fits. */
        const unsigned burst = (step % 9u) + 1u;
        for (unsigned i = 0; i < burst; i++) {
            if (!ring_buffer_push(&rb, next_written)) {
                break;
            }
            next_written++;
            produced++;
        }

        /* Consume a burst of 1..5. */
        const unsigned drain = (step % 5u) + 1u;
        for (unsigned i = 0; i < drain; i++) {
            uint8_t byte = 0;
            if (!ring_buffer_pop(&rb, &byte)) {
                break;
            }
            if (byte != next_expected) {
                printf("    step %u: expected 0x%02X, got 0x%02X\n", step,
                       next_expected, byte);
                CHECK(false);
                return;
            }
            next_expected++;
        }
    }

    CHECK(produced > 1000); /* the exercise actually ran */
}

TEST_MAIN(
    RUN(a_fresh_buffer_is_empty);
    RUN(usable_capacity_is_one_less_than_the_storage);
    RUN(init_rejects_storage_too_small_to_hold_anything);
    RUN(a_byte_comes_back_out);
    RUN(bytes_come_back_in_order);
    RUN(pushing_into_a_full_buffer_fails_without_losing_data);
    RUN(popping_an_empty_buffer_fails);
    RUN(peek_does_not_consume);

    RUN(data_survives_many_wraps);
    RUN(a_block_write_that_wraps_reads_back_intact);
    RUN(block_transfers_survive_many_wraps);

    RUN(a_block_write_larger_than_the_space_writes_what_fits);
    RUN(a_block_read_larger_than_the_contents_reads_what_is_there);
    RUN(count_and_free_always_add_up_to_capacity);
    RUN(clear_discards_everything);
    RUN(interleaved_partial_transfers_preserve_stream_order);
)
