#ifndef SAMPLE_DEVICE_I2S_H
#define SAMPLE_DEVICE_I2S_H


typedef struct {
  SampleDevice base;
  SPI_TypeDef *SPI_periph;
} SampleDeviceI2S;


#ifdef __cplusplus
extern "C" {
#endif

void sdev_init_i2s(SampleDeviceI2S *sdev, SampleDeviceCfg *cfg, void *ctx, SPI_TypeDef *SPI_periph);

unsigned i2s_synth_out(SampleDevice *sdev, int16_t *buf, unsigned buf_count);

#ifdef __cplusplus
}
#endif

#endif // SAMPLE_DEVICE_I2S_H
