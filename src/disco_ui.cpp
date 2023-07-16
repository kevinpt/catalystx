#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "lib_cfg/build_config.h"
#include "lib_cfg/cstone_cfg_stm32.h"
#include "app_main.h"

#include "cstone/platform_io.h"


#ifdef PLATFORM_EMBEDDED
#  define STM32_HAL_LEGACY  // Inhibit legacy header
#  include "stm32f4xx_hal.h"
#  include "stm32f4xx_ll_usart.h"
#  include "stm32f429i_discovery.h"
#  include "stm32f429i_discovery_lcd.h"
#  include "stm32f429i_discovery_ts.h"
//#  include "stm32f4xx_it.h"
#endif

#include "FreeRTOS.h"
#include "timers.h"


#include "cstone/prop_id.h"
#include "app_prop_id.h"
#include "cstone/prop_db.h"
#include "cstone/prop_serialize.h"
#include "util/mempool.h"
#include "cstone/umsg.h"

#include "lvgl/lvgl.h"
#include "ui_panel.h"
#include "ui_units.h"
#include "app_ui.h"

#ifndef PLATFORM_EMBEDDED
#  include "lv_drivers/sdl/sdl.h"
#endif


#include "debounce.h"
#include "util/getopt_r.h"

#include "disco_ui.hpp"

// AHB burst transfers span 1K blocks and must be on a 64B boundary to avoid sequential access
#define BURST_ALIGN       1024
#define LCD_COLOR_DEPTH   32

LV_IMG_DECLARE(cursor_v);


UIReactWidgets    g_react_widgets;  // Table of reactive widgets indexed by prop
UIWidgetRegistry  g_widget_reg;     // Table of UIWidgetEntry indexed by type string


#define NAV_BUTTON_TIMEOUT_MS 200
static uint32_t s_nav_button_event = P_EVENT_BUTTON__UP_PRESS;
static TickType_t s_nav_button_timestamp = 0;

void set_nav_button_state(uint32_t event) {
  s_nav_button_event = event;
  s_nav_button_timestamp = xTaskGetTickCount();
}

static uint32_t get_nav_button_state(bool *pressed) {
  TickType_t now = xTaskGetTickCount();
  *pressed = false;

  if(s_nav_button_timestamp > 0 && (now - s_nav_button_timestamp) < NAV_BUTTON_TIMEOUT_MS) {
    // Simulate release after delay
    *pressed = true;
  }

  return s_nav_button_event;
}


// LVGL indev driver callback
void nav_input_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  uint32_t last_btn_event;
  uint32_t last_key = 0;
  bool pressed;

  last_btn_event = get_nav_button_state(&pressed);

  // Translate key events into LVGL keys
  switch(last_btn_event) {
    case P_EVENT_BUTTON__UP_PRESS:
      last_key = LV_KEY_UP;
      break;

    case P_EVENT_BUTTON__DOWN_PRESS:
      last_key = LV_KEY_DOWN;
      break;

    case P_EVENT_BUTTON__LEFT_PRESS:
      last_key = LV_KEY_LEFT;
      break;

    case P_EVENT_BUTTON__RIGHT_PRESS:
      last_key = LV_KEY_RIGHT;
      break;

    case P_EVENT_BUTTON__SEL_PRESS:
      last_key = LV_KEY_ENTER;
      break;

    default:
      break;
  }

  data->key = last_key;
  data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}


#ifdef PLATFORM_EMBEDDED

// Frame buffers for LTDC

// LTDC frame buffer should be on 1K boundary to permit AHB burst transfers (AN4861)
// Double frame buffers should be in separate FSMC banks if possible

#if LCD_COLOR_DEPTH > 16
  typedef uint32_t LCDPixel;
#elif LCD_COLOR_DEPTH > 8
  typedef uint16_t LCDPixel;
#else
  typedef uint8_t LCDPixel;
#endif

__attribute__(( section(".sdram") ))
static LCDPixel framebuf_bg0[LCD_HOR_SPAN * LCD_VER_RES] alignas(BURST_ALIGN);

#ifdef USE_DOUBLE_BUF
__attribute__(( section(".sdram") ))
static LCDPixel framebuf_bg1[LCD_HOR_SPAN * LCD_VER_RES] alignas(BURST_ALIGN);
#endif

__attribute__(( section(".sdram") ))
static LCDPixel framebuf_fg[LCD_HOR_SPAN * LCD_VER_RES] alignas(BURST_ALIGN);



extern LTDC_HandleTypeDef LtdcHandler;

#  ifdef USE_DOUBLE_BUF
static void lcd_switch_buffer(lv_color_t *active_buf) {
  // Set new frame address in shadow registers
  // and schedule update for vsync.
  BSP_LCD_SetLayerAddress_NoReload(LCD_BACKGROUND_LAYER, (uintptr_t)active_buf);
  HAL_LTDC_Reload(&LtdcHandler, LTDC_RELOAD_VERTICAL_BLANKING);
}
#  endif


static void lcd_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {

  static_assert(LV_COLOR_DEPTH <= LCD_COLOR_DEPTH, "Color depth incompatible");

#ifdef USE_DOUBLE_BUF
  lcd_switch_buffer(color_p);
  // lv_disp_flush_ready() called on reload interrupt

#else // Single full frame buffer

  lv_coord_t hres = disp_drv->hor_res;
  lv_coord_t vres = disp_drv->ver_res;

  // Area must be within display
  if(!(area->x2 < 0 || area->y2 < 0 || area->x1 > hres - 1 || area->y1 > vres - 1)) {

    // Copy area into display buffer
#  if LV_COLOR_DEPTH == 32 || LV_COLOR_DEPTH == 24 || LV_COLOR_DEPTH == LCD_COLOR_DEPTH
    size_t w = lv_area_get_width(area);
    // Copy entire lines
    for(int32_t y = area->y1; y <= area->y2 && y < disp_drv->ver_res; y++) {
      memcpy(&framebuf_bg0[y * hres + area->x1], color_p, w * sizeof(lv_color_t));
      color_p += w;
    }
#  else // Mismatched color depths
    // Copy individual pixels and expand them into 32-bits
    for(int32_t y = area->y1; y <= area->y2 && y < disp_drv->ver_res; y++) {
      for(int32_t x = area->x1; x <= area->x2; x++) {
        framebuf_bg0[y * hres + x] = lv_color_to32(*color_p);
        color_p++;
      }
    }
#  endif
  }

lv_disp_flush_ready(disp_drv);
#endif // USE_DOUBLE_BUF

}


// LVGL indev driver callback
static void touch_input_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  data->point = g_touch_state.point;
  data->state = g_touch_state.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}


static void ts_save_calibration(TouchCalibration *touch_cal) {
  // Save cal params as persistent properties
  PropDBEntry entry = {0};

  prop_db_transact_begin(&g_prop_db);

  // Common attributes
  entry.persist = true;
  entry.kind = P_KIND_UINT;

  entry.value = touch_cal->x_scale;
  prop_set(&g_prop_db, P_HW_TOUCH_CAL__X_SCALE, &entry, P_RSRC_HW_LOCAL_TASK);

  entry.value = touch_cal->y_scale;
  prop_set(&g_prop_db, P_HW_TOUCH_CAL__Y_SCALE, &entry, P_RSRC_HW_LOCAL_TASK);

  entry.kind = P_KIND_INT;
  entry.value = touch_cal->x_offset;
  prop_set(&g_prop_db, P_HW_TOUCH_CAL__X_OFFSET, &entry, P_RSRC_HW_LOCAL_TASK);

  entry.value = touch_cal->y_offset;
  prop_set(&g_prop_db, P_HW_TOUCH_CAL__Y_OFFSET, &entry, P_RSRC_HW_LOCAL_TASK);

  prop_db_transact_end(&g_prop_db);

}


bool ts_load_calibration(TouchCalibration *touch_cal) {
  PropDBEntry entry;

  if(!prop_get(&g_prop_db, P_HW_TOUCH_CAL__X_SCALE, &entry))
    return false;

  touch_cal->x_scale = entry.value;

  if(!prop_get(&g_prop_db, P_HW_TOUCH_CAL__Y_SCALE, &entry))
    return false;

  touch_cal->y_scale = entry.value;


  if(!prop_get(&g_prop_db, P_HW_TOUCH_CAL__X_OFFSET, &entry))
    return false;

  touch_cal->x_offset = entry.value;

  if(!prop_get(&g_prop_db, P_HW_TOUCH_CAL__Y_OFFSET, &entry))
    return false;

  touch_cal->y_offset = entry.value;

  g_use_touch_calibration = true;
  return true;
}



bool ts_set_calibration(TouchCalPoint *p0, TouchCalPoint *p1) {
  g_touch_cal.x_scale = ((int32_t)p1->phys.x - p0->phys.x) * TS_CAL_FP_SCALE / 
                        ((int32_t)p1->ts.x - p0->ts.x);
  g_touch_cal.x_offset = (int32_t)p0->phys.x * TS_CAL_FP_SCALE - g_touch_cal.x_scale * p0->ts.x;

  g_touch_cal.y_scale = ((int32_t)p1->phys.y - p0->phys.y) * TS_CAL_FP_SCALE / 
                        ((int32_t)p1->ts.y - p0->ts.y);
  g_touch_cal.y_offset = (int32_t)p0->phys.y * TS_CAL_FP_SCALE - g_touch_cal.y_scale * p0->ts.y;

  printf("P0: %d %d -> %d %d\n", p0->phys.x, p0->phys.y, p0->ts.x, p0->ts.y);
  printf("P1: %d %d -> %d %d\n", p1->phys.x, p1->phys.y, p1->ts.x, p1->ts.y);

  printf("CAL:  %d %d  x %d %d\n", g_touch_cal.x_scale, g_touch_cal.x_offset,
        g_touch_cal.y_scale, g_touch_cal.y_offset);

  // Sanity check cal settings
  bool status = true;

  int32_t scale_mag = g_touch_cal.x_scale - TS_CAL_FP_SCALE;
  if(scale_mag < 0) scale_mag = -scale_mag;
  if(scale_mag*10 / TS_CAL_FP_SCALE >= 2) {
    printf("X mag bad %ld\n", scale_mag);
    status = false;
  }

  scale_mag = g_touch_cal.y_scale - TS_CAL_FP_SCALE;
  if(scale_mag < 0) scale_mag = -scale_mag;
  if(scale_mag*10 / TS_CAL_FP_SCALE >= 2) {
    printf("Y mag bad %ld\n", scale_mag);
    status = false;
  }

  int32_t px_off = g_touch_cal.x_offset / TS_CAL_FP_SCALE;
  if(px_off < 0) px_off = -px_off;
  if(px_off > 40) {
    puts("X off bad");
    status = false;
  }

  px_off = g_touch_cal.y_offset / TS_CAL_FP_SCALE;
  if(px_off < 0) px_off = -px_off;
  if(px_off > 40) {
    puts("Y off bad");
    status = false;
  }

  if(status)
    ts_save_calibration(&g_touch_cal);

  return status;
}


int32_t cmd_tscal(uint8_t argc, char *argv[], void *eval_ctx) {
  GetoptState state = {0};
  state.report_errors = true;

  int c;
  bool show_cal = false;

  while((c = getopt_r(argv, "c", &state)) != -1) {
    switch(c) {
    case 'c':
      show_cal = true; break;

    default:
    case ':':
    case '?':
      return -3;
      break;
    }
  }

  if(show_cal) {
    printf("X-scale:  %u\n", g_touch_cal.x_scale);
    printf("Y-scale:  %u\n", g_touch_cal.y_scale);
    printf("X-offset: %d\n", g_touch_cal.x_offset);
    printf("Y-offset: %d\n", g_touch_cal.y_offset);

  } else {
    begin_ts_cal();
  }
  return 0;
}



static inline void ts__apply_calibration(lv_point_t *ts, lv_point_t *pt) {
  int32_t x = ((int32_t)ts->x * g_touch_cal.x_scale + g_touch_cal.x_offset) / TS_CAL_FP_SCALE;
  int32_t y = ((int32_t)ts->y * g_touch_cal.y_scale + g_touch_cal.y_offset) / TS_CAL_FP_SCALE;

  // Clamp to positive values
  pt->x = x >= 0 ? x : 0;
  pt->y = y >= 0 ? y : 0;
}


//Debouncer<TOUCH_POLL_TASK_MS, TOUCH_DEBOUNCE_FILTER_MS> touch_button(false);
static Debouncer s_touch_button;


void touch_poll_task_cb(TimerHandle_t timer) {
  TS_StateTypeDef  ts_state;

  BSP_TS_GetState(&ts_state);

  debouncer_filter_sample(&s_touch_button, ts_state.TouchDetected);

  /*  The TS position is only valid when a touch is active. Because the filter
      introduces a delay, the position data is already invalid when the filtered
      touch is released. We qualify the touch point updates with the filtered and
      unfiltered state to prevent setting the cursor to an invalid location.
  */
  bool pressed = debouncer_filtered(&s_touch_button);
  if(pressed && ts_state.TouchDetected) {
    // Convert to LVGL point
    lv_point_t ts;
    ts.x = ts_state.X;
    ts.y = ts_state.Y;

    if(g_use_touch_calibration) {
      ts__apply_calibration(&ts, &g_touch_state.point);
    } else {  // No calibration (required when performing calibration process)
      g_touch_state.point = ts;
    }
  }

  g_touch_state.pressed = pressed;
}


void lvgl_stm32_init(void) {
  lv_init();

  // Setup LVGL draw buffers

  static lv_disp_drv_t s_disp_drv;
  static lv_disp_draw_buf_t disp_buf1;

#ifdef USE_DOUBLE_BUF

  lv_disp_draw_buf_init(&disp_buf1, framebuf_bg0, framebuf_bg1, LCD_HOR_RES * LCD_VER_RES);

#else // Create smaller draw buffers

#  define DRAW_BUF_LINES  40
  __attribute__(( section(".sdram") ))
  static lv_color_t buf1_1[LCD_HOR_RES * DRAW_BUF_LINES] alignas(BURST_ALIGN);
  lv_disp_draw_buf_init(&disp_buf1, buf1_1, NULL, LCD_HOR_RES * DRAW_BUF_LINES);

//  __attribute__(( section(".sdram") ))
//  static lv_color_t buf1_2[LCD_HOR_RES * DRAW_BUF_LINES] alignas(BURST_ALIGN);
//  lv_disp_draw_buf_init(&disp_buf1, buf1_1, buf1_2, LCD_HOR_RES * DRAW_BUF_LINES);

#endif

  // Configure display

  lv_disp_drv_init(&s_disp_drv);
  s_disp_drv.draw_buf     = &disp_buf1;
  s_disp_drv.flush_cb     = lcd_flush;
  s_disp_drv.hor_res      = LCD_HOR_RES;
  s_disp_drv.ver_res      = LCD_VER_RES;
  s_disp_drv.antialiasing = 1;
//  s_disp_drv.rotated      = LV_DISP_ROT_270;
//  s_disp_drv.sw_rotate    = 1;  // Does not work in full frame mode
#ifdef USE_DOUBLE_BUF
  s_disp_drv.full_refresh = 1;
#endif

  g_disp_main = lv_disp_drv_register(&s_disp_drv);


  lv_group_t *def_grp = lv_group_create();
  lv_group_set_default(def_grp);

  // Configure input devices

  // Nav buttons
  static lv_indev_drv_t nav_indev_drv;
  lv_indev_drv_init(&nav_indev_drv);
  nav_indev_drv.type = LV_INDEV_TYPE_ENCODER;
  nav_indev_drv.read_cb = nav_input_read;
  lv_indev_t *nav_indev = lv_indev_drv_register(&nav_indev_drv);
  lv_indev_set_group(nav_indev, def_grp);

  // Touchscreen
  static lv_indev_drv_t ts_indev_drv;
  lv_indev_drv_init(&ts_indev_drv);
  ts_indev_drv.type = LV_INDEV_TYPE_POINTER;
  ts_indev_drv.read_cb = touch_input_read;

#if 0
  lv_indev_drv_register(&ts_indev_drv);
#else
  // Show mouse cursor
  lv_indev_t *ts_indev = lv_indev_drv_register(&ts_indev_drv);
  lv_obj_t *cursor_obj = lv_img_create(lv_scr_act());
  lv_img_set_src(cursor_obj, &cursor_v);
  lv_indev_set_cursor(ts_indev, cursor_obj);
#endif

#  define TOUCH_POLL_TASK_MS 10
#  define TOUCH_DEBOUNCE_FILTER_MS 50
  debouncer_init(&s_touch_button, TOUCH_POLL_TASK_MS, TOUCH_DEBOUNCE_FILTER_MS, false);

  TimerHandle_t touch_poll_timer = xTimerCreate(  // Touch screen poll
    "TouchPoll",
    TOUCH_POLL_TASK_MS,
    pdTRUE, // uxAutoReload
    NULL,   // pvTimerID
    touch_poll_task_cb
  );

  xTimerStart(touch_poll_timer, 0);
}

#endif // PLATFORM_EMBEDDED

#ifndef PLATFORM_EMBEDDED
void lvgl_sim_init(void) {
  lv_init();

  monitor_init(); // Monitor driver for display

  // Setup LVGL draw buffers

  static lv_disp_drv_t s_disp_drv;
  static lv_disp_draw_buf_t disp_buf1;

#ifdef USE_DOUBLE_BUF

  lv_disp_draw_buf_init(&disp_buf1, framebuf_bg0, framebuf_bg1, LCD_HOR_RES * LCD_VER_RES);

#else // Create smaller draw buffers

#  define DRAW_BUF_LINES  40
  static lv_color_t buf1_1[LCD_HOR_RES * DRAW_BUF_LINES] alignas(BURST_ALIGN);
//  lv_disp_draw_buf_init(&disp_buf1, buf1_1, NULL, LCD_HOR_RES * DRAW_BUF_LINES);

  static lv_color_t buf1_2[LCD_HOR_RES * DRAW_BUF_LINES] alignas(BURST_ALIGN);
  lv_disp_draw_buf_init(&disp_buf1, buf1_1, buf1_2, LCD_HOR_RES * DRAW_BUF_LINES);

#endif

  // Configure display

  lv_disp_drv_init(&s_disp_drv);
  s_disp_drv.draw_buf     = &disp_buf1;
  s_disp_drv.flush_cb     = monitor_flush;
  s_disp_drv.hor_res      = LCD_HOR_RES;
  s_disp_drv.ver_res      = LCD_VER_RES;
  s_disp_drv.antialiasing = 1;
//  s_disp_drv.rotated      = LV_DISP_ROT_270;
//  s_disp_drv.sw_rotate    = 1;  // Does not work in full frame mode
#ifdef USE_DOUBLE_BUF
  s_disp_drv.full_refresh = 1;
#endif

  g_disp_main = lv_disp_drv_register(&s_disp_drv);


  lv_group_t *def_grp = lv_group_create();
  lv_group_set_default(def_grp);

  // Configure input devices

  // Mouse cursor
  static lv_indev_drv_t mouse_drv;
  lv_indev_drv_init(&mouse_drv);
  mouse_drv.type = LV_INDEV_TYPE_POINTER;
  mouse_drv.read_cb = sdl_mouse_read;
  lv_indev_drv_register(&mouse_drv);

  // Mousewheel acts as encoder
  static lv_indev_drv_t mousewheel_drv;
  lv_indev_drv_init(&mousewheel_drv);
  mousewheel_drv.type = LV_INDEV_TYPE_ENCODER;
  mousewheel_drv.read_cb = sdl_mousewheel_read;

  lv_indev_t *enc_indev = lv_indev_drv_register(&mousewheel_drv);
  lv_indev_set_group(enc_indev, def_grp);

  // Keyboard control
  static lv_indev_drv_t keyboard_drv;
  lv_indev_drv_init(&keyboard_drv);
  keyboard_drv.type = LV_INDEV_TYPE_KEYPAD;
  keyboard_drv.read_cb = sdl_keyboard_read;

  lv_indev_t *kb_indev = lv_indev_drv_register(&keyboard_drv);
  lv_indev_set_group(kb_indev, def_grp);
}
#endif // PLATFORM_EMBEDDED

#ifdef USE_DOUBLE_BUF
void LTDC_reload_event_callback(LTDC_HandleTypeDef *hltdc) {
  // NOTE: Move this call to a deferred interrupt task if LV_COLOR_SCREEN_TRANSP is
  // enabled. Otherwise a full frame memset is called in interrupt context.
  lv_disp_flush_ready(&s_disp_drv);
}
#endif


#ifdef PLATFORM_EMBEDDED
void lcd_init(void) {
//  printf("## LCD: %lux%lu\n", BSP_LCD_GetXSize(), BSP_LCD_GetYSize());

  // Default pixel format: ARGB8888
  BSP_LCD_LayerDefaultInit(LCD_BACKGROUND_LAYER, (uintptr_t)framebuf_bg0);
  BSP_LCD_LayerDefaultInit(LCD_FOREGROUND_LAYER, (uintptr_t)framebuf_fg);

#  if LCD_COLOR_DEPTH == 16
  HAL_LTDC_SetPixelFormat(&LtdcHandler, LTDC_PIXEL_FORMAT_RGB565, LCD_BACKGROUND_LAYER);
  HAL_LTDC_SetPixelFormat(&LtdcHandler, LTDC_PIXEL_FORMAT_RGB565, LCD_FOREGROUND_LAYER);
#  endif

  // Configure background layer
  BSP_LCD_SelectLayer(LCD_BACKGROUND_LAYER);
  BSP_LCD_Clear(LCD_COLOR_BLACK);

  // Configure foreground layer
  BSP_LCD_SelectLayer(LCD_FOREGROUND_LAYER);
  BSP_LCD_Clear(LCD_COLOR_GREEN);
  BSP_LCD_SetColorKeying(LCD_FOREGROUND_LAYER, LCD_COLOR_GREEN);
  BSP_LCD_SetTransparency(LCD_FOREGROUND_LAYER, 50);

  BSP_LCD_SetBackColor(LCD_COLOR_GREEN);

  BSP_LCD_SetTextColor(LCD_COLOR_RED);
  // Add transparent overlay for screen portion that won't appear on GBA LCD (240x160)
  BSP_LCD_FillRect(0,160, 240, 160);

  //BSP_LCD_DisplayStringAt(20,20, (uint8_t *)"Hello!", LEFT_MODE);


  BSP_LCD_SetLayerVisible(LCD_BACKGROUND_LAYER, ENABLE);
  BSP_LCD_SetLayerVisible(LCD_FOREGROUND_LAYER, ENABLE);

  BSP_LCD_DisplayOn();

  // Setup touchscreen
  BSP_TS_Init(BSP_LCD_GetXSize(), BSP_LCD_GetYSize());

#  ifdef USE_DOUBLE_BUF
  // Configure interrupt for LTDC reload
  HAL_LTDC_RegisterCallback(&LtdcHandler, HAL_LTDC_RELOAD_EVENT_CB_ID, LTDC_reload_event_callback);
  HAL_NVIC_SetPriority(LTDC_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(LTDC_IRQn);
#  endif
}
#endif // PLATFORM_EMBEDDED

