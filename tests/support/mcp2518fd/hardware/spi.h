#ifndef TEST_MCP2518FD_SPI_H
#define TEST_MCP2518FD_SPI_H
#include <stddef.h>
#include <stdint.h>
typedef unsigned int uint;
typedef struct { uint32_t baudrate; } spi_inst_t;
extern spi_inst_t test_spi[2];
#define spi0 (&test_spi[0])
#define spi1 (&test_spi[1])
uint spi_set_baudrate(spi_inst_t *spi, uint baudrate);
uint spi_get_baudrate(const spi_inst_t *spi);
int spi_write_blocking(spi_inst_t *spi, const uint8_t *data, size_t length);
int spi_read_blocking(spi_inst_t *spi, uint8_t repeated, uint8_t *data, size_t length);
#endif
