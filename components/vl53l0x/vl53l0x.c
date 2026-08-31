#include <string.h>

#include "pico/stdlib.h"

#include "vl53l0x.h"

/* Registers used here. The full map is much larger and mostly undocumented. */
#define REG_SYSRANGE_START 0x00
#define REG_SYSTEM_SEQUENCE_CONFIG 0x01
#define REG_SYSTEM_INTERMEASUREMENT_PERIOD 0x04
#define REG_SYSTEM_INTERRUPT_CONFIG_GPIO 0x0A
#define REG_SYSTEM_INTERRUPT_CLEAR 0x0B
#define REG_RESULT_INTERRUPT_STATUS 0x13
#define REG_RESULT_RANGE_STATUS 0x14
#define REG_MSRC_CONFIG_CONTROL 0x60
#define REG_FINAL_RANGE_CONFIG_MIN_COUNT_RATE_RTN_LIMIT 0x44
#define REG_PRE_RANGE_CONFIG_VCSEL_PERIOD 0x50
#define REG_PRE_RANGE_CONFIG_TIMEOUT_MACROP_HI 0x51
#define REG_FINAL_RANGE_CONFIG_VCSEL_PERIOD 0x70
#define REG_FINAL_RANGE_CONFIG_TIMEOUT_MACROP_HI 0x71
#define REG_MSRC_CONFIG_TIMEOUT_MACROP 0x46
#define REG_IDENTIFICATION_MODEL_ID 0xC0
#define REG_VHV_CONFIG_PAD_SCL_SDA_EXTSUP_HV 0x89
#define REG_I2C_SLAVE_DEVICE_ADDRESS 0x8A
#define REG_GLOBAL_CONFIG_SPAD_ENABLES_REF_0 0xB0
#define REG_DYNAMIC_SPAD_REF_EN_START_OFFSET 0x4F
#define REG_DYNAMIC_SPAD_NUM_REQUESTED_REF_SPAD 0x4E
#define REG_GLOBAL_CONFIG_REF_EN_START_SELECT 0xB6
#define REG_PRE_RANGE_CONFIG_VALID_PHASE_HIGH 0x57
#define REG_PRE_RANGE_CONFIG_VALID_PHASE_LOW 0x56
#define REG_FINAL_RANGE_CONFIG_VALID_PHASE_HIGH 0x48
#define REG_FINAL_RANGE_CONFIG_VALID_PHASE_LOW 0x47
#define REG_GLOBAL_CONFIG_VCSEL_WIDTH 0x32
#define REG_ALGO_PHASECAL_CONFIG_TIMEOUT 0x30
#define REG_ALGO_PHASECAL_LIM 0x30

/* How long to wait for a measurement or a calibration before giving up. */
#define IO_TIMEOUT_MS 500u

#define SPAD_MAP_BYTES 6u

/* ---------------------------------------------------------------------------
 * Register helpers
 *
 * Thin wrappers that collapse i2c_device's richer error set into "it worked or
 * it did not", because inside a hundred-write sequence the distinction is not
 * actionable — any failure means abandoning the sequence.
 * -------------------------------------------------------------------------*/

static bool write8(vl53l0x_t *sensor, uint8_t reg, uint8_t value)
{
    return i2c_device_write_bytes(&sensor->device, reg, &value, 1) == I2C_DEVICE_OK;
}

static bool write16(vl53l0x_t *sensor, uint8_t reg, uint16_t value)
{
    /* The sensor is big-endian, which is what i2c_device was configured with. */
    return i2c_device_write_value(&sensor->device, reg, 2, value) == I2C_DEVICE_OK;
}

static bool read8(vl53l0x_t *sensor, uint8_t reg, uint8_t *value)
{
    uint32_t raw = 0;
    if (i2c_device_read_value(&sensor->device, reg, 1, &raw) != I2C_DEVICE_OK) {
        return false;
    }
    *value = (uint8_t)raw;
    return true;
}

static bool read16(vl53l0x_t *sensor, uint8_t reg, uint16_t *value)
{
    uint32_t raw = 0;
    if (i2c_device_read_value(&sensor->device, reg, 2, &raw) != I2C_DEVICE_OK) {
        return false;
    }
    *value = (uint16_t)raw;
    return true;
}

/* Apply a list of register writes, stopping at the first failure. */
typedef struct {
    uint8_t reg;
    uint8_t value;
} register_write_t;

static bool write_all(vl53l0x_t *sensor, const register_write_t *writes, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (!write8(sensor, writes[i].reg, writes[i].value)) {
            return false;
        }
    }
    return true;
}

/* ---------------------------------------------------------------------------
 * Reading the sensor's current timing
 * -------------------------------------------------------------------------*/

static bool read_sequence_timeouts(vl53l0x_t *sensor, vl53l0x_sequence_timeouts_t *out)
{
    uint8_t encoded = 0;

    if (!read8(sensor, REG_PRE_RANGE_CONFIG_VCSEL_PERIOD, &encoded)) {
        return false;
    }
    out->pre_range_vcsel_period_pclks = vl53l0x_decode_vcsel_period(encoded);

    uint8_t msrc_encoded = 0;
    if (!read8(sensor, REG_MSRC_CONFIG_TIMEOUT_MACROP, &msrc_encoded)) {
        return false;
    }
    /* The MSRC timeout is a plain byte plus one, not the compressed format. */
    out->msrc_dss_tcc_mclks = (uint16_t)(msrc_encoded + 1u);
    out->msrc_dss_tcc_us =
        vl53l0x_timeout_mclks_to_us(out->msrc_dss_tcc_mclks,
                                    out->pre_range_vcsel_period_pclks);

    uint16_t pre_range_encoded = 0;
    if (!read16(sensor, REG_PRE_RANGE_CONFIG_TIMEOUT_MACROP_HI, &pre_range_encoded)) {
        return false;
    }
    out->pre_range_mclks = (uint16_t)vl53l0x_decode_timeout(pre_range_encoded);
    out->pre_range_us =
        vl53l0x_timeout_mclks_to_us(out->pre_range_mclks,
                                    out->pre_range_vcsel_period_pclks);

    if (!read8(sensor, REG_FINAL_RANGE_CONFIG_VCSEL_PERIOD, &encoded)) {
        return false;
    }
    out->final_range_vcsel_period_pclks = vl53l0x_decode_vcsel_period(encoded);

    uint16_t final_range_encoded = 0;
    if (!read16(sensor, REG_FINAL_RANGE_CONFIG_TIMEOUT_MACROP_HI, &final_range_encoded)) {
        return false;
    }
    out->final_range_mclks = (uint16_t)vl53l0x_decode_timeout(final_range_encoded);

    /*
     * The final range timeout register counts the pre-range phase as well when
     * pre-range is enabled, so the pre-range portion is subtracted to get the
     * final range's own share. Missing this makes every budget calculation
     * over-count by the pre-range time.
     */
    uint8_t sequence_config = 0;
    if (!read8(sensor, REG_SYSTEM_SEQUENCE_CONFIG, &sequence_config)) {
        return false;
    }
    if (vl53l0x_decode_sequence_steps(sequence_config).pre_range &&
        out->final_range_mclks > out->pre_range_mclks) {
        out->final_range_mclks =
            (uint16_t)(out->final_range_mclks - out->pre_range_mclks);
    }
    out->final_range_us =
        vl53l0x_timeout_mclks_to_us(out->final_range_mclks,
                                    out->final_range_vcsel_period_pclks);

    return true;
}

/* ---------------------------------------------------------------------------
 * Start-up
 *
 * The sequence below is ST's, reproduced from a known-good implementation. The
 * magic register numbers are not explained anywhere ST published, so they are
 * left as numbers rather than given invented names — a name that guesses at
 * what a register does is worse than an address that admits it does not know.
 * -------------------------------------------------------------------------*/

/* Read how many reference SPADs the part has, and of which kind. */
static bool read_spad_info(vl53l0x_t *sensor, uint8_t *count, bool *is_aperture)
{
    static const register_write_t before[] = {
        { 0x80, 0x01 }, { 0xFF, 0x01 }, { 0x00, 0x00 },
        { 0xFF, 0x06 },
    };
    if (!write_all(sensor, before, count_of(before))) {
        return false;
    }

    uint8_t value = 0;
    if (!read8(sensor, 0x83, &value) || !write8(sensor, 0x83, (uint8_t)(value | 0x04))) {
        return false;
    }

    static const register_write_t middle[] = {
        { 0xFF, 0x07 }, { 0x81, 0x01 }, { 0x80, 0x01 },
        { 0x94, 0x6B }, { 0x83, 0x00 },
    };
    if (!write_all(sensor, middle, count_of(middle))) {
        return false;
    }

    /* Wait for the part to produce the value. */
    const absolute_time_t deadline = make_timeout_time_ms(IO_TIMEOUT_MS);
    do {
        if (!read8(sensor, 0x83, &value)) {
            return false;
        }
        if (value != 0x00) {
            break;
        }
        if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0) {
            return false;
        }
    } while (true);

    if (!write8(sensor, 0x83, 0x01) || !read8(sensor, 0x92, &value)) {
        return false;
    }

    *count = value & 0x7Fu;
    *is_aperture = (value & 0x80u) != 0;

    static const register_write_t after_a[] = {
        { 0x81, 0x00 }, { 0xFF, 0x06 },
    };
    if (!write_all(sensor, after_a, count_of(after_a))) {
        return false;
    }
    if (!read8(sensor, 0x83, &value) || !write8(sensor, 0x83, (uint8_t)(value & ~0x04u))) {
        return false;
    }

    static const register_write_t after_b[] = {
        { 0xFF, 0x01 }, { 0x00, 0x01 }, { 0xFF, 0x00 }, { 0x80, 0x00 },
    };
    return write_all(sensor, after_b, count_of(after_b));
}

/* The long undocumented block that configures the ranging phases. */
static bool write_tuning_settings(vl53l0x_t *sensor)
{
    static const register_write_t tuning[] = {
        { 0xFF, 0x01 }, { 0x00, 0x00 }, { 0xFF, 0x00 }, { 0x09, 0x00 },
        { 0x10, 0x00 }, { 0x11, 0x00 }, { 0x24, 0x01 }, { 0x25, 0xFF },
        { 0x75, 0x00 }, { 0xFF, 0x01 }, { 0x4E, 0x2C }, { 0x48, 0x00 },
        { 0x30, 0x20 }, { 0xFF, 0x00 }, { 0x30, 0x09 }, { 0x54, 0x00 },
        { 0x31, 0x04 }, { 0x32, 0x03 }, { 0x40, 0x83 }, { 0x46, 0x25 },
        { 0x60, 0x00 }, { 0x27, 0x00 }, { 0x50, 0x06 }, { 0x51, 0x00 },
        { 0x52, 0x96 }, { 0x56, 0x08 }, { 0x57, 0x30 }, { 0x61, 0x00 },
        { 0x62, 0x00 }, { 0x64, 0x00 }, { 0x65, 0x00 }, { 0x66, 0xA0 },
        { 0xFF, 0x01 }, { 0x22, 0x32 }, { 0x47, 0x14 }, { 0x49, 0xFF },
        { 0x4A, 0x00 }, { 0xFF, 0x00 }, { 0x7A, 0x0A }, { 0x7B, 0x00 },
        { 0x78, 0x21 }, { 0xFF, 0x01 }, { 0x23, 0x34 }, { 0x42, 0x00 },
        { 0x44, 0xFF }, { 0x45, 0x26 }, { 0x46, 0x05 }, { 0x40, 0x40 },
        { 0x0E, 0x06 }, { 0x20, 0x1A }, { 0x43, 0x40 }, { 0xFF, 0x00 },
        { 0x34, 0x03 }, { 0x35, 0x44 }, { 0xFF, 0x01 }, { 0x31, 0x04 },
        { 0x4B, 0x09 }, { 0x4C, 0x05 }, { 0x4D, 0x04 }, { 0xFF, 0x00 },
        { 0x44, 0x00 }, { 0x45, 0x20 }, { 0x47, 0x08 }, { 0x48, 0x28 },
        { 0x67, 0x00 }, { 0x70, 0x04 }, { 0x71, 0x01 }, { 0x72, 0xFE },
        { 0x76, 0x00 }, { 0x77, 0x00 }, { 0xFF, 0x01 }, { 0x0D, 0x01 },
        { 0xFF, 0x00 }, { 0x80, 0x01 }, { 0x01, 0xF8 }, { 0xFF, 0x01 },
        { 0x8E, 0x01 }, { 0x00, 0x01 }, { 0xFF, 0x00 }, { 0x80, 0x00 },
    };
    return write_all(sensor, tuning, count_of(tuning));
}

/* Run one of the sensor's self-calibrations and wait for it. */
static bool run_calibration(vl53l0x_t *sensor, uint8_t sequence, uint8_t start)
{
    if (!write8(sensor, REG_SYSTEM_SEQUENCE_CONFIG, sequence)) {
        return false;
    }
    if (!write8(sensor, REG_SYSRANGE_START, start)) {
        return false;
    }

    const absolute_time_t deadline = make_timeout_time_ms(IO_TIMEOUT_MS);
    uint8_t status = 0;
    do {
        if (!read8(sensor, REG_RESULT_INTERRUPT_STATUS, &status)) {
            return false;
        }
        if ((status & 0x07u) != 0) {
            break;
        }
        if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0) {
            return false;
        }
    } while (true);

    return write8(sensor, REG_SYSTEM_INTERRUPT_CLEAR, 0x01) &&
           write8(sensor, REG_SYSRANGE_START, 0x00);
}

vl53l0x_result_t vl53l0x_probe(i2c_inst_t *i2c, uint8_t address)
{
    i2c_device_t device;
    if (i2c_device_init(&device, i2c, address, I2C_ENDIAN_BIG, 0) != I2C_DEVICE_OK) {
        return VL53L0X_ERR_INVALID_ARG;
    }

    uint32_t model = 0;
    if (i2c_device_read_value(&device, REG_IDENTIFICATION_MODEL_ID, 1, &model)
            != I2C_DEVICE_OK) {
        return VL53L0X_ERR_NO_DEVICE;
    }

    /* Something answered; is it the right thing? Catching this here saves
       diagnosing nonsense measurements from a different device later. */
    return (model == VL53L0X_MODEL_ID) ? VL53L0X_OK : VL53L0X_ERR_WRONG_MODEL;
}

vl53l0x_result_t vl53l0x_init(vl53l0x_t *sensor, i2c_inst_t *i2c, uint8_t address)
{
    if (sensor == NULL) {
        return VL53L0X_ERR_INVALID_ARG;
    }

    memset(sensor, 0, sizeof(*sensor));

    /* Big-endian: this sensor's multi-byte registers are high byte first, which
       is the common case i2c_device defaults to. */
    if (i2c_device_init(&sensor->device, i2c, address, I2C_ENDIAN_BIG, 0)
            != I2C_DEVICE_OK) {
        return VL53L0X_ERR_INVALID_ARG;
    }

    const vl53l0x_result_t probed = vl53l0x_probe(i2c, address);
    if (probed != VL53L0X_OK) {
        return probed;
    }

    /* 2V8 mode: the reference designs run the I/O at 2.8 V. */
    uint8_t value = 0;
    if (!read8(sensor, REG_VHV_CONFIG_PAD_SCL_SDA_EXTSUP_HV, &value) ||
        !write8(sensor, REG_VHV_CONFIG_PAD_SCL_SDA_EXTSUP_HV, (uint8_t)(value | 0x01))) {
        return VL53L0X_ERR_I2C;
    }

    static const register_write_t standard_mode[] = {
        { 0x88, 0x00 }, { 0x80, 0x01 }, { 0xFF, 0x01 }, { 0x00, 0x00 },
    };
    if (!write_all(sensor, standard_mode, count_of(standard_mode))) {
        return VL53L0X_ERR_I2C;
    }

    /* The one value read out here and written back on every measurement. */
    if (!read8(sensor, 0x91, &sensor->stop_variable)) {
        return VL53L0X_ERR_I2C;
    }

    static const register_write_t after_stop[] = {
        { 0x00, 0x01 }, { 0xFF, 0x00 }, { 0x80, 0x00 },
    };
    if (!write_all(sensor, after_stop, count_of(after_stop))) {
        return VL53L0X_ERR_I2C;
    }

    /* Disable the signal-rate check inside MSRC, and set the default limit. */
    if (!read8(sensor, REG_MSRC_CONFIG_CONTROL, &value) ||
        !write8(sensor, REG_MSRC_CONFIG_CONTROL, (uint8_t)(value | 0x12))) {
        return VL53L0X_ERR_I2C;
    }
    if (!write16(sensor, REG_FINAL_RANGE_CONFIG_MIN_COUNT_RATE_RTN_LIMIT,
                 VL53L0X_SIGNAL_RATE_DEFAULT)) {
        return VL53L0X_ERR_I2C;
    }
    if (!write8(sensor, REG_SYSTEM_SEQUENCE_CONFIG, 0xFF)) {
        return VL53L0X_ERR_I2C;
    }

    /* Reference SPAD management. */
    uint8_t spad_count = 0;
    bool spad_is_aperture = false;
    if (!read_spad_info(sensor, &spad_count, &spad_is_aperture)) {
        return VL53L0X_ERR_I2C;
    }

    uint8_t spad_map[SPAD_MAP_BYTES];
    if (i2c_device_read_bytes(&sensor->device, REG_GLOBAL_CONFIG_SPAD_ENABLES_REF_0,
                              spad_map, sizeof(spad_map)) != I2C_DEVICE_OK) {
        return VL53L0X_ERR_I2C;
    }

    static const register_write_t before_spads[] = {
        { 0xFF, 0x01 },
        { REG_DYNAMIC_SPAD_REF_EN_START_OFFSET, 0x00 },
        { REG_DYNAMIC_SPAD_NUM_REQUESTED_REF_SPAD, 0x2C },
        { 0xFF, 0x00 },
        { REG_GLOBAL_CONFIG_REF_EN_START_SELECT, 0xB4 },
    };
    if (!write_all(sensor, before_spads, count_of(before_spads))) {
        return VL53L0X_ERR_I2C;
    }

    /*
     * Enable exactly `spad_count` reference SPADs, starting from the first of
     * the right kind. Aperture parts must not use the first twelve.
     */
    const uint8_t first_spad = spad_is_aperture ? 12u : 0u;
    uint8_t enabled = 0;
    for (uint8_t spad = 0; spad < 48u; spad++) {
        if (spad < first_spad || enabled == spad_count) {
            spad_map[spad / 8u] = (uint8_t)(spad_map[spad / 8u] & ~(1u << (spad % 8u)));
        } else if ((spad_map[spad / 8u] & (1u << (spad % 8u))) != 0) {
            enabled++;
        }
    }

    if (i2c_device_write_bytes(&sensor->device, REG_GLOBAL_CONFIG_SPAD_ENABLES_REF_0,
                               spad_map, sizeof(spad_map)) != I2C_DEVICE_OK) {
        return VL53L0X_ERR_I2C;
    }

    if (!write_tuning_settings(sensor)) {
        return VL53L0X_ERR_I2C;
    }

    /* Interrupt on new sample ready, active low. */
    if (!write8(sensor, REG_SYSTEM_INTERRUPT_CONFIG_GPIO, 0x04) ||
        !read8(sensor, 0x84, &value) ||
        !write8(sensor, 0x84, (uint8_t)(value & ~0x10u)) ||
        !write8(sensor, REG_SYSTEM_INTERRUPT_CLEAR, 0x01)) {
        return VL53L0X_ERR_I2C;
    }

    /* Adopt whatever budget the sequence currently implies, then calibrate. */
    vl53l0x_sequence_timeouts_t timeouts;
    if (!read_sequence_timeouts(sensor, &timeouts)) {
        return VL53L0X_ERR_I2C;
    }
    uint8_t sequence_config = 0;
    if (!read8(sensor, REG_SYSTEM_SEQUENCE_CONFIG, &sequence_config)) {
        return VL53L0X_ERR_I2C;
    }
    const vl53l0x_sequence_steps_t steps =
        vl53l0x_decode_sequence_steps(sequence_config);
    sensor->timing_budget_us = vl53l0x_budget_used_us(&steps, &timeouts);

    if (!write8(sensor, REG_SYSTEM_SEQUENCE_CONFIG, 0xE8)) {
        return VL53L0X_ERR_I2C;
    }

    /* VHV first, then phase. Both are the sensor calibrating itself. */
    if (!run_calibration(sensor, 0x01, 0x41) ||
        !run_calibration(sensor, 0x02, 0x01)) {
        return VL53L0X_ERR_TIMEOUT;
    }

    if (!write8(sensor, REG_SYSTEM_SEQUENCE_CONFIG, 0xE8)) {
        return VL53L0X_ERR_I2C;
    }

    sensor->initialised = true;
    return VL53L0X_OK;
}

vl53l0x_result_t vl53l0x_set_address(vl53l0x_t *sensor, uint8_t new_address)
{
    if (sensor == NULL || !sensor->initialised) {
        return VL53L0X_ERR_INVALID_ARG;
    }
    if (new_address < I2C_ADDRESS_MIN || new_address > I2C_ADDRESS_MAX) {
        return VL53L0X_ERR_INVALID_ARG;
    }

    /* The register takes the 7-bit address. */
    if (!write8(sensor, REG_I2C_SLAVE_DEVICE_ADDRESS, (uint8_t)(new_address & 0x7Fu))) {
        return VL53L0X_ERR_I2C;
    }

    /* It takes effect at once, so the handle has to follow it immediately or
       every later transfer goes to an address nothing answers. */
    sensor->device.address = new_address;
    sleep_ms(1);

    return (vl53l0x_probe(sensor->device.i2c, new_address) == VL53L0X_OK)
        ? VL53L0X_OK
        : VL53L0X_ERR_NO_DEVICE;
}

/* ---------------------------------------------------------------------------
 * Configuration
 * -------------------------------------------------------------------------*/

vl53l0x_result_t vl53l0x_set_signal_rate_limit(vl53l0x_t *sensor, uint16_t limit_128ths)
{
    if (sensor == NULL || !sensor->initialised) {
        return VL53L0X_ERR_INVALID_ARG;
    }
    return write16(sensor, REG_FINAL_RANGE_CONFIG_MIN_COUNT_RATE_RTN_LIMIT, limit_128ths)
        ? VL53L0X_OK : VL53L0X_ERR_I2C;
}

vl53l0x_result_t vl53l0x_set_timing_budget(vl53l0x_t *sensor, uint32_t budget_us)
{
    if (sensor == NULL || !sensor->initialised) {
        return VL53L0X_ERR_INVALID_ARG;
    }

    vl53l0x_sequence_timeouts_t timeouts;
    uint8_t sequence_config = 0;
    if (!read_sequence_timeouts(sensor, &timeouts) ||
        !read8(sensor, REG_SYSTEM_SEQUENCE_CONFIG, &sequence_config)) {
        return VL53L0X_ERR_I2C;
    }

    const vl53l0x_sequence_steps_t steps =
        vl53l0x_decode_sequence_steps(sequence_config);

    uint32_t final_range_us = 0;
    if (!vl53l0x_final_range_timeout_us(&steps, &timeouts, budget_us, &final_range_us)) {
        return VL53L0X_ERR_BUDGET;
    }
    if (!steps.final_range) {
        sensor->timing_budget_us = budget_us;
        return VL53L0X_OK;
    }

    uint32_t final_range_mclks =
        vl53l0x_timeout_us_to_mclks(final_range_us,
                                    timeouts.final_range_vcsel_period_pclks);

    /* The register counts the pre-range phase too, so it is added back in. */
    if (steps.pre_range) {
        final_range_mclks += timeouts.pre_range_mclks;
    }

    if (!write16(sensor, REG_FINAL_RANGE_CONFIG_TIMEOUT_MACROP_HI,
                 vl53l0x_encode_timeout(final_range_mclks))) {
        return VL53L0X_ERR_I2C;
    }

    sensor->timing_budget_us = budget_us;
    return VL53L0X_OK;
}

vl53l0x_result_t vl53l0x_set_vcsel_periods(vl53l0x_t *sensor,
                                           uint8_t pre_range_pclks,
                                           uint8_t final_range_pclks)
{
    if (sensor == NULL || !sensor->initialised) {
        return VL53L0X_ERR_INVALID_ARG;
    }
    /* Only even periods are representable, so an odd one is refused rather
       than silently rounded. */
    if ((pre_range_pclks % 2u) != 0 || (final_range_pclks % 2u) != 0 ||
        pre_range_pclks < 12u || pre_range_pclks > 18u ||
        final_range_pclks < 8u || final_range_pclks > 14u) {
        return VL53L0X_ERR_INVALID_ARG;
    }

    /*
     * The valid-phase and timing registers that go with each period. These
     * pairings are ST's; only the combinations below are supported by the
     * reference implementation, which is why the range is restricted above.
     */
    const uint8_t pre_range_phase_high = (pre_range_pclks >= 18u) ? 0x32u :
                                         (pre_range_pclks >= 16u) ? 0x30u :
                                         (pre_range_pclks >= 14u) ? 0x2Eu : 0x18u;

    if (!write8(sensor, REG_PRE_RANGE_CONFIG_VALID_PHASE_HIGH, pre_range_phase_high) ||
        !write8(sensor, REG_PRE_RANGE_CONFIG_VALID_PHASE_LOW, 0x08) ||
        !write8(sensor, REG_PRE_RANGE_CONFIG_VCSEL_PERIOD,
                vl53l0x_encode_vcsel_period(pre_range_pclks))) {
        return VL53L0X_ERR_I2C;
    }

    const uint8_t final_phase_high = (final_range_pclks >= 14u) ? 0x2Cu :
                                     (final_range_pclks >= 12u) ? 0x28u :
                                     (final_range_pclks >= 10u) ? 0x24u : 0x10u;

    if (!write8(sensor, REG_FINAL_RANGE_CONFIG_VALID_PHASE_HIGH, final_phase_high) ||
        !write8(sensor, REG_FINAL_RANGE_CONFIG_VALID_PHASE_LOW, 0x08) ||
        !write8(sensor, REG_GLOBAL_CONFIG_VCSEL_WIDTH, 0x03) ||
        !write8(sensor, REG_FINAL_RANGE_CONFIG_VCSEL_PERIOD,
                vl53l0x_encode_vcsel_period(final_range_pclks))) {
        return VL53L0X_ERR_I2C;
    }

    /* Changing a period changes what a macro clock is worth, so the budget has
       to be re-applied or the sensor silently measures for a different time. */
    return vl53l0x_set_timing_budget(sensor, sensor->timing_budget_us);
}

vl53l0x_result_t vl53l0x_apply_profile(vl53l0x_t *sensor, vl53l0x_profile_t profile)
{
    if (sensor == NULL || !sensor->initialised) {
        return VL53L0X_ERR_INVALID_ARG;
    }

    switch (profile) {
        case VL53L0X_PROFILE_DEFAULT:
            return vl53l0x_set_signal_rate_limit(sensor, VL53L0X_SIGNAL_RATE_DEFAULT) ==
                       VL53L0X_OK
                ? vl53l0x_set_timing_budget(sensor, 33000)
                : VL53L0X_ERR_I2C;

        case VL53L0X_PROFILE_FAST:
            return vl53l0x_set_timing_budget(sensor, 20000);

        case VL53L0X_PROFILE_ACCURATE:
            return vl53l0x_set_timing_budget(sensor, 200000);

        case VL53L0X_PROFILE_LONG_RANGE: {
            /* Order matters: the periods change what a macro clock is worth, so
               the budget is set last. */
            const vl53l0x_result_t limited =
                vl53l0x_set_signal_rate_limit(sensor, VL53L0X_SIGNAL_RATE_LONG_RANGE);
            if (limited != VL53L0X_OK) {
                return limited;
            }
            const vl53l0x_result_t periods = vl53l0x_set_vcsel_periods(sensor, 18, 14);
            if (periods != VL53L0X_OK) {
                return periods;
            }
            return vl53l0x_set_timing_budget(sensor, 33000);
        }

        default:
            return VL53L0X_ERR_INVALID_ARG;
    }
}

/* ---------------------------------------------------------------------------
 * Measuring
 * -------------------------------------------------------------------------*/

/* Write the stop variable back, which every measurement needs. */
static bool arm_measurement(vl53l0x_t *sensor)
{
    const register_write_t writes[] = {
        { 0x80, 0x01 }, { 0xFF, 0x01 }, { 0x00, 0x00 },
        { 0x91, sensor->stop_variable },
        { 0x00, 0x01 }, { 0xFF, 0x00 }, { 0x80, 0x00 },
    };
    return write_all(sensor, writes, count_of(writes));
}

/*
 * Turn a raw reading into a result.
 *
 * The range status is checked before the distance, because the sensor reports
 * 8190 mm when nothing is in range — a value that is in range for the API and
 * would be taken for a wall eight metres away.
 */
static vl53l0x_result_t interpret(vl53l0x_t *sensor, uint16_t raw,
                                  uint16_t *millimetres)
{
    uint8_t status = 0;
    if (!read8(sensor, REG_RESULT_RANGE_STATUS, &status)) {
        return VL53L0X_ERR_I2C;
    }

    /* The status is in the top five bits. 11 is a good measurement. */
    const uint8_t range_status = (uint8_t)((status & 0x78u) >> 3);

    if (raw >= VL53L0X_OUT_OF_RANGE_MM || range_status == 4u) {
        *millimetres = VL53L0X_OUT_OF_RANGE_MM;
        return VL53L0X_ERR_OUT_OF_RANGE;
    }
    if (range_status != 11u) {
        *millimetres = raw;
        return VL53L0X_ERR_BAD_MEASUREMENT;
    }

    *millimetres = raw;
    return VL53L0X_OK;
}

static vl53l0x_result_t wait_for_measurement(vl53l0x_t *sensor)
{
    const absolute_time_t deadline = make_timeout_time_ms(IO_TIMEOUT_MS);
    uint8_t status = 0;

    do {
        if (!read8(sensor, REG_RESULT_INTERRUPT_STATUS, &status)) {
            return VL53L0X_ERR_I2C;
        }
        if ((status & 0x07u) != 0) {
            return VL53L0X_OK;
        }
        if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0) {
            return VL53L0X_ERR_TIMEOUT;
        }
    } while (true);
}

vl53l0x_result_t vl53l0x_read_single(vl53l0x_t *sensor, uint16_t *millimetres)
{
    if (sensor == NULL || !sensor->initialised || millimetres == NULL) {
        return VL53L0X_ERR_INVALID_ARG;
    }
    *millimetres = 0;

    if (!arm_measurement(sensor) || !write8(sensor, REG_SYSRANGE_START, 0x01)) {
        return VL53L0X_ERR_I2C;
    }

    /* Wait for the start bit to clear, then for the measurement. */
    const absolute_time_t deadline = make_timeout_time_ms(IO_TIMEOUT_MS);
    uint8_t start = 0;
    do {
        if (!read8(sensor, REG_SYSRANGE_START, &start)) {
            return VL53L0X_ERR_I2C;
        }
        if ((start & 0x01u) == 0) {
            break;
        }
        if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0) {
            return VL53L0X_ERR_TIMEOUT;
        }
    } while (true);

    const vl53l0x_result_t waited = wait_for_measurement(sensor);
    if (waited != VL53L0X_OK) {
        return waited;
    }

    uint16_t raw = 0;
    if (!read16(sensor, (uint8_t)(REG_RESULT_RANGE_STATUS + 10u), &raw)) {
        return VL53L0X_ERR_I2C;
    }

    const vl53l0x_result_t interpreted = interpret(sensor, raw, millimetres);

    if (!write8(sensor, REG_SYSTEM_INTERRUPT_CLEAR, 0x01)) {
        return VL53L0X_ERR_I2C;
    }
    return interpreted;
}

vl53l0x_result_t vl53l0x_start_continuous(vl53l0x_t *sensor, uint32_t period_ms)
{
    if (sensor == NULL || !sensor->initialised) {
        return VL53L0X_ERR_INVALID_ARG;
    }
    if (!arm_measurement(sensor)) {
        return VL53L0X_ERR_I2C;
    }

    if (period_ms != 0) {
        /*
         * The period is counted in units the sensor derives from its own
         * oscillator, so the calibration value has to be read and applied —
         * without it the interval is wrong by whatever that part's oscillator
         * happens to be off by.
         */
        uint16_t oscillator_calibrate = 0;
        if (!read16(sensor, 0xF8, &oscillator_calibrate)) {
            return VL53L0X_ERR_I2C;
        }
        uint32_t period = period_ms;
        if (oscillator_calibrate != 0) {
            period *= oscillator_calibrate;
        }
        if (i2c_device_write_value(&sensor->device,
                                   REG_SYSTEM_INTERMEASUREMENT_PERIOD, 4, period)
                != I2C_DEVICE_OK) {
            return VL53L0X_ERR_I2C;
        }
        if (!write8(sensor, REG_SYSRANGE_START, 0x04)) { /* timed mode */
            return VL53L0X_ERR_I2C;
        }
    } else {
        if (!write8(sensor, REG_SYSRANGE_START, 0x02)) { /* back to back */
            return VL53L0X_ERR_I2C;
        }
    }

    sensor->continuous = true;
    return VL53L0X_OK;
}

vl53l0x_result_t vl53l0x_stop_continuous(vl53l0x_t *sensor)
{
    if (sensor == NULL || !sensor->initialised) {
        return VL53L0X_ERR_INVALID_ARG;
    }

    static const register_write_t writes[] = {
        { REG_SYSRANGE_START, 0x01 },
        { 0xFF, 0x01 }, { 0x00, 0x00 }, { 0x91, 0x00 },
        { 0x00, 0x01 }, { 0xFF, 0x00 },
    };
    if (!write_all(sensor, writes, count_of(writes))) {
        return VL53L0X_ERR_I2C;
    }

    sensor->continuous = false;
    return VL53L0X_OK;
}

bool vl53l0x_data_ready(vl53l0x_t *sensor)
{
    if (sensor == NULL || !sensor->initialised) {
        return false;
    }

    uint8_t status = 0;
    if (!read8(sensor, REG_RESULT_INTERRUPT_STATUS, &status)) {
        return false;
    }
    return (status & 0x07u) != 0;
}

vl53l0x_result_t vl53l0x_read_continuous(vl53l0x_t *sensor, uint16_t *millimetres)
{
    if (sensor == NULL || !sensor->initialised || millimetres == NULL) {
        return VL53L0X_ERR_INVALID_ARG;
    }
    *millimetres = 0;

    if (!vl53l0x_data_ready(sensor)) {
        return VL53L0X_ERR_NOT_READY;
    }

    uint16_t raw = 0;
    if (!read16(sensor, (uint8_t)(REG_RESULT_RANGE_STATUS + 10u), &raw)) {
        return VL53L0X_ERR_I2C;
    }

    const vl53l0x_result_t interpreted = interpret(sensor, raw, millimetres);

    /* Cleared so the next measurement can be recognised as new. */
    if (!write8(sensor, REG_SYSTEM_INTERRUPT_CLEAR, 0x01)) {
        return VL53L0X_ERR_I2C;
    }
    return interpreted;
}

vl53l0x_result_t vl53l0x_last_range_status(vl53l0x_t *sensor, uint8_t *status)
{
    if (sensor == NULL || !sensor->initialised || status == NULL) {
        return VL53L0X_ERR_INVALID_ARG;
    }

    uint8_t raw = 0;
    if (!read8(sensor, REG_RESULT_RANGE_STATUS, &raw)) {
        return VL53L0X_ERR_I2C;
    }
    *status = (uint8_t)((raw & 0x78u) >> 3);
    return VL53L0X_OK;
}

const char *vl53l0x_range_status_name(uint8_t status)
{
    /* From ST's device error codes. Only a few occur in practice, and knowing
       which one is the difference between a wiring fault and a dark target. */
    switch (status) {
        case 0:  return "data not ready";
        case 1:  return "VCSEL continuity test failed";
        case 2:  return "VCSEL watchdog test failed";
        case 3:  return "no VHV value found";
        case 4:  return "signal too weak (nothing in range)";
        case 5:  return "sigma too high (noisy)";
        case 6:  return "target below minimum distance";
        case 7:  return "phase out of valid limits";
        case 8:  return "hardware failure";
        case 9:  return "range ignored";
        case 10: return "reference SPAD check failed";
        case 11: return "good";
        default: return "unknown";
    }
}

const char *vl53l0x_result_name(vl53l0x_result_t result)
{
    switch (result) {
        case VL53L0X_OK:                   return "ok";
        case VL53L0X_ERR_INVALID_ARG:      return "invalid argument";
        case VL53L0X_ERR_NO_DEVICE:        return "nothing answered";
        case VL53L0X_ERR_WRONG_MODEL:      return "not a VL53L0X";
        case VL53L0X_ERR_I2C:              return "i2c transfer failed";
        case VL53L0X_ERR_TIMEOUT:          return "sensor did not finish in time";
        case VL53L0X_ERR_BUDGET:           return "timing budget cannot be met";
        case VL53L0X_ERR_NOT_READY:        return "no measurement waiting";
        case VL53L0X_ERR_OUT_OF_RANGE:     return "nothing in range";
        case VL53L0X_ERR_BAD_MEASUREMENT:  return "sensor rejected the reading";
        default:                           return "unknown";
    }
}
