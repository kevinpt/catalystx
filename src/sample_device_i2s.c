#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "lib_cfg/build_config.h"
#include "lib_cfg/cstone_cfg_stm32.h"
#include "build_info.h"
#include "cstone/platform.h"

#include "stm32f4xx_hal.h"

#include "sample_device.h"
#include "sample_device_i2s.h"

#include "cstone/iqueue_int16_t.h"
#include "audio_synth.h"



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


void sdev_init_i2s(SampleDeviceI2S *sdev, SampleDeviceCfg *cfg, void *ctx, I2S_HandleTypeDef *hi2s) {
  sdev_init((SampleDevice *)sdev, cfg, ctx);
//  memcpy(&sdev->base.cfg, cfg, sizeof *cfg);
//  sdev->base.ctx = ctx;
  sdev->hi2s = hi2s;
}

