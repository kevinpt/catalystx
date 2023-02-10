#ifndef STM32_IT_H
#define STM32_IT_H

#ifdef __cplusplus
extern "C" {
#endif

extern bool g_enable_rtos_sys_tick;


// ISR prototypes here to force C linkage
void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void DebugMon_Handler(void);

void SysTick_Handler(void);


// Peripheral interrupt handlers
// ISR defs in STM32CubeF4/Drivers/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/startup_stm32f4xx.s
// IRQ defs in STM32CubeF4/Drivers/CMSIS/Device/ST/STM32F4xx/Include/stm32f429xx.h

//void LTDC_IRQHandler(void);
//void OTG_HS_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif // STM32_IT_H
