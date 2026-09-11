#ifndef PICO_FRAMEWORK_CAN_FD_FRAME_H
#define PICO_FRAMEWORK_CAN_FD_FRAME_H

#include "can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_FD_MAX_DATA_LENGTH 64u

/* A separate type keeps the classic CAN API and queue storage unchanged.
   length is bytes, not DLC: FD permits 0..8, 12, 16, 20, 24, 32, 48, 64.
   No implicit padding. Remote requests are only valid in classic frames.
   error_state_indicator is received metadata; transmitters own their ESI. */
typedef struct {
    uint32_t id;
    uint8_t length;
    bool extended;
    bool remote;
    bool fd;
    bool bitrate_switch;
    bool error_state_indicator;
    uint8_t data[CAN_FD_MAX_DATA_LENGTH];
} can_fd_message_t;

bool can_fd_message_is_valid(const can_fd_message_t *message);
/* Invalid DLC (>15) returns 0; invalid byte length returns false. */
uint8_t can_fd_dlc_to_length(uint8_t dlc);
bool can_fd_length_to_dlc(uint8_t length, uint8_t *dlc);
bool can_fd_message_from_classic(const can_message_t *source, can_fd_message_t *dest);
/* Rejects FD frames, even if their payload would fit in eight bytes. */
bool can_fd_message_to_classic(const can_fd_message_t *source, can_message_t *dest);

#ifdef __cplusplus
}
#endif
#endif
