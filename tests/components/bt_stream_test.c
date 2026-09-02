/*
 * Host-side tests for the Bluetooth serial buffering.
 *
 * RFCOMM will only accept a packet when the peer has granted credit, so an
 * attempt to send at an arbitrary moment is refused rather than queued. Every
 * case below is about that: output that has to wait, a link that takes fewer
 * bytes than offered, a buffer that fills, and a peer that leaves mid-reply.
 *
 * None of it is convenient to provoke by pairing a laptop, which is why the
 * adapter is free of BTstack.
 */

#include <string.h>

#include "test.h"

#include "bt_stream.h"

/* A fake link that records what it was given and can be told to refuse. */
static struct {
    uint8_t sent[4096];
    size_t sent_length;
    uint16_t accept_limit;   /* 0 means accept everything offered */
    uint16_t overstate_by;   /* claim this many bytes more than were taken */
    unsigned calls;
} g_link;

static uint16_t fake_send(void *ctx, const uint8_t *data, uint16_t length)
{
    (void)ctx;
    g_link.calls++;

    uint16_t accepted = length;
    if (g_link.accept_limit != 0 && accepted > g_link.accept_limit) {
        accepted = g_link.accept_limit;
    }
    if (g_link.sent_length + accepted > sizeof(g_link.sent)) {
        accepted = (uint16_t)(sizeof(g_link.sent) - g_link.sent_length);
    }

    memcpy(&g_link.sent[g_link.sent_length], data, accepted);
    g_link.sent_length += accepted;
    return (uint16_t)(accepted + g_link.overstate_by);
}

static uint8_t incoming_storage[64];
static uint8_t outgoing_storage[64];
static bt_stream_t stream;

static void setup(void)
{
    memset(&g_link, 0, sizeof(g_link));
    CHECK(bt_stream_init(&stream, incoming_storage, sizeof(incoming_storage),
                         outgoing_storage, sizeof(outgoing_storage),
                         fake_send, NULL));
    bt_stream_set_connected(&stream, true);
}

static const char *sent_text(void)
{
    static char text[4097];
    memcpy(text, g_link.sent, g_link.sent_length);
    text[g_link.sent_length] = '\0';
    return text;
}

/* ---------------------------------------------------------------------------
 * Output waits for permission
 * -------------------------------------------------------------------------*/

TEST(nothing_is_sent_until_the_link_offers)
{
    /* The whole reason this adapter exists: a write cannot go out immediately. */
    setup();
    CHECK_EQ_INT(bt_stream_write(&stream, "hello", 5), 5);

    CHECK_EQ_INT(g_link.calls, 0);
    CHECK(bt_stream_has_output(&stream));

    bt_stream_on_can_send(&stream, 128);
    CHECK_EQ_INT(g_link.calls, 1);
    CHECK_EQ_STR(sent_text(), "hello");
    CHECK(!bt_stream_has_output(&stream));
}

TEST(an_offer_with_nothing_buffered_sends_nothing)
{
    setup();
    CHECK(!bt_stream_on_can_send(&stream, 128));
    CHECK_EQ_INT(g_link.calls, 0);
}

TEST(several_writes_coalesce_into_one_packet)
{
    /* One packet per write would waste the link; RFCOMM has real per-packet
       overhead and the console writes in small pieces. */
    setup();
    bt_stream_write(&stream, "one ", 4);
    bt_stream_write(&stream, "two ", 4);
    bt_stream_write(&stream, "three", 5);

    bt_stream_on_can_send(&stream, 128);
    CHECK_EQ_INT(g_link.calls, 1);
    CHECK_EQ_STR(sent_text(), "one two three");
}

TEST(output_longer_than_the_mtu_takes_several_offers)
{
    setup();

    /* 40 bytes through an MTU of 10. */
    char payload[41];
    for (unsigned i = 0; i < 40; i++) {
        payload[i] = (char)('a' + (i % 26));
    }
    payload[40] = '\0';
    bt_stream_write(&stream, payload, 40);

    unsigned offers = 0;
    while (bt_stream_has_output(&stream) && offers < 20) {
        bt_stream_on_can_send(&stream, 10);
        offers++;
    }

    CHECK_EQ_INT(offers, 4);
    CHECK_EQ_INT(g_link.sent_length, 40);
    CHECK_EQ_STR(sent_text(), payload);
}

TEST(an_offer_reports_whether_more_remains)
{
    /* The caller's cue to ask for another opportunity rather than waiting for
       one, which is how a long reply gets out. */
    setup();
    bt_stream_write(&stream, "0123456789abcdef", 16);

    CHECK(bt_stream_on_can_send(&stream, 8));    /* more left */
    CHECK(!bt_stream_on_can_send(&stream, 8));   /* that was the rest */
    CHECK_EQ_STR(sent_text(), "0123456789abcdef");
}

TEST(bytes_the_link_refuses_stay_queued)
{
    /*
     * RFCOMM's send can take fewer bytes than offered. Anything dropped here
     * would be a hole in the middle of a reply.
     */
    setup();
    g_link.accept_limit = 3;

    bt_stream_write(&stream, "abcdefghij", 10);

    unsigned offers = 0;
    while (bt_stream_has_output(&stream) && offers < 20) {
        bt_stream_on_can_send(&stream, 100);
        offers++;
    }

    CHECK_EQ_STR(sent_text(), "abcdefghij");
    CHECK_EQ_INT(stream.dropped_outgoing, 0);
}

TEST(a_partly_accepted_packet_does_not_reorder_what_follows)
{
    /*
     * The case the previous test misses: the MTU is smaller than what is
     * queued, so bytes remain behind the packet being offered. Requeueing the
     * refused suffix would put it after those, and `abcdefghij` would leave as
     * `abefghijcd` — a console reply with its middle transposed, which reads
     * as corruption rather than as loss.
     */
    setup();
    g_link.accept_limit = 2;

    bt_stream_write(&stream, "abcdefghij", 10);

    unsigned offers = 0;
    while (bt_stream_has_output(&stream) && offers < 40) {
        bt_stream_on_can_send(&stream, 4);   /* MTU below the 10 queued */
        offers++;
    }

    CHECK_EQ_STR(sent_text(), "abcdefghij");
    CHECK_EQ_INT(stream.dropped_outgoing, 0);
}

TEST(a_partial_send_leaves_the_refused_bytes_at_the_front)
{
    /* The single step in isolation, so a failure says which half is wrong. */
    setup();
    g_link.accept_limit = 2;

    bt_stream_write(&stream, "abcdefghij", 10);
    CHECK(bt_stream_on_can_send(&stream, 4));
    CHECK_EQ_STR(sent_text(), "ab");

    /* Eight left, and "cd" must still be the next two. */
    g_link.accept_limit = 0;
    g_link.sent_length = 0;
    bt_stream_on_can_send(&stream, 4);
    CHECK_EQ_STR(sent_text(), "cdef");
}

TEST(a_link_claiming_more_than_it_was_offered_consumes_only_the_offer)
{
    /*
     * Defensive. The count comes back from BTstack, and a send reporting more
     * than the packet it was handed must not drag the bytes queued behind that
     * packet out of the buffer unsent.
     */
    setup();
    g_link.overstate_by = 100;
    bt_stream_write(&stream, "abcdefgh", 8);

    bt_stream_on_can_send(&stream, 3);
    CHECK_EQ_STR(sent_text(), "abc");

    /* Five left, not zero, and still in order. */
    g_link.overstate_by = 0;
    g_link.sent_length = 0;
    while (bt_stream_has_output(&stream)) {
        bt_stream_on_can_send(&stream, 8);
    }
    CHECK_EQ_STR(sent_text(), "defgh");
}

TEST(a_link_that_takes_nothing_loses_nothing)
{
    setup();
    g_link.accept_limit = 0;   /* accept everything... */
    bt_stream_write(&stream, "kept", 4);

    /* ...then change to accepting none. */
    g_link.accept_limit = 1;
    bt_stream_on_can_send(&stream, 100);
    CHECK_EQ_INT(g_link.sent_length, 1);

    /* The remaining three are still queued. */
    CHECK(bt_stream_has_output(&stream));
    g_link.accept_limit = 0;
    bt_stream_on_can_send(&stream, 100);
    CHECK_EQ_STR(sent_text(), "kept");
}

/* ---------------------------------------------------------------------------
 * Full buffers
 * -------------------------------------------------------------------------*/

TEST(a_full_output_buffer_drops_the_tail_not_the_head)
{
    /*
     * The decision worth pinning. Losing the start of a reply loses the
     * context that made it meaningful; losing the end merely truncates it.
     */
    setup();

    char payload[200];
    for (unsigned i = 0; i < sizeof(payload); i++) {
        payload[i] = (char)('A' + (i % 26));
    }

    const size_t accepted = bt_stream_write(&stream, payload, sizeof(payload));
    CHECK(accepted < sizeof(payload));
    CHECK_EQ_INT(stream.dropped_outgoing, sizeof(payload) - accepted);

    /* What did get through is the beginning, in order. */
    while (bt_stream_has_output(&stream)) {
        bt_stream_on_can_send(&stream, 512);
    }
    CHECK_EQ_INT(g_link.sent_length, accepted);
    CHECK(memcmp(g_link.sent, payload, accepted) == 0);
}

TEST(dropped_output_is_counted_rather_than_hidden)
{
    /* A growing count is how you learn the buffer is too small for what the
       firmware prints. */
    setup();

    char payload[100];
    memset(payload, 'x', sizeof(payload));

    bt_stream_write(&stream, payload, sizeof(payload));
    const uint32_t first = stream.dropped_outgoing;
    CHECK(first > 0);

    bt_stream_write(&stream, payload, sizeof(payload));
    CHECK(stream.dropped_outgoing > first);
}

TEST(a_full_input_buffer_drops_and_counts)
{
    setup();

    uint8_t packet[200];
    memset(packet, 'q', sizeof(packet));

    const size_t stored = bt_stream_on_received(&stream, packet, sizeof(packet));
    CHECK(stored < sizeof(packet));
    CHECK_EQ_INT(stream.dropped_incoming, sizeof(packet) - stored);
}

/* ---------------------------------------------------------------------------
 * Input
 * -------------------------------------------------------------------------*/

TEST(received_bytes_come_out_in_order)
{
    setup();
    bt_stream_on_received(&stream, "help\r\n", 6);

    CHECK_EQ_INT(bt_stream_available(&stream), 6);

    static const char expected[] = "help\r\n";
    for (unsigned i = 0; i < 6; i++) {
        const int byte = bt_stream_read(&stream);
        if (byte != (int)(unsigned char)expected[i]) {
            printf("    byte %u: expected '%c', got %d\n", i, expected[i], byte);
            CHECK(false);
            return;
        }
    }
    CHECK_EQ_INT(bt_stream_read(&stream), -1);
}

TEST(an_empty_input_buffer_reads_as_minus_one)
{
    /* What cli_poll() looks for, so it must be exactly this. */
    setup();
    CHECK_EQ_INT(bt_stream_read(&stream), -1);
}

TEST(input_arriving_in_several_packets_reads_as_one_stream)
{
    setup();
    bt_stream_on_received(&stream, "he", 2);
    bt_stream_on_received(&stream, "ll", 2);
    bt_stream_on_received(&stream, "o\n", 2);

    char received[8];
    size_t at = 0;
    int byte;
    while ((byte = bt_stream_read(&stream)) >= 0 && at < sizeof(received) - 1) {
        received[at++] = (char)byte;
    }
    received[at] = '\0';
    CHECK_EQ_STR(received, "hello\n");
}

/* ---------------------------------------------------------------------------
 * Connection state
 * -------------------------------------------------------------------------*/

TEST(nothing_is_buffered_with_no_peer_attached)
{
    /*
     * Otherwise the buffer fills with output nobody asked for, and then has no
     * room for the reply to whoever connects next.
     */
    setup();
    bt_stream_set_connected(&stream, false);

    CHECK_EQ_INT(bt_stream_write(&stream, "into the void", 13), 0);
    CHECK(!bt_stream_has_output(&stream));
    CHECK_EQ_INT(stream.dropped_outgoing, 13);
}

TEST(a_peer_leaving_discards_both_buffers)
{
    /* Stale text would otherwise be the first thing the next peer sees. */
    setup();
    bt_stream_write(&stream, "half a reply", 12);
    bt_stream_on_received(&stream, "half a command", 14);

    bt_stream_set_connected(&stream, false);

    CHECK(!bt_stream_has_output(&stream));
    CHECK_EQ_INT(bt_stream_available(&stream), 0);
    CHECK_EQ_INT(bt_stream_read(&stream), -1);
}

TEST(a_reconnection_starts_clean)
{
    setup();
    bt_stream_write(&stream, "old", 3);
    bt_stream_set_connected(&stream, false);
    bt_stream_set_connected(&stream, true);

    CHECK(bt_stream_is_connected(&stream));
    CHECK_EQ_INT(bt_stream_write(&stream, "new", 3), 3);

    bt_stream_on_can_send(&stream, 128);
    CHECK_EQ_STR(sent_text(), "new");
}

TEST(an_offer_while_disconnected_sends_nothing)
{
    setup();
    bt_stream_write(&stream, "queued", 6);
    bt_stream_set_connected(&stream, false);

    CHECK(!bt_stream_on_can_send(&stream, 128));
    CHECK_EQ_INT(g_link.calls, 0);
}

/* ---------------------------------------------------------------------------
 * Setup
 * -------------------------------------------------------------------------*/

TEST(init_rejects_what_cannot_work)
{
    bt_stream_t fresh;
    uint8_t a[8];
    uint8_t b[8];

    CHECK(!bt_stream_init(NULL, a, sizeof(a), b, sizeof(b), fake_send, NULL));
    CHECK(!bt_stream_init(&fresh, a, sizeof(a), b, sizeof(b), NULL, NULL));
    CHECK(!bt_stream_init(&fresh, NULL, sizeof(a), b, sizeof(b), fake_send, NULL));
    CHECK(!bt_stream_init(&fresh, a, 1, b, sizeof(b), fake_send, NULL));
    CHECK(bt_stream_init(&fresh, a, sizeof(a), b, sizeof(b), fake_send, NULL));
}

TEST(a_long_session_neither_loses_nor_reorders)
{
    /*
     * Closest to the real thing: a console replying in small pieces while a
     * link takes packets of an awkward size, run long enough for the ring
     * buffers to wrap many times.
     */
    setup();

    uint8_t next_written = 0;
    uint8_t next_expected = 0;
    unsigned total = 0;

    for (unsigned round = 0; round < 2000; round++) {
        /* Write a small burst, as a reply would. */
        for (unsigned i = 0; i < 7; i++) {
            const uint8_t byte = next_written;
            if (bt_stream_write(&stream, &byte, 1) == 1) {
                next_written++;
                total++;
            }
        }

        /* Give the link an awkward-sized opportunity. */
        g_link.sent_length = 0;
        bt_stream_on_can_send(&stream, 5);

        for (size_t i = 0; i < g_link.sent_length; i++) {
            if (g_link.sent[i] != next_expected) {
                printf("    round %u: expected 0x%02X, got 0x%02X\n", round,
                       next_expected, g_link.sent[i]);
                CHECK(false);
                return;
            }
            next_expected++;
        }
    }

    CHECK(total > 1000);
}

TEST_MAIN(
    RUN(nothing_is_sent_until_the_link_offers);
    RUN(an_offer_with_nothing_buffered_sends_nothing);
    RUN(several_writes_coalesce_into_one_packet);
    RUN(output_longer_than_the_mtu_takes_several_offers);
    RUN(an_offer_reports_whether_more_remains);
    RUN(bytes_the_link_refuses_stay_queued);
    RUN(a_partly_accepted_packet_does_not_reorder_what_follows);
    RUN(a_partial_send_leaves_the_refused_bytes_at_the_front);
    RUN(a_link_claiming_more_than_it_was_offered_consumes_only_the_offer);
    RUN(a_link_that_takes_nothing_loses_nothing);

    RUN(a_full_output_buffer_drops_the_tail_not_the_head);
    RUN(dropped_output_is_counted_rather_than_hidden);
    RUN(a_full_input_buffer_drops_and_counts);

    RUN(received_bytes_come_out_in_order);
    RUN(an_empty_input_buffer_reads_as_minus_one);
    RUN(input_arriving_in_several_packets_reads_as_one_stream);

    RUN(nothing_is_buffered_with_no_peer_attached);
    RUN(a_peer_leaving_discards_both_buffers);
    RUN(a_reconnection_starts_clean);
    RUN(an_offer_while_disconnected_sends_nothing);

    RUN(init_rejects_what_cannot_work);
    RUN(a_long_session_neither_loses_nor_reorders);
)
