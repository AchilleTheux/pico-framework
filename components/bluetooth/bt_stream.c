#include <string.h>

#include "bt_stream.h"

bool bt_stream_init(bt_stream_t *stream,
                    uint8_t *incoming, size_t incoming_size,
                    uint8_t *outgoing, size_t outgoing_size,
                    bt_stream_send_fn send, void *send_ctx)
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

void bt_stream_set_connected(bt_stream_t *stream, bool connected)
{
    if (stream == NULL || !stream->initialised) {
        return;
    }

    if (!connected) {
        /* Neither half of a conversation with a departed peer is worth keeping,
           and stale output would otherwise be the first thing the next peer
           sees. */
        ring_buffer_clear(&stream->incoming);
        ring_buffer_clear(&stream->outgoing);
        stream->may_send = false;
        stream->mtu = 0;
    }
    stream->connected = connected;
}

size_t bt_stream_write(bt_stream_t *stream, const void *data, size_t length)
{
    if (stream == NULL || !stream->initialised || data == NULL) {
        return 0;
    }

    /*
     * Nothing is buffered with no peer attached. Keeping it would fill the
     * buffer with text nobody asked for and then have no room for the reply to
     * whoever connects next.
     */
    if (!stream->connected) {
        stream->dropped_outgoing += (uint32_t)length;
        return 0;
    }

    const size_t written = ring_buffer_write(&stream->outgoing, data, length);
    if (written < length) {
        /* The tail is what goes. Losing the start of a reply loses the context
           that made it mean anything. */
        stream->dropped_outgoing += (uint32_t)(length - written);
    }
    return written;
}

bool bt_stream_has_output(const bt_stream_t *stream)
{
    if (stream == NULL || !stream->initialised) {
        return false;
    }
    return ring_buffer_count(&stream->outgoing) > 0;
}

bool bt_stream_on_can_send(bt_stream_t *stream, uint16_t mtu)
{
    if (stream == NULL || !stream->initialised || !stream->connected) {
        return false;
    }

    stream->mtu = mtu;
    stream->may_send = true;

    const size_t pending = ring_buffer_count(&stream->outgoing);
    if (pending == 0 || mtu == 0) {
        return false;
    }

    /*
     * One packet's worth. The buffer is a ring, so the bytes may be in two
     * runs; they are copied out contiguously first because the link takes a
     * single pointer and length.
     */
    uint8_t packet[512];
    size_t room = (mtu < sizeof(packet)) ? mtu : sizeof(packet);
    if (room > pending) {
        room = pending;
    }

    /* Peeked rather than consumed: the link may accept fewer bytes than
       offered, and anything it did not take has to stay queued. */
    const size_t staged = ring_buffer_read(&stream->outgoing, packet, room);
    if (staged == 0) {
        return false;
    }

    const uint16_t accepted = stream->send(stream->send_ctx, packet, (uint16_t)staged);
    stream->may_send = false;

    if (accepted < staged) {
        /*
         * Put back what was refused, at the front. ring_buffer has no
         * push-front, so the remainder is written after whatever arrived in the
         * meantime — which cannot happen here, because writes and this flush
         * both run from the same context.
         */
        const size_t remaining = staged - accepted;
        const size_t requeued = ring_buffer_write(&stream->outgoing,
                                                  &packet[accepted], remaining);
        if (requeued < remaining) {
            stream->dropped_outgoing += (uint32_t)(remaining - requeued);
        }
    }

    return ring_buffer_count(&stream->outgoing) > 0;
}

size_t bt_stream_on_received(bt_stream_t *stream, const void *data, size_t length)
{
    if (stream == NULL || !stream->initialised || data == NULL) {
        return 0;
    }

    const size_t stored = ring_buffer_write(&stream->incoming, data, length);
    if (stored < length) {
        stream->dropped_incoming += (uint32_t)(length - stored);
    }
    return stored;
}

int bt_stream_read(bt_stream_t *stream)
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
