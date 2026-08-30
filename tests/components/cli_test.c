/*
 * Host-side tests for the CLI.
 *
 * Because cli.c talks to the world only through cli_stream_t, the entire
 * interpreter — line editing, dispatch and argument parsing — runs here
 * against a fake stream that feeds it a scripted input and captures its
 * output. That is the payoff of keeping the parser transport-independent
 * (DESIGN_DOC.md sections 8 and 17).
 */

#include <string.h>

#include "test.h"

#include "cli.h"

/* ---------------------------------------------------------------------------
 * Fake transport
 * -------------------------------------------------------------------------*/

typedef struct {
    const char *input;
    size_t input_pos;
    char output[1024];
    size_t output_len;
} fake_stream_t;

static int fake_read(void *ctx)
{
    fake_stream_t *s = (fake_stream_t *)ctx;

    if (s->input == NULL || s->input[s->input_pos] == '\0') {
        return -1;
    }
    return (unsigned char)s->input[s->input_pos++];
}

static void fake_write(void *ctx, const char *data, size_t len)
{
    fake_stream_t *s = (fake_stream_t *)ctx;

    for (size_t i = 0; i < len && s->output_len + 1 < sizeof(s->output); i++) {
        s->output[s->output_len++] = data[i];
    }
    s->output[s->output_len] = '\0';
}

/* ---------------------------------------------------------------------------
 * Fixture
 * -------------------------------------------------------------------------*/

static fake_stream_t g_stream;
static char g_line[64];
static cli_t g_cli;

/* What the last handler saw, so tests can assert on parsed arguments. */
static struct {
    int calls;
    void *user_data;
    uint32_t u32;
    int32_t i32;
    float f;
    char text[64];
    bool parse_ok;
} g_seen;

static int handler_noop(cli_t *cli, void *user_data)
{
    (void)cli;
    g_seen.calls++;
    g_seen.user_data = user_data;
    return CLI_OK;
}

static int handler_fails(cli_t *cli, void *user_data)
{
    (void)cli;
    (void)user_data;
    g_seen.calls++;
    return CLI_ERR_RANGE;
}

static int handler_u32(cli_t *cli, void *user_data)
{
    (void)user_data;
    g_seen.calls++;
    g_seen.parse_ok = cli_next_u32(cli, &g_seen.u32);
    return g_seen.parse_ok ? CLI_OK : CLI_ERR_ARG;
}

static int handler_i32(cli_t *cli, void *user_data)
{
    (void)user_data;
    g_seen.calls++;
    g_seen.parse_ok = cli_next_i32(cli, &g_seen.i32);
    return g_seen.parse_ok ? CLI_OK : CLI_ERR_ARG;
}

static int handler_hex(cli_t *cli, void *user_data)
{
    (void)user_data;
    g_seen.calls++;
    g_seen.parse_ok = cli_next_hex32(cli, &g_seen.u32);
    return g_seen.parse_ok ? CLI_OK : CLI_ERR_ARG;
}

static int handler_float(cli_t *cli, void *user_data)
{
    (void)user_data;
    g_seen.calls++;
    g_seen.parse_ok = cli_next_float(cli, &g_seen.f);
    return g_seen.parse_ok ? CLI_OK : CLI_ERR_ARG;
}

static int handler_three(cli_t *cli, void *user_data)
{
    (void)user_data;
    g_seen.calls++;
    uint32_t a = 0, b = 0, c = 0;
    g_seen.parse_ok = cli_next_u32(cli, &a) && cli_next_u32(cli, &b) &&
                      cli_next_u32(cli, &c);
    g_seen.u32 = a * 100 + b * 10 + c;
    return g_seen.parse_ok ? CLI_OK : CLI_ERR_ARG;
}

static int handler_rest(cli_t *cli, void *user_data)
{
    (void)user_data;
    g_seen.calls++;
    const char *rest = cli_rest(cli);
    g_seen.parse_ok = (rest != NULL);
    snprintf(g_seen.text, sizeof(g_seen.text), "%s", rest != NULL ? rest : "");
    return CLI_OK;
}

static int handler_echoes(cli_t *cli, void *user_data)
{
    (void)user_data;
    g_seen.calls++;
    cli_printf(cli, "pong %u\r\n", 42u);
    return CLI_OK;
}

static int g_marker_a;
static int g_marker_b;

static const cli_command_t g_commands[] = {
    { "ping",  "answer with pong",    handler_echoes, NULL },
    { "noop",  NULL,                  handler_noop,   &g_marker_a },
    { "other", "same handler",        handler_noop,   &g_marker_b },
    { "fail",  "always fails",        handler_fails,  NULL },
    { "u32",   "read one unsigned",   handler_u32,    NULL },
    { "i32",   "read one signed",     handler_i32,    NULL },
    { "hex",   "read one hex value",  handler_hex,    NULL },
    { "flt",   "read one float",      handler_float,  NULL },
    { "three", "read three unsigned", handler_three,  NULL },
    { "rest",  "echo the rest",       handler_rest,   NULL },
    { "null",  "no handler",          NULL,           NULL },
};

static void setup(bool echo, bool enable_help)
{
    memset(&g_stream, 0, sizeof(g_stream));
    memset(&g_seen, 0, sizeof(g_seen));
    memset(g_line, 0, sizeof(g_line));

    const cli_config_t config = {
        .commands = g_commands,
        .command_count = sizeof(g_commands) / sizeof(g_commands[0]),
        .stream = { .read = fake_read, .write = fake_write, .ctx = &g_stream },
        .line_buffer = g_line,
        .line_buffer_size = sizeof(g_line),
        .prompt = "> ",
        .echo = echo,
        .enable_help = enable_help,
    };

    CHECK_EQ_INT(cli_init(&g_cli, &config), CLI_INIT_OK);
}

/* Feed a string one character at a time, exactly as a transport would. */
static void feed(const char *text)
{
    for (const char *p = text; *p != '\0'; p++) {
        cli_feed_char(&g_cli, *p);
    }
}

static bool output_contains(const char *needle)
{
    return strstr(g_stream.output, needle) != NULL;
}

/* ---------------------------------------------------------------------------
 * Dispatch
 * -------------------------------------------------------------------------*/

TEST(a_command_runs_when_the_line_ends)
{
    setup(false, false);
    feed("noop");
    CHECK_EQ_INT(g_seen.calls, 0); /* nothing until the newline */

    feed("\n");
    CHECK_EQ_INT(g_seen.calls, 1);
}

TEST(command_names_are_case_insensitive)
{
    setup(false, false);
    feed("NoOp\n");
    CHECK_EQ_INT(g_seen.calls, 1);
}

TEST(user_data_distinguishes_commands_sharing_a_handler)
{
    setup(false, false);
    feed("noop\n");
    CHECK(g_seen.user_data == &g_marker_a);

    feed("other\n");
    CHECK(g_seen.user_data == &g_marker_b);
}

TEST(an_unknown_command_is_reported_and_not_run)
{
    setup(false, false);
    feed("nosuchthing\n");
    CHECK_EQ_INT(g_seen.calls, 0);
    CHECK(output_contains("unknown command: nosuchthing"));
}

TEST(a_blank_line_does_nothing)
{
    setup(false, false);
    feed("\n\n   \n");
    CHECK_EQ_INT(g_seen.calls, 0);
    CHECK(!output_contains("unknown"));
}

TEST(crlf_does_not_produce_a_spurious_second_command)
{
    setup(false, false);
    feed("noop\r\n");
    CHECK_EQ_INT(g_seen.calls, 1);
    CHECK(!output_contains("unknown"));
}

TEST(a_nonzero_return_is_reported_as_an_error)
{
    setup(false, false);
    feed("fail\n");
    CHECK(output_contains("error 2"));
}

TEST(a_successful_command_prints_no_error)
{
    setup(false, false);
    feed("noop\n");
    CHECK(!output_contains("error"));
}

TEST(a_command_with_a_null_handler_does_not_crash)
{
    setup(false, false);
    feed("null\n");
    CHECK(output_contains("error"));
}

TEST(a_handler_can_write_through_the_stream)
{
    setup(false, false);
    feed("ping\n");
    CHECK(output_contains("pong 42"));
}

/* ---------------------------------------------------------------------------
 * Line editing
 * -------------------------------------------------------------------------*/

TEST(backspace_erases_the_previous_character)
{
    setup(false, false);
    feed("noopX\b\n");
    CHECK_EQ_INT(g_seen.calls, 1);
}

TEST(delete_erases_like_backspace)
{
    setup(false, false);
    feed("noopX\x7F\n");
    CHECK_EQ_INT(g_seen.calls, 1);
}

TEST(backspace_on_an_empty_line_is_harmless)
{
    setup(false, false);
    feed("\b\b\bnoop\n");
    CHECK_EQ_INT(g_seen.calls, 1);
}

TEST(an_overlong_line_is_rejected_rather_than_truncated)
{
    setup(false, false);

    /* Without the overflow guard this would truncate to a valid prefix and run
       the wrong command. */
    char long_line[256];
    memset(long_line, 'x', sizeof(long_line) - 1);
    long_line[sizeof(long_line) - 1] = '\0';
    memcpy(long_line, "noop ", 5);

    feed(long_line);
    feed("\n");

    CHECK_EQ_INT(g_seen.calls, 0);
    CHECK(output_contains("line too long"));
}

TEST(the_interpreter_recovers_after_an_overlong_line)
{
    setup(false, false);

    char long_line[256];
    memset(long_line, 'x', sizeof(long_line) - 1);
    long_line[sizeof(long_line) - 1] = '\0';
    feed(long_line);
    feed("\n");

    feed("noop\n");
    CHECK_EQ_INT(g_seen.calls, 1);
}

TEST(control_characters_are_dropped_from_the_line)
{
    setup(false, false);
    feed("no\x01op\x1B\n");
    CHECK_EQ_INT(g_seen.calls, 1);
}

TEST(echo_off_leaves_typed_characters_unwritten)
{
    setup(false, false);
    feed("noop\n");
    CHECK(!output_contains("noop"));
}

TEST(echo_on_writes_typed_characters_back)
{
    setup(true, false);
    feed("noop\n");
    CHECK(output_contains("noop"));
}

TEST(echo_on_erases_destructively_on_backspace)
{
    setup(true, false);
    feed("a\b");
    CHECK(output_contains("a\b \b"));
}

TEST(the_prompt_is_written_after_each_command)
{
    setup(false, false);
    feed("noop\n");
    CHECK(output_contains("> "));
}

/* ---------------------------------------------------------------------------
 * Arguments
 * -------------------------------------------------------------------------*/

TEST(a_decimal_argument_is_parsed)
{
    setup(false, false);
    feed("u32 1234\n");
    CHECK(g_seen.parse_ok);
    CHECK_EQ_U32(g_seen.u32, 1234u);
}

TEST(an_argument_with_a_0x_prefix_is_hexadecimal)
{
    setup(false, false);
    feed("u32 0xFF\n");
    CHECK(g_seen.parse_ok);
    CHECK_EQ_U32(g_seen.u32, 255u);
}

TEST(hex_arguments_parse_with_or_without_a_prefix)
{
    setup(false, false);
    feed("hex beef\n");
    CHECK_EQ_U32(g_seen.u32, 0xBEEFu);

    feed("hex 0xBEEF\n");
    CHECK_EQ_U32(g_seen.u32, 0xBEEFu);
}

TEST(a_trailing_non_digit_makes_an_argument_invalid)
{
    setup(false, false);

    /* Reading "12abc" as 12 would silently do the wrong thing. */
    feed("u32 12abc\n");
    CHECK(!g_seen.parse_ok);
    CHECK(output_contains("error 1"));
}

TEST(a_missing_argument_is_an_error)
{
    setup(false, false);
    feed("u32\n");
    CHECK(!g_seen.parse_ok);
    CHECK(output_contains("error 1"));
}

TEST(arguments_may_be_separated_by_spaces_or_commas)
{
    setup(false, false);
    feed("three 1 2 3\n");
    CHECK_EQ_U32(g_seen.u32, 123u);

    feed("three 4,5,6\n");
    CHECK_EQ_U32(g_seen.u32, 456u);

    feed("three  7 , 8,9 \n");
    CHECK_EQ_U32(g_seen.u32, 789u);
}

TEST(a_signed_argument_accepts_both_signs)
{
    setup(false, false);
    feed("i32 -42\n");
    CHECK_EQ_INT(g_seen.i32, -42);

    feed("i32 +42\n");
    CHECK_EQ_INT(g_seen.i32, 42);
}

TEST(signed_arguments_reach_the_ends_of_the_range)
{
    setup(false, false);
    feed("i32 2147483647\n");
    CHECK(g_seen.parse_ok);
    CHECK_EQ_INT(g_seen.i32, 2147483647L);

    feed("i32 -2147483648\n");
    CHECK(g_seen.parse_ok);
    CHECK_EQ_INT(g_seen.i32, -2147483648L);
}

TEST(signed_arguments_past_the_range_are_rejected)
{
    setup(false, false);
    feed("i32 2147483648\n");
    CHECK(!g_seen.parse_ok);

    feed("i32 -2147483649\n");
    CHECK(!g_seen.parse_ok);
}

TEST(a_float_argument_is_parsed)
{
    setup(false, false);
    feed("flt 1.5\n");
    CHECK(g_seen.parse_ok);
    CHECK(g_seen.f > 1.49f && g_seen.f < 1.51f);

    feed("flt -0.25\n");
    CHECK(g_seen.f < -0.24f && g_seen.f > -0.26f);

    feed("flt 7\n");
    CHECK(g_seen.f > 6.99f && g_seen.f < 7.01f);
}

TEST(a_float_with_two_points_is_rejected)
{
    setup(false, false);
    feed("flt 1.2.3\n");
    CHECK(!g_seen.parse_ok);
}

TEST(rest_returns_the_remainder_of_the_line_verbatim)
{
    setup(false, false);
    feed("rest hello world  spaced\n");
    CHECK(g_seen.parse_ok);
    CHECK_EQ_STR(g_seen.text, "hello world  spaced");
}

TEST(rest_returns_null_when_nothing_follows)
{
    setup(false, false);
    feed("rest\n");
    CHECK(!g_seen.parse_ok);
    CHECK_EQ_STR(g_seen.text, "");
}

/* ---------------------------------------------------------------------------
 * Help
 * -------------------------------------------------------------------------*/

TEST(help_lists_commands_when_enabled)
{
    setup(false, true);
    feed("help\n");
    CHECK(output_contains("ping"));
    CHECK(output_contains("answer with pong"));
    CHECK(output_contains("three"));
}

TEST(question_mark_is_an_alias_for_help)
{
    setup(false, true);
    feed("?\n");
    CHECK(output_contains("ping"));
}

TEST(help_is_unknown_when_disabled)
{
    setup(false, false);
    feed("help\n");
    CHECK(output_contains("unknown command"));
}

/* ---------------------------------------------------------------------------
 * Polling
 * -------------------------------------------------------------------------*/

TEST(poll_drains_the_stream_and_runs_commands)
{
    setup(false, false);
    g_stream.input = "noop\nnoop\nnoop\n";

    cli_poll(&g_cli);

    CHECK_EQ_INT(g_seen.calls, 3);
}

TEST(poll_on_an_empty_stream_does_nothing)
{
    setup(false, false);
    g_stream.input = "";

    cli_poll(&g_cli);

    CHECK_EQ_INT(g_seen.calls, 0);
}

TEST(poll_stops_at_its_budget_rather_than_spinning)
{
    setup(false, false);

    /* A stream that never runs dry must not trap cli_poll() forever. */
    static char endless[CLI_POLL_BUDGET * 4];
    memset(endless, 'x', sizeof(endless) - 1);
    endless[sizeof(endless) - 1] = '\0';
    g_stream.input = endless;

    cli_poll(&g_cli);

    CHECK_EQ_INT((int)g_stream.input_pos, CLI_POLL_BUDGET);
}

/* ---------------------------------------------------------------------------
 * Initialisation
 * -------------------------------------------------------------------------*/

TEST(init_rejects_a_missing_line_buffer)
{
    cli_t cli;
    const cli_config_t config = {
        .commands = g_commands,
        .command_count = 1,
        .line_buffer = NULL,
        .line_buffer_size = 64,
    };
    CHECK_EQ_INT(cli_init(&cli, &config), CLI_INIT_ERR_INVALID_ARG);
}

TEST(init_rejects_a_command_without_a_name)
{
    cli_t cli;
    char line[16];
    static const cli_command_t bad[] = { { NULL, NULL, handler_noop, NULL } };
    const cli_config_t config = {
        .commands = bad,
        .command_count = 1,
        .line_buffer = line,
        .line_buffer_size = sizeof(line),
    };
    CHECK_EQ_INT(cli_init(&cli, &config), CLI_INIT_ERR_INVALID_ARG);
}

TEST(init_accepts_an_empty_command_table)
{
    cli_t cli;
    char line[16];
    const cli_config_t config = {
        .commands = NULL,
        .command_count = 0,
        .line_buffer = line,
        .line_buffer_size = sizeof(line),
    };
    CHECK_EQ_INT(cli_init(&cli, &config), CLI_INIT_OK);
}

TEST_MAIN(
    RUN(a_command_runs_when_the_line_ends);
    RUN(command_names_are_case_insensitive);
    RUN(user_data_distinguishes_commands_sharing_a_handler);
    RUN(an_unknown_command_is_reported_and_not_run);
    RUN(a_blank_line_does_nothing);
    RUN(crlf_does_not_produce_a_spurious_second_command);
    RUN(a_nonzero_return_is_reported_as_an_error);
    RUN(a_successful_command_prints_no_error);
    RUN(a_command_with_a_null_handler_does_not_crash);
    RUN(a_handler_can_write_through_the_stream);

    RUN(backspace_erases_the_previous_character);
    RUN(delete_erases_like_backspace);
    RUN(backspace_on_an_empty_line_is_harmless);
    RUN(an_overlong_line_is_rejected_rather_than_truncated);
    RUN(the_interpreter_recovers_after_an_overlong_line);
    RUN(control_characters_are_dropped_from_the_line);
    RUN(echo_off_leaves_typed_characters_unwritten);
    RUN(echo_on_writes_typed_characters_back);
    RUN(echo_on_erases_destructively_on_backspace);
    RUN(the_prompt_is_written_after_each_command);

    RUN(a_decimal_argument_is_parsed);
    RUN(an_argument_with_a_0x_prefix_is_hexadecimal);
    RUN(hex_arguments_parse_with_or_without_a_prefix);
    RUN(a_trailing_non_digit_makes_an_argument_invalid);
    RUN(a_missing_argument_is_an_error);
    RUN(arguments_may_be_separated_by_spaces_or_commas);
    RUN(a_signed_argument_accepts_both_signs);
    RUN(signed_arguments_reach_the_ends_of_the_range);
    RUN(signed_arguments_past_the_range_are_rejected);
    RUN(a_float_argument_is_parsed);
    RUN(a_float_with_two_points_is_rejected);
    RUN(rest_returns_the_remainder_of_the_line_verbatim);
    RUN(rest_returns_null_when_nothing_follows);

    RUN(help_lists_commands_when_enabled);
    RUN(question_mark_is_an_alias_for_help);
    RUN(help_is_unknown_when_disabled);

    RUN(poll_drains_the_stream_and_runs_commands);
    RUN(poll_on_an_empty_stream_does_nothing);
    RUN(poll_stops_at_its_budget_rather_than_spinning);

    RUN(init_rejects_a_missing_line_buffer);
    RUN(init_rejects_a_command_without_a_name);
    RUN(init_accepts_an_empty_command_table);
)
