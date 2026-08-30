# CLI on uart0 (GPIO 0/1) at 115200, independent of stdio. Use this to check
# that the interpreter really is transport-agnostic: printf() still goes to
# stdio while the CLI has its own port.

set(CMAKE_BUILD_TYPE     "Release" CACHE STRING "Build type")
set(CLI_TEST_USE_UART    ON        CACHE BOOL   "Run the CLI on a dedicated UART instead of stdio")
set(CLI_TEST_ECHO        ON        CACHE BOOL   "Echo typed characters back to the terminal")
set(CLI_TEST_UART_ID     0         CACHE STRING "UART instance")
set(CLI_TEST_UART_TX_PIN 0         CACHE STRING "UART TX pin")
set(CLI_TEST_UART_RX_PIN 1         CACHE STRING "UART RX pin")
set(CLI_TEST_UART_BAUD   115200    CACHE STRING "UART baud rate")
