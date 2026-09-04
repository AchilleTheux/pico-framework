/*
 * Host-side tests for the buffering between a TCP connection and a byte
 * stream.
 *
 * This is where the behaviour that is expensive to observe on hardware gets
 * pinned: what happens when the link takes only part of what it was offered,
 * what happens when the receive buffer is full, and what survives a
 * disconnection. All of it against a fake link, so a partial send is one line
 * here rather than a congested network and a packet capture.
 *
 * The partial-send case is not hypothetical. The same mistake in bt_stream --
 * consuming from the ring before the link had said how much it took -- was a
 * real finding in REVIEW.md, and it reorders a byte stream rather than
 * dropping from it, which is the kind of corruption that reads as a protocol
 * bug on the far end.
 */

#include <string.h>

#include "test.h"

#include "tcp_stream.h"

/* ---------------------------------------------------------------------------
 * A link that can be told how to misbehave
 * -------------------------------------------------------------------------*/

typedef struct {
    uint8_t sent[2048];
    size_t sent_length;

    /* Most this link will take in one call. SIZE_MAX-ish by default. */
    uint16_t accept_limit;

    /* Report having taken more than it was offered, which a correct consumer
       must not believe. */
    bool claim_extra;

    int calls;
} fake_link_t;

static uint16_t fake_send(void *ctx, const uint8_t *data, uint16_t length)
{
    fake_link_t *link = (fake_link_t *)ctx;
    link->calls++;

    uint16_t accepted = length;
    if (accepted > link->accept_limit) {
        accepted = link->accept_limit;
    }

    if (accepted > 0) {
        memcpy(link->sent + link->sent_length, data, accepted);
        link->sent_length += accepted;
    }

    return link->claim_extra ? (uint16_t)(length + 10u) : accepted;
}

typedef struct {
    tcp_stream_t stream;
    fake_link_t link;
    uint8_t incoming[64];
    uint8_t outgoing[64];
} fixture_t;

static void fixture_init(fixture_t *f)
{
    memset(f, 0, sizeof(*f));
    f->link.accept_limit = UINT16_MAX;

    CHECK(tcp_stream_init(&f->stream,
                          f->incoming, sizeof(f->incoming),
                          f->outgoing, sizeof(f->outgoing),
                          fake_send, &f->link));
    tcp_stream_set_connected(&f->stream, true);
}

/* ---------------------------------------------------------------------------
 * Set-up
 * -------------------------------------------------------------------------*/

TEST(init_rejects_what_it_cannot_work_with)
{
    tcp_stream_t stream;
    uint8_t a[8];
    uint8_t b[8];
    fake_link_t link = { .accept_limit = UINT16_MAX };

    CHECK(!tcp_stream_init(NULL, a, sizeof(a), b, sizeof(b), fake_send, &link));
    CHECK(!tcp_stream_init(&stream, a, sizeof(a), b, sizeof(b), NULL, &link));

    /* ring_buffer_init() needs two bytes to tell full from empty. */
    CHECK(!tcp_stream_init(&stream, a, 1, b, sizeof(b), fake_send, &link));
    CHECK(!tcp_stream_init(&stream, a, sizeof(a), b, 0, fake_send, &link));

    CHECK(tcp_stream_init(&stream, a, sizeof(a), b, sizeof(b), fake_send, &link));
}

TEST(a_null_stream_is_survivable_everywhere)
{
    uint8_t scratch[4];

    tcp_stream_set_connected(NULL, true);
    CHECK_EQ_INT(tcp_stream_write(NULL, "x", 1), 0);
    CHECK(!tcp_stream_flush(NULL, 16));
    CHECK(!tcp_stream_has_output(NULL));
    CHECK(!tcp_stream_can_accept(NULL, 1));
    CHECK_EQ_INT(tcp_stream_on_received(NULL, "x", 1), 0);
    CHECK_EQ_INT(tcp_stream_read(NULL), -1);
    CHECK_EQ_INT(tcp_stream_read_bytes(NULL, scratch, sizeof(scratch)), 0);
}

/* ---------------------------------------------------------------------------
 * Output
 * -------------------------------------------------------------------------*/

TEST(output_is_buffered_then_sent_when_the_link_is_asked)
{
    fixture_t f;
    fixture_init(&f);

    CHECK_EQ_INT(tcp_stream_write(&f.stream, "hello", 5), 5);
    CHECK(tcp_stream_has_output(&f.stream));
    CHECK_EQ_INT(tcp_stream_pending(&f.stream), 5);

    /* Nothing has reached the link until it says it has room. */
    CHECK_EQ_INT(f.link.calls, 0);

    CHECK(!tcp_stream_flush(&f.stream, 64));
    CHECK_EQ_INT(f.link.sent_length, 5);
    CHECK(memcmp(f.link.sent, "hello", 5) == 0);
    CHECK(!tcp_stream_has_output(&f.stream));
    CHECK_EQ_U32(f.stream.bytes_sent, 5);
}

TEST(writing_with_no_connection_is_dropped_and_counted)
{
    fixture_t f;
    fixture_init(&f);
    tcp_stream_set_connected(&f.stream, false);

    CHECK_EQ_INT(tcp_stream_write(&f.stream, "hello", 5), 0);
    CHECK_EQ_U32(f.stream.dropped_outgoing, 5);
    CHECK(!tcp_stream_has_output(&f.stream));
}

TEST(output_past_the_buffer_loses_its_tail_and_is_counted)
{
    fixture_t f;
    fixture_init(&f);

    /* 64 bytes of storage is 63 of capacity: one slot tells full from empty. */
    uint8_t big[100];
    memset(big, 'a', sizeof(big));

    CHECK_EQ_INT(tcp_stream_write(&f.stream, big, sizeof(big)), 63);
    CHECK_EQ_U32(f.stream.dropped_outgoing, 100 - 63);

    /* The head survived, which is the half that carries the meaning. */
    CHECK(!tcp_stream_flush(&f.stream, 200));
    CHECK_EQ_INT(f.link.sent_length, 63);
}

TEST(a_partial_send_keeps_the_stream_in_order)
{
    /*
     * The regression this file exists for. Consuming from the ring before the
     * link has said how much it took leaves the rejected suffix queued behind
     * bytes that were already waiting: "abcdefghij", of which the link takes
     * "ab", must go on to send "cdefghij" and never "cdefghijab".
     */
    fixture_t f;
    fixture_init(&f);

    CHECK_EQ_INT(tcp_stream_write(&f.stream, "abcdefghij", 10), 10);

    f.link.accept_limit = 2;
    CHECK(tcp_stream_flush(&f.stream, 10));      /* output remains */
    CHECK_EQ_INT(f.link.sent_length, 2);
    CHECK(memcmp(f.link.sent, "ab", 2) == 0);
    CHECK_EQ_INT(tcp_stream_pending(&f.stream), 8);

    /* And what is still queued is the rest, in order. */
    f.link.accept_limit = UINT16_MAX;
    CHECK(!tcp_stream_flush(&f.stream, 64));
    CHECK_EQ_INT(f.link.sent_length, 10);
    CHECK(memcmp(f.link.sent, "abcdefghij", 10) == 0);
}

TEST(a_link_that_takes_nothing_consumes_nothing)
{
    fixture_t f;
    fixture_init(&f);

    CHECK_EQ_INT(tcp_stream_write(&f.stream, "abcd", 4), 4);

    f.link.accept_limit = 0;
    CHECK(tcp_stream_flush(&f.stream, 64));
    CHECK_EQ_INT(tcp_stream_pending(&f.stream), 4);
    CHECK_EQ_U32(f.stream.bytes_sent, 0);

    /* One call, not a spin: a refusal is a reason to stop, not to try harder. */
    CHECK_EQ_INT(f.link.calls, 1);
}

TEST(a_link_claiming_more_than_it_was_offered_is_not_believed)
{
    fixture_t f;
    fixture_init(&f);

    CHECK_EQ_INT(tcp_stream_write(&f.stream, "abcd", 4), 4);

    f.link.claim_extra = true;
    CHECK(!tcp_stream_flush(&f.stream, 64));

    /* Four bytes were offered, so at most four may be consumed -- not the
       fourteen the link claimed. */
    CHECK_EQ_INT(tcp_stream_pending(&f.stream), 0);
    CHECK_EQ_U32(f.stream.bytes_sent, 4);
}

TEST(flush_sends_no_more_than_the_room_it_was_given)
{
    fixture_t f;
    fixture_init(&f);

    CHECK_EQ_INT(tcp_stream_write(&f.stream, "abcdefghij", 10), 10);

    CHECK(tcp_stream_flush(&f.stream, 4));
    CHECK_EQ_INT(f.link.sent_length, 4);
    CHECK(memcmp(f.link.sent, "abcd", 4) == 0);
    CHECK_EQ_INT(tcp_stream_pending(&f.stream), 6);
}

TEST(flush_loops_past_one_staging_buffer)
{
    /*
     * The staging buffer bounds one copy, not one flush. With a ring larger
     * than TCP_STREAM_SEGMENT_MAX and room for all of it, everything goes.
     */
    tcp_stream_t stream;
    fake_link_t link;
    static uint8_t incoming[8];
    static uint8_t outgoing[TCP_STREAM_SEGMENT_MAX * 3];
    static uint8_t payload[TCP_STREAM_SEGMENT_MAX * 2];

    memset(&link, 0, sizeof(link));
    link.accept_limit = UINT16_MAX;
    memset(payload, 'z', sizeof(payload));

    CHECK(tcp_stream_init(&stream, incoming, sizeof(incoming),
                          outgoing, sizeof(outgoing), fake_send, &link));
    tcp_stream_set_connected(&stream, true);

    CHECK_EQ_INT(tcp_stream_write(&stream, payload, sizeof(payload)), sizeof(payload));
    CHECK(!tcp_stream_flush(&stream, sizeof(payload)));

    CHECK_EQ_INT(link.sent_length, sizeof(payload));
    CHECK(link.calls > 1);           /* more than one staged copy */
    CHECK_EQ_INT(tcp_stream_pending(&stream), 0);
}

TEST(flushing_with_no_connection_sends_nothing)
{
    fixture_t f;
    fixture_init(&f);

    CHECK_EQ_INT(tcp_stream_write(&f.stream, "abcd", 4), 4);
    tcp_stream_set_connected(&f.stream, false);

    CHECK(!tcp_stream_flush(&f.stream, 64));
    CHECK_EQ_INT(f.link.calls, 0);
}

/* ---------------------------------------------------------------------------
 * Input
 * -------------------------------------------------------------------------*/

TEST(received_bytes_are_readable_in_order)
{
    fixture_t f;
    fixture_init(&f);

    CHECK_EQ_INT(tcp_stream_on_received(&f.stream, "abc", 3), 3);
    CHECK_EQ_INT(tcp_stream_available(&f.stream), 3);
    CHECK_EQ_U32(f.stream.bytes_received, 3);

    CHECK_EQ_INT(tcp_stream_read(&f.stream), 'a');
    CHECK_EQ_INT(tcp_stream_read(&f.stream), 'b');
    CHECK_EQ_INT(tcp_stream_read(&f.stream), 'c');
    CHECK_EQ_INT(tcp_stream_read(&f.stream), -1);
}

TEST(read_bytes_takes_what_is_there)
{
    fixture_t f;
    fixture_init(&f);
    char out[8];

    CHECK_EQ_INT(tcp_stream_on_received(&f.stream, "abc", 3), 3);
    CHECK_EQ_INT(tcp_stream_read_bytes(&f.stream, out, sizeof(out)), 3);
    CHECK(memcmp(out, "abc", 3) == 0);
}

TEST(a_segment_that_does_not_fit_is_refused_whole)
{
    /*
     * The property the lwIP glue depends on: a declined segment is re-offered
     * by TCP once the buffer drains, so declining is back-pressure. Storing
     * the part that fits would instead put a hole in the middle of a byte
     * stream, which nothing downstream could detect.
     */
    fixture_t f;
    fixture_init(&f);

    uint8_t chunk[40];
    memset(chunk, 'a', sizeof(chunk));

    CHECK(tcp_stream_can_accept(&f.stream, sizeof(chunk)));
    CHECK_EQ_INT(tcp_stream_on_received(&f.stream, chunk, sizeof(chunk)), 40);

    /* 63 bytes of capacity, 40 used: 40 more will not fit. */
    CHECK(!tcp_stream_can_accept(&f.stream, sizeof(chunk)));
    CHECK_EQ_INT(tcp_stream_on_received(&f.stream, chunk, sizeof(chunk)), 0);

    /* Nothing was half-stored. */
    CHECK_EQ_INT(tcp_stream_available(&f.stream), 40);
    CHECK_EQ_U32(f.stream.bytes_received, 40);

    /* Drain, and the same segment now fits -- which is what re-delivery does. */
    char scratch[40];
    CHECK_EQ_INT(tcp_stream_read_bytes(&f.stream, scratch, sizeof(scratch)), 40);
    CHECK(tcp_stream_can_accept(&f.stream, sizeof(chunk)));
    CHECK_EQ_INT(tcp_stream_on_received(&f.stream, chunk, sizeof(chunk)), 40);
}

/* ---------------------------------------------------------------------------
 * Connection changes
 * -------------------------------------------------------------------------*/

TEST(closing_drops_queued_output_but_keeps_what_arrived)
{
    /*
     * A server that answers and then closes is the ordinary case. Its answer
     * has to outlive the connection or it could never be read.
     */
    fixture_t f;
    fixture_init(&f);

    CHECK_EQ_INT(tcp_stream_write(&f.stream, "request", 7), 7);
    CHECK_EQ_INT(tcp_stream_on_received(&f.stream, "answer", 6), 6);

    tcp_stream_set_connected(&f.stream, false);

    CHECK(!tcp_stream_has_output(&f.stream));
    CHECK_EQ_INT(tcp_stream_available(&f.stream), 6);

    char out[8] = { 0 };
    CHECK_EQ_INT(tcp_stream_read_bytes(&f.stream, out, 6), 6);
    CHECK_EQ_STR(out, "answer");
}

TEST(opening_discards_the_previous_sessions_input)
{
    fixture_t f;
    fixture_init(&f);

    CHECK_EQ_INT(tcp_stream_on_received(&f.stream, "stale", 5), 5);
    tcp_stream_set_connected(&f.stream, false);
    CHECK_EQ_INT(tcp_stream_available(&f.stream), 5);

    /* A reconnect starts clean: the old peer's words must not appear as the
       new one's first. */
    tcp_stream_set_connected(&f.stream, true);
    CHECK_EQ_INT(tcp_stream_available(&f.stream), 0);
    CHECK(tcp_stream_is_connected(&f.stream));
}

TEST_MAIN(
    RUN(init_rejects_what_it_cannot_work_with);
    RUN(a_null_stream_is_survivable_everywhere);
    RUN(output_is_buffered_then_sent_when_the_link_is_asked);
    RUN(writing_with_no_connection_is_dropped_and_counted);
    RUN(output_past_the_buffer_loses_its_tail_and_is_counted);
    RUN(a_partial_send_keeps_the_stream_in_order);
    RUN(a_link_that_takes_nothing_consumes_nothing);
    RUN(a_link_claiming_more_than_it_was_offered_is_not_believed);
    RUN(flush_sends_no_more_than_the_room_it_was_given);
    RUN(flush_loops_past_one_staging_buffer);
    RUN(flushing_with_no_connection_sends_nothing);
    RUN(received_bytes_are_readable_in_order);
    RUN(read_bytes_takes_what_is_there);
    RUN(a_segment_that_does_not_fit_is_refused_whole);
    RUN(closing_drops_queued_output_but_keeps_what_arrived);
    RUN(opening_discards_the_previous_sessions_input);
)
