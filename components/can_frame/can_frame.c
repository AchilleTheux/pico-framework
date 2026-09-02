#include <string.h>

#include "can_frame.h"

bool can_message_is_valid(const can_message_t *message)
{
    return message != NULL && message->length <= CAN_MAX_DATA_LENGTH &&
           can_id_is_valid(message->id, message->extended);
}

uint32_t can_id_value(uint32_t packed)
{
    /* Masked to the width the frame's kind allows, so a stray high bit in a
       standard identifier cannot be mistaken for part of the value. */
    return can_id_is_extended(packed) ? (packed & CAN_EXTENDED_ID_MAX)
                                      : (packed & CAN_STANDARD_ID_MAX);
}

uint32_t can_id_pack(uint32_t id, bool extended, bool remote)
{
    uint32_t packed = extended ? (id & CAN_EXTENDED_ID_MAX)
                               : (id & CAN_STANDARD_ID_MAX);
    if (extended) {
        packed |= CAN_FLAG_EXTENDED;
    }
    if (remote) {
        packed |= CAN_FLAG_RTR;
    }
    return packed;
}

bool can_id_is_valid(uint32_t id, bool extended)
{
    return id <= (extended ? CAN_EXTENDED_ID_MAX : CAN_STANDARD_ID_MAX);
}

uint8_t can_dlc_to_length(uint32_t dlc)
{
    /* The field holds 0 to 15 and some nodes do send more than 8, but a CAN 2.0
       frame carries at most 8 bytes. Clamping here is what stops a caller
       reading past the end of one. */
    return (dlc > CAN_MAX_DATA_LENGTH) ? (uint8_t)CAN_MAX_DATA_LENGTH : (uint8_t)dlc;
}

bool can_filter_matches(const can_filter_t *filter, uint32_t packed_id)
{
    if (filter == NULL) {
        return false;
    }
    /* Only the bits the mask sets have to agree. A mask of zero therefore
       accepts everything, which is the documented meaning. */
    return (packed_id & filter->mask) == (filter->id & filter->mask);
}

bool can_filters_accept(const can_filter_t *filters, size_t count, uint32_t packed_id)
{
    /*
     * No filters means no filtering. The other convention — nothing accepted
     * until something is installed — would give a caller who forgot them a
     * completely silent bus with no indication why.
     */
    if (filters == NULL || count == 0) {
        return true;
    }

    for (size_t i = 0; i < count; i++) {
        if (can_filter_matches(&filters[i], packed_id)) {
            return true;
        }
    }
    return false;
}

/* ---------------------------------------------------------------------------
 * The queue
 *
 * Frames are serialised into a fixed-size record rather than stored as the
 * struct, so the layout does not depend on the compiler's padding and a
 * partially written record can be prevented by checking one length.
 * -------------------------------------------------------------------------*/

#define OFFSET_ID 0u      /* 4 bytes, little-endian */
#define OFFSET_LENGTH 4u
#define OFFSET_FLAGS 5u
#define OFFSET_DATA 6u    /* 8 bytes */
/* bytes 14 and 15 are padding, so a record is a round 16 */

#define FLAG_EXTENDED 0x01u
#define FLAG_REMOTE 0x02u

bool can_queue_init(can_queue_t *queue, uint8_t *storage, size_t size)
{
    if (queue == NULL) {
        return false;
    }

    memset(queue, 0, sizeof(*queue));
    if (!ring_buffer_init(&queue->buffer, storage, size)) {
        return false;
    }
    if (ring_buffer_capacity(&queue->buffer) < CAN_QUEUE_RECORD_SIZE) {
        return false;
    }

    queue->initialised = true;
    return true;
}

bool can_queue_push(can_queue_t *queue, const can_message_t *message)
{
    if (queue == NULL || !queue->initialised || !can_message_is_valid(message)) {
        return false;
    }

    /*
     * Checked before anything is written. ring_buffer_write() stores what fits
     * and reports it, which for a stream is right and for a frame is not: half
     * a frame is not a dropped frame, it is a corrupt one, and it would put
     * every record after it out of step.
     */
    if (ring_buffer_free(&queue->buffer) < CAN_QUEUE_RECORD_SIZE) {
        queue->dropped++;
        return false;
    }

    uint8_t record[CAN_QUEUE_RECORD_SIZE];
    memset(record, 0, sizeof(record));

    record[OFFSET_ID + 0] = (uint8_t)message->id;
    record[OFFSET_ID + 1] = (uint8_t)(message->id >> 8);
    record[OFFSET_ID + 2] = (uint8_t)(message->id >> 16);
    record[OFFSET_ID + 3] = (uint8_t)(message->id >> 24);

    const uint8_t length = message->length;
    record[OFFSET_LENGTH] = length;
    record[OFFSET_FLAGS] = (uint8_t)((message->extended ? FLAG_EXTENDED : 0u) |
                                     (message->remote ? FLAG_REMOTE : 0u));
    if (!message->remote) {
        memcpy(&record[OFFSET_DATA], message->data, length);
    }

    return ring_buffer_write(&queue->buffer, record, sizeof(record)) == sizeof(record);
}

bool can_queue_pop(can_queue_t *queue, can_message_t *message)
{
    if (queue == NULL || !queue->initialised || message == NULL) {
        return false;
    }
    if (ring_buffer_count(&queue->buffer) < CAN_QUEUE_RECORD_SIZE) {
        return false;
    }

    uint8_t record[CAN_QUEUE_RECORD_SIZE];
    if (ring_buffer_read(&queue->buffer, record, sizeof(record)) != sizeof(record)) {
        return false;
    }

    memset(message, 0, sizeof(*message));
    message->id = (uint32_t)record[OFFSET_ID + 0] |
                  ((uint32_t)record[OFFSET_ID + 1] << 8) |
                  ((uint32_t)record[OFFSET_ID + 2] << 16) |
                  ((uint32_t)record[OFFSET_ID + 3] << 24);
    message->length = can_dlc_to_length(record[OFFSET_LENGTH]);
    message->extended = (record[OFFSET_FLAGS] & FLAG_EXTENDED) != 0;
    message->remote = (record[OFFSET_FLAGS] & FLAG_REMOTE) != 0;
    memcpy(message->data, &record[OFFSET_DATA], message->length);

    return true;
}

size_t can_queue_count(const can_queue_t *queue)
{
    if (queue == NULL || !queue->initialised) {
        return 0;
    }
    return ring_buffer_count(&queue->buffer) / CAN_QUEUE_RECORD_SIZE;
}

size_t can_queue_capacity(const can_queue_t *queue)
{
    if (queue == NULL || !queue->initialised) {
        return 0;
    }
    return ring_buffer_capacity(&queue->buffer) / CAN_QUEUE_RECORD_SIZE;
}

void can_queue_clear(can_queue_t *queue)
{
    if (queue != NULL && queue->initialised) {
        ring_buffer_clear(&queue->buffer);
    }
}
