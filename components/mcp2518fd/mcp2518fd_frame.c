#include <string.h>
#include "mcp2518fd_frame.h"

static uint32_t pack_id(uint32_t id, bool extended)
{
    return extended ? (id >> 18) | ((id & 0x3ffffu) << 11) : id;
}

static uint32_t get_word(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool mcp2518fd_frame_pack(const can_fd_message_t *message, uint8_t object[MCP2518FD_OBJECT_SIZE])
{
    if (object == NULL || !can_fd_message_is_valid(message)) return false;
    uint8_t dlc;
    if (!can_fd_length_to_dlc(message->length, &dlc)) return false;
    memset(object, 0, MCP2518FD_OBJECT_SIZE);
    uint32_t id = pack_id(message->id, message->extended);
    for (unsigned i = 0; i < 4; i++) object[i] = (uint8_t)(id >> (8 * i));
    object[4] = dlc | (message->extended ? 0x10u : 0) | (message->remote ? 0x20u : 0) |
                (message->bitrate_switch ? 0x40u : 0) | (message->fd ? 0x80u : 0);
    if (!message->remote) memcpy(object + 8, message->data, message->length);
    return true;
}

bool mcp2518fd_frame_unpack(const uint8_t object[MCP2518FD_OBJECT_SIZE], can_fd_message_t *message)
{
    if (object == NULL || message == NULL) return false;
    uint32_t id = get_word(object);
    can_fd_message_t result = {
        .extended = (object[4] & 0x10u) != 0,
        .remote = (object[4] & 0x20u) != 0,
        .fd = (object[4] & 0x80u) != 0,
    };
    result.id = result.extended ? ((id & 0x7ffu) << 18) | ((id >> 11) & 0x3ffffu) : id & 0x7ffu;
    /* BRS and ESI have no meaning on classic frames. */
    result.bitrate_switch = result.fd && (object[4] & 0x40u) != 0;
    result.error_state_indicator = result.fd && (object[5] & 1u) != 0;
    result.length = result.fd ? can_fd_dlc_to_length(object[4] & 15u) : can_dlc_to_length(object[4] & 15u);
    if (!can_fd_message_is_valid(&result)) return false;
    if (!result.remote) memcpy(result.data, object + 8, result.length);
    *message = result;
    return true;
}

bool mcp2518fd_filter_pack(const can_filter_t *filter, uint32_t *object, uint32_t *mask)
{
    if (filter == NULL || object == NULL || mask == NULL) return false;
    bool extended = can_id_is_extended(filter->id);
    uint32_t allowed = (extended ? CAN_EXTENDED_ID_MAX : CAN_STANDARD_ID_MAX) | CAN_FLAG_EXTENDED;
    if ((filter->mask & CAN_FLAG_EXTENDED) == 0 || (filter->id & ~allowed) != 0 ||
        (filter->mask & ~allowed) != 0) return false;
    *object = pack_id(filter->id & CAN_EXTENDED_ID_MAX, extended) | (extended ? 1u << 30 : 0);
    *mask = pack_id(filter->mask & CAN_EXTENDED_ID_MAX, extended) | (1u << 30);
    return true;
}

uint16_t mcp2518fd_crc16(uint16_t crc, const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (unsigned bit = 0; bit < 8; bit++) {
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000u) ? 0x8005u : 0));
        }
    }
    return crc;
}
