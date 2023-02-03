#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "stm32f4xx_ll_dac.h"

#include "cstone/debug.h"

#include "sample_device.h"


void sdev_init(SampleDevice *sdev, SampleDeviceCfg *cfg, void *ctx) {
  memcpy(&sdev->cfg, cfg, sizeof *cfg);
  sdev->state = SDEV_INACTIVE;
  sdev->ctx = ctx;
}


unsigned sdev_sample_out(SampleDevice *sdev, int16_t *buf) {
  return sdev->cfg.sample_out(sdev, buf, sdev->cfg.half_buf_samples);
}


bool sdev_ctl(SampleDevice *sdev, int op, void *data, size_t data_len) {
  DPRINT("OP: 0x%02X", op);
  switch(op) {
  case SDEV_OP_ACTIVATE:
    sdev->state = SDEV_ACTIVE;
    sdev->cfg.enable(sdev, true);
    break;
  case SDEV_OP_DEACTIVATE:
    if(sdev->state == SDEV_ACTIVE)
      sdev->state = SDEV_SHUTDOWN;  // Let current buffers flush
    break;
  case SDEV_OP_SHUTDOWN_END:
    if(sdev->state == SDEV_SHUTDOWN) {
      sdev->state = SDEV_INACTIVE;
      sdev->cfg.enable(sdev, false);
    }
    break;
  default:
    break;
  }

  return true;
}










