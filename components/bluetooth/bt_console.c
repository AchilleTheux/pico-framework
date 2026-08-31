#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "bt_console.h"

const char *bt_console_result_name(bt_console_result_t result)
{
    switch (result) {
        case BT_CONSOLE_OK:                 return "ok";
        case BT_CONSOLE_ERR_INVALID_ARG:    return "invalid argument";
        case BT_CONSOLE_ERR_UNSUPPORTED:    return "no radio on this board";
        case BT_CONSOLE_ERR_RADIO:          return "the radio would not start";
        case BT_CONSOLE_ERR_STACK:          return "bluetooth stack would not start";
        default:                            return "unknown";
    }
}

#if !BT_CONSOLE_SUPPORTED

/*
 * No radio. The component still builds so it need not be conditionally
 * registered and the CI matrix stays uniform; a caller checks
 * BT_CONSOLE_SUPPORTED at compile time rather than finding out at runtime.
 */

static int unsupported_read(void *ctx)
{
    (void)ctx;
    return -1;
}

static void unsupported_write(void *ctx, const char *data, size_t length)
{
    (void)ctx;
    (void)data;
    (void)length;
}

bt_console_result_t bt_console_init(bt_console_t *console,
                                    const bt_console_config_t *config)
{
    (void)config;
    if (console != NULL) {
        memset(console, 0, sizeof(*console));
    }
    return BT_CONSOLE_ERR_UNSUPPORTED;
}

cli_stream_t bt_console_stream(bt_console_t *console)
{
    return (cli_stream_t){
        .read = unsupported_read,
        .write = unsupported_write,
        .ctx = console,
    };
}

void bt_console_poll(bt_console_t *console) { (void)console; }

#else /* BT_CONSOLE_SUPPORTED */

#include "btstack.h"
#include "pico/cyw43_arch.h"

/* RFCOMM channel the service is offered on. 1 is conventional for a single
   serial port and is what a host assumes when it is not told otherwise. */
#define RFCOMM_CHANNEL 1

/*
 * Only one console can exist, because BTstack's callbacks carry no user
 * pointer — its packet handlers are plain functions. A second instance would
 * have no way to be reached, so the single instance is explicit rather than
 * pretended otherwise by an API that takes a handle it cannot use.
 */
static bt_console_t *g_console;

static uint16_t g_rfcomm_channel_id;
static uint8_t g_spp_service_buffer[150];
static btstack_packet_callback_registration_t g_hci_callback;

/*
 * Handed to bt_stream as its way onto the link. Not named rfcomm_send: that is
 * BTstack's own function, which this calls.
 */
static uint16_t send_over_rfcomm(void *ctx, const uint8_t *data, uint16_t length)
{
    (void)ctx;

    if (g_rfcomm_channel_id == 0) {
        return 0;
    }

    /*
     * rfcomm_send takes all or nothing, so a refusal means zero accepted and
     * bt_stream requeues the lot. Reporting a partial count here would lose
     * whatever it thought it had sent.
     */
    if (rfcomm_send(g_rfcomm_channel_id, (uint8_t *)data, length) != 0) {
        return 0;
    }
    return length;
}

static void handle_packet(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    (void)channel;

    if (g_console == NULL) {
        return;
    }

    switch (type) {
        case HCI_EVENT_PACKET:
            switch (hci_event_packet_get_type(packet)) {
                case HCI_EVENT_PIN_CODE_REQUEST: {
                    /* Legacy pairing, for a host too old for Secure Simple
                       Pairing. "0000" is the conventional default. */
                    bd_addr_t address;
                    hci_event_pin_code_request_get_bd_addr(packet, address);
                    gap_pin_code_response(address, "0000");
                    break;
                }

                case HCI_EVENT_USER_CONFIRMATION_REQUEST: {
                    /*
                     * Secure Simple Pairing "just works": accepted without
                     * anyone comparing a number, because a robot has no screen
                     * to show one on. See the README — this is a deliberate
                     * trade and it is not a secure pairing.
                     */
                    bd_addr_t address;
                    hci_event_user_confirmation_request_get_bd_addr(packet, address);
                    gap_ssp_confirmation_response(address);
                    break;
                }

                case RFCOMM_EVENT_INCOMING_CONNECTION: {
                    /* Accepted unconditionally: refusing would need a policy,
                       and pairing is the gate. */
                    const uint16_t id = rfcomm_event_incoming_connection_get_rfcomm_cid(packet);
                    rfcomm_accept_connection(id);
                    break;
                }

                case RFCOMM_EVENT_CHANNEL_OPENED:
                    if (rfcomm_event_channel_opened_get_status(packet) != 0) {
                        g_rfcomm_channel_id = 0;
                        bt_stream_set_connected(&g_console->stream, false);
                        break;
                    }
                    g_rfcomm_channel_id = rfcomm_event_channel_opened_get_rfcomm_cid(packet);
                    bt_stream_set_connected(&g_console->stream, true);
                    break;

                case RFCOMM_EVENT_CAN_SEND_NOW: {
                    /*
                     * The credit this whole adapter waits for. If output still
                     * remains afterwards, another opportunity is requested
                     * rather than waited for — otherwise a reply longer than
                     * one packet would stall until the next thing happened.
                     */
                    const uint16_t mtu = rfcomm_get_max_frame_size(g_rfcomm_channel_id);
                    if (bt_stream_on_can_send(&g_console->stream, mtu)) {
                        rfcomm_request_can_send_now_event(g_rfcomm_channel_id);
                    }
                    break;
                }

                case RFCOMM_EVENT_CHANNEL_CLOSED:
                    g_rfcomm_channel_id = 0;
                    bt_stream_set_connected(&g_console->stream, false);
                    break;

                default:
                    break;
            }
            break;

        case RFCOMM_DATA_PACKET:
            bt_stream_on_received(&g_console->stream, packet, size);
            break;

        default:
            break;
    }
}

bt_console_result_t bt_console_init(bt_console_t *console,
                                    const bt_console_config_t *config)
{
    if (console == NULL || config == NULL ||
        config->incoming == NULL || config->outgoing == NULL) {
        return BT_CONSOLE_ERR_INVALID_ARG;
    }

    memset(console, 0, sizeof(*console));
    console->name = (config->name != NULL) ? config->name : BT_CONSOLE_DEFAULT_NAME;

    if (!bt_stream_init(&console->stream,
                        config->incoming, config->incoming_size,
                        config->outgoing, config->outgoing_size,
                        send_over_rfcomm, NULL)) {
        return BT_CONSOLE_ERR_INVALID_ARG;
    }

    /*
     * Shares the radio with the wifi component if both are present; whichever
     * runs first brings it up and the second call is a no-op inside the SDK.
     */
    if (cyw43_arch_init() != 0) {
        return BT_CONSOLE_ERR_RADIO;
    }

    g_console = console;
    g_rfcomm_channel_id = 0;

    l2cap_init();
    rfcomm_init();

    /* One channel, and the MTU BTstack's buffer allows. */
    rfcomm_register_service(handle_packet, RFCOMM_CHANNEL, 0xFFFF);

    /* The SDP record is what tells a host this is a serial port; without it a
       laptop pairs and then offers nothing to open. */
    sdp_init();
    memset(g_spp_service_buffer, 0, sizeof(g_spp_service_buffer));
    spp_create_sdp_record(g_spp_service_buffer, sdp_create_service_record_handle(),
                          RFCOMM_CHANNEL, "Console");
    sdp_register_service(g_spp_service_buffer);

    gap_set_local_name(console->name);
    gap_discoverable_control(config->discoverable ? 1 : 0);

    /* Serial port class, so a host shows it with a sensible icon. */
    gap_set_class_of_device(0x001F00);

    g_hci_callback.callback = handle_packet;
    hci_add_event_handler(&g_hci_callback);

    if (hci_power_control(HCI_POWER_ON) != 0) {
        g_console = NULL;
        return BT_CONSOLE_ERR_STACK;
    }

    console->initialised = true;
    return BT_CONSOLE_OK;
}

static int stream_read(void *ctx)
{
    bt_console_t *console = (bt_console_t *)ctx;
    return bt_stream_read(&console->stream);
}

static void stream_write(void *ctx, const char *data, size_t length)
{
    bt_console_t *console = (bt_console_t *)ctx;

    bt_stream_write(&console->stream, data, length);

    /*
     * Ask for an opportunity as soon as there is something to send, rather than
     * waiting for the next poll. A console reply should not sit in a buffer
     * until the main loop comes round.
     */
    if (g_rfcomm_channel_id != 0 && bt_stream_has_output(&console->stream)) {
        rfcomm_request_can_send_now_event(g_rfcomm_channel_id);
    }
}

cli_stream_t bt_console_stream(bt_console_t *console)
{
    return (cli_stream_t){
        .read = stream_read,
        .write = stream_write,
        .ctx = console,
    };
}

void bt_console_poll(bt_console_t *console)
{
    if (console == NULL || !console->initialised) {
        return;
    }

    /* Gives the radio and BTstack their turn. In poll mode nothing happens
       without it. */
    cyw43_arch_poll();

    /*
     * A safety net for output that was buffered while nothing was asking. The
     * write path already requests an opportunity, so this normally finds
     * nothing to do.
     */
    if (g_rfcomm_channel_id != 0 && bt_stream_has_output(&console->stream)) {
        rfcomm_request_can_send_now_event(g_rfcomm_channel_id);
    }
}

#endif /* BT_CONSOLE_SUPPORTED */
