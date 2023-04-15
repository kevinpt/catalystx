#ifndef SAMPLE_DEVICE_SDL_H
#define SAMPLE_DEVICE_SDL_H


typedef struct {
  SampleDevice base;
  SDL_AudioDeviceID SDL_dev;
  SDL_AudioSpec cfg;
} SampleDeviceSDL;


#ifdef __cplusplus
extern "C" {
#endif

bool sdev_init_sdl(SampleDeviceSDL *sdev, SampleDeviceCfg *cfg, void *ctx);

unsigned sdl_synth_out(SampleDevice *sdev, int16_t *buf, unsigned buf_count);

#ifdef __cplusplus
}
#endif

#endif // SAMPLE_DEVICE_SDL_H
