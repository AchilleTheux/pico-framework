/*
 * tcp_stream - the buffering between a TCP connection and a byte stream.
 *
 * A TCP connection is a byte stream, but it is not one a caller may write to
 * whenever it likes: lwIP accepts a write only while its send buffer has room,
 * and refuses everything else. Firmware, meanwhile, writes when it has
 * something to say and expects the write to have happened.
 *
 * This is the piece in between, and it is free of the Pico SDK and of lwIP, so
 * the flow control can be tested against a fake link rather than against a
 * real peer and a real network. Both buffers are caller-owned, as everywhere
 * else in the framework.
 *
 * THE TWO DIRECTIONS ARE NOT SYMMETRIC, deliberately:
 *
 *   Output  is dropped when the buffer is full, and counted. There is nothing
 *           else to do -- the caller has already handed the bytes over and
 *           moved on, and blocking is not an option in a poll loop.
 *
 *   Input   is refused whole, never truncated. TCP has a receive window and
 *           lwIP will re-deliver a segment the application declines, so
 *           declining one is real back-pressure rather than lost data: the
 *           window closes, the peer stops sending, and the bytes arrive later
 *           when the buffer has drained. tcp_stream_can_accept() is what the
 *           lwIP glue asks before taking a segment. This is the whole reason
 *           a TCP receive buffer does not need a drop counter, where
 *           bt_stream's RFCOMM one does.
 */

#ifndef PICO_FRAMEWORK_TCP_STREAM_H
#define PICO_FRAMEWORK_TCP_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ring_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Hand `length` bytes to the link. Returns the number accepted, which may be
 * fewer than offered and may be zero -- lwIP's send buffer fills up.
 */
typedef uint16_t (*tcp_stream_send_fn)(void *ctx, const uint8_t *data, uint16_t length);

/*
 * Most bytes staged contiguously in one go before being offered to the link.
 * The ring may hold its bytes in two runs and the link takes one pointer and
 * one length, so a copy is needed; this bounds the stack cost of that copy.
 * Flushing loops, so this is not a limit on how much one flush can send.
 */
#ifndef TCP_STREAM_SEGMENT_MAX
#define TCP_STREAM_SEGMENT_MAX 512u
#endif

typedef struct {
    ring_buffer_t incoming;
    ring_buffer_t outgoing;

    tcp_stream_send_fn send;
    void *send_ctx;

    /* True while a connection is up. Writes are discarded when it is not:
       buffering output for a peer that has gone would fill the buffer with
       stale bytes and leave no room for the next peer's. */
    bool connected;

    uint32_t bytes_sent;
    uint32_t bytes_received;

    /* Output lost to a full buffer. Counted rather than hidden -- a growing
       count means the buffer is too small for what the firmware is writing. */
    uint32_t dropped_outgoing;

    bool initialised;
} tcp_stream_t;

/*
 * `incoming` and `outgoing` are caller-owned and must outlive the stream. Both
 * must be at least two bytes; see ring_buffer_capacity() for why usable space
 * is one less than given.
 */
bool tcp_stream_init(tcp_stream_t *stream,
                     uint8_t *incoming, size_t incoming_size,
                     uint8_t *outgoing, size_t outgoing_size,
                     tcp_stream_send_fn send, void *send_ctx);

/*
 * Note a connection opening or closing.
 *
 * The two directions are cleared at different moments, and it matters:
 *
 *   Closing  discards queued output, which can no longer reach anyone, but
 *            keeps whatever has already arrived. A server that answers and
 *            then closes is the ordinary case, and its answer is still worth
 *            reading after the connection has gone.
 *
 *   Opening  discards stale input, so bytes from a previous session cannot
 *            appear at the front of a new one.
 */
void tcp_stream_set_connected(tcp_stream_t *stream, bool connected);

static inline bool tcp_stream_is_connected(const tcp_stream_t *stream)
{
    return stream->connected;
}

/* ---------------------------------------------------------------------------
 * Output
 * -------------------------------------------------------------------------*/

/*
 * Buffer output for the link. Returns how many bytes were accepted.
 *
 * When the buffer is full the tail of the write is dropped, not the head, and
 * the count is added to `dropped_outgoing`. Losing the start of a message
 * loses the context that made the rest of it mean anything.
 */
size_t tcp_stream_write(tcp_stream_t *stream, const void *data, size_t length);

/*
 * Push buffered output at the link, up to `room` bytes -- which is what the
 * link says it can take right now, lwIP's tcp_sndbuf() in the real case.
 *
 * Returns true when output still remains, which is the caller's cue to try
 * again after the link reports more room rather than to wait for a timer.
 */
bool tcp_stream_flush(tcp_stream_t *stream, size_t room);

bool tcp_stream_has_output(const tcp_stream_t *stream);

static inline size_t tcp_stream_pending(const tcp_stream_t *stream)
{
    return ring_buffer_count(&stream->outgoing);
}

/* ---------------------------------------------------------------------------
 * Input
 * -------------------------------------------------------------------------*/

/*
 * Is there room for a segment of `length` bytes, whole?
 *
 * Asked before accepting one, so that a segment which does not fit can be
 * declined and re-delivered instead of half-stored. See the header comment.
 */
bool tcp_stream_can_accept(const tcp_stream_t *stream, size_t length);

/*
 * Take a received segment. Returns how many bytes were stored, which is all of
 * them or none: a caller that checked tcp_stream_can_accept() first never sees
 * a partial store, and one that did not gets nothing rather than a hole in the
 * middle of a stream.
 */
size_t tcp_stream_on_received(tcp_stream_t *stream, const void *data, size_t length);

/* The next received byte, or -1 when there is none. Shaped for cli_stream_t. */
int tcp_stream_read(tcp_stream_t *stream);

size_t tcp_stream_read_bytes(tcp_stream_t *stream, void *data, size_t length);

static inline size_t tcp_stream_available(const tcp_stream_t *stream)
{
    return ring_buffer_count(&stream->incoming);
}

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_TCP_STREAM_H */
