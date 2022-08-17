#ifndef APP_MAIN_H
#define APP_MAIN_H

// FIXME: Move to common config settings
#define USE_CONSOLE


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



// Peripheral resources

#define USART_IRQ_HANDLER(id)   USART__IRQ_HANDLER(id)
#define UART_IRQ_HANDLER(id)    UART__IRQ_HANDLER(id)

#define USART__IRQ_HANDLER(id)  USART##id##_IRQHandler (void)
#define UART__IRQ_HANDLER(id)   UART##id##_IRQHandler (void)

#define CONSOLE_UART_ID         1 // Using USART1
#define CONSOLE_UART_PORT       GPIO_PORT_A
#define CONSOLE_UART_BAUD       230400



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
