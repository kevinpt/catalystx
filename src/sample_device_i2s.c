#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "lib_cfg/build_config.h"
#include "lib_cfg/cstone_cfg_stm32.h"
#include "build_info.h"
#include "app_main.h" // FIXME: Remove
#include "cstone/platform.h"
#include "cstone/debug.h"

#ifdef USE_HAL_I2S
#include "stm32f4xx_hal.h"
#else
#include "stm32f4xx_ll_spi.h"
#include "stm32f4xx_ll_dma.h"
#endif

#include "sample_device.h"
#include "sample_device_i2s.h"

#include "cstone/iqueue_int16_t.h"
#include "audio_synth.h"



unsigned i2s_synth_out(SampleDevice *sdev, int16_t *buf, unsigned buf_count) {
  SynthState *audio_synth = (SynthState *)sdev->ctx;

  size_t q_count = synth_gen_samples(audio_synth, buf_count);
  if(audio_synth->voice_state == VOICES_IDLE) {
/*
The synth has no active voices so we can disable the DMA to save on processor load.
We need to fill the DMA buffer with 0-samples before disabling DMA. This will ensure
we don't get garbage playback or speaker pops when something is sequenced wrong. At
this point all voices are idle so this invocation will generate a half buffer of 0's.
We need to keep DMA running for one more cycle to generate the second half. Then we
can invoke the SDEV_OP_SHUTDOWN_END command to terminate DMA after our first half
buffer of 0's has been partially sent.

Note that when the DMA is disabled by SDEV_OP_SHUTDOWN_END it will trigger a TC
interrupt one last time to indicate end of transfer. That will cause a third
invocation of this function.
*/
    switch(sdev->state) {
    case SDEV_ACTIVE:
      DPRINT("ACTIVE-> SHUTDOWN");
      sdev_ctl(sdev, SDEV_OP_DEACTIVATE, NULL, 0);
      // Continue to 0-fill first half of buffer
      break;
    case SDEV_SHUTDOWN:
      DPRINT("SHUTDOWN -> END\tq_count: %u", (unsigned)q_count);
      sdev_ctl(sdev, SDEV_OP_SHUTDOWN_END, NULL, 0);
      // Continue to 0-fill second half of buffer
      break;
    case SDEV_INACTIVE:
      // Third invocation caused by DMA end of transfer
      return 0;
      break;
    default:
      break;
    }  
  }

  int16_t *samples;
  size_t sample_count = iqueue_peek__int16_t(audio_synth->queue, &samples);
  bool peek_twice = sample_count < buf_count;
  if(sample_count > buf_count)
    sample_count = buf_count;

  int16_t *buf_pos = buf;
  size_t read_total = 0;
  while(read_total < sample_count) { // Double up samples to generate left/right pairs for I2S
    *buf_pos++ = *samples;
    *buf_pos++ = *samples++;
    read_total++;
  }

  iqueue_discard__int16_t(audio_synth->queue, sample_count);


  if(peek_twice) {  // Wraparound in queue requires another peek
    sample_count = iqueue_peek__int16_t(audio_synth->queue, &samples);
    if(sample_count > (buf_count - read_total))
      sample_count = buf_count - read_total;

    size_t end_count = read_total + sample_count;
    while(read_total < end_count) { // Double up samples to generate left/right pairs for I2S
      *buf_pos++ = *samples;
      *buf_pos++ = *samples++;
      read_total++;
    }
    iqueue_discard__int16_t(audio_synth->queue, sample_count);
  }


  if(read_total < buf_count) {  // Fill remainder of buffer with 0's
#if 0
    while(read_total < buf_count) { // Fill remainder of buffer with 0's
      *buf_pos++ = 0;
      *buf_pos++ = 0;
      read_total++;
    }
#else
    memset(buf_pos, 0, (buf_count - read_total) * sizeof *buf);
#endif
  }

  return read_total;
}




#ifdef USE_HAL_I2S
static void sdev_enable_i2s(SampleDevice *sdev, bool enable) {
  SampleDeviceI2S *i2s = (SampleDeviceI2S *)sdev;

  if(enable) {
    // FIXME: Convert to I2S
//    if(!LL_DAC_IsDMAReqEnabled(dac->DAC_periph, dac->DAC_channel)) {
//      // Fill buffer with new samples to reduce output delay
//      dac_synth_out(sdev, sdev->cfg.dma_buf_low, sdev->cfg.half_buf_samples*2);
//    }

    HAL_I2S_DMAResume(i2s->hi2s);
  } else {
    HAL_I2S_DMAPause(i2s->hi2s);
  }
}



void sdev_init_i2s(SampleDeviceI2S *sdev, SampleDeviceCfg *cfg, void *ctx, I2S_HandleTypeDef *hi2s) {
  memset(sdev, 0, sizeof *sdev);
  sdev_init((SampleDevice *)sdev, cfg, ctx);
  sdev->base.cfg.enable = sdev_enable_i2s;
  sdev->hi2s = hi2s;
}
#else

static void sdev_enable_i2s(SampleDevice *sdev, bool enable) {
  SampleDeviceI2S *i2s = (SampleDeviceI2S *)sdev;

  if(enable) {
    if(!LL_I2S_IsEnabledDMAReq_TX(i2s->SPI_periph)) {
      // Fill buffer with new samples to reduce output delay
#if 1
      i2s_synth_out(sdev, sdev->cfg.dma_buf_low, sdev->cfg.half_buf_samples*2);
#else
      memset(sdev->cfg.dma_buf_low, 0, sdev->cfg.half_buf_samples * 4 * sizeof(int16_t));
      //memset(sdev->cfg.dma_buf_low, 0, sdev->cfg.half_buf_samples * 2 * sizeof(int16_t));
      //i2s_synth_out(sdev, sdev->cfg.dma_buf_high, sdev->cfg.half_buf_samples);
#endif
    }

    LL_DMA_EnableStream(sdev->cfg.DMA_periph, sdev->cfg.DMA_stream);
    LL_I2S_EnableDMAReq_TX(i2s->SPI_periph);

  } else {
    LL_I2S_DisableDMAReq_TX(i2s->SPI_periph);
    LL_DMA_DisableStream(sdev->cfg.DMA_periph, sdev->cfg.DMA_stream);
  }
}


void sdev_init_i2s(SampleDeviceI2S *sdev, SampleDeviceCfg *cfg, void *ctx, SPI_TypeDef *SPI_periph) {
  memset(sdev, 0, sizeof *sdev);
  sdev_init((SampleDevice *)sdev, cfg, ctx);
  sdev->base.cfg.enable = sdev_enable_i2s;
  sdev->SPI_periph = SPI_periph;
}
#endif

