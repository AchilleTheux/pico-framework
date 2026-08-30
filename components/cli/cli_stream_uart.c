#include "cli_stream.h"

static int uart_read(void *ctx)
{
    uart_inst_t *uart = (uart_inst_t *)ctx;

    if (!uart_is_readable(uart)) {
        return -1;
    }
    return (int)(unsigned char)uart_getc(uart);
}

static void uart_write(void *ctx, const char *data, size_t len)
{
    uart_inst_t *uart = (uart_inst_t *)ctx;

    uart_write_blocking(uart, (const uint8_t *)data, len);
}

cli_stream_t cli_stream_uart(uart_inst_t *uart)
{
    return (cli_stream_t){
        .read  = uart_read,
        .write = uart_write,
        .ctx   = uart,
    };
}
