#include <stdint.h>
#include <stdbool.h>

#include "lib_cfg/build_config.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_rcc.h"

#include "FreeRTOS.h"

#include "cstone/io/uart.h"
#include "cstone/io/usb.h"
#include "i2s.h"
#include "app_main.h"
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

#ifdef USE_TINYUSB
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


  // SD (PC3)
  LL_GPIO_InitTypeDef gpio_cfg = {
    .Pin        = LL_GPIO_PIN_3,
    .Mode       = LL_GPIO_MODE_ALTERNATE,
    .Speed      = LL_GPIO_SPEED_FREQ_MEDIUM,
    .OutputType = LL_GPIO_OUTPUT_PUSHPULL,
    .Pull       = LL_GPIO_PULL_NO,
    .Alternate  = LL_GPIO_AF_5  // Datasheet Table 12 p77
  };
  LL_GPIO_Init(GPIOC, &gpio_cfg);

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

