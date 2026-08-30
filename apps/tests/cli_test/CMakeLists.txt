# Hardware test for the cli component. See README.md.

add_executable(app_cli_test
    main.c
)

# Which transport the interpreter runs on is a property of the bench setup.
option(CLI_TEST_USE_UART "Run the CLI on a dedicated UART instead of stdio" OFF)
option(CLI_TEST_ECHO     "Echo typed characters back to the terminal" ON)
set(CLI_TEST_UART_ID     0      CACHE STRING "UART instance when CLI_TEST_USE_UART is on")
set(CLI_TEST_UART_TX_PIN 0      CACHE STRING "UART TX pin")
set(CLI_TEST_UART_RX_PIN 1      CACHE STRING "UART RX pin")
set(CLI_TEST_UART_BAUD   115200 CACHE STRING "UART baud rate")

target_compile_definitions(app_cli_test PRIVATE
    CLI_TEST_USE_UART=$<BOOL:${CLI_TEST_USE_UART}>
    CLI_TEST_ECHO=$<BOOL:${CLI_TEST_ECHO}>
    CLI_TEST_UART_ID=${CLI_TEST_UART_ID}
    CLI_TEST_UART_TX_PIN=${CLI_TEST_UART_TX_PIN}
    CLI_TEST_UART_RX_PIN=${CLI_TEST_UART_RX_PIN}
    CLI_TEST_UART_BAUD=${CLI_TEST_UART_BAUD}
)

target_link_libraries(app_cli_test PRIVATE
    pico_stdlib
    pico_framework::cli
)

pico_enable_stdio_usb(app_cli_test 1)
pico_enable_stdio_uart(app_cli_test 1)

pico_framework_set_warnings(main.c)

pico_add_extra_outputs(app_cli_test)
