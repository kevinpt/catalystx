#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "lib_cfg/build_config.h"
#include "cstone/platform.h"

#include "util/mempool.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "queue.h"
#include "cstone/rtos.h"
#include "cstone/timing.h"

#include "cstone/led_blink.h"
#include "cstone/umsg.h"
#include "cstone/tasks_core.h"
#include "cstone/prop_id.h"
#include "cstone/prop_db.h"
#include "cstone/prop_serialize.h"

#ifdef PLATFORM_EMBEDDED
#  include "app_gpio.h"
#endif



PropDB      g_prop_db;
//LogDB       g_log_db;
//ErrorLog    g_error_log;
mpPoolSet   g_pool_set;

UMsgTarget  g_tgt_event_buttons;


/* LED blink pattern definitions
   Terminate with 0 to end the pattern or use the BLINK_PAT() macro.
   Pattern begins with LED on then toggles for each following period
   A single entry pattern will flash the LED once for a timeout when
   configured for 1 rep. Patterns can have 15 segments max.
                                           ON   OFF     ON  OFF       ON  OFF ...
*/
BlinkTime g_PatternFastBlink[]  = BLINK_PAT(100, 100);
BlinkTime g_PatternSlowBlink[]  = BLINK_PAT(500, 500);
BlinkTime g_PatternPulseOne[]   = BLINK_PAT(80,  2000-80);
BlinkTime g_PatternPulseTwo[]   = BLINK_PAT(80,  200,    80, 2000-(80+200)*1-80);
BlinkTime g_PatternPulseThree[] = BLINK_PAT(80,  200,    80, 200,      80, 2000-(80+200)*2-80);
BlinkTime g_PatternPulseFour[]  = BLINK_PAT(80,  200,    80, 200,      80, 200,   80, 2000-(80+200)*3-80);
BlinkTime g_PatternFlash200ms[] = BLINK_PAT(200);
BlinkTime g_PatternDelay3s[]    = BLINK_PAT(1, 3000);

static LedBlinker s_blink_heartbeat;

#ifdef PLATFORM_EMBEDDED
//ResetSource g_reset_source;

// Initialize GPIO pins
// Pins with alternate functions are initialized in usb_io_init() and uart_init()
DEF_PIN(g_led_heartbeat,  GPIO_PORT_G, 14,  GPIO_PIN_OUTPUT_H);
DEF_PIN(g_led_status,     GPIO_PORT_G, 13,  GPIO_PIN_OUTPUT_L);

DEF_PIN(g_button1,        GPIO_PORT_A, 0,   GPIO_PIN_INPUT);
#endif



static const PropDefaultDef s_prop_defaults[] = {
  P_UINT(P_DEBUG_SYS_LOCAL_VALUE,      0, 0),
  P_UINT(P_APP_INFO_BUILD_VERSION,    42, P_PERSIST),
  P_END_DEFAULTS
};




void fatal_error(void) {
#ifdef PLATFORM_EMBEDDED
//  Console *con = active_console();
//  // Wait for Console TX queue to empty
//  if(con) {
//    if(!isr_queue_is_empty(con->tx_queue))
//      delay_millis(200);
//  }

  // Shutdown RTOS scheduler
  vTaskSuspendAll();
  g_enable_rtos_sys_tick = false;
  taskDISABLE_INTERRUPTS();

  // Fast blink LED
  while(1) {
    set_led(LED_HEARTBEAT, 1);
    delay_millis(100);

    set_led(LED_HEARTBEAT, 0);
    delay_millis(100);
  }

#else // Hosted simulator build
  while(1) {}
#endif
}


// STM32 HAL helper. Also used by FreeRTOS configASSERT() macro.
void assert_failed(uint8_t *file, uint32_t line) {
  printf(ERROR_PREFIX u8" \U0001F4A5 Assertion in '%s' on line %" PRIu32 A_NONE "\n", file, line);

  fatal_error();
}


#ifdef PLATFORM_EMBEDDED
static void system_clock_init(void) {
  RCC_OscInitTypeDef osc_init;
  RCC_ClkInitTypeDef clk_init;

  __HAL_RCC_PWR_CLK_ENABLE();

  // Enable voltage scaling at lower clock rates
  // VDD 2.7V - 3.6V
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

/*
        Clock diagram in RM0090 Figure 16  p152
                                                   : :        .--------.
  HSE              .---------------------.         | |------->| AHB PS |---> HCLK  180MHz max
  8MHz   .----.   |   .----.     .----.  | PLLCLK  | / SYSCLK '--------'
--|[]|-->| /M |---|-->| *N |--+->| /P |--|-------->|/            |   .---------.
   X3    '----'   |   '----'  |  :----:  |                       +-->| APB1 PS |--> 45MHz max
                  |           '->| /Q |--|---> PLL48CK (48MHz)   |   :---------:
                  | PLL          '----'  |                       '-->| APB2 PS |--> 90MHz max
                  '----------------------'                           '---------'
*/


  // Use PLL driven by HSE
  osc_init.OscillatorType = RCC_OSCILLATORTYPE_HSE; // 8MHz Xtal
  osc_init.HSEState       = RCC_HSE_ON;
  osc_init.PLL.PLLState   = RCC_PLL_ON;
  osc_init.PLL.PLLSource  = RCC_PLLSOURCE_HSE;

#ifndef USE_USB // 180MHz Sysclk
  osc_init.PLL.PLLM       = 8;   // Div factor (2 - 63)
  osc_init.PLL.PLLN       = 360; // Mul factor (50 - 432)
  osc_init.PLL.PLLP       = RCC_PLLP_DIV2; // Sysclk div factor (2,4,6,8)
  osc_init.PLL.PLLQ       = 8;   // Div factor (2 - 15) for OTG FS, SDIO, and RNG (48MHz for USB)
  // 8MHz * 360 / 8 / 2 --> 180MHz  Sysclk
  // 8MHz * 360 / 8 / 8 --> 45MHz

  // AHB = HCLK = Sysclk/1 = 180MHz  (180MHz max)
  // APB1 = AHB/4 = 45MHz (45MHz max)
  // APB2 = AHB/2 = 90MHz (90MHz max)
  // SysTick = AHB = 180MHz

#else // PLL48CK must be 48MHz so Sysclk limited to 168MHz
  osc_init.PLL.PLLM       = 8;   // Div factor (2 - 63)
  osc_init.PLL.PLLN       = 336; // Mul factor (50 - 432)
  osc_init.PLL.PLLP       = RCC_PLLP_DIV2; // Sysclk div factor (2,4,6,8)
  osc_init.PLL.PLLQ       = 7;   // Div factor (2 - 15) for OTG FS, SDIO, and RNG (48MHz for USB)
  // 8MHz * 336 / 8 / 2 --> 168MHz  Sysclk
  // 8MHz * 336 / 8 / 7 --> 48MHz

  // AHB = HCLK = Sysclk/1 = 168MHz  (180MHz max)
  // APB1 = AHB/4 = 42MHz (45MHz max)
  // APB2 = AHB/2 = 84MHz (90MHz max)
  // SysTick = AHB = 168MHz
#endif

  if(HAL_RCC_OscConfig(&osc_init) != HAL_OK)
    fatal_error();

  // Set internal voltage reg. to allow higher clock rates (required to achieve 180MHz)
  // Stop and Standby modes no longer available
  HAL_PWREx_EnableOverDrive();
 
  // Use PLL as Sysclk and set division ratios for derived clocks
  clk_init.ClockType      = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                             RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
  clk_init.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  clk_init.AHBCLKDivider  = RCC_SYSCLK_DIV1;  // HCLK == Sysclk
  clk_init.APB1CLKDivider = RCC_HCLK_DIV4;
  clk_init.APB2CLKDivider = RCC_HCLK_DIV2;

  // NOTE: Latency selected from Table 11 in RM0090
  if(HAL_RCC_ClockConfig(&clk_init, FLASH_LATENCY_5) != HAL_OK)
    fatal_error();

}
#endif // PLATFORM_EMBEDDED


// Abstract LED control
void set_led(uint8_t led_id, const short state) {
#ifdef PLATFORM_EMBEDDED

#define SET_LED_STATE(led, state)  do { \
    if(state == LED_TOGGLE) \
      gpio_toggle(&led);  \
    else \
      gpio_set(&led, state); \
  } while(0)

  switch(led_id) {
    case LED_HEARTBEAT:
      SET_LED_STATE(g_led_heartbeat, state);
      break;

    case LED_STATUS:
      SET_LED_STATE(g_led_status, state);
      break;

  }
#endif
}


bool get_led(uint8_t led_id) {
#ifdef PLATFORM_EMBEDDED
  switch(led_id) {
    case LED_HEARTBEAT:
      return gpio_value(&g_led_heartbeat);
      break;

    case LED_STATUS:
      return gpio_value(&g_led_status);
      break;

  }
#endif
  return false;
}


void platform_init(void) {
#ifdef PLATFORM_STM32
  HAL_Init();

  // Configure the system clock to 180 MHz
  system_clock_init();
  HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);

  // Set initial high priority so SysTick will run before scheduler is called
  HAL_InitTick(0x01);


  // Timer for run time stats and blocking delays
  perf_timer_init();
#endif


}


// UMsg target callback for button events
static void event_button_handler(UMsgTarget *tgt, UMsg *msg) {
  switch(msg->id) {
  case P_EVENT_BUTTON__USER_PRESS:
#ifdef PLATFORM_EMBEDDED
//    gpio_toggle(&g_led_status);
#endif
    break;
  default:
    break;
  }
}



void portable_init(void) {
  // Init LEDs
  blink_init(&s_blink_heartbeat, (uint8_t)LED_HEARTBEAT, g_PatternSlowBlink, BLINK_ALWAYS, NULL);
  blinkers_add(&s_blink_heartbeat);

  // Init memory pools
#define POOL_SIZE_LG  256
#define POOL_SIZE_MD  64
#define POOL_SIZE_SM  20

#define POOL_COUNT_LG 4
#define POOL_COUNT_MD 10
#define POOL_COUNT_SM 20 

  // Static pool data
  static uint8_t s_mem_pool_large[POOL_SIZE_LG * POOL_COUNT_LG + MP_STATIC_PADDING(alignof(uintptr_t))]
    alignas(mpPool);

  static uint8_t s_mem_pool_med[POOL_SIZE_MD * POOL_COUNT_MD + MP_STATIC_PADDING(alignof(uintptr_t))]
    alignas(mpPool);


  mp_init_pool_set(&g_pool_set);
  mpPool *pool;
  pool = mp_create_static_pool((uint8_t *)s_mem_pool_large, sizeof s_mem_pool_large,
                              POOL_SIZE_LG, alignof(uintptr_t));
  mp_add_pool(&g_pool_set, pool);

  pool = mp_create_static_pool((uint8_t *)s_mem_pool_med, sizeof s_mem_pool_med,
                              POOL_SIZE_MD, alignof(uintptr_t));
  mp_add_pool(&g_pool_set, pool);

  pool = mp_create_pool(POOL_COUNT_SM, POOL_SIZE_SM, alignof(uintptr_t));
  mp_add_pool(&g_pool_set, pool);

#ifdef USE_MP_COLLECT_STATS
  Histogram *pool_hist = histogram_init(20, 0, 500, /* track_overflow */ true);
  mp_add_histogram(&g_pool_set, pool_hist);
#endif


  prop_db_init(&g_prop_db, 32, 0, &g_pool_set);
  prop_db_set_defaults(&g_prop_db, s_prop_defaults);


  // Init message hub
  umsg_hub_init(&g_msg_hub, 16);

  // Monitor button presses
  umsg_tgt_callback_init(&g_tgt_event_buttons, event_button_handler);
  umsg_tgt_add_filter(&g_tgt_event_buttons, P_EVENT_BUTTON_n_PRESS | P3_MSK | P4_MSK);
  umsg_hub_subscribe(&g_msg_hub, &g_tgt_event_buttons);


}


int main(void) {
  platform_init();
  portable_init();

  // Init settings from prop DB
//  sys_props_init();

  // Any event messages sent before now were discarded because there wasn't a hub
  prop_db_set_msg_hub(&g_prop_db, (UMsgTarget *)&g_msg_hub);
  g_prop_db.persist_updated = false; // Clear flag set by any init code

  // Prepare FreeRTOS
  core_tasks_init();
#ifdef PLATFORM_EMBEDDED
//  usb_tasks_init();
#endif
  app_tasks_init();

#ifdef PLATFORM_EMBEDDED
  // Reduce SysTick priority before starting scheduler
  HAL_InitTick(TICK_INT_PRIORITY);
  g_enable_rtos_sys_tick = true;
#endif

  // Run FreeRTOS
  vTaskStartScheduler();  // No return from here

  while(1) {};
}
