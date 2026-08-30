#include "cli_stream.h"

#include "pico/stdlib.h"

static int stdio_read(void *ctx)
{
    (void)ctx;

    /* A zero timeout makes this a poll: PICO_ERROR_TIMEOUT when nothing is
       waiting, which is the -1 cli_poll() looks for. */
    const int c = getchar_timeout_us(0);
    return c == PICO_ERROR_TIMEOUT ? -1 : c;
}

static void stdio_write(void *ctx, const char *data, size_t len)
{
    (void)ctx;

    /* No fwrite: the data is not NUL-terminated and may contain any byte. */
    for (size_t i = 0; i < len; i++) {
        putchar_raw(data[i]);
    }
}

cli_stream_t cli_stream_stdio(void)
{
    return (cli_stream_t){
        .read  = stdio_read,
        .write = stdio_write,
        .ctx   = NULL,
    };
}
