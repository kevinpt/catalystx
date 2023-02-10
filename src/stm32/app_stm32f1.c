#include <stdint.h>
#include <stdbool.h>

#include "stm32f1xx_hal.h"

#include "cstone/io/gpio.h"
#include "cstone/io/uart_stm32.h"
#include "app_main.h"
#include "app_stm32.h"


void uart_io_init(void) {
  // Configure GPIO
  GPIO_InitTypeDef uart_pin_cfg = {0};

  gpio_enable_port(GPIO_PORT_A);

  // TX
  uart_pin_cfg.Pin        = GPIO_PIN_9;
  uart_pin_cfg.Mode       = GPIO_MODE_AF_PP;
  uart_pin_cfg.Speed      = GPIO_SPEED_FREQ_LOW;
  uart_pin_cfg.Pull       = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &uart_pin_cfg);

  // RX
  uart_pin_cfg.Pin        = GPIO_PIN_10;
  uart_pin_cfg.Mode       = GPIO_MODE_INPUT; //GPIO_MODE_AF_OD;
  HAL_GPIO_Init(GPIOA, &uart_pin_cfg);
}


#if 0
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

#else
void system_clock_init(void) {
  RCC_OscInitTypeDef osc_init = {0};
  RCC_ClkInitTypeDef clk_init = {0};

  __HAL_RCC_PWR_CLK_ENABLE();

  // Enable voltage scaling at lower clock rates
  // VDD 2.7V - 3.6V
//  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

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
  osc_init.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  osc_init.PLL.PLLState   = RCC_PLL_ON;
  osc_init.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
  osc_init.PLL.PLLMUL     = RCC_PLL_MUL9;

  // 72MHz Sysclk
  // 8MHz /1 * 9 --> 72MHz  Sysclk
  // 8MHz * 360 / 8 / 8 --> 45MHz

  // AHB = HCLK = Sysclk/1 = 180MHz  (180MHz max)
  // APB1 = AHB/4 = 45MHz (45MHz max)
  // APB2 = AHB/2 = 90MHz (90MHz max)
  // SysTick = AHB = 180MHz

  if(HAL_RCC_OscConfig(&osc_init) != HAL_OK)
    fatal_error();

  // Set internal voltage reg. to allow higher clock rates (required to achieve 180MHz)
  // Stop and Standby modes no longer available
//  HAL_PWREx_EnableOverDrive();
 
  // Use PLL as Sysclk and set division ratios for derived clocks
  clk_init.ClockType      = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                             RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
  clk_init.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  clk_init.AHBCLKDivider  = RCC_SYSCLK_DIV1;  // HCLK == Sysclk
  clk_init.APB1CLKDivider = RCC_HCLK_DIV2;  // 36MHz max
  clk_init.APB2CLKDivider = RCC_HCLK_DIV1;  // 72MHz max

  // NOTE: Latency selected from p58 of RM0008
  if(HAL_RCC_ClockConfig(&clk_init, /*flash latency*/2) != HAL_OK)
    fatal_error();

#if 1
  // Output SysClk on MCO1 pin PA8
  HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_SYSCLK, RCC_MCODIV_1);
#endif
}
#endif
