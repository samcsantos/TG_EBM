#ifndef FUNCOES_AD5933_H
#define FUNCOES_AD5933_H

#include <stdint.h>
#include "stm32f1xx_hal.h"


/* -------------------------------------------------------------------------- */
/* AD5933 test states                                                         */
/* -------------------------------------------------------------------------- */

typedef enum
{
    TEST_TYPE_CAL_RES,
    TEST_TYPE_RES,
    TEST_TYPE_CAP
} TestType;

typedef struct
{
    const char *name;
    const char *prompt;
    uint32_t freq_hz;
    uint16_t settling_cycles;
    double expected_value;
    TestType type;
} TestCase;

/*
 * expected_value meaning:
 *
 *   TEST_TYPE_CAL_RES: calibration resistance in ohms
 *   TEST_TYPE_RES:     expected resistance in ohms
 *   TEST_TYPE_CAP:     expected capacitance in farads
 */

extern const TestCase tests[];
extern const uint8_t test_count;

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

void wait_usb_ready(uint32_t timeout_ms);

void ad5933_hw_init(void);

HAL_StatusTypeDef run_test(const TestCase *tc);

uint8_t button_pressed_event(void);

void uart_puts(const char *s);
void uart_print_u32(uint32_t v);

void tests_reset_calibration(void);

#endif /* FUNCOES_AD5933_H */
