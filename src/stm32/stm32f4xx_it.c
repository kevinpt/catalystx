#include <stdbool.h>
#include <time.h>

#include "lib_cfg/build_config.h"
#include "lib_cfg/cstone_cfg_stm32.h"
#include "app_main.h"

#include "stm32f4xx_hal.h"
#include "stm32f4xx_ll_tim.h"
#include "stm32f4xx_ll_usart.h"
//#include "stm32f429i_discovery.h"
#ifdef USE_AUDIO_DAC
#  include "stm32f4xx_ll_dac.h"
#endif
#include "stm32f4xx_ll_dma.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"


#ifdef USE_TINYUSB
#  include "device/usbd.h"
#endif


#include "cstone/console.h"
#include "cstone/io/uart.h"
#include "cstone/io/gpio.h"
#include "cstone/faults.h"
#include "cstone/rtc_device.h"
#include "cstone/rtc_soft.h"
#include "cstone/iqueue_int16_t.h"
#ifdef USE_AUDIO
#  include "audio_synth.h"
#  include "sample_device.h"
#  ifdef USE_AUDIO_DAC
#    include "sample_device_dac.h"
#  endif
#endif

#include "stm32/stm32f4xx_it.h"


// https://maskray.me/blog/2021-11-07-init-ctors-init-array

// Preinit code that will be executed before static constructors
static void system_preinit(void) {
  // Configure NVIC
  HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4); // 4-bits priority, no subpriority


  SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;    // Usage fault on div by 0

  // Enable configurable fault handlers
  HAL_NVIC_SetPriority(UsageFault_IRQn, 0, 0);
  HAL_NVIC_SetPriority(BusFault_IRQn, 0, 0);
  HAL_NVIC_SetPriority(MemoryManagement_IRQn, 0, 0);

  SCB->SHCSR |= SCB_SHCSR_USGFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_MEMFAULTENA_Msk;

#if 0 && !defined NDEBUG
  // Disable write buffering to locate imprecise fault (will become precise)
  // ONLY use this for debugging. Do not keep it disabled for release.
  SCnSCB->ACTLR |= SCnSCB_ACTLR_DISDEFWBUF_Msk;
#endif
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
__attribute__((section(".preinit_array")))
static void (*preinit_funcs[])(void) = { &system_preinit };
#pragma GCC diagnostic pop


// ******************** Exception handlers ********************

void NMI_Handler(void) { // Avoid default handler loop
}



// https://siliconlabs.force.com/community/s/article/how-to-read-the-link-register-lr-for-an-arm-cortex-m-series-device?language=en_US
__attribute__(( always_inline ))
static inline uint32_t get_LR(void) {
  register uint32_t lr;
  asm volatile ("MOV %0, LR\n" : "=r" (lr));
  return lr;
}

// Examine EXC_RETURN in LR to determine which stack pointer was in use.
// This must be invoked directly within an exception handler.
// REF: B1.5.8 Exception return behavior, ARM v7-M Architecture Reference Manual
#define EXCEPTION_STACK()  ((get_LR() & (1ul << 2)) ? __get_PSP() : __get_MSP())

// Common body for exception handlers
// NOTE: sp must end up in a register or the stack pointer could be altered by the function
//       entry code.
#define EXCEPTION_HANDLER(origin) do { \
  register uint32_t sp = EXCEPTION_STACK(); \
  fault_record_init(&g_fault_record, (origin), (CMExceptionFrame *)sp); \
  if(IS_DEBUGGER_ATTACHED()) { \
    __BKPT(0); \
  } else { \
    fatal_error(); \
  } \
  } while(0)


void HardFault_Handler(void) {
  EXCEPTION_HANDLER(FAULT_HARD);
}

void MemManage_Handler(void) {
  EXCEPTION_HANDLER(FAULT_MEM);
}

void BusFault_Handler(void) {
  EXCEPTION_HANDLER(FAULT_BUS);
}

void UsageFault_Handler(void) {
  EXCEPTION_HANDLER(FAULT_USAGE);
}

void DebugMon_Handler(void) {
}


// ******************** OS support handlers ********************

// We run our own SysTick handler so that xPortSysTickHandler()
// isn't called before the scheduler is running. This lets us
// use HAL_Delay() in init code.

bool g_enable_rtos_sys_tick = false;

extern void xPortSysTickHandler(void);

// Handler for bootup init
void SysTick_Handler(void) {
  if(g_enable_rtos_sys_tick)
    xPortSysTickHandler();
  HAL_IncTick();
#ifdef USE_LVGL
  lv_tick_inc(1);
#endif
}



// ******************** Peripheral interrupt handlers ********************
// See startup_stm32f4xx.s for handler names

//extern LTDC_HandleTypeDef LtdcHandler;

//void LTDC_IRQHandler(void) {
//  HAL_LTDC_IRQHandler(&LtdcHandler);
//}


static void process_uart_console_irq(int id) {
  bool tx_empty = false;
  uint8_t ch;
  Console *con = console_find((ConsoleID){.kind=CON_UART, .id=id});
  USART_TypeDef *uart_dev = uart_get_device(id);

  // Using LL to handle TX/RX since HAL is too poorly implemented

  // TX
  if(LL_USART_IsActiveFlag_TXE(uart_dev) && LL_USART_IsEnabledIT_TXE(uart_dev)) {
    if(con && isr_queue_pop_one(con->stream.tx_queue, &ch) > 0) {
      LL_USART_TransmitData8(uart_dev, ch);
    } else { // Last byte shifting out now; No more TXE events
      LL_USART_DisableIT_TXE(uart_dev);
      LL_USART_EnableIT_TC(uart_dev);

      tx_empty = true;
    }

  } else if(LL_USART_IsActiveFlag_TC(uart_dev) && LL_USART_IsEnabledIT_TC(uart_dev)) {
    LL_USART_DisableIT_TXE(uart_dev);
    LL_USART_DisableIT_TC(uart_dev);
  }

  // RX
  if(LL_USART_IsActiveFlag_RXNE(uart_dev) && LL_USART_IsEnabledIT_RXNE(uart_dev)) {
    ch = LL_USART_ReceiveData8(uart_dev); // Clears RXNE flag
    if(con)
      console_rx_enqueue(con, &ch, 1);
  }

  // When FreeRTOS is active we need to signal blocking print functions when they can resume
  static BaseType_t high_prio_task;
  if(tx_empty && xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
    if(con) {
      xSemaphoreGiveFromISR(con->stream.tx_empty, &high_prio_task);
      portYIELD_FROM_ISR(high_prio_task);
    }
  }
}


// Satisfy -Wmissing-prototypes
void USART_IRQ_HANDLER(CONSOLE_UART_ID);

void USART_IRQ_HANDLER(CONSOLE_UART_ID) {
  process_uart_console_irq(CONSOLE_UART_ID);
}

#ifdef USE_TINYUSB
void OTG_HS_IRQHandler(void);
void OTG_HS_IRQHandler(void) {
  tud_int_handler(BOARD_DEVICE_RHPORT_NUM);
}
#endif

#if USE_AUDIO
//HAL_I2S_IRQHandler()
//HAL_I2SEx_FullDuplex_IRQHandler()

/*extern I2S_HandleTypeDef g_i2s;
void SPI2_IRQHandler(void);
void SPI2_IRQHandler(void) {
  HAL_I2S_IRQHandler(&g_i2s);
}*/

#  ifdef USE_AUDIO_I2S
#    ifdef USE_HAL_I2S
extern DMA_HandleTypeDef g_dma;

void DMA1_Stream4_IRQHandler(void);
void DMA1_Stream4_IRQHandler(void) {
  HAL_DMA_IRQHandler(&g_dma);
}
#    else // DMA

#    endif
#  endif

#  ifdef USE_AUDIO_DAC

void TIM6_DAC_IRQHandler(void);
void TIM6_DAC_IRQHandler(void) {
#if 0
  if(LL_TIM_IsActiveFlag_UPDATE(DAC_TIMER) && LL_TIM_IsEnabledIT_UPDATE(DAC_TIMER)) {
    LL_TIM_ClearFlag_UPDATE(DAC_TIMER);
  }
#endif

  if(LL_DAC_IsActiveFlag_DMAUDR1(DAC1)) {  // Handle underrun
    LL_DAC_ClearFlag_DMAUDR1(DAC1);
  }
}



extern SynthState g_audio_synth;
extern SampleDevice *g_dev_audio;
extern TaskHandle_t g_audio_synth_task;

void DMA1_Stream5_IRQHandler(void);
void DMA1_Stream5_IRQHandler(void) {
  BaseType_t high_prio_task;

  if(LL_DMA_IsActiveFlag_TE5(DMA1) == 1) {  // Handle transfer error
    LL_DMA_ClearFlag_TE5(DMA1);

  } else if(LL_DMA_IsActiveFlag_HT5(DMA1)) {
    LL_DMA_ClearFlag_HT5(DMA1);

    // Fill low half of DMA buffer
    g_audio_synth.next_buf = g_dev_audio->cfg.dma_buf_low;
    vTaskNotifyGiveFromISR(g_audio_synth_task, &high_prio_task);
    // FIXME: Need to call portYIELD_FROM_ISR(high_prio_task);

  } else if(LL_DMA_IsActiveFlag_TC5(DMA1)) {
    LL_DMA_ClearFlag_TC5(DMA1);

    // Fill high half of DMA buffer
    g_audio_synth.next_buf = g_dev_audio->cfg.dma_buf_high;
    vTaskNotifyGiveFromISR(g_audio_synth_task, &high_prio_task);
    // FIXME: Need to call portYIELD_FROM_ISR(high_prio_task);

  }

}
#  endif // USE_AUDIO_DAC
#endif // USE_AUDIO


extern RTCDevice g_rtc_soft_device;
void TIM3_IRQHandler(void);
void TIM3_IRQHandler(void) {
  if(LL_TIM_IsActiveFlag_UPDATE(RTC_TIMER) && LL_TIM_IsEnabledIT_UPDATE(RTC_TIMER)) {
    LL_TIM_ClearFlag_UPDATE(RTC_TIMER);
    rtc_soft_update(&g_rtc_soft_device, 1);
//    set_led(LED_STATUS, LED_TOGGLE);
  }
}


