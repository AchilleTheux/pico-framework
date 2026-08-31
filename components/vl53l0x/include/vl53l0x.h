/*
 * vl53l0x - ST VL53L0X time-of-flight distance sensor.
 *
 * A register map and a start-up sequence over i2c_device.h. The sequence is
 * ST's, by way of the widely used Pololu implementation — a long run of
 * undocumented register writes that are reproduced rather than reasoned about,
 * because ST never published what they mean.
 *
 * The timing arithmetic around them is in vl53l0x_timing.h, which has no SDK
 * dependency and is unit-tested; that is where drivers for this part actually
 * go wrong.
 *
 * Everything here is synchronous. The firmware this was ported from had to fold
 * the start-up into a seven-hundred-line state machine because its I2C layer
 * was queued and asynchronous; with a blocking i2c_device the same sequence is
 * a straight line, which is most of why this file is a fraction of the size.
 *
 * Ranging, though, is not blocking. Continuous mode plus vl53l0x_data_ready()
 * lets a main loop poll without waiting, which is what a robot wants — a single
 * measurement takes tens of milliseconds and nothing should stop for that.
 */

#ifndef PICO_FRAMEWORK_VL53L0X_H
#define PICO_FRAMEWORK_VL53L0X_H

#include <stdbool.h>
#include <stdint.h>

#include "i2c_device.h"

#include "vl53l0x_timing.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Every VL53L0X wakes at this address. Several on one bus therefore have to be
   brought up one at a time and re-addressed; see vl53l0x_set_address(). */
#define VL53L0X_DEFAULT_ADDRESS 0x29u

/* What the identification register reads on a genuine part. */
#define VL53L0X_MODEL_ID 0xEEu

/* Reported when nothing is in range. Distinguished from a real reading, which
   is the mistake a naive driver makes — 8190 mm looks like a measurement. */
#define VL53L0X_OUT_OF_RANGE_MM 8190u

typedef enum {
    VL53L0X_OK = 0,
    VL53L0X_ERR_INVALID_ARG,
    VL53L0X_ERR_NO_DEVICE,      /* nothing answered at the address */
    VL53L0X_ERR_WRONG_MODEL,    /* something answered, but not a VL53L0X */
    VL53L0X_ERR_I2C,            /* a transfer failed mid-sequence */
    VL53L0X_ERR_TIMEOUT,        /* the sensor did not finish in time */
    VL53L0X_ERR_BUDGET,         /* the timing budget cannot be met */
    VL53L0X_ERR_NOT_READY,      /* no measurement waiting */
    VL53L0X_ERR_OUT_OF_RANGE,   /* the measurement is valid: nothing in range */
    VL53L0X_ERR_BAD_MEASUREMENT, /* the sensor rejected its own reading */
} vl53l0x_result_t;

/*
 * Ready-made configurations, so the accuracy and range trade-off is a choice
 * from a short list rather than four registers to work out.
 */
typedef enum {
    /* 33 ms budget, the sensor's own default. About 1.2 m in good conditions. */
    VL53L0X_PROFILE_DEFAULT,

    /* 20 ms budget. Noisier and shorter ranged; for a fast control loop. */
    VL53L0X_PROFILE_FAST,

    /* 200 ms budget. Quieter, and slow enough to notice. */
    VL53L0X_PROFILE_ACCURATE,

    /*
     * Longer VCSEL periods and a lower signal-rate threshold: about 2 m, at
     * the cost of more false readings off dark or angled surfaces.
     */
    VL53L0X_PROFILE_LONG_RANGE,
} vl53l0x_profile_t;

typedef struct {
    i2c_device_t device;

    /*
     * Read out of the sensor during start-up and written back when a
     * measurement starts. ST's own code calls it StopVariable and does not say
     * what it is; ranging does not work without it.
     */
    uint8_t stop_variable;

    uint32_t timing_budget_us;
    bool continuous;
    bool initialised;
} vl53l0x_t;

/*
 * Bring a sensor up on an already-configured I2C bus.
 *
 * Takes around 40 ms: the sequence includes two reference calibrations that the
 * sensor performs itself. Fails rather than proceeding if the identification
 * register does not read back as a VL53L0X, so a wrong address or a different
 * device on the bus is caught here rather than by nonsense measurements later.
 */
vl53l0x_result_t vl53l0x_init(vl53l0x_t *sensor, i2c_inst_t *i2c, uint8_t address);

/*
 * Move the sensor to a different I2C address.
 *
 * Every VL53L0X wakes at 0x29, so more than one on a bus needs each brought up
 * alone — with the others held in reset by their XSHUT pin — and moved before
 * the next is released. The change takes effect immediately and does not
 * survive a power cycle, so it has to be redone at every start-up.
 */
vl53l0x_result_t vl53l0x_set_address(vl53l0x_t *sensor, uint8_t new_address);

/* Does a sensor answer, and is it a VL53L0X? */
vl53l0x_result_t vl53l0x_probe(i2c_inst_t *i2c, uint8_t address);

/* ---------------------------------------------------------------------------
 * Configuration
 * -------------------------------------------------------------------------*/

vl53l0x_result_t vl53l0x_apply_profile(vl53l0x_t *sensor, vl53l0x_profile_t profile);

/*
 * How long one measurement may take. Longer is quieter and reaches further;
 * VL53L0X_MIN_TIMING_BUDGET_US is the floor.
 *
 * Fails with VL53L0X_ERR_BUDGET if the enabled phases cannot fit, rather than
 * clamping — a budget that was quietly ignored looks exactly like one that was
 * honoured until the readings come back wrong.
 */
vl53l0x_result_t vl53l0x_set_timing_budget(vl53l0x_t *sensor, uint32_t budget_us);

static inline uint32_t vl53l0x_get_timing_budget(const vl53l0x_t *sensor)
{
    return sensor->timing_budget_us;
}

/*
 * Minimum return signal rate for a reading to be accepted, in units of
 * 1/128 MCPS — the register's own unit, so no floating point is involved.
 *
 * Lower accepts weaker returns, which reaches further and produces more false
 * readings off dark or angled surfaces. The sensor's default is 0.25 MCPS,
 * which is VL53L0X_SIGNAL_RATE_DEFAULT.
 */
#define VL53L0X_SIGNAL_RATE_DEFAULT 32u    /* 0.25 MCPS */
#define VL53L0X_SIGNAL_RATE_LONG_RANGE 13u /* about 0.10 MCPS */

vl53l0x_result_t vl53l0x_set_signal_rate_limit(vl53l0x_t *sensor, uint16_t limit_128ths);

/*
 * VCSEL pulse periods, in PLL clocks. Only even values exist. Longer reaches
 * further; the defaults are 14 and 10, and long range uses 18 and 14.
 */
vl53l0x_result_t vl53l0x_set_vcsel_periods(vl53l0x_t *sensor,
                                           uint8_t pre_range_pclks,
                                           uint8_t final_range_pclks);

/* ---------------------------------------------------------------------------
 * Measuring
 * -------------------------------------------------------------------------*/

/*
 * Take one measurement and wait for it. Blocks for roughly the timing budget.
 *
 * Returns VL53L0X_ERR_OUT_OF_RANGE when the measurement succeeded but nothing
 * was in range, which is a different thing from a failure and is reported
 * separately so a caller cannot mistake 8190 mm for a wall.
 */
vl53l0x_result_t vl53l0x_read_single(vl53l0x_t *sensor, uint16_t *millimetres);

/*
 * Start measuring repeatedly. `period_ms` of 0 measures back to back, which is
 * as fast as the timing budget allows.
 */
vl53l0x_result_t vl53l0x_start_continuous(vl53l0x_t *sensor, uint32_t period_ms);
vl53l0x_result_t vl53l0x_stop_continuous(vl53l0x_t *sensor);

/* Is a measurement waiting? Never blocks. */
bool vl53l0x_data_ready(vl53l0x_t *sensor);

/*
 * Take the waiting measurement. Returns VL53L0X_ERR_NOT_READY if there is
 * none, so a main loop can call this without checking first if it prefers.
 */
vl53l0x_result_t vl53l0x_read_continuous(vl53l0x_t *sensor, uint16_t *millimetres);

/* ---------------------------------------------------------------------------
 * Diagnostics
 * -------------------------------------------------------------------------*/

/* The sensor's own verdict on the last measurement, from RESULT_RANGE_STATUS.
   0 to 11; 11 means good. */
vl53l0x_result_t vl53l0x_last_range_status(vl53l0x_t *sensor, uint8_t *status);

/* What each range status means. Never NULL. */
const char *vl53l0x_range_status_name(uint8_t status);

const char *vl53l0x_result_name(vl53l0x_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_VL53L0X_H */
