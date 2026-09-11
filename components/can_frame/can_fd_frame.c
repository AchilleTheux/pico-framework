#include <string.h>
#include "can_fd_frame.h"

static const uint8_t lengths[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};

uint8_t can_fd_dlc_to_length(uint8_t dlc)
{
    return dlc < 16 ? lengths[dlc] : 0;
}

bool can_fd_length_to_dlc(uint8_t length, uint8_t *dlc)
{
    if (dlc == NULL) return false;
    for (uint8_t i = 0; i < 16; i++) {
        if (lengths[i] == length) {
            *dlc = i;
            return true;
        }
    }
    return false;
}

bool can_fd_message_is_valid(const can_fd_message_t *message)
{
    if (message == NULL || !can_id_is_valid(message->id, message->extended)) return false;
    if (!message->fd) {
        return message->length <= CAN_MAX_DATA_LENGTH && !message->bitrate_switch &&
               !message->error_state_indicator;
    }
    uint8_t dlc;
    return !message->remote && can_fd_length_to_dlc(message->length, &dlc);
}

bool can_fd_message_from_classic(const can_message_t *source, can_fd_message_t *dest)
{
    if (dest == NULL || !can_message_is_valid(source)) return false;
    *dest = (can_fd_message_t){
        .id = source->id, .length = source->length,
        .extended = source->extended, .remote = source->remote,
    };
    if (!source->remote) memcpy(dest->data, source->data, source->length);
    return true;
}

bool can_fd_message_to_classic(const can_fd_message_t *source, can_message_t *dest)
{
    if (dest == NULL || !can_fd_message_is_valid(source) || source->fd) return false;
    *dest = (can_message_t){
        .id = source->id, .length = source->length,
        .extended = source->extended, .remote = source->remote,
    };
    if (!source->remote) memcpy(dest->data, source->data, source->length);
    return true;
}
