/*
 * can_frame - CAN identifiers, frame lengths, acceptance filters, and the
 * queue between the interrupt and the main loop.
 *
 * Everything about a CAN frame that is arithmetic rather than radio, free of
 * the Pico SDK and of can2040, so all of it is unit-tested on the host.
 *
 * Each piece here exists because getting it wrong fails *quietly*:
 *
 *   Identifiers   a standard frame's id is 11 bits and an extended frame's is
 *                 29, with the distinction carried in a flag rather than in the
 *                 value. An 11-bit id sent as extended reaches nothing, and no
 *                 error is reported by either end.
 *
 *   Length        the length field holds 0 to 15 but a CAN 2.0 frame carries at
 *                 most 8 bytes. A frame claiming 12 has 8; reading 12 reads
 *                 four bytes of whatever follows.
 *
 *   Filters       a mask with a bit clear accepts both values of that bit. A
 *                 mask of zero accepts the entire bus, which looks exactly like
 *                 a working filter until two nodes talk at once.
 *
 *   The queue     frames arrive in an interrupt and are read in the main loop.
 *                 A partially written frame is not a dropped frame, it is a
 *                 corrupt one, which is worse.
 */

#ifndef PICO_FRAMEWORK_CAN_FRAME_H
#define PICO_FRAMEWORK_CAN_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ring_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A CAN 2.0 frame carries at most this many bytes, whatever the length field
   says. */
#define CAN_MAX_DATA_LENGTH 8u

#define CAN_STANDARD_ID_MAX 0x7FFu       /* 11 bits */
#define CAN_EXTENDED_ID_MAX 0x1FFFFFFFu  /* 29 bits */

/*
 * Flags as can2040 packs them into its id word. Reproduced here rather than
 * included from it, so this file stays free of the library and testable — and
 * asserted against it where the two meet.
 */
#define CAN_FLAG_RTR (1u << 30)
#define CAN_FLAG_EXTENDED (1u << 31)

/*
 * A frame, in the form an application wants: the identifier as a number, and
 * the flags as flags. Converting to and from the packed word is a documented,
 * tested step rather than something a caller does with shifts.
 */
typedef struct {
    uint32_t id;                        /* 11 or 29 bits, no flags */
    uint8_t length;                     /* 0 to CAN_MAX_DATA_LENGTH */
    bool extended;
    bool remote;                        /* a remote transmission request */
    uint8_t data[CAN_MAX_DATA_LENGTH];
} can_message_t;

/* True when the message can be represented by a CAN 2.0 frame. */
bool can_message_is_valid(const can_message_t *message);

/* ---------------------------------------------------------------------------
 * Identifiers
 * -------------------------------------------------------------------------*/

static inline bool can_id_is_extended(uint32_t packed)
{
    return (packed & CAN_FLAG_EXTENDED) != 0;
}

static inline bool can_id_is_remote(uint32_t packed)
{
    return (packed & CAN_FLAG_RTR) != 0;
}

/* The identifier without its flags, masked to the width its kind allows. */
uint32_t can_id_value(uint32_t packed);

/* Pack an identifier and its flags into the word the library uses. */
uint32_t can_id_pack(uint32_t id, bool extended, bool remote);

/*
 * True when `id` fits the width `extended` implies. An 11-bit bus given a
 * 12-bit identifier transmits something else entirely, so this is checked
 * before a frame is queued rather than discovered on a scope.
 */
bool can_id_is_valid(uint32_t id, bool extended);

/* ---------------------------------------------------------------------------
 * Length
 * -------------------------------------------------------------------------*/

/*
 * Bytes a frame actually carries, for a length field of 0 to 15.
 *
 * Anything above 8 is 8. CAN 2.0 allows the field to hold up to 15 and some
 * nodes do send it; treating 12 as twelve reads four bytes past the frame.
 */
uint8_t can_dlc_to_length(uint32_t dlc);

/* ---------------------------------------------------------------------------
 * Acceptance filters
 * -------------------------------------------------------------------------*/

/*
 * A frame is accepted when every bit the mask sets matches.
 *
 * Both fields hold packed identifiers, flags included, so a filter can select
 * on the extended and remote bits as well as on the value:
 *
 *   standard frames only    .id = 0,                .mask = CAN_FLAG_EXTENDED
 *   one exact standard id   .id = can_id_pack(0x123, false, false),
 *                           .mask = CAN_STANDARD_ID_MAX | CAN_FLAG_EXTENDED
 *   a block of eight ids    .id = can_id_pack(0x120, false, false),
 *                           .mask = 0x7F8 | CAN_FLAG_EXTENDED
 *   data frames, not remote .id = 0,                .mask = CAN_FLAG_RTR
 */
typedef struct {
    uint32_t id;
    uint32_t mask;
} can_filter_t;

bool can_filter_matches(const can_filter_t *filter, uint32_t packed_id);

/*
 * True when any filter accepts the frame.
 *
 * **An empty set accepts everything.** That is the useful default — a bus is
 * worth watching before it is worth filtering — and the opposite convention
 * would make a caller that forgot to install filters see a silent, completely
 * dead bus.
 */
bool can_filters_accept(const can_filter_t *filters, size_t count, uint32_t packed_id);

/* ---------------------------------------------------------------------------
 * The queue between the interrupt and the main loop
 *
 * Frames arrive in can2040's callback, which runs in interrupt context, and are
 * read by the main loop. Whole frames go in or none of one does.
 * -------------------------------------------------------------------------*/

/* Bytes one frame occupies in the queue. */
#define CAN_QUEUE_RECORD_SIZE 16u

/* Storage for `n` frames. */
#define CAN_QUEUE_STORAGE_SIZE(n) ((n) * CAN_QUEUE_RECORD_SIZE + 1u)

typedef struct {
    ring_buffer_t buffer;

    /*
     * Frames dropped because the queue was full. Counted rather than hidden: on
     * a busy bus this is the number that tells you the queue is too small or the
     * filters too permissive, and silence would leave a gap in a log with
     * nothing to explain it.
     */
    uint32_t dropped;
    bool initialised;
} can_queue_t;

/* `storage` is caller-owned and must hold at least CAN_QUEUE_STORAGE_SIZE(1). */
bool can_queue_init(can_queue_t *queue, uint8_t *storage, size_t size);

/*
 * Add a valid frame. Returns false for an invalid frame. When there is not
 * room for the whole frame it also counts a drop — never room for part.
 *
 * Safe to call from an interrupt while the main loop reads, and only from one
 * such producer; see ring_buffer.h.
 */
bool can_queue_push(can_queue_t *queue, const can_message_t *message);

/* Take the oldest frame. False when there is none. */
bool can_queue_pop(can_queue_t *queue, can_message_t *message);

/* Frames waiting. */
size_t can_queue_count(const can_queue_t *queue);

/* Frames the queue can hold. */
size_t can_queue_capacity(const can_queue_t *queue);

void can_queue_clear(can_queue_t *queue);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_CAN_FRAME_H */
