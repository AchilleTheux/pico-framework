#include <string.h>

#include "pico/stdlib.h"

#include "crc.h"

#include "firmware_service.h"

#if FIRMWARE_SERVICE_ENABLE_APPLY
#include "firmware_apply.h"
#endif

/*
 * An image is linked to run from the start of flash, so the addresses in its
 * HEX file are XIP addresses. Offset 0 of the staging region corresponds to
 * this address, not to where staging physically sits.
 */
#define IMAGE_BASE_ADDRESS XIP_BASE

/* ---------------------------------------------------------------------------
 * Flash-backed page writer
 * -------------------------------------------------------------------------*/

static bool write_page(void *ctx, uint32_t offset, const uint8_t *page, uint32_t size)
{
    firmware_service_t *service = (firmware_service_t *)ctx;

    /* Verified, because a page that did not land is otherwise discovered by
       the board failing to boot after the install. */
    return flash_storage_program_verified(&service->layout->staging, offset,
                                          page, size) == FLASH_STORAGE_OK;
}

bool firmware_service_init(firmware_service_t *service)
{
    if (service == NULL) {
        return false;
    }

    memset(service, 0, sizeof(*service));
    service->layout = flash_layout_get();

    /* Nothing works if the chip could not be divided up. */
    if (service->layout->staging.size == 0) {
        return false;
    }

    service->initialised = true;
    return true;
}

/* ---------------------------------------------------------------------------
 * The line filter
 * -------------------------------------------------------------------------*/

bool firmware_service_line_filter(cli_t *cli, const char *line, void *user_data)
{
    firmware_service_t *service = (firmware_service_t *)user_data;

    if (service == NULL || !service->initialised || line[0] != ':') {
        return false;
    }

    /*
     * A record outside a transfer is claimed and reported rather than left to
     * dispatch. Letting it through would answer with "unknown command", which
     * on a link carrying a whole file means one such reply per line.
     */
    if (!service->receiving) {
        cli_write(cli, "no transfer in progress; send fwbegin first\r\n");
        return true;
    }

    service->cli = cli;

    const firmware_receive_result_t result =
        firmware_receive_line(&service->receive, line);

    switch (result) {
        case FIRMWARE_RECEIVE_OK:
            /* Quiet by default: a reply per record would double the traffic on
               a link that is already the slow part. */
            if (++service->records_since_progress >= FIRMWARE_SERVICE_PROGRESS_INTERVAL) {
                service->records_since_progress = 0;
                cli_printf(cli, ". %lu bytes\r\n",
                           (unsigned long)firmware_receive_image_size(&service->receive));
            }
            break;

        case FIRMWARE_RECEIVE_COMPLETE:
            service->receiving = false;
            cli_printf(cli, "received %lu bytes in %lu records\r\n",
                       (unsigned long)firmware_receive_image_size(&service->receive),
                       (unsigned long)service->receive.records);
            break;

        default:
            service->receiving = false;
            cli_printf(cli, "transfer failed: %s\r\n",
                       firmware_receive_result_name(result));
            break;
    }

    return true;
}

/* ---------------------------------------------------------------------------
 * Commands
 * -------------------------------------------------------------------------*/

static int cmd_begin(cli_t *cli, void *user_data)
{
    firmware_service_t *service = (firmware_service_t *)user_data;

    service->verified = false;
    service->records_since_progress = 0;

    /*
     * An optional size lets the sender say how much to clear. It matters: the
     * staging region is most of the chip, and erasing all of it takes about a
     * second per 20 sectors — eleven seconds on a 2 MiB part, against under
     * one for a 50 KiB image. Omitted, the whole region is erased, which is
     * always correct and always slow.
     *
     * Erasing only part of it can leave the tail of a larger previous image
     * beyond the new one. That is harmless: verification and installation both
     * cover only the new image's own length.
     */
    uint32_t erase_size = service->layout->staging.size;
    uint32_t requested;
    if (cli_next_u32(cli, &requested)) {
        if (requested == 0 || requested > service->layout->staging.size) {
            cli_printf(cli, "size must be 1 to %lu\r\n",
                       (unsigned long)service->layout->staging.size);
            return CLI_ERR_RANGE;
        }
        erase_size = flash_round_up_to_sector(requested);
    }

    cli_printf(cli, "erasing %lu KiB of staging...\r\n",
               (unsigned long)(erase_size / 1024u));

    const flash_storage_result_t erased =
        flash_storage_erase(&service->layout->staging, 0, erase_size);
    if (erased != FLASH_STORAGE_OK) {
        cli_printf(cli, "erase failed: %s\r\n", flash_storage_result_name(erased));
        return CLI_ERR_FAILED;
    }

    const firmware_receive_config_t config = {
        .base_address = IMAGE_BASE_ADDRESS,
        .capacity = service->layout->staging.size,
        .write_page = write_page,
        .write_ctx = service,
    };

    if (firmware_receive_begin(&service->receive, &config) != FIRMWARE_RECEIVE_OK) {
        cli_write(cli, "could not start the transfer\r\n");
        return CLI_ERR_FAILED;
    }

    service->receiving = true;
    cli_write(cli, "ready, send the hex file\r\n");
    return CLI_OK;
}

static int cmd_status(cli_t *cli, void *user_data)
{
    firmware_service_t *service = (firmware_service_t *)user_data;

    cli_printf(cli, "state      %s\r\n",
               service->receiving ? "receiving"
                                  : (firmware_receive_is_complete(&service->receive)
                                         ? "complete" : "idle"));
    cli_printf(cli, "records    %lu\r\n", (unsigned long)service->receive.records);
    cli_printf(cli, "bytes      %lu\r\n", (unsigned long)service->receive.bytes);
    cli_printf(cli, "image size %lu\r\n",
               (unsigned long)firmware_receive_image_size(&service->receive));
    cli_printf(cli, "pages      %lu\r\n", (unsigned long)service->receive.pages_written);
    cli_printf(cli, "capacity   %lu\r\n", (unsigned long)service->layout->staging.size);
    cli_printf(cli, "verified   %s\r\n", service->verified ? "yes" : "no");

    if (service->receive.error != FIRMWARE_RECEIVE_OK) {
        cli_printf(cli, "error      %s\r\n",
                   firmware_receive_result_name(service->receive.error));
    }
    return CLI_OK;
}

static int cmd_verify(cli_t *cli, void *user_data)
{
    firmware_service_t *service = (firmware_service_t *)user_data;

    uint32_t expected;
    if (!cli_next_hex32(cli, &expected)) {
        cli_write(cli, "usage: fwverify <crc32 of the image, hex>\r\n");
        return CLI_ERR_ARG;
    }

    if (!firmware_receive_is_complete(&service->receive)) {
        cli_write(cli, "no complete image staged\r\n");
        return CLI_ERR_STATE;
    }

    const uint32_t size = firmware_receive_image_size(&service->receive);

    /*
     * Checksummed by reading flash back, not by accumulating as records
     * arrived. Records may arrive out of order and an image may have gaps, so
     * a running total would depend on arrival order; reading back also
     * confirms what actually landed rather than what was sent.
     */
    const uint32_t actual = flash_storage_crc32(&service->layout->staging, 0, size);

    cli_printf(cli, "staged %lu bytes, crc 0x%08lX\r\n",
               (unsigned long)size, (unsigned long)actual);

    if (actual != expected) {
        cli_printf(cli, "MISMATCH: expected 0x%08lX\r\n", (unsigned long)expected);
        service->verified = false;
        return CLI_ERR_FAILED;
    }

    service->verified = true;
    service->verified_crc32 = actual;
    cli_write(cli, "verified\r\n");
    return CLI_OK;
}

#if FIRMWARE_SERVICE_ENABLE_APPLY
static int cmd_apply(cli_t *cli, void *user_data)
{
    firmware_service_t *service = (firmware_service_t *)user_data;

    /*
     * Refused unless the image has been checked against the sender's checksum
     * in this session. Installing overwrites the only working firmware on the
     * board, so the one thing worth insisting on is that what is about to be
     * copied is what was meant to be sent.
     */
    if (!service->verified) {
        cli_write(cli, "refusing: run fwverify first\r\n");
        return CLI_ERR_STATE;
    }

    const uint32_t size = firmware_receive_image_size(&service->receive);
    if (size == 0 || size > service->layout->application.size) {
        cli_write(cli, "refusing: staged image does not fit\r\n");
        return CLI_ERR_RANGE;
    }

    cli_printf(cli, "installing %lu bytes and rebooting\r\n", (unsigned long)size);
    cli_write(cli, "do not remove power\r\n");

    /* Give the reply time to reach the other end: nothing after this returns. */
    sleep_ms(100);

    firmware_apply(&service->layout->staging, &service->layout->application, size);

    /* Unreachable: firmware_apply lets the watchdog reboot the chip. */
    cli_write(cli, "install did not take effect\r\n");
    return CLI_ERR_FAILED;
}
#endif

size_t firmware_service_commands(firmware_service_t *service,
                                 cli_command_t *out, size_t capacity)
{
    if (service == NULL || out == NULL) {
        return 0;
    }

    static const struct {
        const char *name;
        const char *help;
        cli_command_fn handler;
    } table[] = {
        { "fwbegin",  "fwbegin [size] - erase staging, start a transfer", cmd_begin },
        { "fwstatus", "how the transfer is going",                   cmd_status },
        { "fwverify", "fwverify <crc32> - check the staged image",   cmd_verify },
#if FIRMWARE_SERVICE_ENABLE_APPLY
        { "fwapply",  "install the staged image and reboot",         cmd_apply },
#endif
    };

    const size_t count = (capacity < count_of(table)) ? capacity : count_of(table);

    for (size_t i = 0; i < count; i++) {
        out[i] = (cli_command_t){
            .name = table[i].name,
            .help = table[i].help,
            .handler = table[i].handler,
            .user_data = service,
        };
    }

    return count;
}
