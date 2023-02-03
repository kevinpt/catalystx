#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "lib_cfg/build_config.h"
#ifdef PLATFORM_STM32F4
#  include "stm32f4xx_hal.h"
#endif
#ifdef TEST_CRC
#  include "stm32f4xx_ll_crc.h"
#endif

#include "FreeRTOS.h"
#include "semphr.h"

#include "cstone/debug.h"
#include "cstone/profile.h"
#include "cstone/obj_metadata.h"
#include "cstone/crc32_stm32.h"
#include "cstone/console.h"
#include "cstone/dump_reg.h"
//#include "cstone/umsg.h"
#include "app_cmds.h"
#include "cstone/prop_id.h"
#include "cstone/prop_db.h"
#include "app_prop_id.h"


#include "util/term_color.h"
#include "util/getopt_r.h"
#include "util/range_strings.h"
#include "util/string_ops.h"
#ifdef TEST_CRC
#  include "util/crc16.h"
#  include "util/crc32.h"
#endif

#include "cstone/iqueue_int16_t.h"
#if USE_AUDIO
#  include "cstone/umsg.h"
#  include "audio_synth.h"
#endif


static int32_t cmd_demo(uint8_t argc, char *argv[], void *eval_ctx) {
  printf("  argv[0]  %s\n", argv[0]);
  for(int i = 1; i < argc; i++) {
    printf("      [%d]  %s\n", i, argv[i]);
  }

  return 0;
}


#if USE_AUDIO

static void key__input_redirect(Console *con, KeyCode key_code, void *eval_ctx) {
  static uint8_t key_state[10] = {0};

  // Toggle key states by pressing digits 0-9
  if(key_code >= '0' && key_code <= '9') {
    uint32_t key = key_code - '0';
    uint32_t id;
    key_state[key] = !key_state[key];
    bool release = (key_state[key] == 0);
    key += 81;

    if(release)
      id = P_EVENT_KEY_n_RELEASE | P2_ARR(key);
    else
      id = P_EVENT_KEY_n_PRESS | P2_ARR(key);

    UMsg msg = {
      .id = id,
      .source = P_RSRC_HW_LOCAL_TASK
    };

    umsg_hub_send(umsg_sys_hub(), &msg, 1);

  } else if(key_code == CH_CTRL_C) {
    shell_cancel_redirect(&con->shell);
  }
}


static int32_t cmd_key(uint8_t argc, char *argv[], void *eval_ctx) {
  GetoptState state = {0};
  state.report_errors = true;
  int c;

  bool interactive = false;
  bool release = false;

  while((c = getopt_r(argv, "rih", &state)) != -1) {
    switch(c) {
    case 'i': interactive = true; break;
    case 'r': release = true; break;

    case 'h':
      puts("key [-i] [-p] [-r] [-h]");
      return 0;
      break;

    default:
    case ':':
    case '?':
      return -3;
      break;
    }
  }

  if(interactive) {
    puts("Play keys 0-9");
    Console *con = active_console();
    shell_redirect_input(&con->shell, key__input_redirect);
    return 0;
  }

  uint32_t key = 69+12;
  uint32_t id;

  if(release)
    id = P_EVENT_KEY_n_RELEASE | P2_ARR(key);
  else
    id = P_EVENT_KEY_n_PRESS | P2_ARR(key);

  UMsg msg = { .id = id,
    .source = P_RSRC_HW_LOCAL_TASK };

  umsg_hub_send(umsg_sys_hub(), &msg, 1);

  return 0;
}
#endif


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

#if 1
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


static RegField s_reg_SPI_CR2_fields[] = {
  REG_BIT("TXEIE",    7),
  REG_BIT("RXNEIE",   6),
  REG_BIT("ERRIE",    5),
  REG_BIT("FRF",      4),
  REG_BIT("SSOE",     2),
  REG_BIT("TXDMAEN",  1),
  REG_BIT("RXDMAEN",  0),
  REG_END
};

static RegLayout s_reg_SPI_CR2 = {
  .name     = "SPI%d_CR2",
  .fields   = s_reg_SPI_CR2_fields,
  .reg_bits = 32
};

static RegField s_reg_SPI_SR_fields[] = {
  REG_BIT("FRE",      8),
  REG_BIT("BSY",      7),
  REG_BIT("OVR",      6),
  REG_BIT("MODF",     5),
  REG_BIT("CRCERR",   4),
  REG_BIT("UDR",      3),
  REG_BIT("CHSIDE",   2),
  REG_BIT("TXE",      1),
  REG_BIT("RXNE",     0),
  REG_END
};

static RegLayout s_reg_SPI_SR = {
  .name     = "SPI%d_SR",
  .fields   = s_reg_SPI_SR_fields,
  .reg_bits = 32
};

static int32_t cmd_rcc(uint8_t argc, char *argv[], void *eval_ctx) {
//  const USB_OTG_GlobalTypeDef *usb_glbl = USB_OTG_HS;
//  const USB_OTG_DeviceTypeDef *usb_dev = DEVICE_BASE(1);
//  const uint32_t *usb_pcgcctl = PCGCCTL_BASE(1);

  dump_register(&s_reg_rcc_cfgr,        RCC->CFGR, 2, /*show_bitmap*/true);
  dump_register(&s_reg_RCC_PLLI2SCFGR,  RCC->PLLI2SCFGR, 2, /*show_bitmap*/true);

#if 1
  dump_register(&s_reg_DMA_HISR,    DMA1->HISR , 2, /*show_bitmap*/true);
  dump_register(&s_reg_DMA_SxCR,    DMA1_Stream4->CR , 2, /*show_bitmap*/true);
  dump_register(&s_reg_DMA_SxNDTR,  DMA1_Stream4->NDTR , 2, /*show_bitmap*/true);
  dump_register(&s_reg_DMA_SxPAR,   DMA1_Stream4->PAR , 2, /*show_bitmap*/true);
  dump_register(&s_reg_DMA_SxM0AR,  DMA1_Stream4->M0AR , 2, /*show_bitmap*/true);
  dump_register(&s_reg_DMA_SxFCR,   DMA1_Stream4->FCR , 2, /*show_bitmap*/true);
#endif

  dump_register(&s_reg_SPI_I2SCFGR, SPI2->I2SCFGR,  2, /*show_bitmap*/true);
  dump_register(&s_reg_SPI_I2SPR,   SPI2->I2SPR,    2, /*show_bitmap*/true);
  dump_register(&s_reg_SPI_CR2,     SPI2->CR2,      2, /*show_bitmap*/true);
  dump_register(&s_reg_SPI_SR,      SPI2->SR,       2, /*show_bitmap*/true);
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

#ifdef TEST_CRC
static void test_crc32(void) {
  _Alignas(uint32_t)
  //uint8_t data[] = {100,2,3,4,5,6,7,8};
  //uint8_t data[] = {3,2,1,0};
  uint8_t data[] = {0,1,2,3,4,5,6,7};
//  uint8_t data[] = {4,3,2,100,2,8,7,6,5};

  uint32_t crc = crc32_init();
  for(size_t i = 0; i < sizeof data; i++) {
    crc = crc32_update(crc, data[i]);
  }
  crc = crc32_finish(crc);

  puts("CRC-32:");
  printf("\tserial = 0x%08"PRIX32"\n", crc);

  crc = crc32_init();
  crc = crc32_update_small_stm32(crc, data, sizeof data);
  crc = crc32_finish(crc);

  printf("\tsmall  = 0x%08"PRIX32"\n", crc);


  __HAL_RCC_CRC_CLK_ENABLE();
  LL_CRC_ResetCRCCalculationUnit(CRC);
  for(size_t i = 0; i < (sizeof data)/4; i++) {
  //    uint32_t dswap = __builtin_bswap32(*(uint32_t*)&data[i << 2]);
    uint32_t dswap = *(uint32_t*)&data[i << 2];
    LL_CRC_FeedData32(CRC, dswap);
  }
  crc = LL_CRC_ReadData32(CRC);
  printf("\thw     = 0x%08"PRIX32"\n", crc);

  crc32_init_hw();
  crc = crc32_update_hw(data, sizeof data);
  printf("\thw2    = 0x%08"PRIX32"\n", crc);
}




static bool firmware_crc(const ObjMemRegion *regions, uint32_t *crc) {
  *crc = 0;

  uint32_t elf_crc;
  uint32_t id;
#if 1
  crc32_init_hw();
  crc32_dma_init();

id = profile_add(0, "CRC32 DMA");
profile_start(id);
  for(int i = 0; i < OBJ_MAX_REGIONS; i++) {
    const ObjMemRegion *cur = &regions[i];
    if(cur->end - cur->start == 0) // No more regions
      break;

    uint8_t *block = cur->start;
    size_t remain = cur->end - cur->start;
    while(remain > 0) {
      size_t processed = crc32_dma_process_block(block, remain);
      crc32_dma_wait();
      remain -= processed;
      block += processed;
    }
  }
profile_stop(id);
  elf_crc = crc32_get_hw();
  printf("## elf_crc (dma): 0x%08"PRIX32"\n", elf_crc);

//#else
  crc32_init_hw();

id = profile_add(0, "CRC32 HW");
profile_start(id);
  for(int i = 0; i < OBJ_MAX_REGIONS; i++) {
    const ObjMemRegion *cur = &regions[i];
    if(cur->end - cur->start == 0) // No more regions
      break;

//    printf("## cur: %08X - %08X\n", cur->start, cur->end);
    uint8_t *block = cur->start;
    elf_crc = crc32_update_hw(block, cur->end - cur->start);
//    printf("## elf_crc (hw): 0x%08X\n", elf_crc);
  }
profile_stop(id);

profile_report_all();
//profile_delete_all();

  *crc = crc32_finish(elf_crc);
#endif
  return true;
}


extern const ObjectMetadata g_metadata;
#define CHAR_PASS   A_BGRN u8"✓" A_NONE
#define CHAR_FAIL   A_BRED u8"✗" A_NONE

// FIXME: Replace
static void XXvalidate_metadata(void) {
  // Check firmware crc
  uint32_t obj_crc;
  firmware_crc(g_metadata.mem_regions, &obj_crc);

  // Check metadata CRC
#define FIELD_SIZE(t, f)  sizeof(((t *)0)->f)
  uint16_t meta_crc = crc16_init();
  // Skip over initial CRCs in metadata struct
  size_t meta_offset = offsetof(ObjectMetadata, meta_crc) + FIELD_SIZE(ObjectMetadata, meta_crc);
  meta_crc = crc16_update_block(meta_crc, (uint8_t *)&g_metadata + meta_offset,
                                sizeof g_metadata - meta_offset);
  meta_crc = crc16_finish(meta_crc);

  printf("   App CRC: 0x%08"PRIX32" %s\n", obj_crc, obj_crc == g_metadata.obj_crc ? CHAR_PASS : CHAR_FAIL);
  printf("  Meta CRC: 0x%04X %s\n", meta_crc, meta_crc == g_metadata.meta_crc ? CHAR_PASS : CHAR_FAIL);
}


static int32_t cmd_crc(uint8_t argc, char *argv[], void *eval_ctx) {
  test_crc32();

  XXvalidate_metadata();

  return 0;
}
#endif // TEST_CRC

const ConsoleCommandDef g_app_cmd_set[] = {
#ifdef TEST_CRC
  CMD_DEF("crc",      cmd_crc,   "Test CRC"),
#endif
  CMD_DEF("demo",     cmd_demo,  "Demo app command"),
#if USE_AUDIO
  CMD_DEF("audio",    cmd_audio,      "Sound control"),
  CMD_DEF("key",      cmd_key,        "Play key"),
#endif
#ifdef PLATFORM_STM32F4
  CMD_DEF("rcc",      cmd_rcc,        "Debug RCC"),
#endif
  CMD_END
};


