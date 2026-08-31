#include <stddef.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/platform.h"

#include "can.h"

/* can_frame.h deliberately duplicates these values to remain host-testable. */
_Static_assert(CAN_FLAG_RTR == (uint32_t)CAN2040_ID_RTR, "can2040 RTR flag changed");
_Static_assert(CAN_FLAG_EXTENDED == (uint32_t)CAN2040_ID_EFF,
               "can2040 EFF flag changed");
_Static_assert(offsetof(can_bus_t, controller) == 0,
               "can2040 controller must be first in can_bus_t");

/* One can2040 instance can occupy each physical PIO block. */
static can_bus_t *active_buses[NUM_PIOS];

/* Placeholders used only to reserve can2040's hard-coded 0..31 instruction
   range in the SDK allocator. Two halves avoid a 32-bit allocator mask edge
   case; can2040 replaces all of the instructions at start. */
static const uint16_t reservation_instructions[PIO_INSTRUCTION_COUNT / 2u];
static const pio_program_t reservation_program_low = {
    .instructions = reservation_instructions,
    .length = PIO_INSTRUCTION_COUNT / 2u,
    .origin = 0,
};
static const pio_program_t reservation_program_high = {
    .instructions = reservation_instructions,
    .length = PIO_INSTRUCTION_COUNT / 2u,
    .origin = PIO_INSTRUCTION_COUNT / 2u,
};

static bool bus_is_active(const can_bus_t *bus)
{
    if (bus == NULL) {
        return false;
    }
    for (uint i = 0; i < NUM_PIOS; i++) {
        if (active_buses[i] == bus) {
            return true;
        }
    }
    return false;
}

static void __not_in_flash_func(can_irq_dispatch)(uint pio_num)
{
    can_bus_t *bus = active_buses[pio_num];
    if (bus != NULL && bus->initialised) {
        can2040_pio_irq_handler(&bus->controller);
    }
}

static void __not_in_flash_func(can_pio0_irq_handler)(void)
{
    can_irq_dispatch(0);
}

static void __not_in_flash_func(can_pio1_irq_handler)(void)
{
    can_irq_dispatch(1);
}

#if NUM_PIOS > 2
static void __not_in_flash_func(can_pio2_irq_handler)(void)
{
    can_irq_dispatch(2);
}
#endif

static irq_handler_t irq_handler_for(uint pio_num)
{
    switch (pio_num) {
        case 0:
            return can_pio0_irq_handler;
        case 1:
            return can_pio1_irq_handler;
#if NUM_PIOS > 2
        case 2:
            return can_pio2_irq_handler;
#endif
        default:
            return NULL;
    }
}

static bool pins_are_compatible(int rx_pin, int tx_pin)
{
    if (rx_pin < 0 || rx_pin >= (int)NUM_BANK0_GPIOS || tx_pin < CAN_NO_TX_PIN ||
        tx_pin >= (int)NUM_BANK0_GPIOS || tx_pin == rx_pin) {
        return false;
    }

#if NUM_BANK0_GPIOS > 32
    /* RP2350 PIO pin windows are 0..31 or 16..47. */
    if (tx_pin >= 0 && ((rx_pin < 16 && tx_pin > 31) ||
                        (rx_pin > 31 && tx_pin < 16))) {
        return false;
    }
#endif
    return true;
}

static bool bitrate_is_usable(uint32_t bitrate)
{
    if (bitrate == 0 || bitrate > CAN_MAX_BITRATE) {
        return false;
    }

    /* can2040 writes this encoded divider into the 24-bit CLKDIV field. */
    const uint64_t divider = (8ull * clock_get_hz(clk_sys)) / bitrate;
    return divider != 0 && divider <= 0x00ffffffu;
}

static bool pio_is_free(PIO pio)
{
    for (uint sm = 0; sm < NUM_PIO_STATE_MACHINES; sm++) {
        if (pio_sm_is_claimed(pio, sm)) {
            return false;
        }
    }
    return pio_can_add_program_at_offset(pio, &reservation_program_low, 0) &&
           pio_can_add_program_at_offset(pio, &reservation_program_high,
                                         PIO_INSTRUCTION_COUNT / 2u);
}

static void claim_pio(PIO pio)
{
    for (uint sm = 0; sm < NUM_PIO_STATE_MACHINES; sm++) {
        pio_sm_claim(pio, sm);
    }
}

static void unclaim_pio(PIO pio)
{
    for (uint sm = 0; sm < NUM_PIO_STATE_MACHINES; sm++) {
        pio_sm_unclaim(pio, sm);
    }
}

static void __not_in_flash_func(can2040_callback)(struct can2040 *controller,
                                                   uint32_t notify,
                                                   struct can2040_msg *message)
{
    /* C guarantees that a pointer to a struct's first member converts back to
       the enclosing struct. This avoids a registry scan in the hot IRQ path. */
    can_bus_t *bus = (can_bus_t *)controller;

    if (notify == CAN2040_NOTIFY_ERROR) {
        bus->controller_errors++;
        return;
    }
    if (notify != CAN2040_NOTIFY_RX || message == NULL) {
        return;
    }
    if (!can_filters_accept(bus->filters, bus->filter_count, message->id)) {
        bus->filtered++;
        return;
    }

    can_message_t received = {
        .id = can_id_value(message->id),
        .length = can_dlc_to_length(message->dlc),
        .extended = can_id_is_extended(message->id),
        .remote = can_id_is_remote(message->id),
    };
    if (!received.remote) {
        memcpy(received.data, message->data, received.length);
    }
    can_queue_push(&bus->rx_queue, &received);
}

can_result_t can_bus_init(can_bus_t *bus, const can_bus_config_t *config)
{
    if (bus == NULL || config == NULL || config->pio == NULL ||
        !PIO_IS_INSTANCE(config->pio) || config->rx_storage == NULL ||
        config->rx_storage_size < CAN_QUEUE_STORAGE_SIZE(1) ||
        (config->filter_count != 0 && config->filters == NULL) ||
        !pins_are_compatible(config->rx_pin, config->tx_pin)) {
        return CAN_ERR_INVALID_ARG;
    }

    const uint32_t bitrate = config->bitrate != 0 ? config->bitrate : CAN_DEFAULT_BITRATE;
    if (!bitrate_is_usable(bitrate)) {
        return CAN_ERR_INVALID_ARG;
    }

    const uint pio_num = PIO_NUM(config->pio);
    const uint irq_num = PIO_IRQ_NUM(config->pio, 0);
    const irq_handler_t handler = irq_handler_for(pio_num);
    if (handler == NULL) {
        return CAN_ERR_INVALID_ARG;
    }
    if (bus_is_active(bus)) {
        return CAN_ERR_PIO_IN_USE;
    }
    if (active_buses[pio_num] != NULL || !pio_is_free(config->pio)) {
        return CAN_ERR_PIO_IN_USE;
    }
    if (irq_has_handler(irq_num)) {
        return CAN_ERR_IRQ_IN_USE;
    }

    claim_pio(config->pio);
    if (pio_add_program_at_offset(config->pio, &reservation_program_low, 0) < 0) {
        unclaim_pio(config->pio);
        return CAN_ERR_PIO_IN_USE;
    }
    if (pio_add_program_at_offset(config->pio, &reservation_program_high,
                                  PIO_INSTRUCTION_COUNT / 2u) < 0) {
        pio_remove_program(config->pio, &reservation_program_low, 0);
        unclaim_pio(config->pio);
        return CAN_ERR_PIO_IN_USE;
    }

    memset(bus, 0, sizeof(*bus));
    bus->pio = config->pio;
    bus->filters = config->filters;
    bus->filter_count = config->filter_count;
    bus->bitrate = bitrate;
    bus->irq_num = irq_num;
    bus->rx_pin = config->rx_pin;
    bus->tx_pin = config->tx_pin;
    if (!can_queue_init(&bus->rx_queue, config->rx_storage, config->rx_storage_size)) {
        pio_remove_program(config->pio, &reservation_program_high,
                           PIO_INSTRUCTION_COUNT / 2u);
        pio_remove_program(config->pio, &reservation_program_low, 0);
        unclaim_pio(config->pio);
        return CAN_ERR_INVALID_ARG;
    }

    can2040_setup(&bus->controller, pio_num);
    can2040_callback_config(&bus->controller, can2040_callback);

    active_buses[pio_num] = bus;
    bus->initialised = true;

    irq_set_exclusive_handler(irq_num, handler);
    irq_set_priority(irq_num, config->irq_priority);
    irq_set_enabled(irq_num, true);

    can2040_start(&bus->controller, clock_get_hz(clk_sys), bitrate,
                  config->rx_pin, config->tx_pin);
    return CAN_OK;
}

void can_bus_deinit(can_bus_t *bus)
{
    if (bus == NULL || !bus_is_active(bus)) {
        return;
    }

    const uint pio_num = PIO_NUM(bus->pio);
    const irq_handler_t handler = irq_handler_for(pio_num);

    irq_set_enabled(bus->irq_num, false);
    can2040_stop(&bus->controller);
    pio_set_sm_mask_enabled(bus->pio, 0x0fu, false);

    bus->initialised = false;
    active_buses[pio_num] = NULL;
    if (irq_get_exclusive_handler(bus->irq_num) == handler) {
        irq_remove_handler(bus->irq_num, handler);
    }
    pio_remove_program(bus->pio, &reservation_program_high,
                       PIO_INSTRUCTION_COUNT / 2u);
    pio_remove_program(bus->pio, &reservation_program_low, 0);
    unclaim_pio(bus->pio);
}

can_result_t can_bus_send(can_bus_t *bus, const can_message_t *message)
{
    if (bus == NULL || !can_message_is_valid(message)) {
        return CAN_ERR_INVALID_ARG;
    }
    if (!bus_is_active(bus)) {
        return CAN_ERR_NOT_INITIALISED;
    }
    if (bus->tx_pin == CAN_NO_TX_PIN) {
        return CAN_ERR_MONITOR_MODE;
    }

    struct can2040_msg outgoing = {
        .id = can_id_pack(message->id, message->extended, message->remote),
        .dlc = message->length,
    };
    if (!message->remote) {
        memcpy(outgoing.data, message->data, message->length);
    }

    return can2040_transmit(&bus->controller, &outgoing) == 0
               ? CAN_OK
               : CAN_ERR_TX_FULL;
}

bool can_bus_can_send(can_bus_t *bus)
{
    return bus != NULL && bus_is_active(bus) && bus->tx_pin != CAN_NO_TX_PIN &&
           can2040_check_transmit(&bus->controller) != 0;
}

can_result_t can_bus_receive(can_bus_t *bus, can_message_t *message)
{
    if (bus == NULL || message == NULL) {
        return CAN_ERR_INVALID_ARG;
    }
    if (!bus_is_active(bus)) {
        return CAN_ERR_NOT_INITIALISED;
    }
    return can_queue_pop(&bus->rx_queue, message) ? CAN_OK : CAN_ERR_RX_EMPTY;
}

size_t can_bus_available(const can_bus_t *bus)
{
    return bus != NULL && bus_is_active(bus) ? can_queue_count(&bus->rx_queue) : 0;
}

void can_bus_clear_receive(can_bus_t *bus)
{
    if (bus != NULL && bus_is_active(bus)) {
        can_queue_clear(&bus->rx_queue);
    }
}

can_result_t can_bus_get_stats(can_bus_t *bus, can_bus_stats_t *stats)
{
    if (bus == NULL || stats == NULL) {
        return CAN_ERR_INVALID_ARG;
    }
    if (!bus_is_active(bus)) {
        return CAN_ERR_NOT_INITIALISED;
    }

    struct can2040_stats controller_stats;
    can2040_get_statistics(&bus->controller, &controller_stats);
    *stats = (can_bus_stats_t){
        .received = controller_stats.rx_total,
        .transmitted = controller_stats.tx_total,
        .transmit_attempts = controller_stats.tx_attempt,
        .parse_errors = controller_stats.parse_error,
        .filtered = bus->filtered,
        .queue_dropped = bus->rx_queue.dropped,
        .controller_errors = bus->controller_errors,
    };
    return CAN_OK;
}

const char *can_result_name(can_result_t result)
{
    switch (result) {
        case CAN_OK:                  return "ok";
        case CAN_ERR_INVALID_ARG:     return "invalid argument";
        case CAN_ERR_PIO_IN_USE:      return "PIO block in use";
        case CAN_ERR_IRQ_IN_USE:      return "PIO IRQ in use";
        case CAN_ERR_NOT_INITIALISED: return "not initialised";
        case CAN_ERR_MONITOR_MODE:    return "monitor mode";
        case CAN_ERR_TX_FULL:         return "transmit queue full";
        case CAN_ERR_RX_EMPTY:        return "receive queue empty";
        default:                      return "unknown";
    }
}
