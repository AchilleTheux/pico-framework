/*
 * firmware_service - updating the firmware over whatever the CLI is attached to.
 *
 * Puts the pieces together: a line filter that claims Intel HEX records, a
 * page writer backed by flash_storage, and a set of CLI commands. An
 * application gets serial firmware update by registering both with its CLI.
 *
 * The exchange mirrors the one the reference firmware used — begin, stream the
 * records, apply — with a verification step added between the last two, since
 * checking the image before overwriting the running one costs a second and
 * removes the main way this goes wrong.
 *
 *     > fwbegin              erase staging, start listening for records
 *     :020000041000EA        (the .hex file, streamed straight in)
 *     :10000000...
 *     :00000001FF
 *     > fwstatus             what arrived
 *     > fwverify <crc32>     compare what landed against the sender's checksum
 *     > fwapply              install it and reboot
 *
 * fwapply is the only step that can leave a board unbootable, and it is
 * compiled out unless FIRMWARE_SERVICE_ENABLE_APPLY is set. See its own
 * documentation before turning that on.
 */

#ifndef PICO_FRAMEWORK_FIRMWARE_SERVICE_H
#define PICO_FRAMEWORK_FIRMWARE_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cli.h"
#include "flash_storage.h"

#include "firmware_receive.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Whether to build the command that overwrites the running firmware.
 *
 * Off by default, and deliberately awkward to turn on: everything up to and
 * including verification is safe to run on any board, while apply is not. An
 * application opts in through its profile.
 */
#ifndef FIRMWARE_SERVICE_ENABLE_APPLY
#define FIRMWARE_SERVICE_ENABLE_APPLY 0
#endif

/* Records are echoed back with a progress line every this many, so a long
   transfer shows movement without one reply per record. */
#ifndef FIRMWARE_SERVICE_PROGRESS_INTERVAL
#define FIRMWARE_SERVICE_PROGRESS_INTERVAL 64u
#endif

typedef struct {
    firmware_receive_t receive;
    const flash_layout_t *layout;

    /* Set while a transfer is in progress, so a stray record outside one is
       reported rather than silently written. */
    bool receiving;

    /* Result of the last verification, so apply can refuse an image that was
       never checked. */
    bool verified;
    uint32_t verified_crc32;

    uint32_t records_since_progress;
    cli_t *cli;   /* for progress output during a transfer */

    bool initialised;
} firmware_service_t;

/* Prepares the service. Erases nothing; fwbegin does that. */
bool firmware_service_init(firmware_service_t *service);

/*
 * The line filter to give to cli_config_t. Claims lines beginning with ':'
 * while a transfer is running, and leaves everything else to be dispatched as
 * a command.
 *
 * Pass the service as `line_filter_user_data`.
 */
bool firmware_service_line_filter(cli_t *cli, const char *line, void *user_data);

/*
 * The commands to register. Returns how many were written, which is one fewer
 * when apply is compiled out.
 *
 * The service pointer becomes each command's user_data, so the array the
 * caller passes must outlive the CLI.
 */
size_t firmware_service_commands(firmware_service_t *service,
                                 cli_command_t *out, size_t capacity);

/* Enough room for every command, apply included. */
#define FIRMWARE_SERVICE_MAX_COMMANDS 4u

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_FIRMWARE_SERVICE_H */
