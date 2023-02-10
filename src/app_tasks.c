#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include "lib_cfg/build_config.h"
#include "cstone/platform.h"
#include "app_main.h"

#ifdef PLATFORM_EMBEDDED
#  include "stm32f4xx_hal.h"
//#  include "stm32f4xx_ll_dma.h"
//#  include "stm32f4xx_ll_spi.h"

#  include "FreeRTOS.h"
#  include "semphr.h"
#  include "timers.h"
#endif

#include "util/dhash.h"
#include "util/range_strings.h"

#include "cstone/prop_id.h"
#include "cstone/iqueue_int16_t.h"
#include "cstone/umsg.h"
#include "cstone/rtos.h"
#include "cstone/led_blink.h"
#include "cstone/debug.h"
#ifdef USE_CRON
#  include "cstone/prop_db.h"
#  include "cstone/cron_events.h"
#endif


#include "app_tasks.h"
#ifdef PLATFORM_EMBEDDED
#  include "app_gpio.h"
#  include "debounce.h"
#endif

#if defined PLATFORM_EMBEDDED && USE_AUDIO
#  include "audio_synth.h"
#  include "sample_device.h"
#endif

#ifndef COUNT_OF
#  define COUNT_OF(a) (sizeof(a) / sizeof(*(a)))
#endif



extern UMsgHub g_msg_hub;

#ifdef BOARD_STM32F429I_DISC1
// TASK: Create a debounce filter and button event manager for button1
static Debouncer s_user_button;

static void debounce_task_cb(TimerHandle_t timer) {
  debouncer_filter_sample(&s_user_button, gpio_value(&g_button1));

  if(debouncer_rising_edge(&s_user_button)) {
    UMsg msg = { .id = P_EVENT_BUTTON_USER_PRESS,
      .source = P_RSRC_HW_LOCAL_TASK };
    umsg_hub_send(&g_msg_hub, &msg, 1);
  }

  if(debouncer_falling_edge(&s_user_button)) {
    UMsg msg = { .id = P_EVENT_BUTTON_USER_RELEASE,
      .source = P_RSRC_HW_LOCAL_TASK };
    umsg_hub_send(&g_msg_hub, &msg, 1);
  }

}
#endif // BOARD_STM32F429I_DISC1


void app_tasks_init(void) {

#ifdef BOARD_STM32F429I_DISC1
  // Button pulled low by default
  debouncer_init(&s_user_button, DEBOUNCE_TASK_MS, DEBOUNCE_FILTER_MS, /*init_filter*/false);

  TimerHandle_t debounce_timer = xTimerCreate(  // Button debounce filter
    "DEBOUNCE",
    DEBOUNCE_TASK_MS,
    pdTRUE, // uxAutoReload
    NULL,   // pvTimerID
    debounce_task_cb
  );

  xTimerStart(debounce_timer, 0);
#endif // BOARD_STM32F429I_DISC1

#ifdef USE_CRON
  cron_init();
#endif
}


#if USE_AUDIO

extern SynthState g_audio_synth;
extern SampleDevice *g_dev_audio;

TaskHandle_t g_audio_synth_task;


static void audio_synth_task(void *ctx) {
  while(1) {
    // Woken by notification from DMA ISR callbacks
    ulTaskNotifyTake(/*xClearCountOnExit*/ pdTRUE, portMAX_DELAY);
    sdev_sample_out(g_dev_audio, g_audio_synth.next_buf);
  }
}



void audio_tasks_init(void) {
  xTaskCreate(audio_synth_task, "synth", STACK_BYTES(1024*2),
              NULL, TASK_PRIO_HIGH, &g_audio_synth_task);
}

#endif // USE_AUDIO
