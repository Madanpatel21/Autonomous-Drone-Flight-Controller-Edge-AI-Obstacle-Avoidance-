#include "hal_interfaces.h"
#include <string.h>

/* STM32 backend skeleton (DEC-001, HW-001: STM32F765VIT6).
 * HARDWARE-GATED: this file compiles only with arm-none-eabi-gcc + STM32F7 HAL
 * drivers (not present in this environment). Function bodies are structural
 * placeholders showing the intended peripheral mapping (see PCB_DESIGN_RECORD.md).
 * NOTHING here is validated; timing/peripheral behavior requires Phase 08 bring-up.
 * Do not record any of this as hardware evidence. */

#if FC_TARGET_STM32

#include "stm32f7xx_hal.h"

static IWDG_HandleTypeDef g_iwdg;

uint64_t hal_time_us(void)
{
    /* DWT cycle counter at 216 MHz */
    return DWT->CYCCNT / (SystemCoreClock / 1000000u);
}

void hal_sleep_until_us(uint64_t deadline_us)
{
    while (hal_time_us() < deadline_us) { __WFI(); }
}

hal_status_t hal_imu_read(fc_imu_sample_t *out)
{
    /* SPI1 DMA transfer of ICM-42688-P accel/gyro banks; non-blocking (FW-003).
     * Mapping per PCB_DESIGN_RECORD.md pin table. */
    (void)out;
    return HAL_NOT_READY;  /* replaced by driver Phase 10 */
}

hal_status_t hal_actuator_write(const float motor[4])
{
    /* TIM1 CH1..CH4 DMA DShot600 burst transfer (HW-006) */
    (void)motor;
    return HAL_NOT_READY;
}

/* ... remaining interfaces implemented by per-peripheral drivers in Phase 10;
 * each follows the same non-blocking + health-counter pattern as hal/sim. */

hal_status_t hal_wdg_init(uint32_t timeout_ms)
{
    /* IWDG: LSI 32 kHz, prescaler/prer per timeout; feeds from rate_control task */
    g_iwdg.Instance = IWDG;
    g_iwdg.Init.Prescaler = IWDG_PRESCALER_32;
    g_iwdg.Init.Reload = timeout_ms * 32u;   /* ~1 kHz LSI ticks after prescale */
    return (HAL_IWDG_Init(&g_iwdg) == HAL_OK) ? HAL_OK : HAL_ERROR;
}

void hal_wdg_feed(void) { HAL_IWDG_Refresh(&g_iwdg); }

#else
/* POSIX build of this file is empty: sim backend is the active POSIX HAL. */
#endif /* FC_TARGET_STM32 */
