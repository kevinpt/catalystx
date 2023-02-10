#include <stdbool.h>

#include "lib_cfg/build_config.h"

#include "stm32f1xx_hal.h"
#include "stm32f1xx_ll_usart.h"
//#include "stm32f429i_discovery.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"


#if USE_TINYUSB
#  include "device/usbd.h"
#endif


#include "app_main.h"
#include "cstone/console.h"
#include "cstone/io/uart_stm32.h"
#include "cstone/faults.h"

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


static inline void process_uart_console_irq(int id) {
  bool tx_empty = false;
  uint8_t ch;
  // FIXME: Make con static ?
  Console *con = console_find((ConsoleID){.kind=CON_UART, .id=id});
  USART_TypeDef *uart_dev = uart_get_device(id);

  if(con && uart_dev) {
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
}


// Satisfy -Wmissing-prototypes
void USART_IRQ_HANDLER(CONSOLE_UART_ID);

void USART_IRQ_HANDLER(CONSOLE_UART_ID) {
  process_uart_console_irq(CONSOLE_UART_ID);
}

#if USE_TINYUSB
void OTG_HS_IRQHandler(void) {
  tud_int_handler(BOARD_DEVICE_RHPORT_NUM);
}
#endif

