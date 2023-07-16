#ifndef DISCO_UI_HPP
#define DISCO_UI_HPP

#include "cstone/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

void set_nav_button_state(uint32_t event);
void nav_input_read(lv_indev_drv_t *drv, lv_indev_data_t *data);

bool ts_load_calibration(TouchCalibration *touch_cal);
bool ts_set_calibration(TouchCalPoint *p0, TouchCalPoint *p1);
int32_t cmd_tscal(uint8_t argc, char *argv[], void *eval_ctx);

#ifdef PLATFORM_EMBEDDED
void lvgl_stm32_init(void);
void lcd_init(void);
#else
void lvgl_sim_init(void);
#endif

#ifdef __cplusplus
}
#endif

#endif // DISCO_UI_HPP
