/*
 * mcp2515_frame - packing a can_message_t identifier and length into the
 * SIDH/SIDL/EID8/EID0/DLC byte layout the MCP2515 uses in every TX and RX
 * buffer, and back.
 *
 * SDK-independent so the bit-level layout — the part that fails quietly,
 * as a frame with the wrong identifier rather than an error — is
 * host-tested rather than trusted from an SPI capture the first time it
 * runs on hardware.
 */

#ifndef PICO_FRAMEWORK_MCP2515_FRAME_H
#define PICO_FRAMEWORK_MCP2515_FRAME_H

#include <stdint.h>

#include "can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SIDH, SIDL, EID8, EID0, DLC — every TX/RX buffer starts with these five
   bytes, followed by up to CAN_MAX_DATA_LENGTH data bytes. */
#define MCP2515_FRAME_HEADER_SIZE 5u

/*
 * Pack `message`'s identifier, extended/remote flags, and length into the
 * five-byte header a TX buffer expects, starting at SIDH. Does not touch
 * `message->data`; the caller streams that separately when the frame is not
 * a remote request.
 */
void mcp2515_frame_pack_header(const can_message_t *message, uint8_t header[MCP2515_FRAME_HEADER_SIZE]);

/*
 * The reverse: fill `message`'s id/extended/remote/length from a five-byte
 * SIDH..DLC header read out of an RX buffer. Does not touch `message->data`;
 * the caller reads that separately when `!message->remote`.
 */
void mcp2515_frame_unpack_header(const uint8_t header[MCP2515_FRAME_HEADER_SIZE],
                                  can_message_t *message);

/*
 * Pack a filter or mask's 29-bit value into the four-byte SIDH/SIDL/EID8/EID0
 * layout RXFn and RXMn registers share with TX/RX buffers (everything but
 * DLC). `packed` carries CAN_FLAG_EXTENDED as can_id_pack() produces it; for
 * a mask, pass CAN_FLAG_EXTENDED set so the extended-vs-standard bit is
 * always compared — the MCP2515 does not support masking it off, only
 * matching an exact frame type per filter.
 */
void mcp2515_frame_pack_id(uint32_t packed, uint8_t out[4]);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_MCP2515_FRAME_H */
