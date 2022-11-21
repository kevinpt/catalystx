#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "lib_cfg/build_config.h"
#include "lib_cfg/cstone_cfg_stm32.h"
#include "build_info.h"
#include "cstone/platform.h"
#include "cstone/debug.h"

#include "stm32f4xx_hal.h"

#include "sample_device.h"
#include "sample_device_i2s.h"

#include "cstone/iqueue_int16_t.h"
#include "audio_synth.h"



#if 0
unsigned i2s_synth_out(SampleDevice *sdev, int16_t *buf, unsigned buf_count) {
  SynthState *audio_synth = (SynthState *)sdev->ctx;

  synth_gen_samples(audio_synth, buf_count);

  // FIXME: Handle split queue like DAC code
  int16_t *samples;
  size_t sample_count = iqueue_peek__int16_t(audio_synth->queue, &samples);

  if(sample_count < buf_count) // Not good
    buf_count = sample_count;  // We'll just leave previous garbage in buffer
  sample_count = buf_count;

  int16_t *buf_pos = buf;
  while(sample_count) {
    *buf_pos++ = *samples;
    *buf_pos++ = *samples++;
    sample_count--;
  }

  iqueue_discard__int16_t(audio_synth->queue, buf_count);
  return buf_count;
}
#else
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


  if(read_total < buf_count) {
    // FIXME: Replace with memset
    while(read_total < buf_count) { // Fill remainder of buffer with 0's
      *buf_pos++ = 0;
      *buf_pos++ = 0;
      read_total++;
    }

    // FIXME: Remove
/*    buf_pos -= 4;
    for(int i = 0; i < 4; i++) { // Fill remainder of buffer with 0's
      *buf_pos++ = 0;
    }*/
/*
    buf_pos = &buf[1];
    for(int16_t i = 0; i < 32; i++) {
      *buf_pos++ = (DAC_SAMPLE_ZERO / 4) + (i << 10);
    }
*/

  }

  return read_total;
}

#endif

//extern I2S_HandleTypeDef g_i2s;

static void sdev_enable_i2s(SampleDevice *sdev, bool enable) {
  SampleDeviceI2S *i2s = (SampleDeviceI2S *)sdev;

  if(enable) {
    // FIXME: Convert to I2S
//    if(!LL_DAC_IsDMAReqEnabled(dac->DAC_periph, dac->DAC_channel)) {
//      // Fill buffer with new samples to reduce output delay
//      dac_synth_out(sdev, sdev->cfg.dma_buf_low, sdev->cfg.half_buf_samples*2);
//    }

//    LL_DMA_EnableStream(sdev->cfg.DMA_periph, sdev->cfg.DMA_stream);
//    LL_DAC_EnableDMAReq(dac->DAC_periph, dac->DAC_channel);
    HAL_I2S_DMAResume(i2s->hi2s);
  } else {
//    LL_DAC_DisableDMAReq(dac->DAC_periph, dac->DAC_channel);
//    LL_DMA_DisableStream(sdev->cfg.DMA_periph, sdev->cfg.DMA_stream);
    HAL_I2S_DMAPause(i2s->hi2s);
  }
}

void sdev_init_i2s(SampleDeviceI2S *sdev, SampleDeviceCfg *cfg, void *ctx, I2S_HandleTypeDef *hi2s) {
  memset(sdev, 0, sizeof *sdev);
  sdev_init((SampleDevice *)sdev, cfg, ctx);
  sdev->base.cfg.enable = sdev_enable_i2s;
  sdev->hi2s = hi2s;
}

