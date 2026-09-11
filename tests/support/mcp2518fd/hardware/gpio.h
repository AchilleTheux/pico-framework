#ifndef TEST_MCP2518FD_GPIO_H
#define TEST_MCP2518FD_GPIO_H
#include <stdbool.h>
#include "hardware/spi.h"
#define NUM_BANK0_GPIOS 30u
#define GPIO_OUT 1
#define GPIO_IN 0
void gpio_init(uint pin);
void gpio_deinit(uint pin);
void gpio_put(uint pin, bool value);
void gpio_set_dir(uint pin, bool out);
void gpio_pull_up(uint pin);
bool gpio_get(uint pin);
#endif
