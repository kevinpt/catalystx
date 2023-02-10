#ifndef APP_MAIN_H
#define APP_MAIN_H

// ******************** Configuration settings ********************

#define APP_NAME        "Catalyst"
#define APP_NAME_SHORT  "CAT"


#define USE_CONSOLE
#define USE_PROP_ID_FIELD_NAMES
#define USE_CRON


// Console settings
#ifdef USE_CONSOLE
#  ifdef PLATFORM_EMBEDDED
#    define CONSOLE_TX_QUEUE_SIZE   (512 + 1024)
#  else
// TX queue mostly not used on hosted build. Just keep a vestigial buffer for stdio console.
#    define CONSOLE_TX_QUEUE_SIZE   32
#  endif

// Size RX queue to store full rate data between CONSOLE_TASK_MS polls.
// Assume 230400 bps: 17ms * 230400/10 Bps = 392 bytes
#  define CONSOLE_RX_QUEUE_SIZE     ((CONSOLE_UART_BAUD / 10) * (CONSOLE_TASK_MS+1) / 1000)

#  define CONSOLE_LINE_BUF_SIZE     64
#  define CONSOLE_HISTORY_BUF_SIZE  80 // FIXME: Investigate bug with 254 and 255
#endif


// Memory pool settings
#ifdef USE_MINIMAL_TASKS
#  define POOL_SIZE_LG  128
#  define POOL_SIZE_MD  64
#  define POOL_SIZE_SM  20

#  define POOL_COUNT_LG 1
#  define POOL_COUNT_MD 2
#  define POOL_COUNT_SM 8

#else
#  define POOL_SIZE_LG  256
#  define POOL_SIZE_MD  64
#  define POOL_SIZE_SM  20

#  define POOL_COUNT_LG 3
#  define POOL_COUNT_MD 10
#  define POOL_COUNT_SM 10
#endif


// **** Log DB Configuration ****
//#define LOG_TO_RAM  // Force settings to RAM for debug

#if defined LOG_TO_RAM || defined PLATFORM_HOSTED // Small in-memory filesystem for testing
#  define LOG_NUM_SECTORS 3
#  define LOG_SECTOR_SIZE 128

#else // Log to flash storage
#  if defined BOARD_STM32F429I_DISC1
    // STORAGE0 in STM32 sectors 1-3
#    define LOG_NUM_SECTORS 3
#    define LOG_SECTOR_SIZE (16 * 1024)
#  elif defined BOARD_STM32F401_BLACK_PILL
    // STORAGE0 in STM32 sectors 1-3
#    define LOG_NUM_SECTORS 3
#    define LOG_SECTOR_SIZE (16 * 1024)
#  endif
#endif


// **** Peripheral resources ****

#define USART_IRQ_HANDLER(id)   USART__IRQ_HANDLER(id)
#define UART_IRQ_HANDLER(id)    UART__IRQ_HANDLER(id)

#define USART__IRQ_HANDLER(id)  USART##id##_IRQHandler (void)
#define UART__IRQ_HANDLER(id)   UART##id##_IRQHandler (void)

#define CONSOLE_UART_ID         1 // Using USART1
#define CONSOLE_UART_PORT       GPIO_PORT_A
#define CONSOLE_UART_BAUD       230400


#define CONSOLE_USB_ID          0



#define AUDIO_SAMPLE_RATE       16000
#if USE_AUDIO
#  if defined BOARD_STM32F429I_DISC1
#    define USE_AUDIO_DAC
#  else
#    define USE_AUDIO_I2S
#  endif
#endif
#define AUDIO_DMA_BUF_SAMPLES   512

// ******************** App properties ********************

#define P_DEBUG_SYS_LOCAL_VALUE     (P1_DEBUG | P2_SYS | P3_LOCAL | P4_VALUE)
#define P_APP_INFO_BUILD_VERSION   (P1_APP | P2_INFO | P3_BUILD | P4_VERSION)



typedef enum {
  LED_HEARTBEAT = 0,
  LED_STATUS
} SystemLed;

// Argument to set_led() to toggle state
#define LED_TOGGLE -1


#ifndef COUNT_OF
#  define COUNT_OF(a) (sizeof(a) / sizeof(*(a)))
#endif


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
