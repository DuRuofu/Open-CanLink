/*
 * ch32v20x_it.c - interrupt service routines for open-can-link CH32 bridge
 */

#include "ch32v20x_it.h"
#include "ch32v20x.h"

/* External system tick counter (incremented every 1ms from TIM2 ISR) */
extern volatile uint32_t g_sys_tick_ms;

void NMI_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void HardFault_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void NMI_Handler(void)
{
    while (1) { }
}

void HardFault_Handler(void)
{
    NVIC_SystemReset();
    while (1) { }
}

/*
 * TIM2_IRQHandler — 100μs tick
 *   Each interrupt: accumulate 100μs → 1ms → increment g_sys_tick_ms
 */
void TIM2_IRQHandler(void)
{
    static uint16_t us_accum = 0;

    us_accum += 100;  /* 100μs per tick */
    if (us_accum >= 1000) {
        us_accum -= 1000;
        g_sys_tick_ms++;
    }

    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
}
