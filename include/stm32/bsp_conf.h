#ifndef BSP_CONF_H
#define BSP_CONF_H

//#define __DMAx_CLK_ENABLE                 __HAL_RCC_DMA2_CLK_ENABLE
#define __DMAx_CLK_DISABLE                __HAL_RCC_DMA2_CLK_DISABLE
//#define SDRAM_DMAx_CHANNEL                DMA_CHANNEL_0
//#define SDRAM_DMAx_STREAM                 DMA2_Stream0
//#define SDRAM_DMAx_IRQn                   DMA2_Stream0_IRQn
#define BSP_SDRAM_DMA_IRQHandler          SDRAM_DMAx_IRQHandler

#endif // BSP_CONF_H
