#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "lib_cfg/build_config.h"
#include "lib_cfg/cstone_cfg_stm32.h"
#include "build_info.h"
#include "cstone/platform.h"
#include "cstone/debug.h"
#include "cstone/iqueue_int16_t.h"
#include "sample_device.h"
#include "audio_synth.h"

#include "SDL.h"
#include "sample_device_sdl.h"


unsigned sdl_synth_out(SampleDevice *sdev, int16_t *buf, unsigned buf_count) {
  SynthState *audio_synth = (SynthState *)sdev->ctx;

  synth_gen_samples(audio_synth, buf_count);
  if(!update_sample_dev_state(sdev, audio_synth))
    return 0;


  int16_t *samples;
  size_t sample_count = iqueue_peek__int16_t(audio_synth->queue, &samples);
  bool peek_twice = sample_count < buf_count;
  if(sample_count > buf_count)
    sample_count = buf_count;

  int16_t *buf_pos = buf;
  size_t read_total = 0;
  while(read_total < sample_count) {
    *buf_pos++ = *samples++;
    read_total++;
  }

  iqueue_discard__int16_t(audio_synth->queue, sample_count);


  if(peek_twice) {  // Wraparound in queue requires another peek
    sample_count = iqueue_peek__int16_t(audio_synth->queue, &samples);
    if(sample_count > (buf_count - read_total))
      sample_count = buf_count - read_total;

    size_t end_count = read_total + sample_count;
    while(read_total < end_count) {
      *buf_pos++ = *samples++;
      read_total++;
    }
    iqueue_discard__int16_t(audio_synth->queue, sample_count);
  }


  if(read_total < buf_count) {  // Fill remainder of buffer with 0's
    memset(buf_pos, 0, (buf_count - read_total) * sizeof *buf);
  }

  return read_total;
}


static void sdev_enable_sdl(SampleDevice *sdev, bool enable) {
  SampleDeviceSDL *sdl = (SampleDeviceSDL *)sdev;
  SDL_PauseAudioDevice(sdl->SDL_dev, !enable);
}


static void sdl_dev_cb(void *userdata, uint8_t *stream, int len) {
  SampleDevice *sdev = (SampleDevice *)userdata;
  sdl_synth_out(sdev, (int16_t *)stream, len/2);
}


bool sdev_init_sdl(SampleDeviceSDL *sdev, SampleDeviceCfg *cfg, void *ctx) {
  memset(sdev, 0, sizeof *sdev);
  sdev_init((SampleDevice *)sdev, cfg, ctx);
  sdev->base.cfg.enable = sdev_enable_sdl;


  if(SDL_Init(SDL_INIT_AUDIO) < 0)
    return false;
  atexit(SDL_Quit);

  SDL_AudioSpec request_cfg = {
    .freq = 16000,
    .format = AUDIO_S16,
    .channels = 1,
    .samples = 1024,
    .callback = sdl_dev_cb,
    .userdata = sdev
  };

  sdev->SDL_dev = SDL_OpenAudioDevice(NULL, 0, &request_cfg, &sdev->cfg, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
  //printf("## SDL: freq=%d  channels=%d\n", sdev->cfg.freq, sdev->cfg.channels);
  return sdev->SDL_dev > 0;
}

