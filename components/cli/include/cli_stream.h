/*
 * cli_stream - concrete transports for the CLI.
 *
 * Each factory fills in a cli_stream_t to hand to cli_init(). These are the
 * only files in the component that touch the Pico SDK; cli.c itself stays
 * transport-agnostic (DESIGN_DOC.md section 8).
 *
 * Both transports are non-blocking on read and blocking on write, matching
 * what cli_poll() expects.
 */

#ifndef PICO_FRAMEWORK_CLI_STREAM_H
#define PICO_FRAMEWORK_CLI_STREAM_H

#include "hardware/uart.h"

#include "cli.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Whatever pico_stdio is currently routed to — USB CDC, the default UART, or
 * both, as chosen by pico_enable_stdio_usb() / pico_enable_stdio_uart().
 *
 * The simplest choice, and the right one unless the CLI needs a port of its
 * own. Note that anything else calling printf() shares the same output.
 */
cli_stream_t cli_stream_stdio(void);

/*
 * A specific UART instance, independent of stdio. Use this to keep the CLI on
 * its own port while printf() goes elsewhere — or to avoid USB entirely.
 *
 * The caller configures and owns the UART: this only reads and writes it. The
 * instance must outlive the stream.
 */
cli_stream_t cli_stream_uart(uart_inst_t *uart);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_CLI_STREAM_H */
