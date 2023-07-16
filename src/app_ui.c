#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>

#include "lib_cfg/build_config.h"
#include "lib_cfg/cstone_cfg_stm32.h"
#include "app_main.h"

#include "cstone/prop_id.h"
#include "app_prop_id.h"
#include "cstone/prop_db.h"
#include "util/mempool.h"
#include "cstone/umsg.h"
#include "cstone/debug.h"

//#include "util/dhash.h"
#include "lvgl/lvgl.h"
#include "ui_panel.h"
#include "ui_units.h"
#include "app_ui.h"
#include "util/range_strings.h"
#include "util/intmath.h"


#define TILEVIEW_COLOR          lv_palette_lighten(LV_PALETTE_GREY, 3)
#define TILEVIEW_COLOR_DARK     lv_palette_darken(LV_PALETTE_GREY, 4)

#define TACHO_MAIN_COLOR        lv_palette_lighten(LV_PALETTE_GREY, 1)
#define TACHO_MAIN_COLOR_DARK   lv_palette_darken(LV_PALETTE_GREY, 3)
#define TACHO_IND_COLOR         lv_palette_main(LV_PALETTE_BLUE)
#define TACHO_BAR_W             12

#define GEAR_POS_BG_COLOR       lv_palette_darken(LV_PALETTE_BLUE_GREY, 2)

#define UTF8_DEGREE "\xc2\xb0"

#define FIXED_24_8    (1 << 8)

lv_disp_t *g_disp_main;

AppPanels g_panels = {0};
UIStyles g_ui_styles;

extern UIReactWidgets g_react_widgets;
extern UIWidgetRegistry  g_widget_reg;

extern const lv_font_t rubik_med_italic_24;
extern const lv_font_t rubik_med_italic_48;

LV_IMG_DECLARE(cal_target);
LV_IMG_DECLARE(gear_pos);
LV_IMG_DECLARE(battery_icon);
LV_IMG_DECLARE(temp_icon);
LV_IMG_DECLARE(speedo_icon);
LV_IMG_DECLARE(speedo_avg_icon);
LV_IMG_DECLARE(fuel_icon);
LV_IMG_DECLARE(checked_icon);
LV_IMG_DECLARE(triumph_logo);
LV_IMG_DECLARE(sprint_st_40);
LV_IMG_DECLARE(sprint_st_side_stand_40);


extern bool ts_set_calibration(TouchCalPoint *p0, TouchCalPoint *p1);


// ******************** App props ********************


static int32_t convert_si_value(int32_t si_value, int32_t fp_scale, uint32_t prop) {
  PropDBEntry value;
  prop_get(&g_prop_db, prop, &value); // Get units enum for si_value

  return convert_to_unit(si_value, fp_scale, (UIUnits)value.value);
}


static void update_speed_value(uint32_t prop) {
  PropDBEntry value;

  printf("## Update speed = P%04lX\n", prop);

  if(prop_get(&g_prop_db, prop, &value)) {
    lv_obj_t *obj = ui_panel_get_obj(&g_panels.instr, W_INSTR_LBL_SPEED);
    if(obj) {
      int32_t unit_val = convert_si_value(value.value, FIXED_24_8, P_APP_GUI_UNITS__SPEED);
      // Format 24.8 fixed point value
      unit_val = ufixed_to_uint(unit_val, FIXED_24_8); // Round up
          printf("## value = %d  %ld\n", value.value, unit_val);
      lv_label_set_text_fmt(obj, "%3" PRIu32, unit_val); // Right justify 3 digits
    }

    // Update max speed
    uint32_t cur_speed = value.value;
    prop_get(&g_prop_db, P_SENSOR_ECU__SPEED__MAX, &value);
    if(cur_speed > value.value) {
      value.value = cur_speed;
      prop_set(&g_prop_db, P_SENSOR_ECU__SPEED__MAX, &value, 0);
    }

  }
}


static void update_voltage_value(uint32_t prop) {
  PropDBEntry value;
  char buf[8]; // "nn.n V"
  AppendRange rng;

  if(prop_get(&g_prop_db, prop, &value)) {
    lv_obj_t *obj = ui_panel_get_obj(&g_panels.instr, W_INSTR_LBL_VOLTAGE);
    if(obj) {
      // Format 24.8 fixed point value
      range_init(&rng, buf, sizeof(buf));
      range_cat_ufixed(&rng, value.value, FIXED_24_8, 1);
      range_cat_str(&rng, " V");
      lv_label_set_text(obj, buf);
    }
  }
}

static void update_coolant_temp_value(uint32_t prop) {
  PropDBEntry value;

  if(prop_get(&g_prop_db, prop, &value)) {
    lv_obj_t *obj = ui_panel_get_obj(&g_panels.instr, W_INSTR_LBL_COOLANT_TEMP);
    if(obj) {
      int32_t unit_val = convert_si_value(value.value, 1, P_APP_GUI_UNITS__TEMPERATURE);
      prop_get(&g_prop_db, P_APP_GUI_UNITS__TEMPERATURE, &value); // Get units enum
      lv_label_set_text_fmt(obj, "%3" PRIu32 " " UTF8_DEGREE "%s", unit_val, get_unit_text(value.value));
    }
  }
}


static void update_fuel_value(uint32_t prop) {
  PropDBEntry value;

  if(prop_get(&g_prop_db, prop, &value)) {
    lv_obj_t *obj = ui_panel_get_obj(&g_panels.instr, W_INSTR_LBL_FUEL);
    if(obj) {
      if(value.value > 100)
        value.value = 100;
      lv_label_set_text_fmt(obj, "%3" PRIu32 " %%", (uint32_t)value.value);
    }
  }
}


static void update_speed_average(uint32_t prop) {
  PropDBEntry value;
  char buf[14]; // "nnn.nn km/h"
  AppendRange rng;

  if(prop_get(&g_prop_db, prop, &value)) {
    lv_obj_t *obj = ui_panel_get_obj(&g_panels.instr, W_INSTR_LBL_SPEED_AVG);
    if(obj) {
      uint32_t unit_val = convert_si_value(value.value, FIXED_24_8, P_APP_GUI_UNITS__SPEED);
//      printf("## SPEED AVG: %u  %lu\n", value.value, unit_val);

      // Format 24.8 fixed point value with units
      range_init(&rng, buf, sizeof(buf));
      range_cat_ufixed(&rng, unit_val, FIXED_24_8, 1);
      range_cat_char(&rng, ' ');
      prop_get(&g_prop_db, P_APP_GUI_UNITS__SPEED, &value); // Get units enum
      range_cat_str(&rng, get_unit_text(value.value));
      lv_label_set_text(obj, buf);
    }
  }
}

static void update_speed_max(uint32_t prop) {
  PropDBEntry value;
  char buf[14]; // "nnn.nn km/h"
  AppendRange rng;

  if(prop_get(&g_prop_db, prop, &value)) {
    lv_obj_t *obj = ui_panel_get_obj(&g_panels.instr, W_INSTR_LBL_SPEED_MAX);
    if(obj) {
      uint32_t unit_val = convert_si_value(value.value, FIXED_24_8, P_APP_GUI_UNITS__SPEED);
//      printf("## SPEED MAX: %u  %lu\n", value.value, unit_val);

      // Format 24.8 fixed point value with units
      range_init(&rng, buf, sizeof(buf));
      range_cat_ufixed(&rng, unit_val, FIXED_24_8, 0);
      range_cat_char(&rng, ' ');
      prop_get(&g_prop_db, P_APP_GUI_UNITS__SPEED, &value); // Get units enum
      range_cat_str(&rng, get_unit_text(value.value));
      lv_label_set_text(obj, buf);
    }
  }
}


void gui_prop_msg_handler(UMsgTarget *tgt, UMsg *msg) {
  // Avoid message loops for props set by LVGL widget updates
  if(msg->source == P_RSRC_GUI_LOCAL_WIDGET)
    return;

  PropDBEntry value;
  switch(msg->id) {
  case P_APP_GUI_INFO__DARK:
    // Change LVGL dark mode setting
    if(prop_get(&g_prop_db, msg->id, &value))
      set_theme_mode(g_disp_main, value.value);
    break;

  case P_SENSOR_ECU__SPEED__VALUE:
    update_speed_value(msg->id);
    break;

  case P_APP_GUI_UNITS__SPEED:
    {
      update_speed_value(P_SENSOR_ECU__SPEED__VALUE);
      update_speed_average(P_SENSOR_ECU__SPEED__AVERAGE);
      update_speed_max(P_SENSOR_ECU__SPEED__MAX);

      // Update units label
      lv_obj_t *label = ui_panel_get_obj(&g_panels.instr, W_INSTR_LBL_SPEED_UNIT);
      if(label)
        set_unit_text(label, P_APP_GUI_UNITS__SPEED);
    }
    break;

  case P_APP_GUI_UNITS__TEMPERATURE:
    update_coolant_temp_value(P_SENSOR_ECU__COOLANT_TEMP__VALUE);
    break;

  case P_SENSOR_ECU__VOLTAGE__VALUE:
    update_voltage_value(msg->id);
    break;

  case P_SENSOR_ECU__COOLANT_TEMP__VALUE:
    update_coolant_temp_value(msg->id);
    break;

  case P_SENSOR_ECU__FUEL__VALUE:
    update_fuel_value(msg->id);
    break;

  case P_SENSOR_ECU__SPEED__AVERAGE:
    update_speed_average(msg->id);
    break;

  case P_SENSOR_ECU__SPEED__MAX:
    update_speed_max(msg->id);
    break;


  case P_APP_GUI_MENU__MODE:
    if(prop_get(&g_prop_db, msg->id, &value))
      update_gui_menu_mode(value.value);
    break;

  case P_SENSOR_ECU__RPM__VALUE:
    if(prop_get(&g_prop_db, msg->id, &value))
      update_tacho(value.value);
    break;

  case P_SENSOR_ECU__SIDESTAND__VALUE:
    if(prop_get(&g_prop_db, msg->id, &value))
      update_sidestand(value.value);
    break;

  case P_SENSOR_ECU__GEAR__VALUE:
    if(prop_get(&g_prop_db, msg->id, &value))
      update_gear_pos(value.value);
    break;

  default:
    break;
  }

  ui_react_widgets_update(&g_react_widgets, &g_widget_reg, msg->id);
}


// Properties that need to be checked on startup to ensure initial state of
// GUI is correct before first frame is rendered.
static const uint32_t s_default_gui_props[] = {
  P_APP_GUI_INFO__DARK,
  P_APP_GUI_UNITS__SPEED,
  P_APP_GUI_UNITS__TEMPERATURE
};

void gui_prop_init(void) {
  UMsg msg = {0};

  // Send messages directly to the handler since the msg hub isn't usable
  // before the scheduler is active.
  for(unsigned i = 0; i < COUNT_OF(s_default_gui_props); i++) {
    msg.id = s_default_gui_props[i];
    gui_prop_msg_handler(NULL, &msg);
  }
}


// ******************** App styling ********************

lv_theme_t *set_theme_mode(lv_disp_t *disp, bool dark_mode) {
  // Change global theme
  lv_theme_t *th = lv_theme_default_init(disp,
                            lv_palette_main(LV_PALETTE_BLUE),
                            lv_palette_main(LV_PALETTE_TEAL),
                            dark_mode, LV_FONT_DEFAULT);

  lv_obj_t *obj;
  obj = ui_panel_get_obj(&g_panels.instr, W_INSTR_TILEVIEW);
  if(obj) { // Instrument panel has been configured
    lv_color_t tileview_color = dark_mode ? TILEVIEW_COLOR_DARK : TILEVIEW_COLOR;
    lv_color_t tacho_color = dark_mode ? TACHO_MAIN_COLOR_DARK : TACHO_MAIN_COLOR;

    // Modify status tileview style
    lv_obj_set_style_bg_color(obj, tileview_color, 0);

    // Modify tacho style
    lv_style_t *sty_tach = ui_styles_get(&g_ui_styles, P_APP_GUI_STYLE__TACH);
    if(sty_tach)
      lv_style_set_bg_color(sty_tach, tacho_color);

    // Modify graphic tacho corner piece
    obj = ui_panel_get_obj(&g_panels.instr, W_INSTR_TACH_C);
    if(obj)
      lv_obj_set_style_arc_color(obj, tacho_color, LV_PART_MAIN);
  }

  return th;
}


void app_styles_init(void) {
  lv_style_t *style;
  ui_styles_init(&g_ui_styles);

  // UI_STYLE_TACH
  style = (lv_style_t *)malloc(sizeof(*style));
  if(style && ui_styles_add(&g_ui_styles, P_APP_GUI_STYLE__TACH, style)) {
    lv_style_init(style);
    lv_style_set_radius(style, 0);
    lv_style_set_bg_color(style, TACHO_MAIN_COLOR_DARK);
    lv_style_set_bg_opa(style, LV_OPA_COVER);
  }

  // UI_STYLE_TACH_IND
  style = (lv_style_t *)malloc(sizeof(*style));
  if(style && ui_styles_add(&g_ui_styles, P_APP_GUI_STYLE__TACH_IND, style)) {
    lv_style_init(style);
    lv_style_set_radius(style, 0);
    lv_style_set_bg_color(style, TACHO_IND_COLOR);
  }


  // UI_STYLE_FRAME_CLEAR
  style = (lv_style_t *)malloc(sizeof(*style));
  if(style && ui_styles_add(&g_ui_styles, P_APP_GUI_STYLE__FRAME_CLEAR, style)) {
    lv_style_init(style);
    lv_style_set_radius(style, 0);
    //lv_style_set_bg_opa(style, LV_OPA_60);
    lv_style_set_bg_opa(style, LV_OPA_0);
    lv_style_set_border_width(style, 0);
    lv_style_set_pad_all(style, 0);
  }

  // UI_STYLE_MENU_BAR
  style = (lv_style_t *)malloc(sizeof(*style));
  if(style && ui_styles_add(&g_ui_styles, P_APP_GUI_STYLE__MENU_BAR, style)) {
    lv_style_init(style);
    lv_style_set_radius(style, 0);
    lv_style_set_bg_color(style, lv_palette_darken(LV_PALETTE_BLUE_GREY, 2));
    lv_style_set_bg_grad_color(style, lv_palette_main(LV_PALETTE_BLUE_GREY));
    lv_style_set_bg_grad_dir(style, LV_GRAD_DIR_VER);

    lv_style_set_bg_opa(style, LV_OPA_100);
    //lv_style_set_bg_opa(style, LV_OPA_0);
    lv_style_set_border_width(style, 0);
    lv_style_set_pad_all(style, 0);
  }


  // UI_STYLE_MENU_ITEM
  style = (lv_style_t *)malloc(sizeof(*style));
  if(style && ui_styles_add(&g_ui_styles, P_APP_GUI_STYLE__MENU_ITEM, style)) {
    lv_style_init(style);
    lv_style_set_radius(style, 4);
    lv_style_set_bg_color(style, lv_palette_darken(LV_PALETTE_BLUE, 2));
    lv_style_set_bg_opa(style, LV_OPA_100);
    lv_style_set_border_width(style, 0);
    lv_style_set_pad_all(style, 1);
    lv_style_set_outline_pad(style, 1);
    lv_style_set_pad_row(style, 1);
  }

}

void app_screens_init(void) {
  ui_instruments_init(&g_panels.instr);
  ui_ts_cal_init(&g_panels.ts_cal);
  ui_splash_init(&g_panels.splash);
}


// ******************** Instrument panel ********************


static void mode_event_handler(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *obj = lv_event_get_target(e);
  if(code == LV_EVENT_VALUE_CHANGED) {
    bool checked = lv_obj_has_state(obj, LV_STATE_CHECKED);

    PropDBEntry entry = {
      .value  = checked,
      .size   = 0,
      .kind   = P_KIND_UINT
    };
    prop_set(&g_prop_db, P_APP_GUI_INFO__DARK, &entry, P_RSRC_GUI_LOCAL_WIDGET);

    set_theme_mode(g_disp_main, checked);
  }
}


static void menu_mode_event_handler(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *obj = lv_event_get_target(e);
  if(code == LV_EVENT_VALUE_CHANGED) {
    bool checked = lv_obj_has_state(obj, LV_STATE_CHECKED);

    PropDBEntry entry = {
      .value  = checked,
      .size   = 0,
      .kind   = P_KIND_UINT
    };
    prop_set(&g_prop_db, P_APP_GUI_MENU__MODE, &entry, P_RSRC_GUI_LOCAL_WIDGET);

    update_gui_menu_mode(checked);
  }
}


static void speed_slider_event_cb(lv_event_t * e) {
    lv_obj_t *slider = lv_event_get_target(e);
    lv_obj_t *lbl_speed = ui_panel_get_obj(&g_panels.instr, W_INSTR_LBL_SPEED);
    lv_label_set_text_fmt(lbl_speed, "%3" PRIu32, (uint32_t)lv_slider_get_value(slider)*2);
}

void update_tacho(uint16_t rpm) {
  const uint16_t corner_start = 1000;
  const uint16_t corner_end   = 2500;
  const uint16_t top_end      = 9500;

  lv_obj_t *lbl_rpm = ui_panel_get_obj(&g_panels.instr, W_INSTR_LBL_RPM);
  if(lbl_rpm)
    lv_label_set_text_fmt(lbl_rpm, "%5u", rpm);

  uint32_t pct;
  lv_obj_t *tach_c = ui_panel_get_obj(&g_panels.instr, W_INSTR_TACH_C);
  if(!tach_c) return;

  lv_obj_t *tach_v = ui_panel_get_obj(&g_panels.instr, W_INSTR_TACH_V);
  if(!tach_v) return;

  lv_obj_t *tach_h = ui_panel_get_obj(&g_panels.instr, W_INSTR_TACH_H);
  if(!tach_h) return;


  if(rpm <= corner_start) {
    lv_bar_set_value(tach_h, 0, LV_ANIM_OFF);
    lv_arc_set_value(tach_c, 0);

    pct = (uint32_t)rpm * 100 / corner_start;
    lv_bar_set_value(tach_v, pct, LV_ANIM_OFF);

  } else if(rpm <= corner_end) {
    lv_bar_set_value(tach_v, 100, LV_ANIM_OFF);
    lv_bar_set_value(tach_h, 0, LV_ANIM_OFF);

    pct = (uint32_t)(rpm-corner_start) * 100 / (corner_end - corner_start);
    lv_arc_set_value(tach_c, pct);

  } else {
    lv_bar_set_value(tach_v, 100, LV_ANIM_OFF);
    lv_arc_set_value(tach_c, 100);

    if(rpm < top_end)
      pct = (uint32_t)(rpm-corner_end) * 100 / (top_end - corner_end);
    else
      pct = 100;
    lv_bar_set_value(tach_h, pct, LV_ANIM_OFF);
  }
}

static void rpm_slider_event_cb(lv_event_t * e) {
  lv_obj_t *slider = lv_event_get_target(e);

  uint16_t rpm = lv_slider_get_value(slider)*100;
  update_tacho(rpm);
}


static void set_sprint_icon_angle(void *img, int32_t a) {
  lv_img_set_angle(img, a);
}

#define SS_RETRACT_ANGLE  200 // 20 deg.
#define SS_RETRACT_ZOOM   100 // 0.4x
#define SS_RETRACT_Y      (-20)

static void set_stand_icon_loc(void *img, int32_t p) {
  int32_t a = SS_RETRACT_ANGLE + (p * (-200 - SS_RETRACT_ANGLE) / 1024); // +/- 20 deg
  int32_t z = SS_RETRACT_ZOOM + (p * (256 - SS_RETRACT_ZOOM) / 1024); // 0.4 - 1.0
  int32_t y = SS_RETRACT_Y + p * (-7 - SS_RETRACT_Y) / 1024; // -20 - -7
  lv_img_set_angle(img, a);
  lv_img_set_zoom(img, z);
  lv_obj_set_style_translate_y(img, y, LV_PART_MAIN);
}

static void set_menu_sprint_pos(void *obj, int32_t x) {
  lv_obj_set_style_translate_x(obj, x, LV_PART_MAIN);
}


static lv_anim_timeline_t *s_icon_anim = NULL;

void update_sidestand(bool down) {
  static bool currently_down = false;

  if(down != currently_down) {
    lv_anim_timeline_stop(s_icon_anim);
    // Forward = Tilting down
    // Reverse = Tilting up (!down)
    lv_anim_timeline_set_reverse(s_icon_anim, !down);
    lv_anim_timeline_start(s_icon_anim);

    currently_down = down;
  }
}


static int32_t s_cur_gear_pos = 0; // Assume Neutral by default
static lv_anim_t s_gear_pos_img_a;
#define GEAR_POS_ANIM_P_SCALE   128

void update_gear_pos(uint32_t gear) {
  if(gear > 6)
    gear = 6;

  // Configure animation for change
  if((int32_t)gear != s_cur_gear_pos) {
    lv_anim_set_values(&s_gear_pos_img_a,
                      (int32_t)s_cur_gear_pos * GEAR_POS_ANIM_P_SCALE,
                      (int32_t)gear * GEAR_POS_ANIM_P_SCALE);
    lv_anim_start(&s_gear_pos_img_a);
  }

  s_cur_gear_pos = gear;
}


static void set_gear_pos(void *img, int32_t p) {
  signed gear_pos_y_off = -32*(6*GEAR_POS_ANIM_P_SCALE - p) / GEAR_POS_ANIM_P_SCALE;
  lv_obj_set_style_translate_y(img, gear_pos_y_off, LV_PART_MAIN);
}


static void set_speedo_pos(void *obj, int32_t p) {
#define SPEEDO_ANIM_DELTA_X   (-30)
#define SPEEDO_ANIM_DELTA_Y   (-66)

  int32_t x = p * (SPEEDO_ANIM_DELTA_X) / 1024;
  int32_t y = p * (SPEEDO_ANIM_DELTA_Y) / 1024;

  lv_obj_set_style_translate_x(obj, x, LV_PART_MAIN);
  lv_obj_set_style_translate_y(obj, y, LV_PART_MAIN);
}


static void set_tacho_pos(void *obj, int32_t xy) {
  lv_obj_set_style_translate_x(obj, xy, LV_PART_MAIN);
  lv_obj_set_style_translate_y(obj, xy, LV_PART_MAIN);

  lv_obj_t *tach_v = ui_panel_get_obj(&g_panels.instr, W_INSTR_TACH_V);
  lv_obj_set_style_translate_x(tach_v, xy, LV_PART_MAIN);
  lv_obj_set_style_translate_y(tach_v, xy, LV_PART_MAIN);

  lv_obj_t *tach_h = ui_panel_get_obj(&g_panels.instr, W_INSTR_TACH_H);
  lv_obj_set_style_translate_x(tach_h, xy, LV_PART_MAIN);
  lv_obj_set_style_translate_y(tach_h, xy, LV_PART_MAIN);
}

static void set_num_tacho_pos(void *obj, int32_t y) {
  lv_obj_set_style_translate_y(obj, y, LV_PART_MAIN);
}

static void set_gear_pos_pos(void *obj, int32_t y) {
  lv_obj_set_style_translate_y(obj, y, LV_PART_MAIN);
}

static void set_menu_pos(void *obj, int32_t y) {
  lv_obj_set_style_translate_y(obj, y, LV_PART_MAIN);
}

static void set_tv_pos(void *obj, int32_t y) {
  lv_obj_set_style_translate_y(obj, y, LV_PART_MAIN);
}


static lv_anim_timeline_t *s_menu_anim = NULL;

void update_gui_menu_mode(uint32_t mode) {
  static bool menu_active = false;
  bool new_mode = (bool)mode;

  if(new_mode != menu_active) {
    lv_anim_timeline_stop(s_menu_anim);
    lv_anim_timeline_set_reverse(s_menu_anim, !new_mode);
    lv_anim_timeline_start(s_menu_anim);
    menu_active = new_mode;
  }
}


void set_unit_text(lv_obj_t *lbl, uint32_t prop) {
  PropDBEntry value;
  prop_get(&g_prop_db, prop, &value);

  lv_label_set_text(lbl, get_unit_text(value.value));
}


/*static void back_event_handler(lv_event_t * e)*/
/*{*/
/*    lv_obj_t * obj = lv_event_get_target(e);*/
/*    lv_obj_t * menu = lv_event_get_user_data(e);*/

/*    if(lv_menu_back_btn_is_root(menu, obj)) {*/
/*        lv_obj_t * mbox1 = lv_msgbox_create(NULL, "Hello", "Root back btn click.", NULL, true);*/
/*        lv_obj_center(mbox1);*/
/*    }*/
/*}*/

/* MENU:

Configure
  Dark mode
  Units

DTCs
  Refresh
  Clear


*/

typedef void (*MenuClickHandler)(uint32_t id);

typedef struct UIMenuItem {
  uint32_t id;
  const char *label;
  uint32_t prop;
  MenuClickHandler onclick;
  struct UIMenuItem *child;
} UIMenuItem;

const UIMenuItem s_ui_menu[] = {
  {.id = M_CONFIGURE, .label = "Configure",
    .child = (UIMenuItem[]){
      {.id = M_CONFIGURE_DARK,  .label = "[x]|Dark mode"},
      {.id = M_CONFIGURE_UNITS, .label = "|Units|[s]"},
      {0}
    }
  },
//  {.id = M_DTCS+1, .label = "foobar"},
  {.id = M_SECTION, .label = "sect"},
  {.id = M_DTCS,      .label = "DTCs",
    .child = (UIMenuItem[]){
      {.id = M_DTCS_REFRESH,  .label = "Refresh"},
      {.id = M_DTCS_CLEAR,    .label = "Clear"},
      {0}
    }
  },
  {0}
};


static lv_obj_t *ui__menu_create_subtree(lv_obj_t *menu, const UIMenuItem menu_def[]) {
  lv_style_t *sty_menu_item = ui_styles_get(&g_ui_styles, P_APP_GUI_STYLE__MENU_ITEM);

  // Create a subtree page
  lv_obj_t *page = lv_menu_page_create(menu, NULL);
  lv_obj_t *cont;
  lv_obj_t *label;

  if(page) {
    lv_obj_set_style_pad_row(page, 2, LV_PART_MAIN);

    for(const UIMenuItem *cur_item = &menu_def[0]; cur_item->id; cur_item++) {
      switch(cur_item->id) {
      case M_SEPARATOR:
        lv_menu_separator_create(page);
        break;

      case M_SECTION:
        cont = lv_menu_section_create(page);
        label = lv_label_create(cont);
        lv_label_set_text(label, cur_item->label);
        DPRINT("Sect: %s", cur_item->label);
        break;

      default: // Menu item
        // Parse label
        cont = lv_menu_cont_create(page);
        lv_obj_add_style(cont, sty_menu_item, LV_PART_MAIN);
        label = lv_label_create(cont);
        lv_label_set_text(label, cur_item->label);
        DPRINT("Item: %s", cur_item->label);

        // Add item to menu index

        if(cur_item->child) {
          lv_obj_t *subpage = ui__menu_create_subtree(menu, cur_item->child);
          if(subpage)
            lv_menu_set_load_page_event(menu, cont, subpage);
        }
        break;
      }

    }
  }

  return page;
}


lv_obj_t *ui_menu_create_tree(lv_obj_t *parent, const UIMenuItem menu_def[]);

lv_obj_t *ui_menu_create_tree(lv_obj_t *parent, const UIMenuItem menu_def[]) {
  lv_obj_t *menu = lv_menu_create(parent);

  if(menu) {
    lv_obj_t *page = ui__menu_create_subtree(menu, menu_def);

    if(page)
      lv_menu_set_page(menu, page);
  }

  return menu;
}


static void sprint_click(lv_event_t *e) {
  // Toggle sidestand property
  PropDBEntry value;
  prop_get(&g_prop_db, P_SENSOR_ECU__SIDESTAND__VALUE, &value);
  value.value = value.value ? 0 : 1;
  prop_set(&g_prop_db, P_SENSOR_ECU__SIDESTAND__VALUE, &value, 0);
}


void ui_instruments_init(UIPanel *panel) {
  ui_panel_init(panel);
  lv_obj_t *label;

  lv_obj_clear_flag(g_panels.instr.screen, LV_OBJ_FLAG_SCROLLABLE);

  // Set default font
  lv_obj_set_style_text_font(g_panels.instr.screen, &lv_font_montserrat_16, 0);


  // Font style for RPM value
  static lv_style_t style_big_text;
  lv_style_init(&style_big_text);
  lv_style_set_text_font(&style_big_text, &rubik_med_italic_24);

  // Bar tacho

  // Corner part
  lv_obj_t *tach_c = lv_arc_create(g_panels.instr.screen);
  ui_panel_add_obj(panel, W_INSTR_TACH_C, tach_c);
  lv_obj_set_size(tach_c, 80,80);
  lv_arc_set_rotation(tach_c, 180);
  lv_arc_set_bg_angles(tach_c, 0, 90);
  lv_obj_set_style_arc_color(tach_c, TACHO_MAIN_COLOR_DARK, LV_PART_MAIN);
  lv_obj_set_style_arc_color(tach_c, TACHO_IND_COLOR, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(tach_c, false, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(tach_c, false, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(tach_c, TACHO_BAR_W, LV_PART_MAIN);
  lv_obj_set_style_arc_width(tach_c, TACHO_BAR_W, LV_PART_INDICATOR);
  lv_obj_remove_style(tach_c, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(tach_c, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(tach_c, LV_ALIGN_TOP_LEFT, 2,2);
  lv_arc_set_value(tach_c, 0);

  lv_style_t *sty_tach = ui_styles_get(&g_ui_styles, P_APP_GUI_STYLE__TACH);
  lv_style_t *sty_tach_ind = ui_styles_get(&g_ui_styles, P_APP_GUI_STYLE__TACH_IND);
  lv_style_t *sty_frame_clear = ui_styles_get(&g_ui_styles, P_APP_GUI_STYLE__FRAME_CLEAR);
//  lv_style_t *sty_menu_bar = ui_styles_get(&g_ui_styles, P_APP_GUI_STYLE__MENU_BAR);
//  lv_style_t *sty_menu_item = ui_styles_get(&g_ui_styles, P_APP_GUI_STYLE__MENU_ITEM);

  // Vert part
  lv_obj_t *tach_v = lv_bar_create(g_panels.instr.screen);
  ui_panel_add_obj(panel, W_INSTR_TACH_V, tach_v);
  lv_obj_set_size(tach_v, TACHO_BAR_W,50+13);
  lv_obj_add_style(tach_v, sty_tach, LV_PART_MAIN);
  lv_obj_add_style(tach_v, sty_tach_ind, LV_PART_INDICATOR);
  lv_obj_align_to(tach_v, tach_c, LV_ALIGN_BOTTOM_LEFT, 0,10+13);
  lv_bar_set_value(tach_v, 0, LV_ANIM_OFF);

  // Horiz part
  lv_obj_t *tach_h = lv_bar_create(g_panels.instr.screen);
  ui_panel_add_obj(panel, W_INSTR_TACH_H, tach_h);
  lv_obj_set_size(tach_h, 50+146,TACHO_BAR_W);
  lv_obj_add_style(tach_h, sty_tach, LV_PART_MAIN);
  lv_obj_add_style(tach_h, sty_tach_ind, LV_PART_INDICATOR);
  lv_obj_align_to(tach_h, tach_c, LV_ALIGN_TOP_RIGHT, 10+146,0);
  lv_bar_set_value(tach_h, 0, LV_ANIM_OFF);

#define LABEL_COLOR  lv_palette_darken(LV_PALETTE_GREY, 1)

  // Numeric tacho
  lv_obj_t *obj_num_tacho = lv_obj_create(g_panels.instr.screen);
//  ui_panel_add_obj(panel, W_INSTR_OBJ_TACHO, obj_num_tacho);
  lv_obj_clear_flag(obj_num_tacho, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(obj_num_tacho, 132, 30);
  lv_obj_add_style(obj_num_tacho, sty_frame_clear, LV_PART_MAIN);
  lv_obj_align(obj_num_tacho, LV_ALIGN_TOP_LEFT, 26, 18);

  lv_obj_t *lbl_rpm = lv_label_create(obj_num_tacho);
  ui_panel_add_obj(panel, W_INSTR_LBL_RPM, lbl_rpm);
  lv_obj_add_style(lbl_rpm, &style_big_text, 0);
  lv_label_set_text(lbl_rpm, "    0");
  lv_obj_align(lbl_rpm, LV_ALIGN_TOP_LEFT, 0, 0);

  label = lv_label_create(obj_num_tacho);
  lv_obj_set_style_text_font(label, &rubik_med_italic_24, 0);
  lv_obj_set_style_text_color(label, LABEL_COLOR, LV_PART_MAIN);
  lv_label_set_text(label, "rpm");
  lv_obj_align_to(label, lbl_rpm, LV_ALIGN_OUT_RIGHT_BOTTOM, 4,0);


  // Speedo
  lv_obj_t *obj_speedo = lv_obj_create(g_panels.instr.screen);
//  ui_panel_add_obj(panel, W_INSTR_OBJ_SPEED, obj_speedo);
  lv_obj_clear_flag(obj_speedo, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(obj_speedo, 160, 40);
  lv_obj_add_style(obj_speedo, sty_frame_clear, LV_PART_MAIN);
  lv_obj_align(obj_speedo, LV_ALIGN_TOP_LEFT, -SPEEDO_ANIM_DELTA_X, -SPEEDO_ANIM_DELTA_Y);

  lv_obj_t *lbl_speed = lv_label_create(obj_speedo);
  ui_panel_add_obj(panel, W_INSTR_LBL_SPEED, lbl_speed);
  lv_obj_set_style_text_font(lbl_speed, &rubik_med_italic_48, LV_PART_MAIN);
  lv_label_set_text(lbl_speed, "  0");
  lv_obj_align(lbl_speed, LV_ALIGN_TOP_LEFT, 0, -10);

  label = lv_label_create(obj_speedo);
  ui_panel_add_obj(panel, W_INSTR_LBL_SPEED_UNIT, label);
  lv_obj_set_style_text_font(label, &rubik_med_italic_24, 0);
  lv_obj_set_style_text_color(label, LABEL_COLOR, LV_PART_MAIN);
  set_unit_text(label, P_APP_GUI_UNITS__SPEED);
  lv_obj_align_to(label, lbl_speed, LV_ALIGN_OUT_RIGHT_BOTTOM, 2,-5);



  // Gear indicator
#define GEAR_POS_BORDER_W  2
  lv_obj_t *obj_gear_pos = lv_obj_create(g_panels.instr.screen);
  ui_panel_add_obj(panel, W_INSTR_GEAR_POS, obj_gear_pos);
  lv_obj_clear_flag(obj_gear_pos, LV_OBJ_FLAG_SCROLLABLE);
  lv_coord_t img_w = gear_pos.header.w;
  lv_obj_set_size(obj_gear_pos, img_w+2*GEAR_POS_BORDER_W, img_w+2*GEAR_POS_BORDER_W);
//  lv_obj_add_style(obj_gear_pos, sty_frame_clear, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(obj_gear_pos, LV_OPA_100, LV_PART_MAIN);
  lv_obj_set_style_border_width(obj_gear_pos, GEAR_POS_BORDER_W, LV_PART_MAIN);
  lv_obj_set_style_border_color(obj_gear_pos, GEAR_POS_BG_COLOR, LV_PART_MAIN);
  lv_obj_set_style_pad_all(obj_gear_pos, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(obj_gear_pos, 4, LV_PART_MAIN);


  lv_obj_align(obj_gear_pos, LV_ALIGN_TOP_RIGHT, -2,20);

  lv_obj_t *img_gear_pos = lv_img_create(obj_gear_pos);
  ui_panel_add_obj(panel, W_INSTR_GEAR_POS_IMG, img_gear_pos);
  lv_img_set_src(img_gear_pos, &gear_pos);
  lv_obj_set_size(img_gear_pos, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_align(img_gear_pos, LV_ALIGN_TOP_LEFT, 0,0);
  signed gear_pos_y_off = -32*(6-s_cur_gear_pos); // Set initial position
  lv_obj_set_style_translate_y(img_gear_pos, gear_pos_y_off, LV_PART_MAIN);

  // Animate gear changes
#define GEAR_POS_ANIM_TIME_MS 500
  lv_anim_init(&s_gear_pos_img_a);
  lv_anim_set_var(&s_gear_pos_img_a, img_gear_pos);

  lv_anim_set_exec_cb(&s_gear_pos_img_a, set_gear_pos);
  lv_anim_set_values(&s_gear_pos_img_a, 0, 6*GEAR_POS_ANIM_P_SCALE);
  lv_anim_set_time(&s_gear_pos_img_a, GEAR_POS_ANIM_TIME_MS);
  lv_anim_set_repeat_count(&s_gear_pos_img_a, 1);
  lv_anim_set_path_cb(&s_gear_pos_img_a, lv_anim_path_ease_out);



  // Side stand status
  lv_obj_t *obj_sprint = lv_obj_create(g_panels.instr.screen);
//  ui_panel_add_obj(panel, W_INSTR_OBJ_STAND, obj_speedo);
  lv_obj_clear_flag(obj_sprint, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(obj_sprint, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(obj_sprint, sprint_click, LV_EVENT_PRESSED, NULL);


  lv_obj_set_size(obj_sprint, 44, 44);
  lv_obj_add_style(obj_sprint, sty_frame_clear, LV_PART_MAIN);
  lv_obj_align(obj_sprint, LV_ALIGN_TOP_RIGHT, -2, 62);

  // Side stand icon
  lv_obj_t *stand_icon = lv_img_create(obj_sprint);
  ui_panel_add_obj(panel, W_INSTR_STAND_ICON, stand_icon);
  lv_img_set_src(stand_icon, &sprint_st_side_stand_40);
  lv_obj_set_size(stand_icon, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

#define SPRINT_ICON_W 35
#define SPRINT_ICON_H 40
  // Sprint icon
  lv_obj_t *sprint_icon = lv_img_create(obj_sprint);
  ui_panel_add_obj(panel, W_INSTR_SPRINT_ICON, sprint_icon);
  lv_img_set_src(sprint_icon, &sprint_st_40);
  lv_obj_set_size(sprint_icon, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_align(sprint_icon, LV_ALIGN_BOTTOM_RIGHT, 0,-1);
  lv_img_set_pivot(sprint_icon, SPRINT_ICON_W/2+1, SPRINT_ICON_H);

  // Position side stand
  lv_obj_align_to(stand_icon, sprint_icon, LV_ALIGN_BOTTOM_MID, 0,0);
  lv_obj_set_style_translate_x(stand_icon, -9, 0);
  lv_obj_set_style_translate_y(stand_icon, SS_RETRACT_Y, 0);
  lv_img_set_pivot(stand_icon, 11, 9+7);
  lv_img_set_angle(stand_icon, SS_RETRACT_ANGLE);  // 20 deg.
  lv_img_set_zoom(stand_icon, SS_RETRACT_ZOOM);   // 0.4x

#define SPRINT_ANIM_TIME_MS   1000

  // Animate Sprint icon
  lv_anim_t stand_a;
  lv_anim_init(&stand_a);
  lv_anim_set_var(&stand_a, stand_icon);

  lv_anim_set_exec_cb(&stand_a, set_stand_icon_loc);
  lv_anim_set_values(&stand_a, 0, 1024);
  lv_anim_set_time(&stand_a, SPRINT_ANIM_TIME_MS);
  lv_anim_set_repeat_count(&stand_a, 1); //LV_ANIM_REPEAT_INFINITE);
//  lv_anim_start(&stand_a);


  lv_anim_t sprint_a;
  lv_anim_init(&sprint_a);
  lv_anim_set_var(&sprint_a, sprint_icon);

  lv_anim_set_exec_cb(&sprint_a, set_sprint_icon_angle);
  lv_anim_set_values(&sprint_a, 0, -200);
  lv_anim_set_time(&sprint_a, SPRINT_ANIM_TIME_MS);
  lv_anim_set_repeat_count(&sprint_a, 1); //LV_ANIM_REPEAT_INFINITE);
//  lv_anim_start(&sprint_a);

  s_icon_anim = lv_anim_timeline_create();
  lv_anim_timeline_add(s_icon_anim, 0, &sprint_a);
  lv_anim_timeline_add(s_icon_anim, 0, &stand_a);
//  lv_anim_timeline_start(s_icon_anim);


  // Settings menu
#define GBA_LCD_H     160
#define MENU_PANE_H   120
#define MENU_BAR_H    10
  //lv_obj_t *menu = lv_tileview_create(g_panels.instr.screen);
  lv_obj_t *menu_pane = lv_obj_create(g_panels.instr.screen);
  ui_panel_add_obj(panel, W_INSTR_MENU_PANE, menu_pane);
  lv_obj_clear_flag(menu_pane, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_set_size(menu_pane, lv_pct(100), MENU_PANE_H);
  lv_obj_add_style(menu_pane, sty_frame_clear, LV_PART_MAIN);
  lv_obj_set_style_bg_color(menu_pane, lv_palette_lighten(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  //lv_obj_align(menu_pane, LV_ALIGN_TOP_LEFT, 0, GBA_LCD_H); // Off screen by default
  lv_obj_align(menu_pane, LV_ALIGN_TOP_LEFT, 0, 40); // FIXME: Remove

#if 0
  // Menu bar
  lv_obj_t *menu_bar = lv_obj_create(menu_pane);
  lv_obj_clear_flag(menu_bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_set_size(menu_bar, lv_pct(100), MENU_BAR_H);
  lv_obj_add_style(menu_bar, sty_menu_bar, LV_PART_MAIN);
  lv_obj_align(menu_bar, LV_ALIGN_TOP_LEFT, 0, 0);
#endif

//  label = lv_label_create(menu_pane);
//  lv_label_set_text(label, "Menu:");
//  lv_obj_align_to(label, menu_pane, LV_ALIGN_TOP_LEFT, 2,2);

#if 0
  lv_obj_t *menu = lv_menu_create(menu_pane);
//  lv_menu_set_mode_root_back_btn(menu, LV_MENU_MODE_ROOT_BACK_BTN_ENABLED);
//  lv_obj_add_event_cb(menu, back_event_handler, LV_EVENT_CLICKED, menu);
  lv_obj_set_size(menu, 100, MENU_PANE_H - 20);
  lv_obj_align(menu, LV_ALIGN_TOP_LEFT, 0, 20);
  //lv_obj_align_to(menu, menu_bar, LV_ALIGN_BOTTOM_LEFT, 0,0);


  /*Create a custom header*/
  lv_obj_t * header = lv_menu_get_main_header(menu);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_CLICKABLE);
#if 1
  lv_obj_del(lv_obj_get_child(header, 0)); /* Delete default back icon */
  lv_obj_t * back_btn = lv_btn_create(header);
  lv_obj_t * back_btn_label = lv_label_create(back_btn);
  lv_label_set_text(back_btn_label, "Back");
  lv_menu_set_main_header_back_btn(menu, back_btn);
  lv_menu_set_main_header_back_icon(menu, back_btn);
#endif

  lv_obj_t * cont;

  /*Create a sub page*/
  lv_obj_t * sub_page = lv_menu_page_create(menu, NULL);

  cont = lv_menu_cont_create(sub_page);
  label = lv_label_create(cont);
  lv_label_set_text(label, "Hello, I am hiding here");

  /*Create a main page*/
  lv_obj_t * main_page = lv_menu_page_create(menu, NULL);
  lv_obj_set_style_pad_row(main_page, 2, LV_PART_MAIN);

  cont = lv_menu_cont_create(main_page);
  lv_obj_add_style(cont, sty_menu_item, LV_PART_MAIN);
  label = lv_label_create(cont);
  lv_label_set_text(label, "Item 1");
  lv_obj_t *menu_icon = lv_img_create(cont);
  lv_img_set_src(menu_icon, &battery_icon);
  lv_obj_set_size(menu_icon, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
//  lv_obj_align(menu_icon, LV_ALIGN_TOP_LEFT, 0,0);
  lv_obj_align_to(menu_icon, label, LV_ALIGN_OUT_RIGHT_MID, 0,0);
  lv_menu_set_load_page_event(menu, cont, sub_page);

  cont = lv_menu_cont_create(main_page);
  lv_obj_add_style(cont, sty_menu_item, LV_PART_MAIN);
  label = lv_label_create(cont);
  lv_label_set_text(label, "Item 2");

  lv_menu_separator_create(main_page);

  cont = lv_menu_cont_create(main_page);
  lv_obj_add_style(cont, sty_menu_item, LV_PART_MAIN);
  label = lv_label_create(cont);
  lv_label_set_text(label, "Item 3 (Click me!)");
  lv_menu_set_load_page_event(menu, cont, sub_page);

  lv_menu_set_page(menu, main_page);

#else

/*
  lv_obj_t *menu = ui_menu_create_tree(menu_pane, s_ui_menu);
  lv_obj_set_size(menu, 160, MENU_PANE_H - MENU_BAR_H);
  lv_obj_align(menu, LV_ALIGN_TOP_LEFT, 0, MENU_BAR_H);
*/
#endif

  // Status tileview
#define TILE_PANE_H 50
  lv_obj_t *tv = lv_tileview_create(g_panels.instr.screen);
  ui_panel_add_obj(panel, W_INSTR_TILEVIEW, tv);
  lv_obj_set_size(tv, lv_pct(100), TILE_PANE_H);
  lv_obj_set_style_bg_color(tv, TILEVIEW_COLOR_DARK, 0);
  //lv_obj_set_style_bg_color(tv, TILEVIEW_COLOR, 0);
  lv_obj_align(tv, LV_ALIGN_TOP_LEFT, 0, GBA_LCD_H-TILE_PANE_H);


  lv_obj_t *tile1 = lv_tileview_add_tile(tv, 0, 0, LV_DIR_HOR);
  ui_panel_add_obj(panel, W_INSTR_TILE1, tile1);
  lv_obj_t *tile2 = lv_tileview_add_tile(tv, 1, 0, LV_DIR_HOR);
  ui_panel_add_obj(panel, W_INSTR_TILE2, tile2);
  lv_obj_t *tile3 = lv_tileview_add_tile(tv, 2, 0, LV_DIR_HOR);
  ui_panel_add_obj(panel, W_INSTR_TILE3, tile3);

  // ** Tile 1 **

  // Voltage

  lv_obj_t *icon = lv_img_create(tile1);
  lv_img_set_src(icon, &battery_icon);
//  lv_obj_set_style_img_recolor(icon, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
//  lv_obj_set_style_img_recolor_opa(icon, LV_OPA_100, LV_PART_MAIN);
  lv_obj_set_size(icon, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0,0);

  label = lv_label_create(tile1);
  ui_panel_add_obj(panel, W_INSTR_LBL_VOLTAGE, label);
  lv_label_set_text(label, "--.- V");
  lv_obj_align_to(label, icon, LV_ALIGN_OUT_RIGHT_MID, 4,0);


  // Coolant temp

  icon = lv_img_create(tile1);
  lv_img_set_src(icon, &temp_icon);
  lv_obj_set_size(icon, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0,25);

  label = lv_label_create(tile1);
  ui_panel_add_obj(panel, W_INSTR_LBL_COOLANT_TEMP, label);

  PropDBEntry value;
  prop_get(&g_prop_db, P_APP_GUI_UNITS__TEMPERATURE, &value); // Get units enum
  lv_label_set_text_fmt(label, "--.- " UTF8_DEGREE "%s", get_unit_text(value.value));
  lv_obj_align_to(label, icon, LV_ALIGN_OUT_RIGHT_MID, 4,0);


  // Fuel level

  icon = lv_img_create(tile1);
  lv_img_set_src(icon, &fuel_icon);
  lv_obj_set_size(icon, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 120,0);

  label = lv_label_create(tile1);
  ui_panel_add_obj(panel, W_INSTR_LBL_FUEL, label);
  lv_label_set_text(label, "-- %");
  lv_obj_align_to(label, icon, LV_ALIGN_OUT_RIGHT_MID, 4,0);


  // Avg. speed

  icon = lv_img_create(tile1);
  lv_img_set_src(icon, &speedo_avg_icon);
  lv_obj_set_size(icon, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 120,25);

  label = lv_label_create(tile1);
  ui_panel_add_obj(panel, W_INSTR_LBL_SPEED_AVG, label);
  lv_label_set_text(label, "-- mph");
  lv_obj_align_to(label, icon, LV_ALIGN_OUT_RIGHT_MID, 4,0);


  // Max speed
  // Avg. Speed
  // Fuel level
  // Fuel consumption
  // TPS

  // DTCs

  // ** Tile 2 **

  // Max. speed

  icon = lv_img_create(tile2);
  lv_img_set_src(icon, &speedo_icon);
  lv_obj_set_size(icon, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0,0);

  label = lv_label_create(tile2);
  ui_panel_add_obj(panel, W_INSTR_LBL_SPEED_MAX, label);
  lv_label_set_text(label, "-- mph");
  lv_obj_align_to(label, icon, LV_ALIGN_OUT_RIGHT_MID, 4,0);


  // ** Tile 3 **

  // Speedo slider
  lv_obj_t *slider = lv_slider_create(tile3);
  lv_obj_set_size(slider, lv_pct(90), 4);
  lv_obj_add_event_cb(slider, speed_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_align(slider, LV_ALIGN_CENTER, 0, -14);

  // Tacho slider
  slider = lv_slider_create(tile3);
  lv_obj_set_size(slider, lv_pct(90), 4);
  lv_obj_add_event_cb(slider, rpm_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_align(slider, LV_ALIGN_CENTER, 0, 14);




  // Debug controls
  lv_obj_t *obj_debug = lv_obj_create(g_panels.instr.screen);
  lv_obj_clear_flag(obj_debug, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(obj_debug, lv_pct(100), 100);
  lv_obj_add_style(obj_debug, sty_frame_clear, LV_PART_MAIN);
  lv_obj_align(obj_debug, LV_ALIGN_TOP_LEFT, 0, 320-100);


  // Dark mode 
  lv_obj_t *sw = lv_switch_create(obj_debug);
  ui_panel_add_obj(panel, W_INSTR_DARK, sw);
  lv_obj_align(sw, LV_ALIGN_TOP_LEFT, 1,1);
  lv_obj_add_event_cb(sw, mode_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

  label = lv_label_create(obj_debug);
  lv_label_set_text(label, "Dark mode");
  lv_obj_align_to(label, sw, LV_ALIGN_OUT_RIGHT_MID, 4,0);

  ui_react_widgets_bind(&g_react_widgets, P_APP_GUI_INFO__DARK, sw);


  // Menu mode 
  lv_obj_t *sw2 = lv_switch_create(obj_debug);
  ui_panel_add_obj(panel, W_INSTR_MENU, sw2);
  lv_obj_align(sw2, LV_ALIGN_TOP_LEFT, 1,34);
  //lv_obj_align_to(label, sw2, LV_ALIGN_OUT_RIGHT_MID, 4,0);
  lv_obj_add_event_cb(sw2, menu_mode_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

  label = lv_label_create(obj_debug);
  lv_label_set_text(label, "Menu");
  lv_obj_align_to(label, sw2, LV_ALIGN_OUT_RIGHT_MID, 4,0);

  ui_react_widgets_bind(&g_react_widgets, P_APP_GUI_MENU__MODE, sw2);



  // Menu animation
#define MENU_ANIM_TIME_MS     500

  lv_anim_t speedo_a;
  lv_anim_init(&speedo_a);
  lv_anim_set_var(&speedo_a, obj_speedo);
  lv_anim_set_exec_cb(&speedo_a, set_speedo_pos);
  lv_anim_set_values(&speedo_a, 0, 1024); // Parameter for x&y offset
  lv_anim_set_time(&speedo_a, MENU_ANIM_TIME_MS);
  lv_anim_set_repeat_count(&speedo_a, 1);
  //lv_anim_set_repeat_count(&speedo_a, LV_ANIM_REPEAT_INFINITE);
  //lv_anim_start(&speedo_a);

  // Bar graph
  lv_anim_t tacho_a;
  lv_anim_init(&tacho_a);
  lv_anim_set_var(&tacho_a, tach_c);
  lv_anim_set_exec_cb(&tacho_a, set_tacho_pos);
  lv_anim_set_values(&tacho_a, 0, -22); // Parameter for x&y offset
  lv_anim_set_time(&tacho_a, MENU_ANIM_TIME_MS);
  lv_anim_set_repeat_count(&tacho_a, 1);

  lv_anim_t num_tacho_a;
  lv_anim_init(&num_tacho_a);
  lv_anim_set_var(&num_tacho_a, obj_num_tacho);
  lv_anim_set_exec_cb(&num_tacho_a, set_num_tacho_pos);
  lv_anim_set_values(&num_tacho_a, 0, -48); // Y offset
  lv_anim_set_time(&num_tacho_a, MENU_ANIM_TIME_MS);
  lv_anim_set_repeat_count(&num_tacho_a, 1);


  lv_anim_t menu_sprint_a;
  lv_anim_init(&menu_sprint_a);
  lv_anim_set_var(&menu_sprint_a, obj_sprint);
  lv_anim_set_exec_cb(&menu_sprint_a, set_menu_sprint_pos);
  lv_anim_set_values(&menu_sprint_a, 0, 46); // X offset
  lv_anim_set_time(&menu_sprint_a, MENU_ANIM_TIME_MS);
  lv_anim_set_repeat_count(&menu_sprint_a, 1);


  lv_anim_t gear_pos_a;
  lv_anim_init(&gear_pos_a);
  lv_anim_set_var(&gear_pos_a, obj_gear_pos);
  lv_anim_set_exec_cb(&gear_pos_a, set_gear_pos_pos);
  lv_anim_set_values(&gear_pos_a, 0, -18); // Y offset
  lv_anim_set_time(&gear_pos_a, MENU_ANIM_TIME_MS);
  lv_anim_set_repeat_count(&gear_pos_a, 1);


  lv_anim_t menu_a;
  lv_anim_init(&menu_a);
  lv_anim_set_var(&menu_a, menu_pane);
  lv_anim_set_exec_cb(&menu_a, set_menu_pos);
  lv_anim_set_values(&menu_a, 0, -MENU_PANE_H); // Y offset
  lv_anim_set_time(&menu_a, MENU_ANIM_TIME_MS);
  lv_anim_set_repeat_count(&menu_a, 1);


  lv_anim_t tv_a;
  lv_anim_init(&tv_a);
  lv_anim_set_var(&tv_a, tv);
  lv_anim_set_exec_cb(&tv_a, set_tv_pos);
  lv_anim_set_values(&tv_a, 0, TILE_PANE_H); // Y offset
  lv_anim_set_time(&tv_a, MENU_ANIM_TIME_MS);
  lv_anim_set_repeat_count(&tv_a, 1);


  // Combine menu animations in timeline
  s_menu_anim = lv_anim_timeline_create();
  lv_anim_timeline_add(s_menu_anim, 0, &speedo_a);
  lv_anim_timeline_add(s_menu_anim, 0, &tacho_a);
  lv_anim_timeline_add(s_menu_anim, 0, &num_tacho_a);
  lv_anim_timeline_add(s_menu_anim, 0, &menu_sprint_a);
  lv_anim_timeline_add(s_menu_anim, 0, &gear_pos_a);
  lv_anim_timeline_add(s_menu_anim, 0, &menu_a);
  lv_anim_timeline_add(s_menu_anim, 0, &tv_a);

}





// ******************** Touchscreen calibration panel ********************


// TS Cal panel widgets
#define W_TS_CAL_TGT0   (P_APP_GUI_WIDGET_n | P3_ARR(0))
#define W_TS_CAL_TGT1   (P_APP_GUI_WIDGET_n | P3_ARR(1))


bool g_use_touch_calibration = false;
TouchState g_touch_state = {0};


static TouchCalPoint s_cal_points[] = {
  {{15,15}},
  {{LCD_HOR_RES - 15, LCD_VER_RES - 15}}
};

static int s_cal_target_ix = 0;


TouchCalibration g_touch_cal = {
  .x_scale = TS_CAL_FP_SCALE, // 1.0
  .x_offset = 0,
  .y_scale = TS_CAL_FP_SCALE, // 1.0
  .y_offset = 0
};



static lv_coord_t point_distance(lv_point_t *p1, lv_point_t *p2) {
  int32_t dx = (int32_t)p1->x - p2->x;
  int32_t dy = (int32_t)p1->x - p2->x;

  if(dx < 0) dx = -dx;
  if(dy < 0) dy = -dy;

  return dx > dy ? dx : dy;
}


static void coord_phys_to_gui(lv_disp_t *disp, lv_point_t *phys, lv_point_t *gui) {
  lv_disp_rot_t rot = lv_disp_get_rotation(disp);
  lv_coord_t w = lv_disp_get_hor_res(disp);
  lv_coord_t h = lv_disp_get_ver_res(disp);

  switch(rot) {
  case LV_DISP_ROT_NONE:
    *gui = *phys;
    break;

  case LV_DISP_ROT_90:
    gui->x = w - phys->y;
    gui->y = phys->x;
    break;

  case LV_DISP_ROT_180:
    gui->x = w - phys->x;
    gui->y = h - phys->y;
    break;

  case LV_DISP_ROT_270:
    gui->x = phys->y;
    gui->y = h - phys->x;
    break;
  }
}



void begin_ts_cal(void) {
  g_use_touch_calibration = false;

  s_cal_target_ix = 0;

  // Show first target
  lv_obj_t *obj;
  obj = ui_panel_get_obj(&g_panels.ts_cal, W_TS_CAL_TGT1);
  lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  obj = ui_panel_get_obj(&g_panels.ts_cal, W_TS_CAL_TGT0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);

  ui_panel_push(&g_panels.ts_cal);
}


static void cal_target_click(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  UIPanel *panel = (UIPanel *)lv_event_get_user_data(e);
  lv_obj_t *obj;

  if(code == LV_EVENT_PRESSED) {

    if(point_distance(&g_touch_state.point, &s_cal_points[s_cal_target_ix].phys) > 12)
      return;

    switch(s_cal_target_ix) {
    case 0:
      s_cal_points[0].ts = g_touch_state.point;

      // Show next target
      obj = ui_panel_get_obj(panel, W_TS_CAL_TGT0);
      lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
      obj = ui_panel_get_obj(panel, W_TS_CAL_TGT1);
      lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);

      puts("Cal 0");
      s_cal_target_ix++;
      break;

    case 1:
    default:
      s_cal_points[1].ts = g_touch_state.point;

#ifdef PLATFORM_EMBEDDED
      // Set calibration
      if(ts_set_calibration(&s_cal_points[0], &s_cal_points[1])) {
        // Valid cal input
        puts("Cal 1: good");
        s_cal_target_ix = 0;

        g_use_touch_calibration = true;
        ui_panel_pop(); // Remove cal panel

      } else { // Bad cal; Retry
        puts("Cal 1: BAD");
        s_cal_target_ix = 0;
        g_use_touch_calibration = false;

        // Show first target
        obj = ui_panel_get_obj(panel, W_TS_CAL_TGT1);
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        obj = ui_panel_get_obj(panel, W_TS_CAL_TGT0);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
      }
#else
      puts("Cal 1: good");
      ui_panel_pop(); // Remove cal panel
#endif

      break;
    }
  }
}


void ui_ts_cal_init(UIPanel *panel) {
  ui_panel_init(panel);

  lv_obj_t *label;

  lv_obj_clear_flag(panel->screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(panel->screen, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(panel->screen, cal_target_click, LV_EVENT_PRESSED, panel);


  label = lv_label_create(panel->screen);
  lv_label_set_text(label, "Press target to calibrate");
  lv_obj_center(label);

  // Target top left
  lv_obj_t *icon = lv_img_create(panel->screen);
  ui_panel_add_obj(panel, W_TS_CAL_TGT0, icon);
  lv_img_set_src(icon, &cal_target);
  lv_obj_set_size(icon, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  // Get icon geometry
  lv_coord_t icon_h = cal_target.header.h;

  lv_point_t gui = {0};
  coord_phys_to_gui(lv_disp_get_default(), &s_cal_points[0].phys, &gui);
  lv_obj_set_pos(icon, gui.x - icon_h/2, gui.y - icon_h/2); // Align center

  // Target bottom right
  icon = lv_img_create(panel->screen);
  ui_panel_add_obj(panel, W_TS_CAL_TGT1, icon);
  lv_img_set_src(icon, &cal_target);
  lv_obj_set_size(icon, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

  coord_phys_to_gui(lv_disp_get_default(), &s_cal_points[1].phys, &gui);
  lv_obj_set_pos(icon, gui.x - icon_h/2, gui.y - icon_h/2); // Align center

  lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
}

// ******************** Splash panel ********************

#define LOGO_ANIM_TIME_MS   1000

static void animate_logo_loc(void *img, int32_t x) {
//  int32_t x = -LCD_HOR_RES + p * (LCD_HOR_RES) / 1024;
  lv_obj_set_style_translate_x(img, x, 0);
}


void ui_splash_init(UIPanel *panel) {
  ui_panel_init(panel);

  // Logo
  lv_obj_t *logo = lv_img_create(g_panels.splash.screen);
  lv_img_set_src(logo, &triumph_logo);
  lv_obj_set_size(logo, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_align(logo, LV_ALIGN_CENTER, 0,0);

  // Animate logo
  lv_anim_t logo_a;
  lv_anim_init(&logo_a);

  lv_anim_set_var(&logo_a, logo);
  lv_anim_set_exec_cb(&logo_a, animate_logo_loc);
  lv_anim_set_values(&logo_a, -LCD_HOR_RES, 0);
  lv_anim_set_time(&logo_a, LOGO_ANIM_TIME_MS);
  lv_anim_set_delay(&logo_a, 400);
  lv_anim_set_repeat_count(&logo_a, 1);
  lv_anim_set_path_cb(&logo_a, lv_anim_path_ease_out);
  lv_anim_start(&logo_a);
}


void ui_set_default(void) {
  prop_set_int(&g_prop_db, P_APP_GUI_MENU__MODE, 1, 0);
}
