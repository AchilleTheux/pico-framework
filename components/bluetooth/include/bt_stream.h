/*
 * bt_stream - the buffering between a Bluetooth serial link and a byte stream.
 *
 * RFCOMM is not a byte stream. It carries packets, and it will only accept one
 * when the peer has granted credit — so an attempt to send at an arbitrary
 * moment is refused rather than queued. A CLI, meanwhile, writes whenever it
 * has something to say and expects the write to have happened.
 *
 * This is the piece in between: output is buffered until the link says it can
 * send, input is buffered until the CLI gets round to reading it. All of it is
 * free of the Pico SDK and of BTstack, so the flow control can be tested
 * against a fake link rather than by pairing a laptop.
 *
 * Both buffers are caller-owned, as everywhere else in the framework.
 */

#ifndef PICO_FRAMEWORK_BT_STREAM_H
#define PICO_FRAMEWORK_BT_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ring_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Hand `length` bytes to the link. Returns the number accepted, which may be
 * fewer than offered and may be zero — RFCOMM's own send can refuse.
 */
typedef uint16_t (*bt_stream_send_fn)(void *ctx, const uint8_t *data, uint16_t length);

typedef struct {
    ring_buffer_t incoming;
    ring_buffer_t outgoing;

    bt_stream_send_fn send;
    void *send_ctx;

    /* Most the link will take in one packet. 0 until the link reports one. */
    uint16_t mtu;

    /* True between the link offering to send and the offer being used. */
    bool may_send;

    /* True while a peer is attached. Writes are discarded when it is not:
       buffering output for a console nobody is reading would fill the buffer
       with stale text and then drop the live reply. */
    bool connected;

    /*
     * Bytes lost, and in which direction. Counted rather than hidden, because
     * a console that silently truncates is worse than one that says it did —
     * and a growing count of dropped output means the buffer is too small for
     * whatever the firmware is printing.
     */
    uint32_t dropped_outgoing;
    uint32_t dropped_incoming;

    bool initialised;
} bt_stream_t;

/*
 * `incoming` and `outgoing` are caller-owned and must outlive the stream. Both
 * must be at least two bytes; see ring_buffer_capacity() for why usable space
 * is one less than given.
 */
bool bt_stream_init(bt_stream_t *stream,
                    uint8_t *incoming, size_t incoming_size,
                    uint8_t *outgoing, size_t outgoing_size,
                    bt_stream_send_fn send, void *send_ctx);

/* Note a peer attaching or leaving. Leaving discards both buffers, since
   neither half of a conversation with a departed peer is worth keeping. */
void bt_stream_set_connected(bt_stream_t *stream, bool connected);

static inline bool bt_stream_is_connected(const bt_stream_t *stream)
{
    return stream->connected;
}

/* ---------------------------------------------------------------------------
 * Output
 * -------------------------------------------------------------------------*/

/*
 * Buffer output for the link. Returns how many bytes were accepted.
 *
 * When the buffer is full the **tail** of the write is dropped, not the head.
 * Losing the start of a reply loses the context that made it meaningful; losing
 * the end of one merely truncates it. The count is added to
 * `dropped_outgoing` either way.
 */
size_t bt_stream_write(bt_stream_t *stream, const void *data, size_t length);

/*
 * The link is ready to take a packet. Flushes as much buffered output as the
 * MTU and the link allow.
 *
 * Returns true when output still remains, which is the caller's cue to ask the
 * link for another opportunity rather than waiting for one.
 */
bool bt_stream_on_can_send(bt_stream_t *stream, uint16_t mtu);

/* Is there buffered output waiting for an opportunity to send? */
bool bt_stream_has_output(const bt_stream_t *stream);

/* ---------------------------------------------------------------------------
 * Input
 * -------------------------------------------------------------------------*/

/*
 * Take a received packet. Returns how many bytes were stored; anything beyond
 * the buffer is counted in `dropped_incoming`.
 */
size_t bt_stream_on_received(bt_stream_t *stream, const void *data, size_t length);

/* The next received byte, or -1 when there is none. Shaped for cli_stream_t. */
int bt_stream_read(bt_stream_t *stream);

static inline size_t bt_stream_available(const bt_stream_t *stream)
{
    return ring_buffer_count(&stream->incoming);
}

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_BT_STREAM_H */
