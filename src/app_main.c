#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdalign.h>
#include <string.h>

#include "lib_cfg/build_config.h"
#include "cstone/platform.h"
#include "app_main.h"

#if defined PLATFORM_EMBEDDED
#  include "app_gpio.h"
#  include "stm32/app_stm32.h"
#  include "stm32f4xx_it.h" // FIXME: Rename to generic stm32_it

#  if defined PLATFORM_STM32F1
#    include "stm32f1xx_hal.h"
#    include "stm32f1xx_ll_rcc.h"
#  else
#    include "stm32f4xx_hal.h"
#    include "stm32f4xx_ll_rcc.h"
#  endif
#endif

#include "FreeRTOS.h"
#include "semphr.h"
#include "queue.h"
#include "cstone/rtos.h"
#include "cstone/timing.h"
#include "cstone/target.h"
#include "cstone/debug.h"
#include "cstone/faults.h"
#include "cstone/led_blink.h"
#include "cstone/umsg.h"
#include "cstone/tasks_core.h"
#include "cstone/prop_id.h"
#include "cstone/prop_db.h"
#include "cstone/log_ram.h"
#include "cstone/error_log.h"
#include "cstone/log_db.h"
#include "cstone/log_info.h"
#ifndef LOG_TO_RAM
#  ifdef PLATFORM_EMBEDDED
#    include "cstone/log_stm32.h"
#  else
#    include "log_evfs.h"
#  endif
#endif
#include "cstone/log_props.h"

#ifdef USE_CONSOLE
#  include "cstone/console.h"
#  ifdef PLATFORM_EMBEDDED
#    include "cstone/console_uart.h"
//#    include "cstone/console_usb.h"
#    include "cstone/cmds_stm32.h"
#    include "cstone/io/uart_stm32.h"
#  else
#    include "cstone/console_stdio.h"
//#    include "file_cmds.h"
#  endif
#  include "cstone/cmds_core.h"
#  include "app_cmds.h"
#endif

#include "util/mempool.h"



PropDB      g_prop_db;
LogDB       g_log_db;
ErrorLog    g_error_log;
mpPoolSet   g_pool_set;
UMsgTarget  g_tgt_event_buttons;


#if defined LOG_TO_RAM
// Small in-memory database for testing
static uint8_t s_log_db_data[LOG_NUM_SECTORS * LOG_SECTOR_SIZE];

#else // Log to filesystem in hosted OS or flash memory
#  ifndef PLATFORM_EMBEDDED
  EvfsFile *s_log_db_file = NULL;
#  else
// Allocate flash storage in sectors 1, 2, and 3
__attribute__(( section(".storage0") ))
static uint8_t s_log_db_data[LOG_NUM_SECTORS * LOG_SECTOR_SIZE];
#  endif
#endif


#define ERROR_LOG_SECTOR_SIZE   (4 * sizeof(ErrorEntry))
#define ERROR_LOG_NUM_SECTORS   2
alignas(ErrorEntry)
static uint8_t s_error_log_data[ERROR_LOG_NUM_SECTORS * ERROR_LOG_SECTOR_SIZE];


#ifdef PLATFORM_EMBEDDED
static LedBlinker s_blink_heartbeat;
ResetSource g_reset_source;

// Initialize GPIO pins
// Pins with alternate functions are initialized in usb_io_init() and uart_init()
#  if defined BOARD_STM32F429I_DISC1
// STM32F429I_DISC1
DEF_PIN(g_led_heartbeat,  GPIO_PORT_G, 14,  GPIO_PIN_OUTPUT_H);
DEF_PIN(g_led_status,     GPIO_PORT_G, 13,  GPIO_PIN_OUTPUT_L);

DEF_PIN(g_button1,        GPIO_PORT_A, 0,   GPIO_PIN_INPUT);

#  elif defined BOARD_MAPLE_MINI
// Maple Mini STM32F103CBT
DEF_PIN(g_led_heartbeat,  GPIO_PORT_B, 1,  GPIO_PIN_OUTPUT_H);

DEF_PIN(g_button1,        GPIO_PORT_B, 8,   GPIO_PIN_INPUT);
#  endif
#endif


static const PropDefaultDef s_prop_defaults[] = {
  P_UINT(P_DEBUG_SYS_LOCAL_VALUE,     0, 0),
  P_UINT(P_APP_INFO_BUILD_VERSION,    APP_VERSION_INT, P_PERSIST),
  P_END_DEFAULTS
};


////////////////////////////////////////////////////////////////////////////////////////////


void fatal_error(void) {
#ifdef PLATFORM_EMBEDDED
#  ifdef USE_CONSOLE
  Console *con = active_console();
  // Wait for Console TX queue to empty
  if(con) {
    if(!isr_queue_is_empty(con->tx_queue))
      delay_millis(200);
  }
#  endif

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


#ifdef PLATFORM_EMBEDDED
// STM32 HAL helper. Also used by FreeRTOS configASSERT() macro.
void assert_failed(uint8_t *file, uint32_t line) {
  printf(ERROR_PREFIX u8" \U0001F4A5 Assertion in '%s' on line %" PRIu32 A_NONE "\n", file, line);

  fatal_error();
}
#endif


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
#  if defined BOARD_STM32F429I_DISC1
      SET_LED_STATE(g_led_status, state);
#  endif
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
#  if defined BOARD_STM32F429I_DISC1
      return gpio_value(&g_led_status);
#  endif
      break;

  }
#endif
  return false;
}


#ifdef USE_CONSOLE
// Console setup
uint8_t show_prompt(void *eval_ctx) {
  fputs(A_YLW "APP> " A_NONE, stdout);
  return 7; // Inform console driver of cursor position in line (0-based)
}

static ConsoleCommandSuite s_cmd_suite = {0};


static void stdio_init(void) {
#ifdef PLATFORM_EMBEDDED
  putnl();  // Newlib-nano has odd behavior where setvbuf() doesn't work unless there is a pending line
#endif
  setvbuf(stdout, NULL, _IONBF, 0);

#ifdef PLATFORM_HOSTED
  configure_posix_terminal();
#endif
}


static void report_sys_name(void) {
  puts(A_BWHT u8"\n✨ Catalyst ✨" A_NONE);
#ifdef PLATFORM_HOSTED
  puts("** Hosted simulator **");
#endif
  putnl();
}

#endif // USE_CONSOLE


static void platform_init(void) {
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


#ifdef USE_CONSOLE
  // Prepare command suite for all subsystems
  command_suite_init(&s_cmd_suite);
  command_suite_add(&s_cmd_suite, g_core_cmd_set);
  command_suite_add(&s_cmd_suite, g_app_cmd_set);
#  ifdef PLATFORM_EMBEDDED
  command_suite_add(&s_cmd_suite, g_stm32_cmd_set);
#  else // Hosted
//  command_suite_add(&s_cmd_suite, g_filesystem_cmd_set);
#  endif

  // Using console with malloc'ed buffers
  ConsoleConfigBasic con_cfg = {
    .tx_queue_size = CONSOLE_TX_QUEUE_SIZE,
    .rx_queue_size = CONSOLE_RX_QUEUE_SIZE,
    .line_buf_size = CONSOLE_LINE_BUF_SIZE,
    .hist_buf_size = CONSOLE_HISTORY_BUF_SIZE,
    .cmd_suite     = &s_cmd_suite
  };

#  ifdef PLATFORM_EMBEDDED
  uart_io_init();
  uart_init(CONSOLE_UART_ID, CONSOLE_UART_PORT, CONSOLE_UART_BAUD);

  uart_console_init(CONSOLE_UART_ID, &con_cfg);

#    ifdef USE_USB
  usb_io_init();
  usb_console_init(CONSOLE_USB_ID, &con_cfg);
#    endif

#  else // Hosted
  stdio_console_init(&con_cfg);
#  endif

  stdio_init();
  report_sys_name();
#endif // USE_CONSOLE


#ifdef PLATFORM_EMBEDDED
  // Report summary of any recorded fault from the past
  if(report_faults(&g_fault_record, /*verbose*/false))
    g_prev_fault_record = g_fault_record;
  memset((char *)&g_fault_record, 0, sizeof g_fault_record);  // Clear for next fault

  g_reset_source = get_reset_source();
  report_reset_source();
  LL_RCC_ClearResetFlags(); // Reset flags persist across resets unless we clear them
#endif


  // Mount log DB
  StorageConfig log_db_cfg = {
    .sector_size  = LOG_SECTOR_SIZE,
    .num_sectors  = LOG_NUM_SECTORS,

#ifdef LOG_TO_RAM
    .ctx          = s_log_db_data,
    .erase_sector = log_ram_erase_sector,
    .read_block   = log_ram_read_block,
    .write_block  = log_ram_write_block
#else
#  ifdef PLATFORM_EMBEDDED // Log to flash
    .ctx          = s_log_db_data,
    .erase_sector = log_stm32_erase_sector,
    .read_block   = log_stm32_read_block,
    .write_block  = log_stm32_write_block
#  else // Log to filesystem
    .ctx          = s_log_db_file,
    .erase_sector = log_evfs_erase_sector,
    .read_block   = log_evfs_read_block,
    .write_block  = log_evfs_write_block
#  endif
#endif
  };

  logdb_init(&g_log_db, &log_db_cfg);
  logdb_mount(&g_log_db);

#if defined PLATFORM_EMBEDDED
  DPRINT("LogDB %dx%d  @ %p", LOG_NUM_SECTORS, LOG_SECTOR_SIZE, s_log_db_data);
#endif


  // Mount error log
  StorageConfig error_log_cfg = {
    .sector_size  = ERROR_LOG_SECTOR_SIZE,
    .num_sectors  = ERROR_LOG_NUM_SECTORS,
    .ctx          = s_error_log_data,

    .erase_sector = log_ram_erase_sector,
    .read_block   = log_ram_read_block,
    .write_block  = log_ram_write_block
  };

  errlog_init(&g_error_log, &error_log_cfg);
  errlog_format(&g_error_log);
  errlog_mount(&g_error_log);
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



static void portable_init(void) {
#ifdef PLATFORM_EMBEDDED
  // Init LEDs
  blink_init(&s_blink_heartbeat, (uint8_t)LED_HEARTBEAT, g_PatternSlowBlink, BLINK_ALWAYS, NULL);
  blinkers_add(&s_blink_heartbeat);
#endif


  // Static pool data
  alignas(mpPool)
  static uint8_t s_mem_pool_large[POOL_SIZE_LG * POOL_COUNT_LG + MP_STATIC_PADDING(alignof(uintptr_t))];

  alignas(mpPool)
  static uint8_t s_mem_pool_med[POOL_SIZE_MD * POOL_COUNT_MD + MP_STATIC_PADDING(alignof(uintptr_t))];

  alignas(mpPool)
  static uint8_t s_mem_pool_small[POOL_SIZE_SM * POOL_COUNT_SM + MP_STATIC_PADDING(alignof(uintptr_t))];


  mp_init_pool_set(&g_pool_set);
  mpPool *pool;
  pool = mp_create_static_pool((uint8_t *)s_mem_pool_large, sizeof s_mem_pool_large,
                              POOL_SIZE_LG, alignof(uintptr_t));
  mp_add_pool(&g_pool_set, pool);

  pool = mp_create_static_pool((uint8_t *)s_mem_pool_med, sizeof s_mem_pool_med,
                              POOL_SIZE_MD, alignof(uintptr_t));
  mp_add_pool(&g_pool_set, pool);

//  pool = mp_create_pool(POOL_COUNT_SM, POOL_SIZE_SM, alignof(uintptr_t));
  pool = mp_create_static_pool((uint8_t *)s_mem_pool_small, sizeof s_mem_pool_small,
                              POOL_SIZE_SM, alignof(uintptr_t));
  mp_add_pool(&g_pool_set, pool);

#ifdef USE_MP_COLLECT_STATS
  Histogram *pool_hist = histogram_init(20, 0, 500, /* track_overflow */ true);
  mp_add_histogram(&g_pool_set, pool_hist);
#endif



  // Load debug flags
  debug_init();

  // Setup system properties
  prop_init();

#if 0
#ifdef USE_PROP_ID_FIELD_NAMES
  // NOTE: This is sorted by qsort() so can't be const
  static PropFieldDef s_prop_fields_app[] = {
    PROP_LIST_APP(PROP_FIELD_DEF)
  };

  static PropNamespace s_app_prop_namespace = {
    .prop_defs      = s_prop_fields_app,
    .prop_defs_len  = COUNT_OF(s_prop_fields_app)
  };

  prop_add_namespace(&s_app_prop_namespace);
#endif
#endif

  prop_db_init(&g_prop_db, 32, 0, &g_pool_set);
  prop_db_set_defaults(&g_prop_db, s_prop_defaults);


  // Load properties from log DB
  unsigned count = restore_props_from_log(&g_prop_db, &g_log_db);
  printf("Retrieved %u properties from log\n", count);


  // Init message hub
  umsg_hub_init(&g_msg_hub, 16);

  // Monitor button presses
  umsg_tgt_callback_init(&g_tgt_event_buttons, event_button_handler);
  umsg_tgt_add_filter(&g_tgt_event_buttons, P_EVENT_BUTTON_n_PRESS | P3_MSK | P4_MSK);
  umsg_hub_subscribe(&g_msg_hub, &g_tgt_event_buttons);

  // Any DB event messages sent before now were discarded because there wasn't a hub
  prop_db_set_msg_hub(&g_prop_db, (UMsgTarget *)&g_msg_hub);
  g_prop_db.persist_updated = false; // Clear flag set by any init code


#ifdef USE_CONSOLE
  // Generate first prompt after all boot messages
  Console *con = active_console();
  if(con)
    shell_show_boot_prompt(&con->shell);
#endif
}


int main(void) {
  platform_init();
  portable_init();

  // Init settings from prop DB
//  app_apply_props();


  // Prepare FreeRTOS
  core_tasks_init();
#ifdef PLATFORM_EMBEDDED
//  usb_tasks_init();
#endif
//  app_tasks_init();


#ifdef PLATFORM_EMBEDDED
  // After enabling g_enable_rtos_sys_tick there is a race condition if SysTick happens
  // before scheduler is initialized. We synchronize using HAL_Delay() to buy time for
  // the scheduler init.
  HAL_Delay(1);
  g_enable_rtos_sys_tick = true;

  // Reduce SysTick priority before starting scheduler
  HAL_InitTick(TICK_INT_PRIORITY);
#endif

  // Run FreeRTOS
  vTaskStartScheduler();  // No return from here

  while(1) {};
}

