#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include "lib_cfg/build_config.h"
#include "cstone/platform.h"

#ifdef PLATFORM_EMBEDDED
#  include "stm32f4xx_hal.h"
//#  include "stm32f4xx_ll_dma.h"
//#  include "stm32f4xx_ll_spi.h"

#  include "FreeRTOS.h"
#  include "semphr.h"
#  include "timers.h"
#endif

#include "cstone/prop_id.h"
#include "cstone/iqueue_int16_t.h"
#include "cstone/umsg.h"
#include "cstone/rtos.h"
#include "cstone/led_blink.h"
#include "cstone/debug.h"

#include "util/dhash.h"

#include "app_main.h"
#include "app_tasks.h"
#ifdef PLATFORM_EMBEDDED
#  include "app_gpio.h"
#  include "debounce.h"
#endif

#if defined PLATFORM_EMBEDDED && USE_AUDIO
#  include "audio_synth.h"
#  include "sample_device.h"
//#  include "i2s.h"
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
    UMsg msg = { .id = P_EVENT_BUTTON__USER_PRESS,
      .source = P_RSRC_HW_LOCAL_TASK };
    umsg_hub_send(&g_msg_hub, &msg, 1);
  }

  if(debouncer_falling_edge(&s_user_button)) {
    UMsg msg = { .id = P_EVENT_BUTTON__USER_RELEASE,
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
#endif // PLATFORM_EMBEDDED
}


#if USE_AUDIO

extern SynthState g_audio_synth;
extern SampleDevice *g_dev_audio;
#  ifdef USE_HAL_I2S
extern DMA_HandleTypeDef g_dma;
extern I2S_HandleTypeDef g_i2s;
#  endif


TaskHandle_t g_audio_synth_task;



#  ifdef USE_HAL_I2S
static BaseType_t high_prio_task;

void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s) {
  set_led(LED_STATUS, 1);

  // Fill low half of DMA buffer
  g_audio_synth.next_buf = g_dev_audio->cfg.dma_buf_low;
  vTaskNotifyGiveFromISR(g_audio_synth_task, &high_prio_task);
  // FIXME: Need to call portYIELD_FROM_ISR(high_prio_task);
}


void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s) {
  set_led(LED_STATUS, 0);

  // Fill high half of DMA buffer
  g_audio_synth.next_buf = g_dev_audio->cfg.dma_buf_high;
  vTaskNotifyGiveFromISR(g_audio_synth_task, &high_prio_task);
  // FIXME: Need to call portYIELD_FROM_ISR(high_prio_task);
}
#  endif

static void audio_synth_task(void *ctx) {
  while(1) {
    // Woken by notification from DMA ISR callbacks
    ulTaskNotifyTake(/*xClearCountOnExit*/ pdTRUE, portMAX_DELAY);
    sdev_sample_out(g_dev_audio, g_audio_synth.next_buf);
  }
}



void audio_tasks_init(void) {
#ifdef USE_AUDIO_I2S
#  ifdef USE_HAL_I2S
  __HAL_RCC_DMA1_CLK_ENABLE();
  HAL_NVIC_SetPriority(DMA1_Stream4_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);

  if(HAL_DMA_Init(&g_dma) != HAL_OK) {
    DPUTS("DMA init error");
  }
  __HAL_LINKDMA(&g_i2s, hdmatx, g_dma);
  __HAL_DMA_ENABLE_IT(&g_dma, DMA_IT_TC);
  __HAL_DMA_ENABLE_IT(&g_dma, DMA_IT_HT);

  HAL_I2S_Transmit_DMA(&g_i2s, (uint16_t *)g_dev_audio->cfg.dma_buf_low,
                        g_dev_audio->cfg.half_buf_samples*4);
  HAL_I2S_DMAPause(&g_i2s);
//  gpio_highz_off(&g_i2s_sd_mode, false); // Shutdown mode on

#  else // LL I2S
/*
  LL_DMA_InitTypeDef dma_cfg = {
    .PeriphOrM2MSrcAddress  = (uint32_t)&SPI2->DR,
    .MemoryOrM2MDstAddress  = (uint32_t)g_audio_buf,
    .Direction              = LL_DMA_DIRECTION_MEMORY_TO_PERIPH,
    .Mode                   = LL_DMA_MODE_CIRCULAR,
    .PeriphOrM2MSrcIncMode  = LL_DMA_PERIPH_NOINCREMENT,
    .MemoryOrM2MDstIncMode  = LL_DMA_MEMORY_INCREMENT,
    .PeriphOrM2MSrcDataSize = LL_DMA_PDATAALIGN_HALFWORD,
    .MemoryOrM2MDstDataSize = LL_DMA_MDATAALIGN_HALFWORD,
    .NbData                 = COUNT_OF(g_audio_buf),
    .Channel                = LL_DMA_CHANNEL_0, // S4_C0 = SPI2_TX  (RM0090 Table 42)
    .Priority               = LL_DMA_PRIORITY_MEDIUM,
    .FIFOMode               = LL_DMA_FIFOMODE_DISABLE,
    .FIFOThreshold          = LL_DMA_FIFOTHRESHOLD_1_4,
    .MemBurst               = LL_DMA_MBURST_SINGLE,
    .PeriphBurst            = LL_DMA_PBURST_SINGLE
  };

  //  LL_DMA_ClearFlag_TE4(DMA1);
  LL_DMA_Init(DMA1, LL_DMA_STREAM_4, &dma_cfg);
  LL_DMA_EnableStream(DMA1, LL_DMA_STREAM_4);

  LL_SPI_EnableDMAReq_TX(SPI2);
*/
#  endif
#endif


  xTaskCreate(audio_synth_task, "synth", STACK_BYTES(1024*2),
              NULL, TASK_PRIO_HIGH, &g_audio_synth_task);
}

#endif // USE_AUDIO
