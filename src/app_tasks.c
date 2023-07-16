#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include "lib_cfg/build_config.h"
#include "cstone/platform.h"
#include "app_main.h"

#ifdef PLATFORM_EMBEDDED
#  include "stm32f4xx_hal.h"
#  include "stm32f4xx_ll_tim.h"
#  include "stm32f4xx_ll_exti.h"

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
#include "cstone/sequence_events.h"
#ifdef USE_CRON
#  include "cstone/prop_db.h"
#  include "cstone/cron_events.h"
#endif


#include "app_tasks.h"
#ifdef PLATFORM_EMBEDDED
#  include "app_gpio.h"
#  include "debounce.h"
#endif

#if USE_AUDIO
#  include "sample_device.h"
#  include "audio_synth.h"
#endif

#if USE_LVGL
#  include "lvgl/lvgl.h"
#endif


#include "app_prop_id.h"
#include "buzzer.h"

#ifndef COUNT_OF
#  define COUNT_OF(a) (sizeof(a) / sizeof(*(a)))
#endif



extern UMsgHub g_msg_hub;

#if defined BOARD_STM32F429I_DISC1 || defined BOARD_STM32F429N_EVAL
// TASK: Create a debounce filter and button event manager
static Debouncer s_user_button;

static void debounce_task_cb(TimerHandle_t timer) {
  debouncer_filter_sample(&s_user_button, gpio_value(&g_button_select));

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


// TASK: LVGL
#if USE_LVGL
static void lvgl_task_cb(void *ctx) {
  lv_timer_handler();
}

#  ifdef PLATFORM_HOSTED
#    define LVGL_TICK_TASK_MS  5
// TIMER TASK: LVGL sys tick
void lvgl_tick_task(TimerHandle_t timer) {
  lv_tick_inc(LVGL_TICK_TASK_MS);
}
#  endif // PLATFORM_HOSTED
#endif // USE_LVGL



void app_tasks_init(void) {

#if defined BOARD_STM32F429I_DISC1 || defined BOARD_STM32F429N_EVAL
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


#if USE_LVGL
void gui_tasks_init(void) {

  static PeriodicTaskCfg lvgl_task_cfg = { // LVGL updates
    .task   = lvgl_task_cb,
    .ctx    = NULL,
    .period = LVGL_TASK_MS,
    .repeat = REPEAT_FOREVER
  };

  create_periodic_task("LVGL", STACK_BYTES(4096), TASK_PRIO_LOW, &lvgl_task_cfg);

#  ifdef PLATFORM_HOSTED
  TimerHandle_t lvgl_tick_timer = xTimerCreate(  // LVGL system tick
    "LVGLtick",
    LVGL_TICK_TASK_MS,
    pdTRUE, // uxAutoReload
    NULL,   // pvTimerID
    lvgl_tick_task
  );

  xTimerStart(lvgl_tick_timer, 0);
#  endif
}
#endif // USE_LVGL


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


#if USE_AUDIO

/*

** Buzzer generated on PC-8500:
           4kHz                              4kHz
____-_-_-_-_-_-_-_-_-_________________-_-_-_-_-_-_-_-_-____________________________

    |      60ms      |      60ms      |      60ms      |      60ms      |


    |<-- pin interrupt                |<-- pin interrupt

    |<-- Start timer; Disable pin interrupt
    |        65ms        |<-- Reenable pin interrupt
                                      |<-- Restart timer; Disable int.
                                      |         65ms         |<-- Reenable pin interrupt
                                      |                125ms               |<-- Counter timeout; End timer

** Event generation:

    | Beep 1
                                      | Beep 2
                                                                           | Beep end


The PC-8500 will beep at 4kHz for 1, 2, 4, or 5+ times. 1 is for normal UI feedback.
2, 4, and 5+ are for increasingly severe warnings/errors. Beeps are always 60ms on
followed by 60ms off.

We need to track beeps as they arrive and generate events so that acoustic sound
can be generated with minimal delay.

Algorithm:
  Wait for rising edge pin interrupt triggered by buzzer start. When the pin triggers,
  disable the pin interrupt and start the timer counting with a 125ms timeout. An
  output compare is set for 65ms to renable the pin interrupt after the initial 4kHz
  pulse train. If another edge happens before the 125ms count expires, we reinitialize
  the counter and disable the pin interrupt for 65ms as before. If the 125ms count
  expires, the buzzer has ended.

  The interrupt handlers for pin change and the timer send command bytes over a queue
  that buzzer_task() waits to receive from.
*/


static void process_beep(uint8_t beep_count) {
  switch(beep_count) {
  case 1:
    //sequence_start(P_APP_SEQUENCE_UI, 1);
    break;
  case 2:
    sequence_start(P_APP_SEQUENCE_WARN, 1);
    break;
  case 4:
    break;
  default:
    if(beep_count >= 5) {
    
    }
    break;
  }
}


TaskHandle_t g_buzzer_task;

// TASK: Receive commands from interrupt handlers managing buzzer input pin
static void buzzer_task(void *ctx) {
  uint8_t beep_count = 0;

  while(1) {
    // Woken by commands on queue sent by ISRs

    uint8_t cmd;
    if(xQueueReceive(g_buzzer_cmd_q, &cmd, pdMS_TO_TICKS(250)) == pdTRUE) {
      switch(cmd) {
      case BUZZ_CMD_PIN_CHANGE:
//        puts("Pin change");
        beep_count++;
        if(beep_count <= 5) // Ignore additional beeps after 5 have arrived
          process_beep(beep_count);
        break;
      case BUZZ_CMD_65MS_TIMEOUT:
        //puts("65ms");
        break;
      case BUZZ_CMD_125MS_TIMEOUT:
//        printf("Beep end: %d\n", beep_count);
        beep_count = 0;
        break;
      default:
        break;
      }

    } else { // Queue timeout
      // Guard against lost 125ms timeout command if queue ever overflows
      if(beep_count > 0)  // Queue has lost a command
        report_error(P_ERROR_BEEP_INFO_TIMEOUT, 0);

      beep_count = 0;
    }
  }
}
#endif // USE_AUDIO


#if 0
#define NOTE_G4  MIDI_NOTE(MIDI_G, 4)
#define NOTE_E5  MIDI_NOTE(MIDI_E, 5)
#define NOTE_C5  MIDI_NOTE(MIDI_C, 5)


#define NOTE_G5  MIDI_NOTE(MIDI_G, 5)
#define NOTE_E6  MIDI_NOTE(MIDI_E, 6)
#define NOTE_C6  MIDI_NOTE(MIDI_C, 6)

#define NOTE_Fs6 MIDI_NOTE(MIDI_Fs, 6)
#define NOTE_G6  MIDI_NOTE(MIDI_G, 6)
#define NOTE_As6 MIDI_NOTE(MIDI_As, 6)
#define NOTE_B6  MIDI_NOTE(MIDI_B, 6)


#define NOTE_C7  MIDI_NOTE(MIDI_C, 7)
#define NOTE_D7  MIDI_NOTE(MIDI_D, 7)
#define NOTE_E7  MIDI_NOTE(MIDI_E, 7)
#define NOTE_F7  MIDI_NOTE(MIDI_F, 7)
#define NOTE_G7  MIDI_NOTE(MIDI_G, 7)
#define NOTE_A7  MIDI_NOTE(MIDI_A, 7)
#define NOTE_B7  MIDI_NOTE(MIDI_B, 7)
#endif


#if USE_AUDIO

#define SONG_BPM    160
#define MS_PER_BEAT (1000ul * 60 / SONG_BPM)
#define QTR_NOTE    120
#define HALF_NOTE   (QTR_NOTE * 2)
#define WHOLE_NOTE  (QTR_NOTE * 4)


#define INST_PRESS(inst, note, delay, hold) \
  {{P_INSTRUMENT_n_PRESS_m   | P1_ARR(inst) | P3_ARR(note), (delay)}, \
   {P_INSTRUMENT_n_RELEASE_m | P1_ARR(inst) | P3_ARR(note), (hold)}}


// INST. PRESS, prev note delay, INST. RELEASE, press duration
static SequenceEventPair s_seq_data_ui[] = {
  INST_PRESS(INST_UI, NOTE_G4, 0,   100),
  INST_PRESS(INST_UI, NOTE_E5, 200, 100),
  INST_PRESS(INST_UI, NOTE_C5, 200, 100)
#if 0
  {{P_INSTRUMENT_n_PRESS_m | P1_ARR(INST_UI) | P3_ARR(NOTE_G5), 0},
      {P_INSTRUMENT_n_RELEASE_m | P1_ARR(INST_UI) | P3_ARR(NOTE_G5), 100}},
  {{P_INSTRUMENT_n_PRESS_m | P1_ARR(INST_UI) | P3_ARR(NOTE_E6), 200},
      {P_INSTRUMENT_n_RELEASE_m | P1_ARR(INST_UI) | P3_ARR(NOTE_E6), 100}},
  {{P_INSTRUMENT_n_PRESS_m | P1_ARR(INST_UI) | P3_ARR(NOTE_C6), 200},
      {P_INSTRUMENT_n_RELEASE_m | P1_ARR(INST_UI) | P3_ARR(NOTE_C6), 100}}
#endif
//  {{P_INSTRUMENT_n_PRESS_m | P1_ARR(1) | P3_ARR(NOTE_E3), 700}, {P_INSTRUMENT_n_RELEASE_m | P1_ARR(1) | P3_ARR(NOTE_E3), 1500}}
};
Sequence g_seq_ui;

static SequenceEventPair s_seq_data_warn[] = {
  INST_PRESS(INST_WARN, NOTE_C7, 0,   100),
  INST_PRESS(INST_WARN, NOTE_C7, 400, 100),
  INST_PRESS(INST_WARN, NOTE_C7, 1000, 100)
};
Sequence g_seq_warn;


static SequenceEventPair s_seq_data_error[] = {
#if 0
  INST_PRESS(INST_WARN, NOTE_B5,  0,   100),
  INST_PRESS(INST_WARN, NOTE_As5, 0,   100),
  INST_PRESS(INST_WARN, NOTE_G5,  150, 100)
  INST_PRESS(INST_WARN, NOTE_Fs5, 0,   100)
#endif
  INST_PRESS(INST_UI, NOTE_C7, 0, 200),
  INST_PRESS(INST_UI, NOTE_C6, 400, 200),
  INST_PRESS(INST_UI, NOTE_C5, 400, 200),
  INST_PRESS(INST_UI, NOTE_C4, 400, 200),
  INST_PRESS(INST_UI, NOTE_C3, 400, 200),
  INST_PRESS(INST_UI, NOTE_C2, 400, 200),
  INST_PRESS(INST_UI, NOTE_C1, 400, 200)
};
Sequence g_seq_error;




#define INST_TWINKLE  INST_ERROR
static SequenceEventPair s_seq_data_twinkle[] = {
  INST_PRESS(INST_TWINKLE, NOTE_C5, 0,           QTR_NOTE),
  INST_PRESS(INST_TWINKLE, NOTE_C5, MS_PER_BEAT, QTR_NOTE),
  INST_PRESS(INST_TWINKLE, NOTE_G5, MS_PER_BEAT, QTR_NOTE),
  INST_PRESS(INST_TWINKLE, NOTE_G5, MS_PER_BEAT, QTR_NOTE),

  INST_PRESS(INST_TWINKLE, NOTE_A5, MS_PER_BEAT, QTR_NOTE),
  INST_PRESS(INST_TWINKLE, NOTE_A5, MS_PER_BEAT, QTR_NOTE),
  INST_PRESS(INST_TWINKLE, NOTE_G5, MS_PER_BEAT, HALF_NOTE),

  INST_PRESS(INST_TWINKLE, NOTE_F5, 2*MS_PER_BEAT, QTR_NOTE),
  INST_PRESS(INST_TWINKLE, NOTE_F5, MS_PER_BEAT, QTR_NOTE),
  INST_PRESS(INST_TWINKLE, NOTE_E5, MS_PER_BEAT, QTR_NOTE),
  INST_PRESS(INST_TWINKLE, NOTE_E5, MS_PER_BEAT, QTR_NOTE),

  INST_PRESS(INST_TWINKLE, NOTE_D5, MS_PER_BEAT, QTR_NOTE),
  INST_PRESS(INST_TWINKLE, NOTE_D5, MS_PER_BEAT, QTR_NOTE),
  INST_PRESS(INST_TWINKLE, NOTE_C5, MS_PER_BEAT, HALF_NOTE)
};
Sequence g_seq_twinkle;


QueueHandle_t g_buzzer_cmd_q = 0;

void buzzer_task_init(void) {
  // Sequences for beep patterns
  if(sequence_init_pairs(&g_seq_ui, s_seq_data_ui, COUNT_OF(s_seq_data_ui), 1, NULL, P_APP_SEQUENCE_UI)) {
    sequence_dump(&g_seq_ui);
    sequence_add(&g_seq_ui);
  }

  if(sequence_init_pairs(&g_seq_warn, s_seq_data_warn, COUNT_OF(s_seq_data_warn),
                          1, NULL, P_APP_SEQUENCE_WARN)) {
//    sequence_dump(&g_seq_warn);
    sequence_add(&g_seq_warn);
  }

  if(sequence_init_pairs(&g_seq_error, s_seq_data_error, COUNT_OF(s_seq_data_error),
                          1, NULL, P_APP_SEQUENCE_ERROR)) {
    sequence_dump(&g_seq_error);
    sequence_add(&g_seq_error);
  }

  if(sequence_init_pairs(&g_seq_twinkle, s_seq_data_twinkle, COUNT_OF(s_seq_data_twinkle),
                          1, NULL, 0)) {
    sequence_dump(&g_seq_twinkle);
    sequence_add(&g_seq_twinkle);
  }


  // Queue for interrupt handlers to communicate with task
  g_buzzer_cmd_q = xQueueCreate(4, sizeof(uint8_t));

  xTaskCreate(buzzer_task, "buzz", STACK_BYTES(1024),
              NULL, TASK_PRIO_LOW, &g_buzzer_task);

}

#endif // USE_AUDIO
