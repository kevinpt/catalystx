#ifndef APP_MAIN_H
#define APP_MAIN_H

// ******************** Configuration settings ********************
#define USE_CONSOLE

// Console settings
#ifdef USE_CONSOLE
#  ifdef PLATFORM_EMBEDDED
#    define CONSOLE_TX_QUEUE_SIZE   512
#  else
// TX queue mostly not used on hosted build. Just keep a vestigial buffer for stdio console.
#    define CONSOLE_TX_QUEUE_SIZE   32
#  endif

// Size RX queue to store full rate data between CONSOLE_TASK_MS polls.
// Assume 230400 bps: 17ms * 230400/10 Bps = 392 bytes
#  define CONSOLE_RX_QUEUE_SIZE     ((CONSOLE_UART_BAUD / 10) * (CONSOLE_TASK_MS+1) / 1000)

#  define CONSOLE_LINE_BUF_SIZE     64
#  define CONSOLE_HISTORY_BUF_SIZE  128 // FIXME: Investigate bug with 254 and 255
#endif


// Memory pool settings
#define POOL_SIZE_LG  256
#define POOL_SIZE_MD  64
#define POOL_SIZE_SM  20

#define POOL_COUNT_LG 4
#define POOL_COUNT_MD 10
#define POOL_COUNT_SM 20




// **** Peripheral resources ****

#define USART_IRQ_HANDLER(id)   USART__IRQ_HANDLER(id)
#define UART_IRQ_HANDLER(id)    UART__IRQ_HANDLER(id)

#define USART__IRQ_HANDLER(id)  USART##id##_IRQHandler (void)
#define UART__IRQ_HANDLER(id)   UART##id##_IRQHandler (void)

#define CONSOLE_UART_ID         1 // Using USART1
#define CONSOLE_UART_PORT       GPIO_PORT_A
#define CONSOLE_UART_BAUD       230400



// ******************** App properties ********************

#define P_DEBUG_SYS_LOCAL_VALUE     (P1_DEBUG | P2_SYS | P3_LOCAL | P4_VALUE)
#define P_APP_INFO_BUILD_VERSION   (P1_APP | P2_INFO | P3_BUILD | P4_VERSION)

// Events
#define P_EVENT_BUTTON_n_PRESS      (P1_EVENT | P2_BUTTON | P2_ARR(0) | P4_PRESS)

// FIXME: Switch to app namespace for P3 field
#define P_EVENT_BUTTON__USER_PRESS  (P_EVENT_BUTTON_n_PRESS | P2_ARR(0))


typedef enum {
  LED_HEARTBEAT = 0,
  LED_STATUS
} SystemLed;

// Argument to set_led() to toggle state
#define LED_TOGGLE -1



#ifdef __cplusplus
extern "C" {
#endif

void fatal_error(void);


bool get_led(uint8_t led_id);
// set_led() proto in cstone/led_blink.h

#ifdef __cplusplus
}
#endif


#endif // APP_MAIN_H
