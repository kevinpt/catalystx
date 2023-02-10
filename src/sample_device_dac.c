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
#include "stm32f4xx_ll_dac.h"
#include "stm32f4xx_ll_dma.h"


#include "sample_device.h"
#include "sample_device_dac.h"

#include "cstone/iqueue_int16_t.h"
#include "audio_synth.h"



unsigned dac_synth_out(SampleDevice *sdev, int16_t *buf, unsigned buf_count) {
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
buffer of 0's has been partially sent to the DAC so it is idle at mid-scale.

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

  // Convert signed 16-bit samples to unsigned 16-bit. DAC will convert them to 12-bit.
  int16_t *buf_pos = buf;
  size_t read_total = 0;
  while(read_total < sample_count) {
    int32_t offset_sample = DAC_SAMPLE_ZERO + (int32_t)*samples++;
    *buf_pos++ = (uint16_t)offset_sample;
    read_total++;
  }

  iqueue_discard__int16_t(audio_synth->queue, sample_count);


  if(peek_twice) {
    sample_count = iqueue_peek__int16_t(audio_synth->queue, &samples);
    if(sample_count > (buf_count - read_total))
      sample_count = buf_count - read_total;

    size_t end_count = read_total + sample_count;
    while(read_total < end_count) {
      int32_t offset_sample = DAC_SAMPLE_ZERO + (int32_t)*samples++;
      *buf_pos++ = (uint16_t)offset_sample;
      read_total++;
    }
    iqueue_discard__int16_t(audio_synth->queue, sample_count);
  }


  if(read_total < buf_count) {
    while(read_total < buf_count) { // Fill remainder of buffer with 0's
      *buf_pos++ = DAC_SAMPLE_ZERO;
      read_total++;
    }
  }

  return read_total;
}


static void sdev_enable_dac(SampleDevice *sdev, bool enable) {
  SampleDeviceDAC *dac = (SampleDeviceDAC *)sdev;

  if(enable) {
    if(!LL_DAC_IsDMAReqEnabled(dac->DAC_periph, dac->DAC_channel)) {
      // Fill buffer with new samples to reduce output delay
      dac_synth_out(sdev, sdev->cfg.dma_buf_low, sdev->cfg.half_buf_samples*2);
    }

    LL_DMA_EnableStream(sdev->cfg.DMA_periph, sdev->cfg.DMA_stream);
    LL_DAC_EnableDMAReq(dac->DAC_periph, dac->DAC_channel);
  } else {
    LL_DAC_DisableDMAReq(dac->DAC_periph, dac->DAC_channel);
    LL_DMA_DisableStream(sdev->cfg.DMA_periph, sdev->cfg.DMA_stream);
  }
}


void sdev_init_dac(SampleDeviceDAC *sdev, SampleDeviceCfg *cfg, void *ctx,
                    DAC_TypeDef *DAC_periph, uint32_t DAC_channel) {

  memset(sdev, 0, sizeof *sdev);
  sdev_init((SampleDevice *)sdev, cfg, ctx);
  sdev->base.cfg.enable = sdev_enable_dac;
  sdev->DAC_periph = DAC_periph;
  sdev->DAC_channel = DAC_channel;
}

