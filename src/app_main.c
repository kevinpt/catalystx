#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdalign.h>
#include <string.h>
#include <stdlib.h>

#include "lib_cfg/build_config.h"
#include "lib_cfg/cstone_cfg_stm32.h"
#include "build_info.h"
#include "cstone/platform.h"
#include "app_main.h"
#include "app_tasks.h"

#if defined PLATFORM_EMBEDDED
#  include "app_gpio.h"
#  include "stm32/app_stm32.h"
#  include "stm32f4xx_it.h" // FIXME: Rename to generic stm32_it

#  if defined PLATFORM_STM32F1
#    include "stm32f1xx_hal.h"
#    include "stm32f1xx_ll_rcc.h"
#  else // PLATFORM_STM32F4
#    include "stm32f4xx_hal.h"
#    include "stm32f4xx_ll_rcc.h"
#    include "stm32f4xx_ll_gpio.h"
#    include "stm32f4xx_ll_rng.h"
#    include "stm32f4xx_ll_tim.h"
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
#include "app_prop_id.h"
#include "cstone/iqueue_int16_t.h"
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
#include "cstone/rtc_device.h"
#include "cstone/rtc_soft.h"
#ifdef PLATFORM_EMBEDDED
#  include "cstone/rtc_stm32.h"
#else
#  include "cstone/rtc_hosted.h"
#endif

#ifdef USE_CONSOLE
#  include "cstone/console.h"
#  ifdef PLATFORM_EMBEDDED
#    include "cstone/console_uart.h"
#    include "cstone/cmds_stm32.h"
#    include "cstone/io/uart.h"
#    include "cstone/console_usb.h"
#    include "cstone/io/usb.h"
#  else
#    include "cstone/console_stdio.h"
#  endif
#  include "cstone/cmds_core.h"
#  include "app_cmds.h"
#  ifdef USE_FILESYSTEM
#    include "cmds_filesys.h"
#  endif
#endif

#ifdef USE_FILESYSTEM
#  include "evfs.h"
#  include "evfs/romfs_fs.h"
#  include "evfs/stdio_fs.h"
#  include "evfs/shim/shim_trace.h"
#endif

#include "util/mempool.h"
#include "util/random.h"

#if defined PLATFORM_EMBEDDED && USE_AUDIO
#  include "audio_synth.h"
#  include "i2s.h"
#endif



PropDB      g_prop_db;
LogDB       g_log_db;
ErrorLog    g_error_log;
mpPoolSet   g_pool_set;
UMsgTarget  g_tgt_event_buttons;
#if USE_AUDIO
UMsgTarget  g_tgt_audio_ctl;
SynthState  g_audio_synth;
#endif


#if defined LOG_TO_RAM
// Small in-memory database for testing
static uint8_t s_log_db_data[LOG_NUM_SECTORS * LOG_SECTOR_SIZE];

#else // Log to filesystem in hosted OS or flash memory
#  if defined PLATFORM_HOSTED
  EvfsFile *s_log_db_file = NULL;
#  else // Embedded
// Allocate flash storage in sectors 1, 2, and 3
__attribute__(( section(".storage0") ))
static uint8_t s_log_db_data[LOG_NUM_SECTORS * LOG_SECTOR_SIZE];
#  endif
#endif


#define ERROR_LOG_SECTOR_SIZE   (4 * sizeof(ErrorEntry))
#define ERROR_LOG_NUM_SECTORS   2
alignas(ErrorEntry)
static uint8_t s_error_log_data[ERROR_LOG_NUM_SECTORS * ERROR_LOG_SECTOR_SIZE];

static RTCDevice s_rtc_device;

#ifdef PLATFORM_EMBEDDED
static LedBlinker s_blink_heartbeat;
ResetSource g_reset_source;

RTCDevice g_rtc_soft_device;

// Initialize GPIO pins
// Pins with alternate functions are initialized in usb_io_init() and uart_init()
#  if defined BOARD_STM32F429I_DISC1
// STM32F429I_DISC1
DEF_PIN(g_led_heartbeat,  GPIO_PORT_G, 14,  GPIO_PIN_OUTPUT_H);
DEF_PIN(g_led_status,     GPIO_PORT_G, 13,  GPIO_PIN_OUTPUT_L);

DEF_PIN(g_button1,        GPIO_PORT_A, 0,   GPIO_PIN_INPUT);
#    ifdef USE_TINYUSB
DEF_PIN(g_usb_pso,        GPIO_PORT_C, 4,   GPIO_PIN_OUTPUT_H);
DEF_PIN(g_usb_oc,         GPIO_PORT_C, 5,   GPIO_PIN_INPUT);
#    endif

#  elif defined BOARD_MAPLE_MINI
// Maple Mini STM32F103CBT
DEF_PIN(g_led_heartbeat,  GPIO_PORT_B, 1,  GPIO_PIN_OUTPUT_H);

DEF_PIN(g_button1,        GPIO_PORT_B, 8,   GPIO_PIN_INPUT);
#  endif
#endif


static const PropDefaultDef s_prop_defaults[] = {
  P_UINT(P_DEBUG_SYS_LOCAL_VALUE,     0, 0),
  P_UINT(P_APP_INFO_BUILD_VERSION,    APP_VERSION_INT, P_PERSIST),
#if USE_AUDIO
  P_UINT(P_APP_AUDIO_INFO_VALUE, 0, 0),
  P_UINT(P_APP_AUDIO_INST0_FREQ,  440, 0),
  P_UINT(P_APP_AUDIO_INST0_WAVE,   1, 0),
  P_UINT(P_APP_AUDIO_INST0_CURVE,  0, 0),
#endif
  P_END_DEFAULTS
};


////////////////////////////////////////////////////////////////////////////////////////////


void fatal_error(void) {
#ifdef PLATFORM_EMBEDDED
#  ifdef USE_CONSOLE
  Console *con = active_console();
  // Wait for Console TX queue to empty
  if(con) {
    if(!isr_queue_is_empty(con->stream.tx_queue))
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
  fputs(A_YLW u8"APP❱ " A_NONE, stdout);
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

#ifdef USE_FILESYSTEM
static void filesystem_init(void) {
  // Configure filesystem
  evfs_init();
  evfs_register_stdio(/*default*/true);
//  evfs_register_trace("t_stdio", "stdio", report, stderr, /*default_vfs*/ true);
#if 0
  EvfsFile *image;
  evfs_open("disco_image.romfs", &image, EVFS_READ); // Open image on stdio
  // Mount image and make it default for future VFS access
  evfs_register_romfs("romfs", image, /*default*/ true);
//  evfs_register_trace("t_romfs", "romfs", report, stderr, /*default_vfs*/ true);
#endif

  unsigned no_dots = 1;
  evfs_vfs_ctrl(EVFS_CMD_SET_NO_DIR_DOTS, &no_dots);

#  if ! defined LOG_TO_RAM
#    define LOG_FILE_PATH  "logdb.dat"
  int fs_status = evfs_open(LOG_FILE_PATH, &s_log_db_file, EVFS_RDWR | EVFS_OPEN_OR_NEW);
  printf("EVFS opened log: %s\n", evfs_err_name(fs_status));
  if(fs_status == EVFS_OK) {
    if(evfs_file_size(s_log_db_file) == 0) { // New logdb file
      // Grow file to required size
      evfs_off_t last_byte = (LOG_NUM_SECTORS * LOG_SECTOR_SIZE) - 1;
      evfs_file_seek(s_log_db_file, last_byte, EVFS_SEEK_TO);
      uint8_t data = 0xAA;
      evfs_file_write(s_log_db_file, &data, 1);
    }
  }
#  endif
}
#endif // USE_FILESYSTEM


#if defined PLATFORM_STM32F4 && !defined LOG_TO_RAM
uint32_t flash_sector_index(uint8_t *addr) {
  uint32_t flash_offset = (uint32_t)addr - 0x8000000ul;

  if(flash_offset < 0x100000ul) { // Bank 1
    if(flash_offset < 0x10000ul) {  // 16K (0-3)
      return flash_offset / (16 * 1024);
    } else if(flash_offset < 0x20000ul) { // 64K (4)
      return FLASH_SECTOR_4;
    } else {  // 128K (5-11)
      return FLASH_SECTOR_4 + flash_offset / (128 * 1024);
    }

  } else {  // Bank 2
    flash_offset -= 0x100000ul;
    if(flash_offset < 0x10000ul) {  // 16K (12-15)
      return FLASH_SECTOR_12 + flash_offset / (16 * 1024);
    } else if(flash_offset < 0x20000ul) { // 64K (16)
      return FLASH_SECTOR_16;
    } else {  // 128K (17-23)
      return FLASH_SECTOR_16 + flash_offset / (128 * 1024);
    }
  }
}
#endif


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
#    ifdef USE_FILESYSTEM
  command_suite_add(&s_cmd_suite, g_filesystem_cmd_set);
#    endif
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

#ifdef USE_FILESYSTEM
  filesystem_init();
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
#  ifndef USE_FILESYSTEM // Log to flash
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

// FIXME: switch to #cmakedefine01
#  if defined LOG_TO_RAM
#    define LOG_TO_RAM2   1
#  else
#    define LOG_TO_RAM2   0
#  endif

#  if defined USE_FILESYSTEM
#    define LOG_TO_FILESYSTEM   1
#  else
#    define LOG_TO_FILESYSTEM   0
#  endif

#  ifndef NDEBUG
  const char *location = LOG_TO_RAM2 ? "RAM" : LOG_TO_FILESYSTEM ? "Filesystem" : "Flash";
#  endif
  DPRINT("LogDB %dx%d  @ %p in %s", LOG_NUM_SECTORS, LOG_SECTOR_SIZE, s_log_db_data,
                                    location);
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


  // Configure RTC

#if 0
  // OSC32_IN (PC14)
  LL_GPIO_InitTypeDef gpio_cfg = {
    .Pin        = LL_GPIO_PIN_14,
    .Mode       = LL_GPIO_MODE_ALTERNATE,
    .Speed      = LL_GPIO_SPEED_FREQ_MEDIUM,
    .OutputType = LL_GPIO_OUTPUT_PUSHPULL,
    .Pull       = LL_GPIO_PULL_NO,
    .Alternate  = LL_GPIO_AF_15  // Datasheet Table 12 p78
  };
  LL_GPIO_Init(GPIOC, &gpio_cfg);
#endif

#ifdef PLATFORM_EMBEDDED

  rtc_stm32_init(&s_rtc_device);
  rtc_soft_init(&g_rtc_soft_device);


  RTC_TIMER_CLK_ENABLE();
  // Base clock is HCLK/2
  // Timer TIM3 is on APB1 @ 90MHz

// RTC timer will tick at 1 sec intervals. We need a clock that is evenly divisible
// into the APB clock frequency and generates a prescaler that fits into uint16_t.
#define RTC_CLOCK_HZ  2000    // Evenly divisible into 90MHz and 84MHz

  // TIM3 is on APB1 @ 45MHz with /4 prescaler. Timer clock is 2x @ 90MHz (180MHz / 2)
  uint16_t prescaler = (uint32_t) ((SystemCoreClock / 2) / RTC_CLOCK_HZ) - 1;

  LL_TIM_InitTypeDef tim_cfg;

  LL_TIM_StructInit(&tim_cfg);
  tim_cfg.Autoreload    = RTC_CLOCK_HZ - 1; // 1 sec tick
  tim_cfg.Prescaler     = prescaler;

  LL_TIM_SetCounter(RTC_TIMER, 0);
  LL_TIM_Init(RTC_TIMER, &tim_cfg);

  LL_TIM_EnableIT_UPDATE(RTC_TIMER);
  LL_TIM_EnableCounter(RTC_TIMER);

  HAL_NVIC_SetPriority(RTC_TIMER_IRQ, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0);
  NVIC_EnableIRQ(RTC_TIMER_IRQ);

#else // Hosted
  rtc_hosted_init(&s_rtc_device);
#endif

  rtc_set_sys_device(&s_rtc_device);
}


// UMsg target callback for button events
static void event_button_handler(UMsgTarget *tgt, UMsg *msg) {
  switch(msg->id) {
  case P_EVENT_BUTTON__USER_RELEASE:
#ifdef PLATFORM_EMBEDDED
    gpio_toggle(&g_led_status);
#endif
    break;
  default:
    break;
  }
}


// Get random value from hardware RNG
uint32_t random_from_system(void) {
  __HAL_RCC_RNG_CLK_ENABLE();
  LL_RNG_Enable(RNG);

  while(!LL_RNG_IsActiveFlag_DRDY(RNG));
  return LL_RNG_ReadRandData32(RNG);
}


#if USE_AUDIO
/*
Adafruit
MAX98357      STM32F429     DISCO1
---------     ----------    -----------
VDD     1                   P1-1  (5V)
GND     2                   P1-64
SD Mode 3     GPIO  PE1     P1-18       0 = Shutdown, 1 = L, z = L/2+R/2
Gain    4     GPIO  PE3     P1-16       0 = 12dB,  1 = 3dB,  z = 9dB
Din     5     SD    PC3     P2-15
BCLK    6     CK    PB10    P2-48
LRCLK   7     WS    PB9     P1-20
*/

DEF_PIN(g_i2s_sd_mode,  GPIO_PORT_E, 3,  GPIO_PIN_OUTPUT_L);  // Shutdown not Serial Data
DEF_PIN(g_i2s_gain,     GPIO_PORT_E, 1,  GPIO_PIN_OUTPUT_H);  // 3dB default

//DEF_PIN(g_i2s_ck,     GPIO_PORT_B, 10,  GPIO_PIN_OUTPUT_H);
//DEF_PIN(g_i2s_sd,     GPIO_PORT_C, 3,  GPIO_PIN_OUTPUT_H);
//DEF_PIN(g_i2s_mco,     GPIO_PORT_C, 9,  GPIO_PIN_OUTPUT_H);


#ifdef USE_HAL_I2S
//#define AUDIO_SAMPLE_RATE   16000

I2S_HandleTypeDef g_i2s = {
  .Instance = SPI2,
  .Init = {
    .Mode           = I2S_MODE_MASTER_TX,
    .Standard       = I2S_STANDARD_PHILIPS,
    .DataFormat     = I2S_DATAFORMAT_16B,
    .MCLKOutput     = I2S_MCLKOUTPUT_DISABLE,
#  if AUDIO_SAMPLE_RATE == 8000
    .AudioFreq      = I2S_AUDIOFREQ_8K,
#  elif AUDIO_SAMPLE_RATE == 16000
    .AudioFreq      = I2S_AUDIOFREQ_16K,
#  else
#    error "Unsupported sample rate"
#  endif
    .CPOL           = I2S_CPOL_LOW,
    .ClockSource    = I2S_CLOCK_PLL,
    .FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE
  }
};


DMA_HandleTypeDef g_dma = {
  .Instance = DMA1_Stream4,
  .Init = {
    .Channel              = DMA_CHANNEL_0,  // S4_C0 = SPI2_TX  (RM0090 Table 42)
    .Direction            = DMA_MEMORY_TO_PERIPH,
    .PeriphInc            = DMA_PINC_DISABLE,
    .MemInc               = DMA_MINC_ENABLE,
    .PeriphDataAlignment  = DMA_PDATAALIGN_HALFWORD,
    .MemDataAlignment     = DMA_MDATAALIGN_HALFWORD,
    .Mode                 = DMA_CIRCULAR,
    .Priority             = DMA_PRIORITY_MEDIUM,
    .FIFOMode             = DMA_FIFOMODE_DISABLE,
    .FIFOThreshold        = DMA_FIFO_THRESHOLD_HALFFULL,
    .MemBurst             = DMA_MBURST_SINGLE,
    .PeriphBurst          = DMA_PBURST_SINGLE
  }
};
#endif // USE_HAL_I2S


static void audio_ctl_handler(UMsgTarget *tgt, UMsg *msg) {
  // Avoid message loops for props set by console commands
//  if(msg->source == P_RSRC_CON_LOCAL_TASK);
//    return;

//  printf(A_CYN "AUDIO: " PROP_ID " = %" PRIu32 "\n" A_NONE, msg->id, (uint32_t)msg->payload);
  static int next_key = 0;

  switch(msg->id) {
  case P_APP_AUDIO_INFO_VALUE: // Enable/disable synth
#if 1
    if(msg->payload)
      HAL_I2S_DMAResume(&g_i2s);
    else
      HAL_I2S_DMAPause(&g_i2s);
#else
    if(msg->payload)
      gpio_highz_off(&g_i2s_sd_mode, true);  // Shutdown off L channel only
      //gpio_highz_on(&g_i2s_sd_mode);  // Disable shutdown L/2+R/2
    else
      gpio_highz_off(&g_i2s_sd_mode, false);  // Activate shutdown
#endif
    break;

  case P_APP_AUDIO_INST0_FREQ:
    synth_set_freq(&g_audio_synth, 0, msg->payload);
    break;

  case P_APP_AUDIO_INST0_WAVE:
    synth_set_waveform(&g_audio_synth, 0, msg->payload);
    break;

  case P_APP_AUDIO_INST0_CURVE:
    synth_set_adsr_curve(&g_audio_synth, 0, msg->payload);
    break;

  case P_EVENT_BUTTON__USER_PRESS:
    synth_press_key(&g_audio_synth, next_key + 69+12, 0);
//    puts("Press");
    break;

  case P_EVENT_BUTTON__USER_RELEASE:
    synth_release_key(&g_audio_synth, next_key + 69+12);
//    printf("Release %d\n", next_key);
    next_key++;
    if(next_key >= 13)
      next_key = 0;
    break;

  default:
    break;
  }
}
#endif // USE_AUDIO


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


  prop_db_init(&g_prop_db, 32, 0, &g_pool_set);
  prop_db_set_defaults(&g_prop_db, s_prop_defaults);

  // Prepare initial seed entropy for PRNG
  // This will be overwritten if the log has a seed property
  char seed_buf[32];
  snprintf(seed_buf, COUNT_OF(seed_buf), "%" PRIu32 "/%s/%s", random_from_system(),
          g_build_date, APP_VERSION);
  uint32_t seed = (uint32_t)random_seed_from_str(seed_buf);
  DPRINT("SEED: '%s' --> %08lX", seed_buf, seed);
  prop_set_uint(&g_prop_db, P_SYS_PRNG_LOCAL_VALUE, seed, 0);
  prop_set_attributes(&g_prop_db, P_SYS_PRNG_LOCAL_VALUE, P_PERSIST);


  // Load properties from log DB
  unsigned count = restore_props_from_log(&g_prop_db, &g_log_db);
  printf("Retrieved %u properties from log\n", count);


  // Init message hub
  umsg_hub_init(&g_msg_hub, 16);

  // Monitor button presses
  umsg_tgt_callback_init(&g_tgt_event_buttons, event_button_handler);
  umsg_tgt_add_filter(&g_tgt_event_buttons, P_EVENT_BUTTON_n_PRESS | P3_MSK | P4_MSK);
  umsg_hub_subscribe(&g_msg_hub, &g_tgt_event_buttons);

#if USE_AUDIO
  umsg_tgt_callback_init(&g_tgt_audio_ctl, audio_ctl_handler);
  umsg_tgt_add_filter(&g_tgt_audio_ctl, (P1_APP | P2_AUDIO | P3_MSK | P4_MSK));
  umsg_tgt_add_filter(&g_tgt_audio_ctl, (P_EVENT_BUTTON_n_PRESS | P3_MSK | P4_MSK));
  umsg_hub_subscribe(&g_msg_hub, &g_tgt_audio_ctl);
#endif

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
#if defined PLATFORM_EMBEDDED && defined USE_TINYUSB
  usb_tasks_init();
#endif
  app_tasks_init();

#if USE_AUDIO
  i2s_io_init();

#ifdef USE_HAL_I2S
  if(HAL_I2S_Init(&g_i2s) != HAL_OK) {
    DPUTS("I2S init error");
  }
#endif

  synth_init(&g_audio_synth, AUDIO_SAMPLE_RATE, 512);

  // Configure synth instruments
  uint16_t modulate_freq = frequency_scale_factor(880, 880+50);

  SynthVoiceCfg voice_cfg = {
    .osc_freq = 0,
    .osc_kind = OSC_SINE,

    .lfo_freq = 6,
    .lfo_kind = OSC_TRIANGLE,

    .adsr.attack  = 200,
    .adsr.decay   = 600,
    .adsr.sustain = 24000,
    .adsr.release = 1500,
    .adsr.curve   = CURVE_LINEAR,

    .lpf_cutoff_freq  = 1000,
    .modulate_freq    = modulate_freq,
    .modulate_amp     = 0, //INT16_MAX - 20000,
    .modulate_cutoff  = 0
  };

  synth_instrument_init(&g_audio_synth, 0, &voice_cfg);

  audio_tasks_init();
#endif // USE_AUDIO


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

