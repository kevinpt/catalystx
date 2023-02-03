#ifndef SAMPLE_DEVICE_H
#define SAMPLE_DEVICE_H

typedef enum {
  SDEV_INACTIVE = 0,
  SDEV_ACTIVE,
  SDEV_SHUTDOWN
} SampleDevState;

#define SDEV_OP_ACTIVATE        0x01
#define SDEV_OP_DEACTIVATE      0x02
#define SDEV_OP_SHUTDOWN_END    0x03


typedef struct SampleDevice  SampleDevice;

typedef unsigned (*SampleDevOutput)(SampleDevice *sdev, int16_t *buf, unsigned buf_count);
typedef void (*SampleDevEnable)(SampleDevice *sdev, bool enable);


typedef struct {
  int16_t  *dma_buf_low;
  int16_t  *dma_buf_high;
  unsigned  half_buf_samples;
  uint8_t   channels;
  DMA_TypeDef *DMA_periph;
  uint32_t  DMA_stream;

  SampleDevOutput sample_out;
  SampleDevEnable enable;
} SampleDeviceCfg;


struct SampleDevice {
  SampleDeviceCfg cfg;
  SampleDevState state;

  void *ctx;
};


#ifdef __cplusplus
extern "C" {
#endif

void sdev_init(SampleDevice *sdev, SampleDeviceCfg *cfg, void *ctx);
unsigned sdev_sample_out(SampleDevice *sdev, int16_t *buf);
bool sdev_ctl(SampleDevice *sdev, int op, void *data, size_t data_len);

#ifdef __cplusplus
}
#endif

#endif // SAMPLE_DEVICE_H
