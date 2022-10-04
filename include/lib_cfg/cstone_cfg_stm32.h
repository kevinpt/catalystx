#ifndef CSTONE_CFG_STM32_H
#define CSTONE_CFG_STM32_H

#define PERF_TIMER                  TIM2
#define PERF_TIMER_CLK_ENABLE       __HAL_RCC_TIM2_CLK_ENABLE
#ifdef PLATFORM_STM32F1
#  define PERF_TIMER_HI             TIM1
#  define PERF_TIMER_HI_CLK_ENABLE  __HAL_RCC_TIM1_CLK_ENABLE
#endif
// See FreeRTOSConfig.h for def of PERF_CLOCK_HZ


// Timer for soft RTC
#define RTC_TIMER                   TIM3
#define RTC_TIMER_CLK_ENABLE        __HAL_RCC_TIM3_CLK_ENABLE
#define RTC_TIMER_IRQ               TIM3_IRQn

#endif // CSTONE_CFG_STM32_H
