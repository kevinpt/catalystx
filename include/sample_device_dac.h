#ifndef SAMPLE_DEVICE_DAC_H
#define SAMPLE_DEVICE_DAC_H


// FIXME Relocate defs
// Timer for DAC (Must support DMA)
#define DAC_TIMER                   TIM6
#define DAC_TIMER_CLK_ENABLE        __HAL_RCC_TIM6_CLK_ENABLE
#define DAC_TIMER_IRQ               TIM6_DAC_IRQn

#define DAC_TIMER_CLOCK_HZ          48000

// Unsigned DAC has offset for mid-scale zero position
#define DAC_SAMPLE_ZERO             0x8000

typedef struct {
  SampleDevice base;

//  DMA_TypeDef *DMA_periph;
//  uint32_t     DMA_stream;
  DAC_TypeDef *DAC_periph;
  uint32_t     DAC_channel;
} SampleDeviceDAC;


#ifdef __cplusplus
extern "C" {
#endif

void sdev_init_dac(SampleDeviceDAC *sdev, SampleDeviceCfg *cfg, void *ctx,
                    DAC_TypeDef *DAC_periph, uint32_t DAC_channel);


unsigned dac_synth_out(SampleDevice *sdev, int16_t *buf, unsigned buf_count);

#ifdef __cplusplus
}
#endif

#endif // SAMPLE_DEVICE_DAC_H
