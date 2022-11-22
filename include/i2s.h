#ifndef I2S_H
#define I2S_H

#ifdef __cplusplus
extern "C" {
#endif

void i2s_io_init(void);
void i2s_hw_init(SampleDeviceI2S *sdev);

#ifdef __cplusplus
}
#endif

#endif // I2S_H

