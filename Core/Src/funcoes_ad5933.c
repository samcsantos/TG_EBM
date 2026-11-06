#include "funcoes_ad5933.h"
#include "ad5933_stm32_2.h"

#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_i2c.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"

#include <string.h>
#include <math.h>
#include <stdbool.h>


/* -------------------------------------------------------------------------- */
/* User configuration                                                         */
/* -------------------------------------------------------------------------- */

#define PI                      3.141592653589793

#define LED_GPIO_PORT           GPIOC
#define LED_GPIO_PIN            GPIO_PIN_13

#define BTN_GPIO_PORT           GPIOB
#define BTN_GPIO_PIN            GPIO_PIN_1

#define DEBOUNCE_MS             50
#define MEASURE_TIMEOUT_MS      5000
#define USB_TX_TIMEOUT_MS       1000U

extern I2C_HandleTypeDef hi2c1;

/* -------------------------------------------------------------------------- */
/* Global variables                                                           */
/* -------------------------------------------------------------------------- */

static AD5933_HandleTypeDef ad5933;
static double gain_factor = 0.0;

/* -------------------------------------------------------------------------- */
/* USB timeout                                                                */
/* -------------------------------------------------------------------------- */

void wait_usb_ready(uint32_t timeout_ms)
  {
      extern USBD_HandleTypeDef hUsbDeviceFS;

      uint32_t start = HAL_GetTick();

      while (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED)
      {
          HAL_Delay(5);

          if ((HAL_GetTick() - start) > timeout_ms)
          {
              break;
          }
      }

      /* Pequena folga depois de configurar */
      HAL_Delay(100);
  }

/*
 * expected_value meaning:
 *
 *   TEST_TYPE_CAL_RES: calibration resistance in ohms
 *   TEST_TYPE_RES:     expected resistance in ohms
 *   TEST_TYPE_CAP:     expected capacitance in farads
 */
const TestCase tests[] =
{
    {
        "CAL_1k",
        "Connect a 1.0 kOhm calibration resistor.",
        1000,
        16,
        1000.0,
        TEST_TYPE_CAL_RES
    },

    {
        "R_220",
        "Connect a 220 Ohm resistor.",
        1000,
        16,
        220.0,
        TEST_TYPE_RES
    },

    {
        "R_1k",
        "Connect a 1.0 kOhm resistor.",
        1000,
        16,
        1000.0,
        TEST_TYPE_RES
    },

    {
        "R_10k",
        "Connect a 10 kOhm resistor.",
        1000,
        32,
        10000.0,
        TEST_TYPE_RES
    },

    {
        "C_10nF",
        "Connect a 10 nF non-polar capacitor.",
        16000,
        32,
        10.0e-9,
        TEST_TYPE_CAP
    },

    {
        "C_100nF",
        "Connect a 100 nF non-polar capacitor.",
        1600,
        32,
        100.0e-9,
        TEST_TYPE_CAP
    },

    {
        "C_1uF",
        "Connect a 1 uF non-polar capacitor.",
        160,
        64,
        1.0e-6,
        TEST_TYPE_CAP
    }
};

const uint8_t test_count =
    (uint8_t)(sizeof(tests) / sizeof(tests[0]));

/* -------------------------------------------------------------------------- */
/* Simple UART printing without printf float                                  */
/* -------------------------------------------------------------------------- */

static void uart_write_buf(const uint8_t *buf, uint16_t len)
{
    while (len > 0U)
    {
        uint16_t chunk = (len > 64U) ? 64U : len;
        uint32_t start = HAL_GetTick();

        for (;;)
        {
            uint8_t ret = CDC_Transmit_FS((uint8_t *)buf, chunk);

            if (ret == USBD_OK)
            {
                buf += chunk;
                len -= chunk;
                break;
            }

            if (ret != USBD_BUSY)
            {
                /* Falha diferente de buffer ocupado */
                return;
            }

            if ((HAL_GetTick() - start) > USB_TX_TIMEOUT_MS)
            {
                /* Timeout esperando a USB transmitir */
                return;
            }
        }
    }
}

void uart_puts(const char *s)
{
    uart_write_buf((const uint8_t *)s, (uint16_t)strlen(s));
}

void uart_putc(char c)
{
    uart_write_buf((const uint8_t *)&c, 1);
}

void uart_print_u32(uint32_t v)
{
    char buf[10];
    char tmp[10];
    uint8_t i = 0;
    uint8_t j = 0;

    if (v == 0)
    {
        uart_putc('0');
        return;
    }

    while (v > 0)
    {
        tmp[j++] = (char)('0' + (v % 10));
        v /= 10;
    }

    while (j > 0)
    {
        buf[i++] = tmp[--j];
    }

    uart_write_buf((uint8_t *)buf, i);
}

static void uart_print_fixed(double v, uint8_t decimals)
{
    static const uint32_t p10[7] =
    {
        1UL,
        10UL,
        100UL,
        1000UL,
        10000UL,
        100000UL,
        1000000UL
    };

    uint32_t integer_part;
    uint32_t fractional_part;
    uint32_t scale;
    double fractional;
    uint8_t i;

    if (decimals > 6)
    {
        decimals = 6;
    }

    if (v < 0.0)
    {
        uart_putc('-');
        v = -v;
    }

    integer_part = (uint32_t)v;
    fractional = v - (double)integer_part;

    scale = p10[decimals];
    fractional_part = (uint32_t)((fractional * (double)scale) + 0.5);

    if (fractional_part >= scale)
    {
        integer_part++;
        fractional_part -= scale;
    }

    uart_print_u32(integer_part);

    if (decimals > 0)
    {
        uart_putc('.');

        for (i = decimals; i > 0; i--)
        {
            uint32_t divisor = p10[i - 1];
            uint8_t digit = (uint8_t)((fractional_part / divisor) % 10);
            uart_putc((char)('0' + digit));
        }
    }
}

static void print_capacitance(double capacitance_f)
{
    if (!(capacitance_f > 0.0))
    {
        uart_puts("0.000 F");
        return;
    }

    if (capacitance_f >= 1.0e-6)
    {
        uart_print_fixed(capacitance_f * 1.0e6, 3);
        uart_puts(" uF");
    }
    else if (capacitance_f >= 1.0e-9)
    {
        uart_print_fixed(capacitance_f * 1.0e9, 3);
        uart_puts(" nF");
    }
    else
    {
        uart_print_fixed(capacitance_f * 1.0e12, 3);
        uart_puts(" pF");
    }
}

static void print_error_percent(double measured, double expected)
{
    double error;

    if (!(expected > 0.0))
    {
        return;
    }

    if (!(measured > 0.0))
    {
        return;
    }

    error = ((measured - expected) / expected) * 100.0;

    uart_puts(", error=");
    uart_print_fixed(error, 2);
    uart_puts("%");
}

/* -------------------------------------------------------------------------- */
/* Utility                                                                    */
/* -------------------------------------------------------------------------- */

void tests_reset_calibration(void)
{
    gain_factor = 0.0;
}

static uint8_t is_positive_finite(double x)
{
    /*
     * This rejects:
     *   NaN
     *   negative values
     *   zero
     *   infinity, practically speaking
     */
    return (uint8_t)((x > 0.0) && (x < 1.0e100));
}

/* -------------------------------------------------------------------------- */
/* Button debounce, non-blocking                                              */
/* -------------------------------------------------------------------------- */

uint8_t button_pressed_event(void)
{
    static uint8_t last_raw = GPIO_PIN_SET;
    static uint8_t stable_state = GPIO_PIN_SET;
    static uint32_t last_change_time = 0;

    uint8_t raw = (uint8_t)HAL_GPIO_ReadPin(BTN_GPIO_PORT, BTN_GPIO_PIN);
    uint32_t now = HAL_GetTick();

    if (raw != last_raw)
    {
        last_raw = raw;
        last_change_time = now;
    }

    if ((now - last_change_time) >= DEBOUNCE_MS)
    {
        if (raw != stable_state)
        {
            stable_state = raw;

            /*
             * Button is connected from PA0 to GND.
             * Internal pull-up is enabled.
             * Pressed = GPIO_PIN_RESET.
             */
            if (stable_state == GPIO_PIN_RESET)
            {
                return 1;
            }
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------- */
/* AD5933 helpers                                                             */
/* -------------------------------------------------------------------------- */

static HAL_StatusTypeDef ad5933_is_sweep_done(uint8_t *sweep_done)
{
    uint32_t status_register;
    HAL_StatusTypeDef status;

    if (sweep_done == NULL)
    {
        return HAL_ERROR;
    }

    status = AD5933_GetRegisterValue(&ad5933,
                                     AD5933_REG_STATUS,
                                     1,
                                     &status_register);

    if (status != HAL_OK)
    {
        return status;
    }

    *sweep_done =
        ((status_register & AD5933_STAT_SWEEP_DONE) != 0) ? 1 : 0;

    return HAL_OK;
}

static HAL_StatusTypeDef wait_data_ready(void)
{
    uint32_t start_time;
    uint8_t ready = 0;
    HAL_StatusTypeDef status;

    start_time = HAL_GetTick();

    while (!ready)
    {
        status = AD5933_IsDataReady(&ad5933, &ready);

        if (status != HAL_OK)
        {
            return status;
        }

        if ((HAL_GetTick() - start_time) > MEASURE_TIMEOUT_MS)
        {
            return HAL_TIMEOUT;
        }

        if (!ready)
        {
            HAL_Delay(2);
        }
    }

    return HAL_OK;
}

/* -------------------------------------------------------------------------- */
/* Run one test case                                                          */
/* -------------------------------------------------------------------------- */

HAL_StatusTypeDef run_test(const TestCase *tc)
{
    HAL_StatusTypeDef status;
    uint8_t sweep_done = 0;

    /*
     * Set settling time.
     *
     * More settling cycles usually mean better stability but slower measurement.
     */


    status = AD5933_SetSettlingTime(&ad5933,
                                    AD5933_SETTLING_X1,
                                    tc->settling_cycles);
/*/ ------------- Teste ------------------------------
    for (uint8_t retry = 0; retry < 5; retry++)
    {
        status = AD5933_SetSettlingTime(&ad5933,
                                        AD5933_SETTLING_X1,
                                        tc->settling_cycles);

        if (status == HAL_OK)
        {
            break;
        }

        HAL_Delay(10);
    }
// ------------- Teste ------------------------------*/

    if (status != HAL_OK)
    {
    	uart_puts("ERROR: SetSettlingTime failed\r\n");
		uart_puts("HAL status = ");
		uart_print_u32((uint32_t)status);
		uart_puts(", I2C error = ");
		uart_print_u32(HAL_I2C_GetError(&hi2c1));
		uart_puts("\r\n");

		return status;
    }

    /*
     * Configure a single-frequency sweep.
     *
     * start_frequency = tc->freq_hz
     * increment_frequency = 0
     * increment_number = 0
     *
     * If your AD5933 does not accept increment_number = 0,
     * change the last parameter to 1.
     */
    status = AD5933_ConfigSweep(&ad5933,
                                tc->freq_hz,
                                0,
                                0);

    if (status != HAL_OK)
    {
        uart_puts("ERROR: ConfigSweep failed\r\n");
        return status;
    }

    /*
     * Start sweep.
     *
     * Your driver internally does:
     *   standby -> reset -> init start frequency -> start sweep
     */
    status = AD5933_StartSweep(&ad5933);

    if (status != HAL_OK)
    {
        uart_puts("ERROR: StartSweep failed\r\n");
        return status;
    }

    /*
     * Wait until AD5933 says data is valid.
     */
    status = wait_data_ready();

    if (status != HAL_OK)
    {
        uart_puts("ERROR: timeout waiting for DATA_VALID\r\n");
        return status;
    }

    /*
     * Calibration case:
     *
     * A known resistor is connected.
     * The driver calculates:
     *
     *   gain_factor = 1 / (magnitude * Rcal)
     */
    if (tc->type == TEST_TYPE_CAL_RES)
    {
        double g;

        g = AD5933_CalculateGainFactor(&ad5933,
                                       (uint32_t)tc->expected_value,
                                       0);

        if (!is_positive_finite(g))
        {
            uart_puts("ERROR: invalid gain factor\r\n");
            return HAL_ERROR;
        }

        gain_factor = g;

        uart_puts("Calibration OK. f=");
        uart_print_u32(tc->freq_hz);
        uart_puts(" Hz, Rcal=");
        uart_print_fixed(tc->expected_value, 1);
        uart_puts(" Ohm, gain=");
        uart_print_fixed(gain_factor, 12);
        uart_puts("\r\n");
    }
    else
    {
        double z_magnitude;

        if (!is_positive_finite(gain_factor))
        {
            uart_puts("ERROR: no valid calibration. Run CAL_1k first.\r\n");
            return HAL_ERROR;
        }

        /*
         * Unknown impedance magnitude:
         *
         *   |Z| = 1 / (gain_factor * magnitude)
         */
        z_magnitude = AD5933_CalculateImpedance(&ad5933,
                                                gain_factor,
                                                0);

        if (!is_positive_finite(z_magnitude))
        {
            uart_puts("ERROR: invalid impedance result\r\n");
            return HAL_ERROR;
        }

        uart_puts("f=");
        uart_print_u32(tc->freq_hz);
        uart_puts(" Hz, |Z|=");
        uart_print_fixed(z_magnitude, 2);
        uart_puts(" Ohm");

        if (tc->type == TEST_TYPE_RES)
        {
            uart_puts(", R_meas=");
            uart_print_fixed(z_magnitude, 2);
            uart_puts(" Ohm");

            print_error_percent(z_magnitude, tc->expected_value);
        }
        else if (tc->type == TEST_TYPE_CAP)
        {
            /*
             * For an ideal capacitor:
             *
             *   |Z| = 1 / (2 * pi * f * C)
             *
             * Therefore:
             *
             *   C = 1 / (2 * pi * f * |Z|)
             */
            double capacitance_f;

            capacitance_f =
                1.0 /
                (2.0 * PI * (double)tc->freq_hz * z_magnitude);

            uart_puts(", C_meas=");
            print_capacitance(capacitance_f);

            print_error_percent(capacitance_f, tc->expected_value);
        }

        uart_puts("\r\n");
    }

    /*
     * Optional status check.
     */
    if (ad5933_is_sweep_done(&sweep_done) == HAL_OK)
    {
        uart_puts("SWEEP_DONE=");
        uart_print_u32(sweep_done);
        uart_puts("\r\n");
    }

    return HAL_OK;
}

/* -------------------------------------------------------------------------- */
/* AD5933 initialization and basic communication check                        */
/* -------------------------------------------------------------------------- */

void ad5933_hw_init(void)
{
    uint32_t status_register = 0;
    HAL_StatusTypeDef status;

    uart_puts("\r\n========================================\r\n");
    uart_puts(" AD5933 STM32F103C8T6 Test\r\n");
    uart_puts("========================================\r\n");

    AD5933_Init(&ad5933, &hi2c1);

    /*
     * Check whether the AD5933 acknowledges its I2C address.
     *
     * AD5933_ADDRESS = 0x0D 7-bit
     * AD5933_I2C_ADDR = 0x1A 8-bit
     */
    status = HAL_I2C_IsDeviceReady(&hi2c1,
                                   AD5933_I2C_ADDR,
                                   3,
                                   100);

    if (status != HAL_OK)
    {
        uart_puts("FATAL: AD5933 not detected on I2C.\r\n");
        uart_puts("Check: PB6/PB7, pull-ups, address, power, GND.\r\n");

        while (1)
        {
            HAL_GPIO_TogglePin(LED_GPIO_PORT, LED_GPIO_PIN);
            HAL_Delay(100);
        }
    }

    uart_puts("AD5933 detected on I2C.\r\n");

    /*
     * Reset AD5933.
     */
    status = AD5933_Reset(&ad5933);
    HAL_Delay(20);

    if (status != HAL_OK)
    {
        uart_puts("WARNING: AD5933_Reset failed.\r\n");
    }

    /*
     * Use internal 16 MHz system clock.
     */
    status = AD5933_SetSystemClock(&ad5933,
                                   AD5933_CONTROL_INT_SYSCLK,
                                   0);

    if (status != HAL_OK)
    {
        uart_puts("ERROR: AD5933_SetSystemClock failed.\r\n");
    }

    /*
     * Select output range and PGA gain.
     *
     * For first tests, start with:
     *   2000 mVpp range
     *   x5 gain
     *
     * If measurements saturate or look wrong, try:
     *   AD5933_RANGE_200mVpp
     *   AD5933_GAIN_X1
     */
    status = AD5933_SetRangeAndGain(&ad5933,
                                    AD5933_RANGE_2000mVpp,
                                    AD5933_GAIN_X5);

    if (status != HAL_OK)
    {
        uart_puts("ERROR: AD5933_SetRangeAndGain failed.\r\n");
    }

    /*
     * Read status register once to verify register reading works.
     */
    status = AD5933_GetRegisterValue(&ad5933,
                                     AD5933_REG_STATUS,
                                     1,
                                     &status_register);

    if (status == HAL_OK)
    {
        uart_puts("AD5933 STATUS register = 0x");
        uart_print_u32(status_register);
        uart_puts("\r\n");
    }
    else
    {
        uart_puts("WARNING: could not read AD5933 STATUS register.\r\n");
    }

    uart_puts("AD5933 initialized.\r\n");
}
