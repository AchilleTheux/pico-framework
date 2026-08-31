/*
 * cli - a line-oriented command interpreter.
 *
 * The parser knows nothing about UARTs. It reads and writes through a
 * cli_stream_t of two function pointers, so the same interpreter serves a
 * UART, USB CDC, or later a TCP socket (DESIGN_DOC.md section 8). Concrete
 * transports live in cli_stream.h.
 *
 * Nothing here calls a Pico SDK function, which is what lets the whole
 * interpreter — line editing, dispatch and argument parsing — be unit-tested
 * on the host against a fake stream.
 *
 * The interpreter never blocks and never allocates. Feed it characters as they
 * arrive; a command runs to completion inside cli_feed_char() when the line
 * ends, so handlers should return promptly rather than spin.
 */

#ifndef PICO_FRAMEWORK_CLI_H
#define PICO_FRAMEWORK_CLI_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Longest text a single cli_printf() call can emit. */
#ifndef CLI_PRINTF_BUFFER_SIZE
#define CLI_PRINTF_BUFFER_SIZE 128
#endif

/* Characters cli_poll() will take in one pass, so a chatty peer cannot starve
   the rest of the main loop. */
#ifndef CLI_POLL_BUDGET
#define CLI_POLL_BUDGET 256
#endif

/* Anything separating one argument from the next. */
#define CLI_SEPARATORS " \t,;"

struct cli;
typedef struct cli cli_t;

/*
 * Status returned by a command handler. Zero is success; anything else is
 * reported to the user. Handlers may return their own positive codes, but
 * these cover the common cases.
 */
typedef enum {
    CLI_OK = 0,
    CLI_ERR_ARG = 1,    /* missing or malformed argument */
    CLI_ERR_RANGE = 2,  /* argument parsed but out of range */
    CLI_ERR_STATE = 3,  /* command is not valid right now */
    CLI_ERR_FAILED = 4, /* the operation itself failed */
} cli_status_t;

typedef int (*cli_command_fn)(cli_t *cli, void *user_data);

/*
 * Consulted for every complete line *before* it is looked up as a command.
 * Return true when the line has been dealt with, and dispatch is skipped.
 *
 * This exists because some things arriving on the same link are not commands
 * at all. A firmware image sent as Intel HEX is a stream of lines beginning
 * with ':', and without a hook each one would be reported as an unknown
 * command. A filter that claims those lines lets an upload share the console
 * with the CLI instead of needing a separate mode or a second port.
 *
 * `line` is the whole line, NUL-terminated, before any tokenising — so the
 * filter sees it exactly as typed, spacing included. It must not be modified.
 */
typedef bool (*cli_line_filter_fn)(cli_t *cli, const char *line, void *user_data);

typedef struct {
    /* Matched case-insensitively. Must be non-empty. */
    const char *name;

    /* One line shown by `help`. May be NULL. */
    const char *help;

    cli_command_fn handler;

    /* Passed back to the handler; lets one function serve several commands. */
    void *user_data;
} cli_command_t;

/*
 * A byte transport. `read` returns the next character, or -1 when none is
 * available; it must not block. `write` emits exactly `len` bytes.
 */
typedef struct {
    int (*read)(void *ctx);
    void (*write)(void *ctx, const char *data, size_t len);
    void *ctx;
} cli_stream_t;

typedef struct {
    const cli_command_t *commands;
    size_t command_count;

    /* Copied into the interpreter; the stream's own ctx must outlive it. */
    cli_stream_t stream;

    /* Caller-owned line buffer. One byte is reserved for the terminator, so a
       64-byte buffer accepts 63-character lines. */
    char *line_buffer;
    size_t line_buffer_size;

    /* Written after each command. NULL or "" for no prompt. */
    const char *prompt;

    /* Echo typed characters back, including destructive backspace. Leave off
       when the peer is a program rather than a terminal. */
    bool echo;

    /* Provide the built-in `help` / `?` command. */
    bool enable_help;

    /*
     * Optional. Called for each non-blank line before command lookup; see
     * cli_line_filter_fn. NULL leaves every line to ordinary dispatch.
     */
    cli_line_filter_fn line_filter;
    void *line_filter_user_data;
} cli_config_t;

typedef enum {
    CLI_INIT_OK = 0,
    CLI_INIT_ERR_INVALID_ARG,
} cli_init_result_t;

struct cli {
    const cli_command_t *commands;
    size_t command_count;
    cli_stream_t stream;
    char *line;
    size_t line_size;
    size_t line_len;
    size_t parse_pos;
    bool overflow;
    const char *prompt;
    bool echo;
    bool enable_help;
    cli_line_filter_fn line_filter;
    void *line_filter_user_data;
    bool initialised;
};

cli_init_result_t cli_init(cli_t *cli, const cli_config_t *config);

/* Consume one input character. A complete line dispatches before returning. */
void cli_feed_char(cli_t *cli, char c);

/* Drain whatever the stream has, up to CLI_POLL_BUDGET characters. */
void cli_poll(cli_t *cli);

/* Write the prompt. Useful once at startup, after cli_init(). */
void cli_write_prompt(cli_t *cli);

/* Discard the line being typed. */
void cli_reset_line(cli_t *cli);

/* ---------------------------------------------------------------------------
 * Output
 * -------------------------------------------------------------------------*/

void cli_write(cli_t *cli, const char *text);
void cli_write_bytes(cli_t *cli, const char *data, size_t len);
void cli_printf(cli_t *cli, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void cli_vprintf(cli_t *cli, const char *fmt, va_list args);

/* ---------------------------------------------------------------------------
 * Arguments
 *
 * Valid only from inside a handler. Each call consumes the next token, so
 * arguments are read left to right. All return false when the argument is
 * missing or malformed, leaving the output untouched.
 * -------------------------------------------------------------------------*/

/* Decimal, or hexadecimal with a 0x prefix. */
bool cli_next_u32(cli_t *cli, uint32_t *out);

/* As above, with an optional leading '-'. */
bool cli_next_i32(cli_t *cli, int32_t *out);

/* Hexadecimal, with or without a 0x prefix. */
bool cli_next_hex32(cli_t *cli, uint32_t *out);

/* Decimal fixed-point, optionally signed. No exponent notation. */
bool cli_next_float(cli_t *cli, float *out);

/*
 * The next whitespace-delimited token, or NULL when the line is exhausted.
 * Points into the line buffer and is valid until the next character is fed.
 */
const char *cli_next_token(cli_t *cli);

/*
 * Everything left on the line with leading separators removed, or NULL when
 * nothing remains. For commands taking free text, which must be the last
 * argument.
 */
const char *cli_rest(cli_t *cli);

/* True when every argument on the line has been consumed. */
bool cli_args_exhausted(cli_t *cli);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_CLI_H */
