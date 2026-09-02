#include "mcp2515_frame.h"

#include <string.h>

#define SIDL_EXIDE 0x08u
#define DLC_RTR 0x40u
#define DLC_MASK 0x0Fu

void mcp2515_frame_pack_id(uint32_t packed, uint8_t out[4])
{
    const bool extended = can_id_is_extended(packed);
    const uint32_t id = can_id_value(packed);

    if (extended) {
        const uint32_t sid = (id >> 18) & 0x7FFu;
        const uint32_t eid = id & 0x3FFFFu;
        out[0] = (uint8_t)(sid >> 3);
        out[1] = (uint8_t)(((sid & 0x7u) << 5) | SIDL_EXIDE | ((eid >> 16) & 0x3u));
        out[2] = (uint8_t)(eid >> 8);
        out[3] = (uint8_t)eid;
    } else {
        out[0] = (uint8_t)(id >> 3);
        out[1] = (uint8_t)((id & 0x7u) << 5);
        out[2] = 0;
        out[3] = 0;
    }
}

void mcp2515_frame_pack_header(const can_message_t *message, uint8_t header[MCP2515_FRAME_HEADER_SIZE])
{
    const uint32_t packed = can_id_pack(message->id, message->extended, message->remote);
    mcp2515_frame_pack_id(packed, header);

    header[4] = (uint8_t)(message->length & DLC_MASK);
    if (message->remote) {
        header[4] |= DLC_RTR;
    }
}

void mcp2515_frame_unpack_header(const uint8_t header[MCP2515_FRAME_HEADER_SIZE],
                                  can_message_t *message)
{
    memset(message, 0, sizeof(*message));

    const bool extended = (header[1] & SIDL_EXIDE) != 0;
    message->extended = extended;
    if (extended) {
        const uint32_t sid = ((uint32_t)header[0] << 3) | ((header[1] >> 5) & 0x7u);
        const uint32_t eid = ((uint32_t)(header[1] & 0x3u) << 16) | ((uint32_t)header[2] << 8) |
                              header[3];
        message->id = (sid << 18) | eid;
    } else {
        message->id = ((uint32_t)header[0] << 3) | ((header[1] >> 5) & 0x7u);
    }
    message->remote = (header[4] & DLC_RTR) != 0;
    message->length = can_dlc_to_length(header[4] & DLC_MASK);
}
