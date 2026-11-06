#ifndef AD5933_STM32_H
#define AD5933_STM32_H

#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_i2c.h"
#include <stdint.h>

/* ========================================================================= */
/* AD5933 Registers                                                          */
/* ========================================================================= */

#define AD5933_REG_CONTROL_HB       0x80U
#define AD5933_REG_CONTROL_LB       0x81U

#define AD5933_REG_FREQ_START       0x82U
#define AD5933_REG_FREQ_INC         0x85U
#define AD5933_REG_INC_NUM          0x88U

#define AD5933_REG_SETTLING_CYCLES  0x8AU

#define AD5933_REG_STATUS           0x8FU

#define AD5933_REG_REAL_DATA        0x94U
#define AD5933_REG_IMAG_DATA        0x96U

/* ========================================================================= */
/* AD5933 Control High Byte Fields                                           */
/* ========================================================================= */

#define AD5933_CONTROL_FUNCTION(x)  ((((uint32_t)(x)) << 4) & 0xF0U)
#define AD5933_CONTROL_RANGE(x)     ((((uint32_t)(x)) << 1) & 0x06U)
#define AD5933_CONTROL_PGA_GAIN(x)  (((uint32_t)(x)) & 0x01U)

/* ========================================================================= */
/* AD5933 Control Function Options                                           */
/* ========================================================================= */

#define AD5933_FUNCTION_NOP                 0x0U
#define AD5933_FUNCTION_INIT_START_FREQ     0x1U
#define AD5933_FUNCTION_START_SWEEP         0x2U
#define AD5933_FUNCTION_INC_FREQ            0x3U
#define AD5933_FUNCTION_REPEAT_FREQ         0x4U
#define AD5933_FUNCTION_POWER_DOWN          0xAU
#define AD5933_FUNCTION_STANDBY             0xBU

/* ========================================================================= */
/* AD5933 Control Low Byte Fields                                            */
/* ========================================================================= */

#define AD5933_CONTROL_RESET          (0x01U << 7)

#define AD5933_CONTROL_INT_SYSCLK     (0x00U << 3)
#define AD5933_CONTROL_EXT_SYSCLK     (0x01U << 3)

/* ========================================================================= */
/* AD5933 Output Voltage Ranges                                              */
/* ========================================================================= */

#define AD5933_RANGE_2000mVpp       0x0U
#define AD5933_RANGE_200mVpp        0x1U
#define AD5933_RANGE_400mVpp        0x2U
#define AD5933_RANGE_1000mVpp       0x3U

/* ========================================================================= */
/* AD5933 PGA Gain                                                           */
/* ========================================================================= */

#define AD5933_GAIN_X5              0x0U
#define AD5933_GAIN_X1              0x1U

/* ========================================================================= */
/* AD5933 Status Bits                                                        */
/* ========================================================================= */

#define AD5933_STAT_DATA_VALID      (0x01U << 1)
#define AD5933_STAT_SWEEP_DONE      (0x01U << 2)

/* ========================================================================= */
/* AD5933 Settling Time                                                      */
/* ========================================================================= */

#define AD5933_SETTLING_X1          0x0U
#define AD5933_SETTLING_X2          0x1U
#define AD5933_SETTLING_X4          0x3U

#define AD5933_SETTLING_CYCLES_MASK   0x01FFU
#define AD5933_SETTLING_MULT_SHIFT    9U
#define AD5933_MAX_SETTLING_CYCLES    511U

/* ========================================================================= */
/* AD5933 I2C                                                                */
/* ========================================================================= */

#define AD5933_ADDRESS              0x0DU
#define AD5933_I2C_ADDR             (AD5933_ADDRESS << 1)

/*
 * Kept for compatibility with your original code.
 * The fixed driver below uses direct register access with
 * HAL_I2C_Mem_Write() and HAL_I2C_Mem_Read().
 */
#define AD5933_ADDR_POINTER         0xB0U

/* ========================================================================= */
/* AD5933 Specifications                                                     */
/* ========================================================================= */

#define AD5933_INTERNAL_SYS_CLK     16000000UL
#define AD5933_MAX_INC_NUM          511U

/*
 * AD5933 frequency registers are 24-bit.
 */
#define AD5933_FREQ_CODE_MASK       0x00FFFFFFUL

/* ========================================================================= */
/* Handle                                                                    */
/* ========================================================================= */

typedef struct
{
    I2C_HandleTypeDef *i2c;

    uint32_t current_sys_clk;
    uint8_t  current_clock_source;
    uint8_t  current_gain;
    uint8_t  current_range;

} AD5933_HandleTypeDef;

/* ========================================================================= */
/* Initialization                                                            */
/* ========================================================================= */

void AD5933_Init(AD5933_HandleTypeDef *dev,
                 I2C_HandleTypeDef *hi2c);

/* ========================================================================= */
/* Register Access                                                           */
/* ========================================================================= */

HAL_StatusTypeDef AD5933_SetRegisterValue(AD5933_HandleTypeDef *dev,
                                          uint8_t register_address,
                                          uint32_t register_value,
                                          uint8_t bytes_number);

HAL_StatusTypeDef AD5933_GetRegisterValue(AD5933_HandleTypeDef *dev,
                                          uint8_t register_address,
                                          uint8_t bytes_number,
                                          uint32_t *register_value);

/* ========================================================================= */
/* Device Configuration                                                      */
/* ========================================================================= */

HAL_StatusTypeDef AD5933_Reset(AD5933_HandleTypeDef *dev);

HAL_StatusTypeDef AD5933_SetSystemClock(AD5933_HandleTypeDef *dev,
                                        uint8_t clock_source,
                                        uint32_t external_clock_frequency);

HAL_StatusTypeDef AD5933_SetRangeAndGain(AD5933_HandleTypeDef *dev,
                                         uint8_t range,
                                         uint8_t gain);

HAL_StatusTypeDef AD5933_SetSettlingTime(AD5933_HandleTypeDef *dev,
                                         uint8_t multiplier,
                                         uint16_t number_cycles);

/* ========================================================================= */
/* Frequency Sweep Configuration                                             */
/* ========================================================================= */

HAL_StatusTypeDef AD5933_ConfigSweep(AD5933_HandleTypeDef *dev,
                                     uint32_t start_frequency,
                                     uint32_t increment_frequency,
                                     uint16_t increment_number);

/* ========================================================================= */
/* Sweep Control                                                             */
/* ========================================================================= */

HAL_StatusTypeDef AD5933_StartSweep(AD5933_HandleTypeDef *dev);

HAL_StatusTypeDef AD5933_IncrementFrequency(AD5933_HandleTypeDef *dev);

HAL_StatusTypeDef AD5933_IsDataReady(AD5933_HandleTypeDef *dev,
                                     uint8_t *data_ready);

HAL_StatusTypeDef AD5933_IsSweepDone(AD5933_HandleTypeDef *dev,
                                     uint8_t *sweep_done);

/* ========================================================================= */
/* Data Acquisition                                                          */
/* ========================================================================= */

HAL_StatusTypeDef AD5933_GetData(AD5933_HandleTypeDef *dev,
                                 int16_t *real_data,
                                 int16_t *imag_data);

/* ========================================================================= */
/* Calculations                                                              */
/* ========================================================================= */

double AD5933_CalculateGainFactor(AD5933_HandleTypeDef *dev,
                                  uint32_t calibration_impedance,
                                  uint8_t frequency_function);

double AD5933_CalculateImpedance(AD5933_HandleTypeDef *dev,
                                 double gain_factor,
                                 uint8_t frequency_function);

#endif /* AD5933_STM32_H */
