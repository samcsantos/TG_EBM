#include "ad5933_stm32_2.h"

#include <math.h>
#include <stddef.h>

#define AD5933_POW_2_27 134217728UL

/*
 * Change this if you do not want blocking I2C forever.
 *
 * For example:
 * #define AD5933_I2C_TIMEOUT 100U
 */
#ifndef AD5933_I2C_TIMEOUT
#define AD5933_I2C_TIMEOUT 1000U
#endif

/* ========================================================================= */
/* Private Helpers                                                           */
/* ========================================================================= */

static HAL_StatusTypeDef AD5933_FrequencyToCode(uint32_t frequency_hz,
                                                uint32_t system_clock_hz,
                                                uint32_t *code)
{
    double raw;

    if ((code == NULL) || (system_clock_hz == 0U))
    {
        return HAL_ERROR;
    }

    /*
     * AD5933 excitation frequency:
     *
     * f = (MCLK / 4) * (frequency_code / 2^27)
     *
     * Therefore:
     *
     * frequency_code = f * 4 * 2^27 / MCLK
     */
    raw = (double)frequency_hz * 4.0 * (double)AD5933_POW_2_27 /
          (double)system_clock_hz;

    if (raw < 0.0)
    {
        raw = 0.0;
    }

    if (raw > (double)AD5933_FREQ_CODE_MASK)
    {
        raw = (double)AD5933_FREQ_CODE_MASK;
    }

    /* Round to nearest integer. */
    *code = (uint32_t)(raw + 0.5);

    return HAL_OK;
}

/* ========================================================================= */
/* Initialization                                                            */
/* ========================================================================= */

void AD5933_Init(AD5933_HandleTypeDef *dev,
                 I2C_HandleTypeDef *hi2c)
{
    if (dev == NULL)
    {
        return;
    }

    dev->i2c = hi2c;

    dev->current_sys_clk = AD5933_INTERNAL_SYS_CLK;
    dev->current_clock_source = AD5933_CONTROL_INT_SYSCLK;

    dev->current_gain = AD5933_GAIN_X5;
    dev->current_range = AD5933_RANGE_2000mVpp;
}

/* ========================================================================= */
/* Register Access                                                           */
/* ========================================================================= */

HAL_StatusTypeDef AD5933_SetRegisterValue(AD5933_HandleTypeDef *dev,
                                          uint8_t register_address,
                                          uint32_t register_value,
                                          uint8_t bytes_number)
{
    uint8_t i;
    uint8_t shift;
    uint8_t byte_to_write;
    HAL_StatusTypeDef status;

    if ((dev == NULL) ||
        (dev->i2c == NULL) ||
        (bytes_number == 0U) ||
        (bytes_number > 4U))
    {
        return HAL_ERROR;
    }

    /*
     * Some AD5933 devices/modules do not accept multi-byte block writes.
     *
     * Therefore, write each register byte separately:
     *
     *   register_address + 0 -> MSB
     *   register_address + 1 -> next byte
     *   ...
     */
    for (i = 0U; i < bytes_number; i++)
    {
        shift = (uint8_t)((bytes_number - 1U - i) * 8U);
        byte_to_write = (uint8_t)((register_value >> shift) & 0xFFU);

        status = HAL_I2C_Mem_Write(dev->i2c,
                                   AD5933_I2C_ADDR,
                                   (uint8_t)(register_address + i),
                                   I2C_MEMADD_SIZE_8BIT,
                                   &byte_to_write,
                                   1U,
                                   AD5933_I2C_TIMEOUT);

        if (status != HAL_OK)
        {
            return status;
        }

        /*
         * Optional: if the device is very sensitive, try uncommenting
         * this small delay between consecutive register writes.
         *
         * HAL_Delay(1);
         */
    }

    return HAL_OK;
}

HAL_StatusTypeDef AD5933_GetRegisterValue(AD5933_HandleTypeDef *dev,
                                          uint8_t register_address,
                                          uint8_t bytes_number,
                                          uint32_t *register_value)
{
    uint8_t i;
    uint8_t byte_read;
    uint32_t value;
    HAL_StatusTypeDef status;

    if ((dev == NULL) ||
        (dev->i2c == NULL) ||
        (register_value == NULL) ||
        (bytes_number == 0U) ||
        (bytes_number > 4U))
    {
        return HAL_ERROR;
    }

    value = 0U;

    /*
     * Read each register byte separately.
     */
    for (i = 0U; i < bytes_number; i++)
    {
        status = HAL_I2C_Mem_Read(dev->i2c,
                                  AD5933_I2C_ADDR,
                                  (uint8_t)(register_address + i),
                                  I2C_MEMADD_SIZE_8BIT,
                                  &byte_read,
                                  1U,
                                  AD5933_I2C_TIMEOUT);

        if (status != HAL_OK)
        {
            return status;
        }

        value = (value << 8) | (uint32_t)byte_read;
    }

    *register_value = value;

    return HAL_OK;
}

/* ========================================================================= */
/* Device Configuration                                                      */
/* ========================================================================= */

HAL_StatusTypeDef AD5933_Reset(AD5933_HandleTypeDef *dev)
{
    if ((dev == NULL) || (dev->i2c == NULL))
    {
        return HAL_ERROR;
    }

    return AD5933_SetRegisterValue(dev,
                                   AD5933_REG_CONTROL_LB,
                                   AD5933_CONTROL_RESET |
                                   dev->current_clock_source,
                                   1U);
}

HAL_StatusTypeDef AD5933_SetSystemClock(AD5933_HandleTypeDef *dev,
                                        uint8_t clock_source,
                                        uint32_t external_clock_frequency)
{
    if ((dev == NULL) || (dev->i2c == NULL))
    {
        return HAL_ERROR;
    }

    if ((clock_source != AD5933_CONTROL_INT_SYSCLK) &&
        (clock_source != AD5933_CONTROL_EXT_SYSCLK))
    {
        return HAL_ERROR;
    }

    if ((clock_source == AD5933_CONTROL_EXT_SYSCLK) &&
        (external_clock_frequency == 0U))
    {
        return HAL_ERROR;
    }

    dev->current_clock_source = clock_source;

    if (clock_source == AD5933_CONTROL_EXT_SYSCLK)
    {
        dev->current_sys_clk = external_clock_frequency;
    }
    else
    {
        dev->current_sys_clk = AD5933_INTERNAL_SYS_CLK;
    }

    return AD5933_SetRegisterValue(dev,
                                   AD5933_REG_CONTROL_LB,
                                   dev->current_clock_source,
                                   1U);
}

HAL_StatusTypeDef AD5933_SetRangeAndGain(AD5933_HandleTypeDef *dev,
                                         uint8_t range,
                                         uint8_t gain)
{
    HAL_StatusTypeDef status;

    if ((dev == NULL) || (dev->i2c == NULL))
    {
        return HAL_ERROR;
    }

    if (range > AD5933_RANGE_1000mVpp)
    {
        return HAL_ERROR;
    }

    if (gain > AD5933_GAIN_X1)
    {
        return HAL_ERROR;
    }

    status = AD5933_SetRegisterValue(dev,
                                     AD5933_REG_CONTROL_HB,
                                     AD5933_CONTROL_FUNCTION(AD5933_FUNCTION_NOP) |
                                     AD5933_CONTROL_RANGE(range) |
                                     AD5933_CONTROL_PGA_GAIN(gain),
                                     1U);

    if (status == HAL_OK)
    {
        dev->current_range = range;
        dev->current_gain = gain;
    }

    return status;
}

HAL_StatusTypeDef AD5933_SetSettlingTime(AD5933_HandleTypeDef *dev,
                                         uint8_t multiplier,
                                         uint16_t number_cycles)
{
    uint32_t value;

    if ((dev == NULL) || (dev->i2c == NULL))
    {
        return HAL_ERROR;
    }

    if (number_cycles > AD5933_MAX_SETTLING_CYCLES)
    {
        return HAL_ERROR;
    }

    if ((multiplier != AD5933_SETTLING_X1) &&
        (multiplier != AD5933_SETTLING_X2) &&
        (multiplier != AD5933_SETTLING_X4))
    {
        return HAL_ERROR;
    }

    value = ((uint32_t)multiplier << AD5933_SETTLING_MULT_SHIFT) |
            ((uint32_t)number_cycles & AD5933_SETTLING_CYCLES_MASK);

    return AD5933_SetRegisterValue(dev,
                                   AD5933_REG_SETTLING_CYCLES,
                                   value,
                                   2U);
}

/* ========================================================================= */
/* Frequency Sweep Configuration                                             */
/* ========================================================================= */

HAL_StatusTypeDef AD5933_ConfigSweep(AD5933_HandleTypeDef *dev,
                                     uint32_t start_frequency,
                                     uint32_t increment_frequency,
                                     uint16_t increment_number)
{
    uint32_t start_frequency_reg;
    uint32_t increment_frequency_reg;
    HAL_StatusTypeDef status;

    if ((dev == NULL) || (dev->i2c == NULL))
    {
        return HAL_ERROR;
    }

    if (AD5933_FrequencyToCode(start_frequency,
                               dev->current_sys_clk,
                               &start_frequency_reg) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (AD5933_FrequencyToCode(increment_frequency,
                               dev->current_sys_clk,
                               &increment_frequency_reg) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (increment_number > AD5933_MAX_INC_NUM)
    {
        increment_number = AD5933_MAX_INC_NUM;
    }

    status = AD5933_SetRegisterValue(dev,
                                     AD5933_REG_FREQ_START,
                                     start_frequency_reg,
                                     3U);
    if (status != HAL_OK)
    {
        return status;
    }

    status = AD5933_SetRegisterValue(dev,
                                     AD5933_REG_FREQ_INC,
                                     increment_frequency_reg,
                                     3U);
    if (status != HAL_OK)
    {
        return status;
    }

    return AD5933_SetRegisterValue(dev,
                                   AD5933_REG_INC_NUM,
                                   (uint32_t)increment_number,
                                   2U);
}

/* ========================================================================= */
/* Sweep Control                                                             */
/* ========================================================================= */

HAL_StatusTypeDef AD5933_StartSweep(AD5933_HandleTypeDef *dev)
{
    HAL_StatusTypeDef status;

    if ((dev == NULL) || (dev->i2c == NULL))
    {
        return HAL_ERROR;
    }

    /* Standby */
    status = AD5933_SetRegisterValue(dev,
                                     AD5933_REG_CONTROL_HB,
                                     AD5933_CONTROL_FUNCTION(AD5933_FUNCTION_STANDBY) |
                                     AD5933_CONTROL_RANGE(dev->current_range) |
                                     AD5933_CONTROL_PGA_GAIN(dev->current_gain),
                                     1U);
    if (status != HAL_OK)
    {
        return status;
    }

    /* Reset */
    status = AD5933_Reset(dev);
    if (status != HAL_OK)
    {
        return status;
    }

    /* Initialize start frequency */
    status = AD5933_SetRegisterValue(dev,
                                     AD5933_REG_CONTROL_HB,
                                     AD5933_CONTROL_FUNCTION(AD5933_FUNCTION_INIT_START_FREQ) |
                                     AD5933_CONTROL_RANGE(dev->current_range) |
                                     AD5933_CONTROL_PGA_GAIN(dev->current_gain),
                                     1U);
    if (status != HAL_OK)
    {
        return status;
    }

    /* Start sweep */
    return AD5933_SetRegisterValue(dev,
                                   AD5933_REG_CONTROL_HB,
                                   AD5933_CONTROL_FUNCTION(AD5933_FUNCTION_START_SWEEP) |
                                   AD5933_CONTROL_RANGE(dev->current_range) |
                                   AD5933_CONTROL_PGA_GAIN(dev->current_gain),
                                   1U);
}

HAL_StatusTypeDef AD5933_IncrementFrequency(AD5933_HandleTypeDef *dev)
{
    if ((dev == NULL) || (dev->i2c == NULL))
    {
        return HAL_ERROR;
    }

    return AD5933_SetRegisterValue(dev,
                                   AD5933_REG_CONTROL_HB,
                                   AD5933_CONTROL_FUNCTION(AD5933_FUNCTION_INC_FREQ) |
                                   AD5933_CONTROL_RANGE(dev->current_range) |
                                   AD5933_CONTROL_PGA_GAIN(dev->current_gain),
                                   1U);
}

HAL_StatusTypeDef AD5933_IsDataReady(AD5933_HandleTypeDef *dev,
                                     uint8_t *data_ready)
{
    uint32_t status_register;
    HAL_StatusTypeDef status;

    if ((dev == NULL) ||
        (dev->i2c == NULL) ||
        (data_ready == NULL))
    {
        return HAL_ERROR;
    }

    status = AD5933_GetRegisterValue(dev,
                                     AD5933_REG_STATUS,
                                     1U,
                                     &status_register);
    if (status != HAL_OK)
    {
        return status;
    }

    *data_ready = ((status_register & AD5933_STAT_DATA_VALID) != 0U) ? 1U : 0U;

    return HAL_OK;
}

HAL_StatusTypeDef AD5933_IsSweepDone(AD5933_HandleTypeDef *dev,
                                     uint8_t *sweep_done)
{
    uint32_t status_register;
    HAL_StatusTypeDef status;

    if ((dev == NULL) ||
        (dev->i2c == NULL) ||
        (sweep_done == NULL))
    {
        return HAL_ERROR;
    }

    status = AD5933_GetRegisterValue(dev,
                                     AD5933_REG_STATUS,
                                     1U,
                                     &status_register);
    if (status != HAL_OK)
    {
        return status;
    }

    *sweep_done = ((status_register & AD5933_STAT_SWEEP_DONE) != 0U) ? 1U : 0U;

    return HAL_OK;
}

/* ========================================================================= */
/* Data Acquisition                                                          */
/* ========================================================================= */

HAL_StatusTypeDef AD5933_GetData(AD5933_HandleTypeDef *dev,
                                 int16_t *real_data,
                                 int16_t *imag_data)
{
    uint32_t tmp_real;
    uint32_t tmp_imag;
    HAL_StatusTypeDef status;

    if ((dev == NULL) ||
        (dev->i2c == NULL) ||
        (real_data == NULL) ||
        (imag_data == NULL))
    {
        return HAL_ERROR;
    }

    /*
     * Read real part:
     *
     * 0x94: Real data MSB
     * 0x95: Real data LSB
     */
    status = AD5933_GetRegisterValue(dev,
                                     AD5933_REG_REAL_DATA,
                                     2U,
                                     &tmp_real);

    if (status != HAL_OK)
    {
        return status;
    }

    /*
     * Read imaginary part:
     *
     * 0x96: Imaginary data MSB
     * 0x97: Imaginary data LSB
     */
    status = AD5933_GetRegisterValue(dev,
                                     AD5933_REG_IMAG_DATA,
                                     2U,
                                     &tmp_imag);

    if (status != HAL_OK)
    {
        return status;
    }

    *real_data = (int16_t)tmp_real;
    *imag_data = (int16_t)tmp_imag;

    return HAL_OK;
}

/* ========================================================================= */
/* Calculations                                                              */
/* ========================================================================= */

double AD5933_CalculateGainFactor(AD5933_HandleTypeDef *dev,
                                  uint32_t calibration_impedance,
                                  uint8_t frequency_function)
{
    int16_t real_data;
    int16_t imag_data;
    double r;
    double i;
    double magnitude;
    double cal;

    /*
     * Currently unused.
     * For proper per-frequency calibration, store gain factors in an array
     * and use frequency_function as the index.
     */
    (void)frequency_function;

    if (AD5933_GetData(dev, &real_data, &imag_data) != HAL_OK)
    {
        return 0.0;
    }

    if (calibration_impedance == 0U)
    {
        return 0.0;
    }

    r = (double)real_data;
    i = (double)imag_data;

    magnitude = sqrt((r * r) + (i * i));

    if (magnitude < 1e-12)
    {
        return 0.0;
    }

    cal = (double)calibration_impedance;

    return 1.0 / (magnitude * cal);
}

double AD5933_CalculateImpedance(AD5933_HandleTypeDef *dev,
                                 double gain_factor,
                                 uint8_t frequency_function)
{
    int16_t real_data;
    int16_t imag_data;
    double r;
    double i;
    double magnitude;

    /*
     * Currently unused.
     * For proper per-frequency impedance calculation, use a gain factor array
     * indexed by frequency_function.
     */
    (void)frequency_function;

    if (AD5933_GetData(dev, &real_data, &imag_data) != HAL_OK)
    {
        return 0.0;
    }

    if (gain_factor <= 0.0)
    {
        return 0.0;
    }

    r = (double)real_data;
    i = (double)imag_data;

    magnitude = sqrt((r * r) + (i * i));

    if (magnitude < 1e-12)
    {
        return 0.0;
    }

    return 1.0 / (magnitude * gain_factor);
}
