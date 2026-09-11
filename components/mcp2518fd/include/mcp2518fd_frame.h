#ifndef PICO_FRAMEWORK_MCP2518FD_FRAME_H
#define PICO_FRAMEWORK_MCP2518FD_FRAME_H

#include "can_fd_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MCP2518FD_OBJECT_SIZE 72u
#define MCP2518FD_MAX_FILTERS 32u

/* Message RAM objects with RX timestamps disabled. All words little endian.
   TX ESI is controlled by the chip; incoming ESI metadata is not forwarded. */
bool mcp2518fd_frame_pack(const can_fd_message_t *message, uint8_t object[MCP2518FD_OBJECT_SIZE]);
bool mcp2518fd_frame_unpack(const uint8_t object[MCP2518FD_OBJECT_SIZE], can_fd_message_t *message);

/* Every filter has an independent mask. A mask must select EXTENDED and
   must not select RTR; ID bits must fit the selected frame format.
   NULL/0 is the separately handled accept-all configuration. */
bool mcp2518fd_filter_pack(const can_filter_t *filter, uint32_t *object, uint32_t *mask);

/* SPI CRC: non-reflected polynomial 0x8005, seed 0xffff, no final XOR.
   Pass the returned value back to extend a CRC over another byte span. */
uint16_t mcp2518fd_crc16(uint16_t crc, const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif
#endif
