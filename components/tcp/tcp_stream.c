#include <string.h>

#include "tcp_stream.h"

bool tcp_stream_init(tcp_stream_t *stream,
                     uint8_t *incoming, size_t incoming_size,
                     uint8_t *outgoing, size_t outgoing_size,
                     tcp_stream_send_fn send, void *send_ctx)
{
    if (stream == NULL || send == NULL) {
        return false;
    }

    memset(stream, 0, sizeof(*stream));

    if (!ring_buffer_init(&stream->incoming, incoming, incoming_size) ||
        !ring_buffer_init(&stream->outgoing, outgoing, outgoing_size)) {
        return false;
    }

    stream->send = send;
    stream->send_ctx = send_ctx;
    stream->initialised = true;
    return true;
}

void tcp_stream_set_connected(tcp_stream_t *stream, bool connected)
{
    if (stream == NULL || !stream->initialised) {
        return;
    }

    if (connected) {
        /* A new session starts clean; anything left from the last one would
           otherwise be read as this peer's first words. */
        ring_buffer_clear(&stream->incoming);
    } else {
        /* Output cannot reach a peer that has gone. Input is left alone: a
           server that answers and closes is normal, and the answer is still
           worth reading. */
        ring_buffer_clear(&stream->outgoing);
    }
    stream->connected = connected;
}

size_t tcp_stream_write(tcp_stream_t *stream, const void *data, size_t length)
{
    if (stream == NULL || !stream->initialised || data == NULL) {
        return 0;
    }

    if (!stream->connected) {
        stream->dropped_outgoing += (uint32_t)length;
        return 0;
    }

    const size_t written = ring_buffer_write(&stream->outgoing, data, length);
    if (written < length) {
        stream->dropped_outgoing += (uint32_t)(length - written);
    }
    return written;
}

bool tcp_stream_has_output(const tcp_stream_t *stream)
{
    if (stream == NULL || !stream->initialised) {
        return false;
    }
    return ring_buffer_count(&stream->outgoing) > 0;
}

bool tcp_stream_flush(tcp_stream_t *stream, size_t room)
{
    if (stream == NULL || !stream->initialised || !stream->connected) {
        return false;
    }

    while (room > 0) {
        const size_t pending = ring_buffer_count(&stream->outgoing);
        if (pending == 0) {
            return false;
        }

        size_t chunk = (room < pending) ? room : pending;
        if (chunk > TCP_STREAM_SEGMENT_MAX) {
            chunk = TCP_STREAM_SEGMENT_MAX;
        }

        /*
         * Peeked rather than consumed. The link may take fewer bytes than it
         * was offered, and there is no way to put the remainder back at the
         * front of a ring: writing it again would queue it behind the bytes
         * already waiting, so a partially accepted "abcd" out of a queued
         * "abcdefghij" would leave the wire carrying "abefghijcd". Nothing is
         * dequeued until the link has said how much it took.
         */
        uint8_t segment[TCP_STREAM_SEGMENT_MAX];
        const size_t staged = ring_buffer_peek_bytes(&stream->outgoing, segment, chunk);
        if (staged == 0) {
            return false;
        }

        const uint16_t accepted = stream->send(stream->send_ctx, segment, (uint16_t)staged);

        /* A link that claimed more than it was offered would otherwise consume
           bytes it never saw. */
        const size_t taken = (accepted < staged) ? (size_t)accepted : staged;
        ring_buffer_discard(&stream->outgoing, taken);
        stream->bytes_sent += (uint32_t)taken;

        if (taken < staged) {
            /* The link is full, whatever it said about room. Stop rather than
               spin: the next opportunity comes from lwIP acknowledging data,
               not from trying harder now. */
            break;
        }
        room -= taken;
    }

    return ring_buffer_count(&stream->outgoing) > 0;
}

bool tcp_stream_can_accept(const tcp_stream_t *stream, size_t length)
{
    if (stream == NULL || !stream->initialised) {
        return false;
    }
    return ring_buffer_free(&stream->incoming) >= length;
}

size_t tcp_stream_on_received(tcp_stream_t *stream, const void *data, size_t length)
{
    if (stream == NULL || !stream->initialised || data == NULL) {
        return 0;
    }

    /*
     * All or nothing. Storing what fits would put a hole in the middle of a
     * byte stream, which is worse than declining the segment and letting TCP
     * re-deliver it once the buffer has drained.
     */
    if (!tcp_stream_can_accept(stream, length)) {
        return 0;
    }

    const size_t stored = ring_buffer_write(&stream->incoming, data, length);
    stream->bytes_received += (uint32_t)stored;
    return stored;
}

int tcp_stream_read(tcp_stream_t *stream)
{
    if (stream == NULL || !stream->initialised) {
        return -1;
    }

    uint8_t byte = 0;
    if (!ring_buffer_pop(&stream->incoming, &byte)) {
        return -1;
    }
    return (int)byte;
}

size_t tcp_stream_read_bytes(tcp_stream_t *stream, void *data, size_t length)
{
    if (stream == NULL || !stream->initialised || data == NULL) {
        return 0;
    }
    return ring_buffer_read(&stream->incoming, data, length);
}
