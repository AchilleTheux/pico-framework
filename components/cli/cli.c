#include "cli.h"

#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Character classification
 *
 * Spelled out rather than taken from <ctype.h>: those functions are
 * locale-dependent and undefined for negative char values, which is a real
 * hazard when feeding them raw serial bytes.
 * -------------------------------------------------------------------------*/

static bool is_separator(char c)
{
    return c != '\0' && strchr(CLI_SEPARATORS, c) != NULL;
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static char to_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool equals_ignore_case(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (to_upper(*a) != to_upper(*b)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == *b;
}

static bool hex_value(char c, uint32_t *out)
{
    if (is_digit(c)) {
        *out = (uint32_t)(c - '0');
    } else if (to_upper(c) >= 'A' && to_upper(c) <= 'F') {
        *out = (uint32_t)(to_upper(c) - 'A' + 10);
    } else {
        return false;
    }
    return true;
}

/* ---------------------------------------------------------------------------
 * Output
 * -------------------------------------------------------------------------*/

void cli_write_bytes(cli_t *cli, const char *data, size_t len)
{
    if (cli == NULL || !cli->initialised || cli->stream.write == NULL || len == 0) {
        return;
    }
    cli->stream.write(cli->stream.ctx, data, len);
}

void cli_write(cli_t *cli, const char *text)
{
    if (text != NULL) {
        cli_write_bytes(cli, text, strlen(text));
    }
}

void cli_vprintf(cli_t *cli, const char *fmt, va_list args)
{
    char buffer[CLI_PRINTF_BUFFER_SIZE];

    const int written = vsnprintf(buffer, sizeof(buffer), fmt, args);
    if (written <= 0) {
        return;
    }

    /* vsnprintf reports what it *would* have written, so clamp to what it
       actually did rather than reading past the buffer. */
    const size_t len = (size_t)written < sizeof(buffer) ? (size_t)written
                                                        : sizeof(buffer) - 1;
    cli_write_bytes(cli, buffer, len);
}

void cli_printf(cli_t *cli, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    cli_vprintf(cli, fmt, args);
    va_end(args);
}

void cli_write_prompt(cli_t *cli)
{
    if (cli != NULL && cli->prompt != NULL) {
        cli_write(cli, cli->prompt);
    }
}

/* ---------------------------------------------------------------------------
 * Argument parsing
 *
 * The cursor `parse_pos` walks the line buffer. Tokens are cut in place by
 * writing a terminator over the separator, so cli_next_token() can hand back a
 * pointer into the buffer without copying.
 * -------------------------------------------------------------------------*/

static void skip_separators(cli_t *cli)
{
    while (cli->parse_pos < cli->line_len && is_separator(cli->line[cli->parse_pos])) {
        cli->parse_pos++;
    }
}

const char *cli_next_token(cli_t *cli)
{
    if (cli == NULL || !cli->initialised) {
        return NULL;
    }

    skip_separators(cli);
    if (cli->parse_pos >= cli->line_len) {
        return NULL;
    }

    const size_t start = cli->parse_pos;
    while (cli->parse_pos < cli->line_len && !is_separator(cli->line[cli->parse_pos])) {
        cli->parse_pos++;
    }

    if (cli->parse_pos < cli->line_len) {
        cli->line[cli->parse_pos] = '\0';
        cli->parse_pos++;
    }

    return &cli->line[start];
}

const char *cli_rest(cli_t *cli)
{
    if (cli == NULL || !cli->initialised) {
        return NULL;
    }

    skip_separators(cli);
    if (cli->parse_pos >= cli->line_len) {
        return NULL;
    }

    const char *rest = &cli->line[cli->parse_pos];
    cli->parse_pos = cli->line_len;
    return rest;
}

bool cli_args_exhausted(cli_t *cli)
{
    if (cli == NULL || !cli->initialised) {
        return true;
    }
    skip_separators(cli);
    return cli->parse_pos >= cli->line_len;
}

/*
 * Parse an unsigned value from a whole token. The token must be consumed
 * entirely, so "12abc" is rejected rather than silently read as 12.
 */
static bool parse_u32(const char *token, uint32_t base, uint32_t *out)
{
    if (*token == '\0') {
        return false;
    }

    uint32_t value = 0;
    for (const char *p = token; *p != '\0'; p++) {
        uint32_t digit;
        if (!hex_value(*p, &digit) || digit >= base) {
            return false;
        }
        value = value * base + digit;
    }

    *out = value;
    return true;
}

/* Strip an optional 0x / 0X prefix, reporting whether one was present. */
static const char *strip_hex_prefix(const char *token, bool *had_prefix)
{
    if (token[0] == '0' && to_upper(token[1]) == 'X') {
        *had_prefix = true;
        return token + 2;
    }
    *had_prefix = false;
    return token;
}

bool cli_next_u32(cli_t *cli, uint32_t *out)
{
    const char *token = cli_next_token(cli);
    if (token == NULL || out == NULL) {
        return false;
    }

    bool hex;
    const char *digits = strip_hex_prefix(token, &hex);
    return parse_u32(digits, hex ? 16 : 10, out);
}

bool cli_next_hex32(cli_t *cli, uint32_t *out)
{
    const char *token = cli_next_token(cli);
    if (token == NULL || out == NULL) {
        return false;
    }

    bool hex;
    const char *digits = strip_hex_prefix(token, &hex);
    return parse_u32(digits, 16, out);
}

bool cli_next_i32(cli_t *cli, int32_t *out)
{
    const char *token = cli_next_token(cli);
    if (token == NULL || out == NULL) {
        return false;
    }

    const bool negative = (*token == '-');
    if (negative || *token == '+') {
        token++;
    }

    bool hex;
    const char *digits = strip_hex_prefix(token, &hex);

    uint32_t magnitude;
    if (!parse_u32(digits, hex ? 16 : 10, &magnitude)) {
        return false;
    }

    /* INT32_MIN has no positive counterpart, so bound each sign separately. */
    if (negative) {
        if (magnitude > 2147483648u) {
            return false;
        }
        *out = (int32_t)(-(int64_t)magnitude);
    } else {
        if (magnitude > 2147483647u) {
            return false;
        }
        *out = (int32_t)magnitude;
    }
    return true;
}

bool cli_next_float(cli_t *cli, float *out)
{
    const char *token = cli_next_token(cli);
    if (token == NULL || out == NULL) {
        return false;
    }

    const char *p = token;
    const bool negative = (*p == '-');
    if (negative || *p == '+') {
        p++;
    }

    float value = 0.0f;
    float divisor = 0.0f;
    bool seen_digit = false;
    bool seen_point = false;

    for (; *p != '\0'; p++) {
        if (*p == '.') {
            if (seen_point) {
                return false;
            }
            seen_point = true;
            divisor = 1.0f;
        } else if (is_digit(*p)) {
            value = value * 10.0f + (float)(*p - '0');
            if (seen_point) {
                divisor *= 10.0f;
            }
            seen_digit = true;
        } else {
            return false;
        }
    }

    if (!seen_digit) {
        return false;
    }

    if (divisor > 0.0f) {
        value /= divisor;
    }

    *out = negative ? -value : value;
    return true;
}

/* ---------------------------------------------------------------------------
 * Dispatch
 * -------------------------------------------------------------------------*/

static void print_help(cli_t *cli)
{
    for (size_t i = 0; i < cli->command_count; i++) {
        const cli_command_t *command = &cli->commands[i];
        if (command->help != NULL) {
            cli_printf(cli, "  %-16s %s\r\n", command->name, command->help);
        } else {
            cli_printf(cli, "  %s\r\n", command->name);
        }
    }
    if (cli->enable_help) {
        cli_write(cli, "  help             list commands\r\n");
    }
}

static const cli_command_t *find_command(const cli_t *cli, const char *name)
{
    for (size_t i = 0; i < cli->command_count; i++) {
        if (equals_ignore_case(name, cli->commands[i].name)) {
            return &cli->commands[i];
        }
    }
    return NULL;
}

static void execute_line(cli_t *cli)
{
    cli->line[cli->line_len] = '\0';
    cli->parse_pos = 0;

    /*
     * A line holding nothing but separators is blank as far as anyone is
     * concerned, so it is discarded before the filter rather than after. That
     * keeps one meaning of "blank" throughout: a filter counting records is
     * never handed whitespace noise off a serial link, just as the dispatcher
     * never reports it as an unknown command.
     */
    if (cli_args_exhausted(cli)) {
        return;
    }
    cli->parse_pos = 0;

    /*
     * The filter is offered the whole line next, before tokenising cuts
     * terminators into the buffer. A filter that claims the line has dealt
     * with it, so nothing further happens — in particular no "unknown
     * command", which is what a stream of Intel HEX records would otherwise
     * produce line after line.
     */
    if (cli->line_filter != NULL &&
        cli->line_filter(cli, cli->line, cli->line_filter_user_data)) {
        return;
    }

    const char *name = cli_next_token(cli);
    if (name == NULL) {
        return; /* unreachable: the blank case is handled above */
    }

    if (cli->enable_help && (equals_ignore_case(name, "help") ||
                             equals_ignore_case(name, "?"))) {
        print_help(cli);
        return;
    }

    const cli_command_t *command = find_command(cli, name);
    if (command == NULL) {
        cli_printf(cli, "unknown command: %s\r\n", name);
        return;
    }

    if (command->handler == NULL) {
        cli_printf(cli, "error %d\r\n", CLI_ERR_STATE);
        return;
    }

    const int status = command->handler(cli, command->user_data);
    if (status != 0) {
        cli_printf(cli, "error %d\r\n", status);
    }
}

/* ---------------------------------------------------------------------------
 * Line assembly
 * -------------------------------------------------------------------------*/

void cli_reset_line(cli_t *cli)
{
    if (cli == NULL) {
        return;
    }
    cli->line_len = 0;
    cli->parse_pos = 0;
    cli->overflow = false;
    if (cli->line != NULL && cli->line_size > 0) {
        cli->line[0] = '\0';
    }
}

static void handle_backspace(cli_t *cli)
{
    if (cli->line_len == 0) {
        return;
    }
    cli->line_len--;
    cli->line[cli->line_len] = '\0';
    if (cli->echo) {
        cli_write(cli, "\b \b");
    }
}

static void handle_end_of_line(cli_t *cli)
{
    if (cli->echo) {
        cli_write(cli, "\r\n");
    }

    if (cli->overflow) {
        /* The line was truncated, so running it would run the wrong command. */
        cli_write(cli, "line too long\r\n");
    } else if (cli->line_len > 0) {
        execute_line(cli);
    }

    cli_reset_line(cli);
    cli_write_prompt(cli);
}

void cli_feed_char(cli_t *cli, char c)
{
    if (cli == NULL || !cli->initialised) {
        return;
    }

    if (c == '\r' || c == '\n') {
        /*
         * A terminal sending CRLF would otherwise dispatch the line and then
         * see a stray empty one; the blank-line check in execute_line() makes
         * that harmless.
         */
        handle_end_of_line(cli);
        return;
    }

    if (c == '\b' || c == 0x7F) {
        handle_backspace(cli);
        return;
    }

    /* Drop remaining control characters rather than putting them in the
       buffer, where they would corrupt token boundaries and the echo. */
    if ((unsigned char)c < 0x20) {
        return;
    }

    if (cli->line_len + 1 >= cli->line_size) {
        cli->overflow = true;
        return;
    }

    cli->line[cli->line_len] = c;
    cli->line_len++;

    if (cli->echo) {
        cli_write_bytes(cli, &c, 1);
    }
}

void cli_poll(cli_t *cli)
{
    if (cli == NULL || !cli->initialised || cli->stream.read == NULL) {
        return;
    }

    for (unsigned i = 0; i < CLI_POLL_BUDGET; i++) {
        const int c = cli->stream.read(cli->stream.ctx);
        if (c < 0) {
            return;
        }
        cli_feed_char(cli, (char)c);
    }
}

/* ---------------------------------------------------------------------------
 * Setup
 * -------------------------------------------------------------------------*/

cli_init_result_t cli_init(cli_t *cli, const cli_config_t *config)
{
    if (cli == NULL || config == NULL ||
        config->line_buffer == NULL || config->line_buffer_size < 2 ||
        (config->command_count > 0 && config->commands == NULL)) {
        return CLI_INIT_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < config->command_count; i++) {
        if (config->commands[i].name == NULL || config->commands[i].name[0] == '\0') {
            return CLI_INIT_ERR_INVALID_ARG;
        }
    }

    *cli = (cli_t){
        .commands     = config->commands,
        .command_count = config->command_count,
        .stream       = config->stream,
        .line         = config->line_buffer,
        .line_size    = config->line_buffer_size,
        .prompt       = config->prompt,
        .echo         = config->echo,
        .enable_help  = config->enable_help,
        .line_filter  = config->line_filter,
        .line_filter_user_data = config->line_filter_user_data,
        .initialised  = true,
    };

    cli_reset_line(cli);
    return CLI_INIT_OK;
}
