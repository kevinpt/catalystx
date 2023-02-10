#include <stdint.h>
#include <stdbool.h>

#include "lib_cfg/build_config.h"
#include "lib_cfg/cstone_cfg_stm32.h"
#include "app_main.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_ll_tim.h"
#include "stm32f4xx_ll_dac.h"
#include "stm32f4xx_ll_spi.h"
#include "stm32f4xx_ll_dma.h"

#include "FreeRTOS.h"

#include "cstone/io/uart.h"
#include "cstone/io/usb.h"
#include "cstone/core_stm32.h"
#include "sample_device.h"
#ifdef USE_AUDIO_DAC
#  include "sample_device_dac.h"
#  include "dac.h"
#endif
#ifdef USE_AUDIO_I2S
#  include "sample_device_i2s.h"
#  include "i2s.h"
#endif
#include "app_stm32.h"
#include "app_gpio.h"


void uart_io_init(void) {
  gpio_enable_port(GPIO_PORT_A);

  // Configure GPIO
  GPIO_InitTypeDef uart_pin_cfg;

  // TX
  uart_pin_cfg.Pin        = GPIO_PIN_9;
  uart_pin_cfg.Mode       = GPIO_MODE_AF_PP;
  uart_pin_cfg.Alternate  = GPIO_AF7_USART1;
  uart_pin_cfg.Speed      = GPIO_SPEED_FREQ_LOW;
  uart_pin_cfg.Pull       = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &uart_pin_cfg);

  // RX
  uart_pin_cfg.Pin        = GPIO_PIN_10;
  uart_pin_cfg.Mode       = GPIO_MODE_AF_OD;
  HAL_GPIO_Init(GPIOA, &uart_pin_cfg);
}


#if defined BOARD_STM32F429I_DISC1
void system_clock_init(void) {
  RCC_OscInitTypeDef osc_init;
  RCC_ClkInitTypeDef clk_init;

  __HAL_RCC_PWR_CLK_ENABLE();

  // Enable voltage scaling at lower clock rates
  // VDD 2.7V - 3.6V
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

/*
        Clock diagram in RM0090 Figure 16  p152
                                                   : :        .--------.
  HSE              .---------------------.         | |------->| AHB PS |---> HCLK  180MHz max
  8MHz   .----.   |   .----.     .----.  | PLLCLK  | / SYSCLK '--------'
--|[]|-->| /M |---|-->| *N |--+->| /P |--|-------->|/            |   .---------.
   X3    '----'   |   '----'  |  :----:  |                       +-->| APB1 PS |--> 45MHz max
                  |           '->| /Q |--|---> PLL48CK (48MHz)   |   :---------:
                  | PLL          '----'  |                       '-->| APB2 PS |--> 90MHz max
                  '----------------------'                           '---------'
*/


  // Use PLL driven by HSE
  osc_init.OscillatorType = RCC_OSCILLATORTYPE_HSE; // 8MHz Xtal
  osc_init.HSEState       = RCC_HSE_ON;
  osc_init.PLL.PLLState   = RCC_PLL_ON;
  osc_init.PLL.PLLSource  = RCC_PLLSOURCE_HSE;

#ifndef USE_USB // 180MHz Sysclk
  osc_init.PLL.PLLM       = 8;   // Div factor (2 - 63)
  osc_init.PLL.PLLN       = 360; // Mul factor (50 - 432)
  osc_init.PLL.PLLP       = RCC_PLLP_DIV2; // Sysclk div factor (2,4,6,8)
  osc_init.PLL.PLLQ       = 8;   // Div factor (2 - 15) for OTG FS, SDIO, and RNG (48MHz for USB)
  // 8MHz * 360 / 8 / 2 --> 180MHz  Sysclk
  // 8MHz * 360 / 8 / 8 --> 45MHz

  // AHB = HCLK = Sysclk/1 = 180MHz  (180MHz max)
  // APB1 = AHB/4 = 45MHz (45MHz max)
  // APB2 = AHB/2 = 90MHz (90MHz max)
  // SysTick = AHB = 180MHz

#else // PLL48CK must be 48MHz so Sysclk limited to 168MHz
  osc_init.PLL.PLLM       = 8;   // Div factor (2 - 63)
  osc_init.PLL.PLLN       = 336; // Mul factor (50 - 432)
  osc_init.PLL.PLLP       = RCC_PLLP_DIV2; // Sysclk div factor (2,4,6,8)
  osc_init.PLL.PLLQ       = 7;   // Div factor (2 - 15) for OTG FS, SDIO, and RNG (48MHz for USB)
  // 8MHz * 336 / 8 / 2 --> 168MHz  Sysclk
  // 8MHz * 336 / 8 / 7 --> 48MHz

  // AHB = HCLK = Sysclk/1 = 168MHz  (180MHz max)
  // APB1 = AHB/4 = 42MHz (45MHz max)
  // APB2 = AHB/2 = 84MHz (90MHz max)
  // SysTick = AHB = 168MHz
#endif

  if(HAL_RCC_OscConfig(&osc_init) != HAL_OK)
    fatal_error();

  // Set internal voltage reg. to allow higher clock rates (required to achieve 180MHz)
  // Stop and Standby modes no longer available
  HAL_PWREx_EnableOverDrive();
 
  // Use PLL as Sysclk and set division ratios for derived clocks
  clk_init.ClockType      = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                             RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
  clk_init.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  clk_init.AHBCLKDivider  = RCC_SYSCLK_DIV1;  // HCLK == Sysclk
  clk_init.APB1CLKDivider = RCC_HCLK_DIV4;
  clk_init.APB2CLKDivider = RCC_HCLK_DIV2;

  // NOTE: Latency selected from Table 11 in RM0090
  if(HAL_RCC_ClockConfig(&clk_init, FLASH_LATENCY_5) != HAL_OK)
    fatal_error();
}

#elif defined BOARD_STM32F401_BLACK_PILL
void system_clock_init(void) {
  RCC_OscInitTypeDef osc_init;
  RCC_ClkInitTypeDef clk_init;

  __HAL_RCC_PWR_CLK_ENABLE();

  // Enable voltage scaling at lower clock rates
  // VDD 2.7V - 3.6V
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

/*
        Clock diagram in RM0368 Figure 12  p94
                                                   : :        .--------.
  HSE              .---------------------.         | |------->| AHB PS |---> HCLK  84MHz max
  25MHz  .----.   |   .----.     .----.  | PLLCLK  | / SYSCLK '--------'
--|[]|-->| /M |---|-->| *N |--+->| /P |--|-------->|/            |   .---------.
   Y2    '----'   |   '----'  |  :----:  |                       +-->| APB1 PS |--> 42MHz max
                  |           '->| /Q |--|---> PLL48CK (48MHz)   |   :---------:
                  | PLL          '----'  |                       '-->| APB2 PS |--> 84MHz max
                  '----------------------'                           '---------'
*/


  // Use PLL driven by HSE
  osc_init.OscillatorType = RCC_OSCILLATORTYPE_HSE; // 25MHz Xtal
  osc_init.HSEState       = RCC_HSE_ON;
  osc_init.PLL.PLLState   = RCC_PLL_ON;
  osc_init.PLL.PLLSource  = RCC_PLLSOURCE_HSE;

  osc_init.PLL.PLLM       = 25;   // Div factor (2 - 63)
  osc_init.PLL.PLLN       = 336; // Mul factor (192 - 432)
  osc_init.PLL.PLLP       = RCC_PLLP_DIV4; // Sysclk div factor (2,4,6,8)
  osc_init.PLL.PLLQ       = 7;   // Div factor (2 - 15) for OTG FS, SDIO, and RNG (48MHz for USB)
  // 25 / 25 * 336 / 4 --> 84 MHz Sysclk
  // 25 / 25 * 336 / 7 --> 48 MHz PLL48CK

  if(HAL_RCC_OscConfig(&osc_init) != HAL_OK)
    fatal_error();


  // Use PLL as Sysclk and set division ratios for derived clocks
  clk_init.ClockType      = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                             RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
  clk_init.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  clk_init.AHBCLKDivider  = RCC_SYSCLK_DIV1;  // HCLK == Sysclk
  clk_init.APB1CLKDivider = RCC_HCLK_DIV2;  // 42 MHz
  clk_init.APB2CLKDivider = RCC_HCLK_DIV1;  // 84 MHz

  // NOTE: Latency selected from Table 6 in RM0368
  if(HAL_RCC_ClockConfig(&clk_init, FLASH_LATENCY_2) != HAL_OK) // 3.3V @ 84MHz
    fatal_error();

}

#else
#  error "Unknown target system"
#endif

#if USE_TINYUSB
void usb_io_init(void) {
/*
USB OTG_HS port:
  ID    B12
  VBUS  B13
  DM    B14
  DP    B15

PSO     C4  Power switch on (active low) to U8; For use as host
OC      C5  Overcurrent input (active low) from U8
*/

  // Configure GPIO
  GPIO_InitTypeDef usb_pin_cfg;

  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  // OTG DP, DM
  usb_pin_cfg.Pin        = GPIO_PIN_14 | GPIO_PIN_15;
  usb_pin_cfg.Mode       = GPIO_MODE_AF_PP;
  usb_pin_cfg.Alternate  = GPIO_AF12_OTG_HS_FS;  // Running as OTG_FS on AF12
  usb_pin_cfg.Speed      = GPIO_SPEED_FREQ_HIGH;
  usb_pin_cfg.Pull       = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &usb_pin_cfg);

  // VBUS
  usb_pin_cfg.Pin        = GPIO_PIN_13;
  usb_pin_cfg.Mode       = GPIO_MODE_INPUT; // An "Additional function" pin set to input
  HAL_GPIO_Init(GPIOB, &usb_pin_cfg);
//HAL_StatusTypeDef USB_CoreInit(USB_OTG_GlobalTypeDef *USBx, USB_OTG_CfgTypeDef cfg)
//HAL_StatusTypeDef USB_DevInit(USB_OTG_GlobalTypeDef *USBx, USB_OTG_CfgTypeDef cfg);
//USB_DevInit

//  USB_OTG_FS->GCCFG |= USB_OTG_GCCFG_NOVBUSSENS;
//  USB_OTG_FS->GCCFG |= USB_OTG_GCCFG_VBUSBSEN;

//  USB_OTG_HS->GCCFG |= USB_OTG_GCCFG_VBUSBSEN;


  // ID
  usb_pin_cfg.Pin        = GPIO_PIN_12;
  usb_pin_cfg.Mode       = GPIO_MODE_AF_OD;
  usb_pin_cfg.Pull       = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &usb_pin_cfg);

  // Limited to USB FS when using internal PHY
  __HAL_RCC_USB_OTG_HS_CLK_ENABLE();

  // Per RM0090 p189: OTGHSULPILPEN must be disabled when OTG_HS used in FS mode.
  // Device enumeration will not work unless this is configured.
  LL_AHB1_GRP1_DisableClockLowPower(LL_AHB1_GRP1_PERIPH_OTGHSULPI);

  HAL_NVIC_SetPriority(OTG_HS_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0);
  HAL_NVIC_EnableIRQ(OTG_HS_IRQn);

  //usb_pso.set_high();

}
#endif


#if USE_AUDIO

#if 0
_Alignas(uint32_t)
static const uint16_t s_demo_dac_data[] = {
  0, 16384, 32768, 49152, 65535
};
#endif

#  ifdef USE_AUDIO_DAC
void dac_hw_init(SampleDeviceDAC *sdev) {
/*
Setup hardware to enable DAC output on GPIO port pin. DAC samples are driven by a timer
triggered DMA transfer from a circular buffer. DMA interrupts signal audio_synth_task()
for new data after every half-buffer is consumed.
*/

  // IO init

  __HAL_RCC_GPIOA_CLK_ENABLE();

  // DAC_OUT1 (PA4 DISCO1)
  LL_GPIO_InitTypeDef gpio_cfg = {
    .Pin        = LL_GPIO_PIN_4,
    .Mode       = LL_GPIO_MODE_ANALOG,
    .Speed      = LL_GPIO_SPEED_FREQ_LOW,
    .OutputType = LL_GPIO_OUTPUT_PUSHPULL,
    .Pull       = LL_GPIO_PULL_NO,
    .Alternate  = LL_GPIO_AF_0  // Datasheet Table 12 p77
  };

  LL_GPIO_Init(GPIOA, &gpio_cfg);

  // Init buffer data to mid-scale
  int16_t *buf_pos = sdev->base.cfg.dma_buf_low;
  size_t buf_samples = sdev->base.cfg.half_buf_samples * 2;
  for(size_t i = 0; i < buf_samples; i++) {
    *buf_pos++ = DAC_SAMPLE_ZERO;
  }


  // Timer setup
  DAC_TIMER_CLK_ENABLE();

  uint32_t timer_clk = timer_clock_rate(DAC_TIMER);
  uint16_t prescaler = (timer_clk / DAC_TIMER_CLOCK_HZ) - 1;

  LL_TIM_InitTypeDef tim_cfg;

  LL_TIM_StructInit(&tim_cfg);
  tim_cfg.Autoreload    = (DAC_TIMER_CLOCK_HZ / AUDIO_SAMPLE_RATE) - 1;
  tim_cfg.Prescaler     = prescaler;

  LL_TIM_SetCounter(DAC_TIMER, 0);
  LL_TIM_Init(DAC_TIMER, &tim_cfg);

  //LL_TIM_EnableIT_UPDATE(DAC_TIMER);
  LL_TIM_SetTriggerOutput(DAC_TIMER, LL_TIM_TRGO_UPDATE);
  LL_TIM_EnableCounter(DAC_TIMER);

  // Note: TIM6 IRQ shared with DAC so we enable for DAC usage only
  HAL_NVIC_SetPriority(DAC_TIMER_IRQ, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0);
  NVIC_EnableIRQ(DAC_TIMER_IRQ);


  // DMA setup
  DMA_TypeDef *DMA_periph = sdev->base.cfg.DMA_periph;
  uint32_t DMA_stream = sdev->base.cfg.DMA_stream;


  if(DMA_periph == DMA1)
    __HAL_RCC_DMA1_CLK_ENABLE();
  else
    __HAL_RCC_DMA2_CLK_ENABLE();


#define DAC_DMA_CHANNEL   LL_DMA_CHANNEL_7    // RM0090 Table 42   Same channel for DAC1 & 2
  LL_DMA_SetChannelSelection(DMA_periph, DMA_stream, DAC_DMA_CHANNEL);
  LL_DMA_ConfigTransfer(DMA_periph,
                        DMA_stream,
                        LL_DMA_DIRECTION_MEMORY_TO_PERIPH |
                        LL_DMA_MODE_CIRCULAR              |
                        LL_DMA_PERIPH_NOINCREMENT         |
                        LL_DMA_MEMORY_INCREMENT           |
                        LL_DMA_PDATAALIGN_HALFWORD        |
                        LL_DMA_MDATAALIGN_HALFWORD        |
                        LL_DMA_PRIORITY_HIGH
                        );

  LL_DMA_ConfigAddresses(DMA_periph,
                         DMA_stream,
                         (uint32_t)sdev->base.cfg.dma_buf_low,
                         LL_DAC_DMA_GetRegAddr(sdev->DAC_periph, sdev->DAC_channel,
                                                LL_DAC_DMA_REG_DATA_12BITS_LEFT_ALIGNED),
                         LL_DMA_DIRECTION_MEMORY_TO_PERIPH);
  
  LL_DMA_SetDataLength(DMA_periph, DMA_stream, buf_samples);
  LL_DMA_EnableIT_HT(DMA_periph, DMA_stream);
  LL_DMA_EnableIT_TC(DMA_periph, DMA_stream);
  LL_DMA_EnableStream(DMA_periph, DMA_stream);

  IRQn_Type dma_irq = stm32_dma_stream_irq(DMA_periph, DMA_stream);
  HAL_NVIC_SetPriority(dma_irq, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY+1, 0);
  NVIC_EnableIRQ(dma_irq);


  // DAC setup
  __HAL_RCC_DAC_CLK_ENABLE();
 
  LL_DAC_InitTypeDef dac_cfg = {
    .TriggerSource      = DAC_TRIG_SOURCE,
    .WaveAutoGeneration = LL_DAC_WAVE_AUTO_GENERATION_NONE,
    .OutputBuffer       = LL_DAC_OUTPUT_BUFFER_ENABLE
  };

  LL_DAC_Init(sdev->DAC_periph, sdev->DAC_channel, &dac_cfg);
  LL_DAC_ConvertData12LeftAligned(sdev->DAC_periph, sdev->DAC_channel, DAC_SAMPLE_ZERO);

//  LL_DAC_EnableDMAReq(DAC1, LL_DAC_CHANNEL_1);
  LL_DAC_EnableIT_DMAUDR1(sdev->DAC_periph);
  
  LL_DAC_Enable(sdev->DAC_periph, sdev->DAC_channel);
  LL_DAC_EnableTrigger(sdev->DAC_periph, sdev->DAC_channel);
}
#  endif // USE_AUDIO_DAC


void i2s_hw_init(SampleDeviceI2S *sdev) {
/*
Setup hardware to enable otput via I2S. DAC samples are driven by a timer
triggered DMA transfer from a circular buffer. DMA interrupts signal audio_synth_task()
for new data after every half-buffer is consumed.
*/
  // PLL setup for I2S clocking
#if AUDIO_SAMPLE_RATE == 8000
  RCC_PeriphCLKInitTypeDef i2s_clk_cfg = {
    .PeriphClockSelection = RCC_PERIPHCLK_I2S,
    .PLLI2S = { // PLLI2S VCO (Output from *N) must be between 100 and 432MHz
      .PLLI2SN = 192, // From RM0090 Table 127
      .PLLI2SR = 2
    }
  };
#elif AUDIO_SAMPLE_RATE == 16000
  // 16kHz I2S
  RCC_PeriphCLKInitTypeDef i2s_clk_cfg = {
    .PeriphClockSelection = RCC_PERIPHCLK_I2S,
    .PLLI2S = { // PLLI2S VCO (Output from *N) must be between 100 and 432MHz
      .PLLI2SN = 192, // From RM0090 Table 127
      .PLLI2SR = 3
    }
  };
#elif AUDIO_SAMPLE_RATE == 100
RCC_PeriphCLKInitTypeDef i2s_clk_cfg = {
    .PeriphClockSelection = RCC_PERIPHCLK_I2S,
    .PLLI2S = { // PLLI2S VCO (Output from *N) must be between 100 and 432MHz
      .PLLI2SN = 192, // From RM0090 Table 127
      .PLLI2SR = 2
    }
  };
#else
#  error "Unsupported sample rate"
#endif

  // 8MHz / 8 * 192 / 2 = 96MHz
  HAL_RCCEx_PeriphCLKConfig(&i2s_clk_cfg);
  __HAL_RCC_I2S_CONFIG(RCC_I2SCLKSOURCE_PLLI2S);


  // IO init

  gpio_highz_on(&g_i2s_sd_mode);  // L/2+R/2
//  gpio_highz_on(&g_i2s_gain);     // 9dB

  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();


  // SD (PC3 DISCO1)  (B15 Blackpill)
  LL_GPIO_InitTypeDef gpio_cfg = {
#ifdef BOARD_STM32F429I_DISC1
    .Pin        = LL_GPIO_PIN_3,
#else
    .Pin        = LL_GPIO_PIN_15,
#endif
    .Mode       = LL_GPIO_MODE_ALTERNATE,
    .Speed      = LL_GPIO_SPEED_FREQ_MEDIUM,
    .OutputType = LL_GPIO_OUTPUT_PUSHPULL,
    .Pull       = LL_GPIO_PULL_NO,
    .Alternate  = LL_GPIO_AF_5  // Datasheet Table 12 p77
  };
#ifdef BOARD_STM32F429I_DISC1
  LL_GPIO_Init(GPIOC, &gpio_cfg);
#else
  LL_GPIO_Init(GPIOB, &gpio_cfg);
#endif

  // WS (PB9)  CK (PB10)
  gpio_cfg.Pin = LL_GPIO_PIN_9 | LL_GPIO_PIN_10;
  LL_GPIO_Init(GPIOB, &gpio_cfg);


  // Init buffer data to mid-scale
  int16_t *buf_pos = sdev->base.cfg.dma_buf_low;
  size_t buf_samples = sdev->base.cfg.half_buf_samples * 4;
  for(size_t i = 0; i < buf_samples; i++) {
    *buf_pos++ = 0; // FIXME: Change to memset
  }


  // DMA setup
  DMA_TypeDef *DMA_periph = sdev->base.cfg.DMA_periph;
  uint32_t DMA_stream = sdev->base.cfg.DMA_stream;


  if(DMA_periph == DMA1)
    __HAL_RCC_DMA1_CLK_ENABLE();
  else
    __HAL_RCC_DMA2_CLK_ENABLE();


#define I2S2_DMA_CHANNEL   LL_DMA_CHANNEL_0    // RM0090 Table 42   S4_C0 = SPI2_TX
  LL_DMA_SetChannelSelection(DMA_periph, DMA_stream, I2S2_DMA_CHANNEL);
  LL_DMA_ConfigTransfer(DMA_periph,
                        DMA_stream,
                        LL_DMA_DIRECTION_MEMORY_TO_PERIPH |
                        LL_DMA_MODE_CIRCULAR              |
                        LL_DMA_PERIPH_NOINCREMENT         |
                        LL_DMA_MEMORY_INCREMENT           |
                        LL_DMA_PDATAALIGN_HALFWORD        |
                        LL_DMA_MDATAALIGN_HALFWORD        |
                        LL_DMA_PRIORITY_HIGH
                        );

  LL_DMA_ConfigAddresses(DMA_periph,
                         DMA_stream,
                         (uint32_t)sdev->base.cfg.dma_buf_low,
                         LL_SPI_DMA_GetRegAddr(sdev->SPI_periph),
                         LL_DMA_DIRECTION_MEMORY_TO_PERIPH);
  
  LL_DMA_SetDataLength(DMA_periph, DMA_stream, buf_samples);
  LL_DMA_EnableIT_HT(DMA_periph, DMA_stream);
  LL_DMA_EnableIT_TC(DMA_periph, DMA_stream);
  LL_DMA_EnableStream(DMA_periph, DMA_stream);

  IRQn_Type dma_irq = stm32_dma_stream_irq(DMA_periph, DMA_stream);
  HAL_NVIC_SetPriority(dma_irq, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY+1, 0);
  NVIC_EnableIRQ(dma_irq);

  // I2S setup

  __HAL_RCC_SPI2_CLK_ENABLE();

  LL_I2S_InitTypeDef cfg = {
    .Mode           = LL_I2S_MODE_MASTER_TX,
    .Standard       = LL_I2S_STANDARD_PHILIPS,
    .DataFormat     = LL_I2S_DATAFORMAT_16B,
    .MCLKOutput     = LL_I2S_MCLK_OUTPUT_DISABLE,
#  if AUDIO_SAMPLE_RATE == 8000
    .AudioFreq      = LL_I2S_AUDIOFREQ_8K,
#  elif AUDIO_SAMPLE_RATE == 16000
    .AudioFreq      = LL_I2S_AUDIOFREQ_16K,
#  else
#    error "Unsupported sample rate"
#  endif
    .ClockPolarity  = LL_I2S_POLARITY_LOW
  };

  LL_I2S_Init(sdev->SPI_periph, &cfg);
  LL_I2S_Enable(sdev->SPI_periph);
  //LL_I2S_EnableDMAReq_TX(sdec->SPI_periph);
}


void i2s_io_init(void) {
#if AUDIO_SAMPLE_RATE == 8000
  RCC_PeriphCLKInitTypeDef i2s_clk_cfg = {
    .PeriphClockSelection = RCC_PERIPHCLK_I2S,
    .PLLI2S = { // PLLI2S VCO (Output from *N) must be between 100 and 432MHz
      .PLLI2SN = 192, // From RM0090 Table 127
      .PLLI2SR = 2
    }
  };
#elif AUDIO_SAMPLE_RATE == 16000
  // 16kHz I2S
  RCC_PeriphCLKInitTypeDef i2s_clk_cfg = {
    .PeriphClockSelection = RCC_PERIPHCLK_I2S,
    .PLLI2S = { // PLLI2S VCO (Output from *N) must be between 100 and 432MHz
      .PLLI2SN = 192, // From RM0090 Table 127
      .PLLI2SR = 3
    }
  };
#elif AUDIO_SAMPLE_RATE == 100
RCC_PeriphCLKInitTypeDef i2s_clk_cfg = {
    .PeriphClockSelection = RCC_PERIPHCLK_I2S,
    .PLLI2S = { // PLLI2S VCO (Output from *N) must be between 100 and 432MHz
      .PLLI2SN = 192, // From RM0090 Table 127
      .PLLI2SR = 2
    }
  };
#else
#  error "Unsupported sample rate"
#endif

  // 8MHz / 8 * 192 / 2 = 96MHz
  HAL_RCCEx_PeriphCLKConfig(&i2s_clk_cfg);


  // IO init

  gpio_highz_on(&g_i2s_sd_mode);  // L/2+R/2
//  gpio_highz_on(&g_i2s_gain);     // 9dB

  __HAL_RCC_SPI2_CLK_ENABLE();

  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();


  // SD (PC3 DISCO1)  (B15 Blackpill)
  LL_GPIO_InitTypeDef gpio_cfg = {
#ifdef BOARD_STM32F429I_DISC1
    .Pin        = LL_GPIO_PIN_3,
#else
    .Pin        = LL_GPIO_PIN_15,
#endif
    .Mode       = LL_GPIO_MODE_ALTERNATE,
    .Speed      = LL_GPIO_SPEED_FREQ_MEDIUM,
    .OutputType = LL_GPIO_OUTPUT_PUSHPULL,
    .Pull       = LL_GPIO_PULL_NO,
    .Alternate  = LL_GPIO_AF_5  // Datasheet Table 12 p77
  };
#ifdef BOARD_STM32F429I_DISC1
  LL_GPIO_Init(GPIOC, &gpio_cfg);
#else
  LL_GPIO_Init(GPIOB, &gpio_cfg);
#endif

  // WS (PB9)  CK (PB10)
  gpio_cfg.Pin = LL_GPIO_PIN_9 | LL_GPIO_PIN_10;
  LL_GPIO_Init(GPIOB, &gpio_cfg);

#if 0
  // MCO2 (PC9)
  gpio_cfg.Pin        = LL_GPIO_PIN_9;
  gpio_cfg.Speed      = LL_GPIO_SPEED_FREQ_VERY_HIGH,
  gpio_cfg.Alternate  = LL_GPIO_AF_0;
  LL_GPIO_Init(GPIOC, &gpio_cfg);

  LL_RCC_ConfigMCO(LL_RCC_MCO2SOURCE_PLLI2S, LL_RCC_MCO2_DIV_5);
#endif
}
#endif

