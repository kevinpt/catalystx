#ifndef CSTONE_CFG_STM32_H
#define CSTONE_CFG_STM32_H

#define PERF_TIMER                  TIM2
#define PERF_TIMER_CLK_ENABLE       __HAL_RCC_TIM2_CLK_ENABLE
#ifdef PLATFORM_STM32F1
#  define PERF_TIMER_HI             TIM1
#  define PERF_TIMER_HI_CLK_ENABLE  __HAL_RCC_TIM1_CLK_ENABLE
#endif
// See FreeRTOSConfig.h for def of PERF_CLOCK_HZ

#endif // CSTONE_CFG_STM32_H
