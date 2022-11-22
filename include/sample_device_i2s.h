#ifndef SAMPLE_DEVICE_I2S_H
#define SAMPLE_DEVICE_I2S_H


typedef struct {
  SampleDevice base;

#ifdef USE_HAL_I2S
  I2S_HandleTypeDef *hi2s;
#else
  SPI_TypeDef *SPI_periph;
#endif

} SampleDeviceI2S;


#ifdef __cplusplus
extern "C" {
#endif

#ifdef USE_HAL_I2S
void sdev_init_i2s(SampleDeviceI2S *sdev, SampleDeviceCfg *cfg, void *ctx, I2S_HandleTypeDef *hi2s);
#else
void sdev_init_i2s(SampleDeviceI2S *sdev, SampleDeviceCfg *cfg, void *ctx, SPI_TypeDef *SPI_periph);
#endif

unsigned i2s_synth_out(SampleDevice *sdev, int16_t *buf, unsigned buf_count);

#ifdef __cplusplus
}
#endif

#endif // SAMPLE_DEVICE_I2S_H
