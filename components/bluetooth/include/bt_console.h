/*
 * bt_console - a serial console over Classic Bluetooth.
 *
 * An SPP server that presents itself as a cli_stream_t, so the framework's CLI
 * runs over Bluetooth with no change to a single command. On a laptop it appears
 * as an ordinary serial port — /dev/rfcomm0 on Linux, a COM port on Windows —
 * so any terminal program works and nothing special is needed on the host side.
 *
 * That is the reason for choosing Classic over BLE. A BLE equivalent needs a
 * custom service and an application on the host to speak it; Classic SPP is a
 * serial port, which is what a console is.
 *
 * The flow control between RFCOMM's packets and the CLI's byte stream is in
 * bt_stream.h, which is free of BTstack and unit-tested.
 *
 * BOARDS WITHOUT A RADIO
 *
 * Compiles for every board. Without a CYW43, BT_CONSOLE_SUPPORTED is 0 and
 * every call reports unsupported, so the component need not be conditionally
 * registered.
 */

#ifndef PICO_FRAMEWORK_BT_CONSOLE_H
#define PICO_FRAMEWORK_BT_CONSOLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cli.h"

#include "bt_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CYW43_WL_GPIO_LED_PIN) || defined(PICO_CYW43_SUPPORTED)
#define BT_CONSOLE_SUPPORTED 1
#else
#define BT_CONSOLE_SUPPORTED 0
#endif

/* How the board appears when a host scans. */
#ifndef BT_CONSOLE_DEFAULT_NAME
#define BT_CONSOLE_DEFAULT_NAME "pico-framework"
#endif

typedef enum {
    BT_CONSOLE_OK = 0,
    BT_CONSOLE_ERR_INVALID_ARG,
    BT_CONSOLE_ERR_UNSUPPORTED,   /* no radio on this board */
    BT_CONSOLE_ERR_RADIO,         /* the CYW43 would not start */
    BT_CONSOLE_ERR_STACK,         /* BTstack refused to come up */
} bt_console_result_t;

typedef struct {
    /*
     * Advertised name, borrowed rather than copied, so it must outlive the
     * console. NULL takes BT_CONSOLE_DEFAULT_NAME.
     */
    const char *name;

    /*
     * Caller-owned buffers for the two directions. Output wants the larger of
     * the two: a `help` listing is a few hundred bytes and arrives faster than
     * RFCOMM will take it.
     */
    uint8_t *incoming;
    size_t incoming_size;
    uint8_t *outgoing;
    size_t outgoing_size;

    /*
     * Whether the board may be found by a host that is not already paired.
     * True is what you want while setting up; a robot in a competition hall
     * surrounded by other people's laptops may prefer false, which still
     * accepts a host it has paired with before.
     */
    bool discoverable;
} bt_console_config_t;

typedef struct {
    bt_stream_t stream;
    const char *name;
    bool initialised;
} bt_console_t;

/*
 * Start the radio, BTstack and the SPP service.
 *
 * Blocks briefly while the CYW43's firmware is uploaded. Safe to call alongside
 * the wifi component — both share one radio and one async context — but see the
 * README on which cyw43_arch the build must link.
 */
bt_console_result_t bt_console_init(bt_console_t *console,
                                    const bt_console_config_t *config);

/*
 * The stream to hand to cli_init(). Valid for the life of the console, whether
 * or not anything is connected — a CLI does not have to be torn down and rebuilt
 * when a peer comes and goes.
 */
cli_stream_t bt_console_stream(bt_console_t *console);

/*
 * Move buffered output along. Call it every time round the main loop, beside
 * cli_poll(); it never blocks.
 *
 * Receiving needs no polling — BTstack delivers into the buffer from its own
 * callback — but sending does, because output produced when the link had no
 * credit is still waiting.
 */
void bt_console_poll(bt_console_t *console);

static inline bool bt_console_is_connected(const bt_console_t *console)
{
    return console->initialised && bt_stream_is_connected(&console->stream);
}

/* Bytes lost for want of buffer, in each direction. A count that grows means
   the buffers are too small for what the firmware prints. */
static inline uint32_t bt_console_dropped_output(const bt_console_t *console)
{
    return console->stream.dropped_outgoing;
}

static inline uint32_t bt_console_dropped_input(const bt_console_t *console)
{
    return console->stream.dropped_incoming;
}

const char *bt_console_result_name(bt_console_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_BT_CONSOLE_H */
