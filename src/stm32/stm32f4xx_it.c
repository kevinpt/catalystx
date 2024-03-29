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

#include "stm32f4xx_ll_tim.h"
#include "stm32f4xx_ll_exti.h"


#include "FreeRTOS.h"
#include "semphr.h"
#include "queue.h"
#include "task.h"


#if USE_TINYUSB
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
#  include "sample_device.h"
#  include "audio_synth.h"
#  ifdef USE_AUDIO_DAC
#    include "sample_device_dac.h"
#  endif
#endif
#include "buzzer.h"

#if USE_LVGL
#  include "lvgl/lvgl.h"
#endif

#include "stm32/stm32_it.h"


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
#if USE_LVGL
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

#if USE_TINYUSB
void OTG_HS_IRQHandler(void);
void OTG_HS_IRQHandler(void) {
  tud_int_handler(BOARD_DEVICE_RHPORT_NUM);
}
#endif

#if USE_AUDIO
extern SynthState g_audio_synth;
extern SampleDevice *g_dev_audio;
extern TaskHandle_t g_audio_synth_task;


#  ifdef USE_AUDIO_I2S
// I2S DMA interrupt
void DMA1_Stream4_IRQHandler(void);
void DMA1_Stream4_IRQHandler(void) {
  BaseType_t high_prio_task;

  if(LL_DMA_IsActiveFlag_TE4(DMA1) == 1) {  // Handle transfer error
    LL_DMA_ClearFlag_TE4(DMA1);

  } else if(LL_DMA_IsActiveFlag_HT4(DMA1)) {
    LL_DMA_ClearFlag_HT4(DMA1);

    // Fill low half of DMA buffer
    g_audio_synth.next_buf = g_dev_audio->cfg.dma_buf_low;
    vTaskNotifyGiveFromISR(g_audio_synth_task, &high_prio_task);
    portYIELD_FROM_ISR(high_prio_task);

  } else if(LL_DMA_IsActiveFlag_TC4(DMA1)) {
    LL_DMA_ClearFlag_TC4(DMA1);

    // Fill high half of DMA buffer
    g_audio_synth.next_buf = g_dev_audio->cfg.dma_buf_high;
    vTaskNotifyGiveFromISR(g_audio_synth_task, &high_prio_task);
    portYIELD_FROM_ISR(high_prio_task);
  }
}
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



// DAC DMA interrupt
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
    portYIELD_FROM_ISR(high_prio_task);

  } else if(LL_DMA_IsActiveFlag_TC5(DMA1)) {
    LL_DMA_ClearFlag_TC5(DMA1);

    // Fill high half of DMA buffer
    g_audio_synth.next_buf = g_dev_audio->cfg.dma_buf_high;
    vTaskNotifyGiveFromISR(g_audio_synth_task, &high_prio_task);
    portYIELD_FROM_ISR(high_prio_task);
  }

}
#  endif // USE_AUDIO_DAC
#endif // USE_AUDIO

#if 1
extern SemaphoreHandle_t g_crc_dma_complete;

// CRC DMA interrupt
void DMA1_Stream7_IRQHandler(void);
void DMA1_Stream7_IRQHandler(void) {
  BaseType_t high_prio_task;

  if(LL_DMA_IsActiveFlag_TC7(DMA1)) {
    LL_DMA_ClearFlag_TC7(DMA1);

    // Process next block

    // All blocks done, signal calling task CRC is ready
    //vTaskNotifyGiveFromISR(g_audio_synth_task, &high_prio_task);
    xSemaphoreGiveFromISR(g_crc_dma_complete, &high_prio_task);
    portYIELD_FROM_ISR(high_prio_task);
  }
}
#endif  // USE_CRC_DMA

extern RTCDevice g_rtc_soft_device;
void TIM3_IRQHandler(void);
void TIM3_IRQHandler(void) {
  if(LL_TIM_IsActiveFlag_UPDATE(RTC_TIMER) && LL_TIM_IsEnabledIT_UPDATE(RTC_TIMER)) {
    LL_TIM_ClearFlag_UPDATE(RTC_TIMER);
    rtc_soft_update(&g_rtc_soft_device, 1);
//    set_led(LED_STATUS, LED_TOGGLE);
  }
}


#if USE_AUDIO
void EXTI15_10_IRQHandler(void);
void EXTI15_10_IRQHandler(void) {
  BaseType_t high_prio_task;

  LL_EXTI_DisableIT_0_31(LL_EXTI_LINE_12); // Don't want continuous ints. for 4kHz buzz
  LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_12);

  // For multiple beeps we need wait 65ms for next dead space between beeps before reenabling
  // the pin EXTI. For the last beep we timeout after 125ms to confirm the end.
  LL_TIM_SetCounter(BUZZ_TIMER, 0);
  LL_TIM_EnableCounter(BUZZ_TIMER); // Start 125ms count

  uint8_t cmd = BUZZ_CMD_PIN_CHANGE;
  xQueueSendFromISR(g_buzzer_cmd_q, &cmd, &high_prio_task);
  portYIELD_FROM_ISR(high_prio_task);
}


void TIM4_IRQHandler(void);
void TIM4_IRQHandler(void) {
  BaseType_t high_prio_task;

  if(LL_TIM_IsActiveFlag_UPDATE(BUZZ_TIMER)) { // 125ms elapsed
    LL_TIM_ClearFlag_UPDATE(BUZZ_TIMER);

    LL_TIM_DisableCounter(BUZZ_TIMER); // Wait for next rising edge to resume counting

    uint8_t cmd = BUZZ_CMD_125MS_TIMEOUT;
    xQueueSendFromISR(g_buzzer_cmd_q, &cmd, &high_prio_task);
    portYIELD_FROM_ISR(high_prio_task);

  } else if(LL_TIM_IsActiveFlag_CC1(BUZZ_TIMER)) { // 65ms elapsed
    LL_TIM_ClearFlag_CC1(BUZZ_TIMER);

    LL_EXTI_EnableIT_0_31(LL_EXTI_LINE_12);

    uint8_t cmd = BUZZ_CMD_65MS_TIMEOUT;
    xQueueSendFromISR(g_buzzer_cmd_q, &cmd, &high_prio_task);
    portYIELD_FROM_ISR(high_prio_task);
  }
}
#endif // USE_AUDIO


#if USE_I2C
// NOTE: IRQ attribute needed to force 8-byte alignment of SP when handling 64-bit objects
//       on ARM architectures before ARMv7 (See IHI0046B_ABI_Advisory_1.pdf)
#define INTERRUPT  __attribute__((interrupt("IRQ")))

void I2C1_EV_IRQHandler(void);

//__attribute__((interrupt("IRQ")))
void I2C1_EV_IRQHandler(void) {
  i2c_event_isr(&g_i2c1);
}

void I2C1_ER_IRQHandler(void);

//__attribute__((interrupt("IRQ")))
void I2C1_ER_IRQHandler(void) {
  i2c_error_isr(&g_i2c1);
}
#endif
