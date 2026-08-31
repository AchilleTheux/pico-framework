# cli

A line-oriented command interpreter for debugging and hardware bring-up.

## What it does

* Command table with case-insensitive names, per-command `user_data`, and a
  built-in `help` / `?`.
* A ready-made set of the commands every firmware wants: `ping`, `version`,
  `uptime`, `reboot`, `bootsel`.
* Argument parsing: unsigned, signed, hex, float, tokens, and free text.
* Line editing: backspace and delete, optional echo, optional prompt.
* Never blocks, never allocates. `cli_poll()` takes whatever the transport has
  and returns.

## The transport split

The interpreter reads and writes through two function pointers:

```c
typedef struct {
    int  (*read)(void *ctx);                        /* -1 when nothing waiting */
    void (*write)(void *ctx, const char *data, size_t len);
    void *ctx;
} cli_stream_t;
```

`cli.c` therefore calls no Pico SDK function at all. That is what
DESIGN_DOC.md section 8 asks for, and it buys two concrete things:

1. **The same interpreter runs anywhere.** `cli_stream_stdio()` and
   `cli_stream_uart()` ship today; a TCP stream is a third implementation of
   the same two functions, with no change to the parser.
2. **The interpreter is unit-testable.** `tests/components/cli_test.c` drives
   the real parser against a fake stream that scripts input and captures
   output, so dispatch, line editing and every argument type are covered on the
   host rather than by typing at a terminal.

## Usage

```c
#include "cli.h"
#include "cli_stream.h"

static int cmd_led(cli_t *cli, void *user_data)
{
    uint32_t on;
    if (!cli_next_u32(cli, &on)) {
        cli_write(cli, "usage: led <0|1>\r\n");
        return CLI_ERR_ARG;
    }
    gpio_put(LED_PIN, on != 0);
    return CLI_OK;
}

static const cli_command_t commands[] = {
    { "led", "led <0|1>", cmd_led, NULL },
};

static char line[128];
static cli_t cli;

const cli_config_t config = {
    .commands = commands,
    .command_count = count_of(commands),
    .stream = cli_stream_stdio(),
    .line_buffer = line,            /* caller-owned, like every buffer here */
    .line_buffer_size = sizeof(line),
    .prompt = "> ",
    .echo = true,
    .enable_help = true,
};

cli_init(&cli, &config);

while (true) {
    cli_poll(&cli);     /* runs any complete command, then returns */
    /* ... the rest of the main loop ... */
}
```

Link it from the application:

```cmake
target_link_libraries(app_my_firmware PRIVATE pico_framework::cli)
```

## Built-in commands

```c
#include "cli_builtins.h"

cli_command_t commands[CLI_BUILTIN_COMMAND_COUNT + 4];
size_t count = cli_builtin_commands(commands, count_of(commands));
commands[count++] = (cli_command_t){ "mine", "...", cmd_mine, NULL };
```

| Command | |
|---------|--|
| `ping` | answers `pong`. The cheapest possible "is it alive" |
| `version` | board, SDK version, build type |
| `uptime` | milliseconds since reset |
| `reboot` | restarts the firmware |
| `bootsel` | restarts into the USB bootloader, so `picotool` can flash it without anyone pressing the button |

Every test application here had been writing some subset of these by hand,
slightly differently. They live in `cli_builtins.c` rather than in `cli.c`
because they need the Pico SDK — the bootrom, the watchdog, the timer — and
`cli.c` is deliberately free of it so the interpreter can be host-tested. The
one built-in that does live in `cli.c` is `help`, since enumerating the command
table is something only the interpreter can do.

**`cli_init()` refuses two commands with the same name**, case-insensitively.
Lookup takes the first match, so a duplicate would leave one silently
unreachable — a real hazard once a ready-made set exists, and one whose
behaviour would depend on registration order. Better a startup failure than a
command that quietly does nothing.

## Sharing the line with something that is not a command

Some things arriving on the console are not commands at all. A firmware image
sent as Intel HEX is a stream of lines beginning with `:`, and without a hook
each one would be answered with `unknown command`.

An optional line filter is consulted for every non-blank line *before* command
lookup. Returning true means the line has been dealt with:

```c
static bool take_hex_records(cli_t *cli, const char *line, void *user_data)
{
    if (line[0] != ':') {
        return false;          /* not ours; let it dispatch as a command */
    }
    firmware_receive_line(user_data, line);
    return true;
}

const cli_config_t config = {
    /* ... */
    .line_filter = take_hex_records,
    .line_filter_user_data = &receiver,
};
```

That lets an upload and the ordinary commands share one console, rather than
needing a separate mode or a second port.

Two details the tests pin down:

* The filter sees the line **before tokenising**, which cuts terminators into
  the buffer as it splits arguments. Running it afterwards would show it only
  the first word.
* A line holding nothing but separators is discarded **before** the filter, so
  "blank" means one thing throughout: a filter counting records is never handed
  whitespace noise off a serial link, just as dispatch never reports it as an
  unknown command.

## Contracts worth knowing

| Behaviour | Rationale |
|-----------|-----------|
| A handler runs to completion inside `cli_feed_char()` | keeps the interpreter free of a state machine; handlers must return promptly rather than spin |
| An overlong line is rejected, not truncated | truncating could silently run a *different*, valid command |
| An argument must consume its whole token | `12abc` is an error, not `12` |
| Arguments split on space, tab, comma or semicolon | matches the comma style the original Eurobot interpreter used |
| Control characters are dropped | they would otherwise corrupt token boundaries and the echo |
| `cli_poll()` stops after `CLI_POLL_BUDGET` characters | a chatty peer cannot starve the rest of the main loop |
| `cli_rest()` consumes the remainder | so it must be the last argument read |
| Returned tokens point into the line buffer | valid until the next character is fed |

## Error reporting

A handler returns 0 for success. Anything else is printed as `error <n>`.
`cli_status_t` names the common cases (`CLI_ERR_ARG`, `CLI_ERR_RANGE`,
`CLI_ERR_STATE`, `CLI_ERR_FAILED`); handlers may return their own codes.

## Choosing a transport

| Transport | When |
|-----------|------|
| `cli_stream_stdio()` | the default. Follows `pico_enable_stdio_usb/uart`. Note that `printf()` elsewhere shares the same output. |
| `cli_stream_uart(uart1)` | the CLI needs its own port — for example while `printf()` goes to USB, or when USB is unavailable |

Both are non-blocking on read and blocking on write. The caller configures and
owns the UART; the transport only reads and writes it.

## Testing

* Host: `make test` covers dispatch, line editing, and every argument type.
* Hardware: `make APP=tests/cli_test flash` — see that test's README.

The stdio transport over USB CDC and the parser's representative success and
error paths were exercised on an RP2040-Zero on 2026-08-31. The dedicated UART
transport still needs an external USB-to-serial adapter test.
