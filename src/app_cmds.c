#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "lib_cfg/build_config.h"
#ifdef PLATFORM_STM32F4
#  include "stm32f4xx_hal.h"
#endif

#include "FreeRTOS.h"
#include "semphr.h"

#include "cstone/console.h"
#include "cstone/dump_reg.h"
//#include "cstone/umsg.h"
#include "app_cmds.h"
#include "cstone/prop_id.h"
#include "cstone/prop_db.h"
#include "app_prop_id.h"


#include "util/getopt_r.h"
#include "util/range_strings.h"
#include "util/string_ops.h"

#include "cstone/iqueue_int16_t.h"
#if USE_AUDIO
#  include "audio_synth.h"
#endif


static int32_t cmd_demo(uint8_t argc, char *argv[], void *eval_ctx) {
  printf("  argv[0]  %s\n", argv[0]);
  for(int i = 1; i < argc; i++) {
    printf("      [%d]  %s\n", i, argv[i]);
  }

  return 0;
}

#ifdef PLATFORM_STM32F4
static RegField s_reg_rcc_cfgr_fields[] = {
  REG_SPAN("MCO2", 31, 30),
  REG_SPAN("MCO2_PRE", 29, 27),
  REG_SPAN("MCO1_PRE", 26, 24),
  REG_BIT("I2SSRC", 23),
  REG_SPAN("MCO1", 22, 21),
  REG_SPAN("RTCPRE", 20, 16),
  REG_SPAN("PPRE2", 15, 13),
  REG_SPAN("PPRE1", 12, 10),
  REG_SPAN("HPRE", 7, 4),
  REG_SPAN("SWS", 3, 2),
  REG_SPAN("SW", 1, 0),
  REG_END
};

static RegLayout s_reg_rcc_cfgr = {
  .name     = "RCC_CFGR",
  .fields   = s_reg_rcc_cfgr_fields,
  .reg_bits = 32
};

static RegField s_reg_RCC_PLLI2SCFGR_fields[] = {
  REG_SPAN("PLLI2SR", 30, 28),
  REG_SPAN("PLLI2SQ", 27, 24),
  REG_SPAN("PLLI2SN", 14, 6),
  REG_END
};

static RegLayout s_reg_RCC_PLLI2SCFGR = {
  .name     = "RCC_PLLI2SCFGR",
  .fields   = s_reg_RCC_PLLI2SCFGR_fields,
  .reg_bits = 32
};

#if 0
static RegField s_reg_DMA_HISR_fields[] = {
  REG_BIT("TCIF7", 27),
  REG_BIT("HTIF7", 26),
  REG_BIT("TEIF7", 25),
  REG_BIT("DMEIF7", 24),
  REG_BIT("FEIF7", 22),
  REG_BIT("TCIF6", 21),
  REG_BIT("HTIF6", 20),
  REG_BIT("TEIF6", 19),
  REG_BIT("DMEIF6", 18),
  REG_BIT("FEIF6", 16),
  REG_BIT("TCIF5", 11),
  REG_BIT("HTIF5", 10),
  REG_BIT("TEIF5", 9),
  REG_BIT("DMEIF5", 8),
  REG_BIT("FEIF5", 6),
  REG_BIT("TCIF4", 5),
  REG_BIT("HTIF4", 4),
  REG_BIT("TEIF4", 3),
  REG_BIT("DMEIF4", 2),
  REG_BIT("FEIF4", 0),
  REG_END
};

static RegLayout s_reg_DMA_HISR = {
  .name     = "DMA_HISR",
  .fields   = s_reg_DMA_HISR_fields,
  .reg_bits = 32
};


static RegField s_reg_DMA_SxCR_fields[] = {
  REG_SPAN("CHSEL", 27, 25),
  REG_SPAN("MBURST", 24, 23),
  REG_SPAN("PBURST", 22, 21),
  REG_BIT("CT", 19),
  REG_BIT("DBM", 18),
  REG_SPAN("PL", 17, 16),
  REG_BIT("PINCOS", 15),
  REG_SPAN("MSIZE", 14, 13),
  REG_SPAN("PSIZE", 12, 11),
  REG_BIT("MINC", 10),
  REG_BIT("PINC", 9),
  REG_BIT("CIRC", 8),
  REG_SPAN("DIR", 7, 6),
  REG_BIT("PFCTRL", 5),
  REG_BIT("TCIE", 4),
  REG_BIT("HTIE", 3),
  REG_BIT("TEIE", 2),
  REG_BIT("DMEIE", 1),
  REG_BIT("EN", 0),
  REG_END
};

static RegLayout s_reg_DMA_SxCR = {
  .name     = "DMA_S%dCR",
  .fields   = s_reg_DMA_SxCR_fields,
  .reg_bits = 32
};

static RegField s_reg_DMA_SxNDTR_fields[] = {
  REG_SPAN("NDT", 15, 0),
  REG_END
};

static RegLayout s_reg_DMA_SxNDTR = {
  .name     = "DMA_S%dNDTR",
  .fields   = s_reg_DMA_SxNDTR_fields,
  .reg_bits = 32
};

static RegField s_reg_DMA_SxPAR_fields[] = {
  REG_SPAN("PAR", 31, 0),
  REG_END
};

static RegLayout s_reg_DMA_SxPAR = {
  .name     = "DMA_S%dPAR",
  .fields   = s_reg_DMA_SxPAR_fields,
  .reg_bits = 32
};

static RegField s_reg_DMA_SxM0AR_fields[] = {
  REG_SPAN("M0A", 31, 0),
  REG_END
};

static RegLayout s_reg_DMA_SxM0AR = {
  .name     = "DMA_S%dM0R",
  .fields   = s_reg_DMA_SxM0AR_fields,
  .reg_bits = 32
};


static RegField s_reg_DMA_SxFCR_fields[] = {
  REG_BIT("FEIE", 7),
  REG_SPAN("FS", 5, 3),
  REG_BIT("DMDIS", 2),
  REG_SPAN("FTH", 1, 0),
  REG_END
};

static RegLayout s_reg_DMA_SxFCR = {
  .name     = "DMA_S%dFCR",
  .fields   = s_reg_DMA_SxFCR_fields,
  .reg_bits = 32
};
#endif

static RegField s_reg_SPI_I2SCFGR_fields[] = {
  REG_BIT("I2SMOD",   11),
  REG_BIT("I2SE",     10),
  REG_SPAN("I2SCFG",  9, 8),
  REG_BIT("PCMSYNC",  7),
  REG_SPAN("I2SSTD",  5, 4),
  REG_BIT("CKPOL",    3),
  REG_SPAN("DATALEN", 2, 1),
  REG_BIT("CHLEN",    0),
  REG_END
};

static RegLayout s_reg_SPI_I2SCFGR = {
  .name     = "SPI%d_I2SCFGR",
  .fields   = s_reg_SPI_I2SCFGR_fields,
  .reg_bits = 32
};

static RegField s_reg_SPI_I2SPR_fields[] = {
  REG_BIT("MCKOE",    9),
  REG_BIT("ODD",      8),
  REG_SPAN("I2SDIV",  7, 0),
  REG_END
};

static RegLayout s_reg_SPI_I2SPR = {
  .name     = "SPI%d_I2SPR",
  .fields   = s_reg_SPI_I2SPR_fields,
  .reg_bits = 32
};


static int32_t cmd_rcc(uint8_t argc, char *argv[], void *eval_ctx) {
//  const USB_OTG_GlobalTypeDef *usb_glbl = USB_OTG_HS;
//  const USB_OTG_DeviceTypeDef *usb_dev = DEVICE_BASE(1);
//  const uint32_t *usb_pcgcctl = PCGCCTL_BASE(1);

  dump_register(&s_reg_rcc_cfgr,        RCC->CFGR, 2, /*show_bitmap*/true);
  dump_register(&s_reg_RCC_PLLI2SCFGR,  RCC->PLLI2SCFGR, 2, /*show_bitmap*/true);

#if 0
  dump_register(&s_reg_DMA_HISR,    DMA1->HISR , 2, /*show_bitmap*/true);
  dump_register(&s_reg_DMA_SxCR,    DMA1_Stream4->CR , 2, /*show_bitmap*/true);
  dump_register(&s_reg_DMA_SxNDTR,  DMA1_Stream4->NDTR , 2, /*show_bitmap*/true);
  dump_register(&s_reg_DMA_SxPAR,   DMA1_Stream4->PAR , 2, /*show_bitmap*/true);
  dump_register(&s_reg_DMA_SxM0AR,  DMA1_Stream4->M0AR , 2, /*show_bitmap*/true);
  dump_register(&s_reg_DMA_SxFCR,   DMA1_Stream4->FCR , 2, /*show_bitmap*/true);
#endif

  dump_register(&s_reg_SPI_I2SCFGR, SPI2->I2SCFGR,  2, /*show_bitmap*/true);
  dump_register(&s_reg_SPI_I2SPR,   SPI2->I2SPR,    2, /*show_bitmap*/true);
  return 0;
}
#endif // PLATFORM_STM32F4

#if USE_AUDIO
extern PropDB g_prop_db;

static int32_t cmd_audio(uint8_t argc, char *argv[], void *eval_ctx) {
  GetoptState state = {0};
  state.report_errors = true;
  int c;

  const char *mode = NULL;
  const char *wave = NULL;
  int32_t frequency = -1;
  int curve = -1;

  while((c = getopt_r(argv, "f:m:w:c:h", &state)) != -1) {
    switch(c) {
    case 'f': frequency = strtol(state.optarg, NULL, 10); break;
    case 'm': mode = state.optarg; break;
    case 'w': wave = state.optarg; break;
    case 'c': curve = strtol(state.optarg, NULL, 10); break;

    case 'h':
      puts("audio [-m on|off] [-f freq] [-w sin|sqr|saw|tri] [-h]");
      return 0;
      break;

    default:
    case ':':
    case '?':
      return -3;
      break;
    }
  }


  if(mode) {
    if(!stricmp(mode, "on")) {
      puts("Audio on");
      prop_set_uint(&g_prop_db, P_APP_AUDIO_INFO_VALUE, 1, P_RSRC_CON_LOCAL_TASK);
    } else if(!stricmp(mode, "off")) {
      puts("Audio off");
      prop_set_uint(&g_prop_db, P_APP_AUDIO_INFO_VALUE, 0, P_RSRC_CON_LOCAL_TASK);
    }
  }

  if(frequency >= 0) {
    prop_set_uint(&g_prop_db, P_APP_AUDIO_INST0_FREQ, (uint32_t)frequency, P_RSRC_CON_LOCAL_TASK);
    printf("Freq = %" PRIu32 "\n", (uint32_t)frequency);
  }

  if(curve >= 0) {
    prop_set_uint(&g_prop_db, P_APP_AUDIO_INST0_CURVE, (uint32_t)curve, P_RSRC_CON_LOCAL_TASK);
    printf("Curve = %d\n", curve);
  }

  if(wave) {
    if(!stricmp(wave, "sin"))
      prop_set_uint(&g_prop_db, P_APP_AUDIO_INST0_WAVE, OSC_SINE, P_RSRC_CON_LOCAL_TASK);
    else if(!stricmp(wave, "sqr"))
      prop_set_uint(&g_prop_db, P_APP_AUDIO_INST0_WAVE, OSC_SQUARE, P_RSRC_CON_LOCAL_TASK);
    else if(!stricmp(wave, "saw"))
      prop_set_uint(&g_prop_db, P_APP_AUDIO_INST0_WAVE, OSC_SAWTOOTH, P_RSRC_CON_LOCAL_TASK);
    else if(!stricmp(wave, "tri"))
      prop_set_uint(&g_prop_db, P_APP_AUDIO_INST0_WAVE, OSC_TRIANGLE, P_RSRC_CON_LOCAL_TASK);
    else if(!stricmp(wave, "noi"))
      prop_set_uint(&g_prop_db, P_APP_AUDIO_INST0_WAVE, OSC_NOISE, P_RSRC_CON_LOCAL_TASK);
    else
      printf("ERROR: Unknown wave kind: '%s'\n", wave);
  }

  return 0;
}
#endif

const ConsoleCommandDef g_app_cmd_set[] = {
  CMD_DEF("demo",     cmd_demo,  "Demo app command"),
#if USE_AUDIO
  CMD_DEF("audio",    cmd_audio,      "Sound control"),
#endif
#ifdef PLATFORM_STM32F4
  CMD_DEF("rcc",      cmd_rcc,        "Debug RCC"),
#endif
  CMD_END
};


