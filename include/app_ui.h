#ifndef APP_UI_H
#define APP_UI_H


#define LCD_HOR_RES       240
#define LCD_HOR_SPAN      LCD_HOR_RES
#define LCD_VER_RES       320

#define TS_CAL_FP_SCALE   1024


#define P_APP_GUI_STYLE_n   (P1_APP | P2_GUI | P3_STYLE | P3_ARR(0))
#define P_APP_GUI_WIDGET_n   (P1_APP | P2_GUI | P3_WIDGET | P3_ARR(0))

// Instrument panel widgets
#define W_INSTR_TACH_C      (P_APP_GUI_WIDGET_n | P3_ARR(0)) // Corner of tacho bar graph
#define W_INSTR_TACH_V      (P_APP_GUI_WIDGET_n | P3_ARR(1)) // Vert bar of tacho bar graph
#define W_INSTR_TACH_H      (P_APP_GUI_WIDGET_n | P3_ARR(2)) // Horiz bar of tacho bar graph
#define W_INSTR_LBL_RPM     (P_APP_GUI_WIDGET_n | P3_ARR(3))
//#define W_INSTR_OBJ_SPEED   (P_APP_GUI_WIDGET_n | P3_ARR(4))
#define W_INSTR_LBL_SPEED   (P_APP_GUI_WIDGET_n | P3_ARR(5))
#define W_INSTR_LBL_SPEED_UNIT (P_APP_GUI_WIDGET_n | P3_ARR(6))
#define W_INSTR_GEAR_POS    (P_APP_GUI_WIDGET_n | P3_ARR(7))
#define W_INSTR_GEAR_POS_IMG (P_APP_GUI_WIDGET_n | P3_ARR(8))
#define W_INSTR_TILEVIEW    (P_APP_GUI_WIDGET_n | P3_ARR(9))
#define W_INSTR_TILE1       (P_APP_GUI_WIDGET_n | P3_ARR(10))
#define W_INSTR_TILE2       (P_APP_GUI_WIDGET_n | P3_ARR(11))
#define W_INSTR_TILE3       (P_APP_GUI_WIDGET_n | P3_ARR(12))
#define W_INSTR_DARK        (P_APP_GUI_WIDGET_n | P3_ARR(13))
#define W_INSTR_STAND_ICON  (P_APP_GUI_WIDGET_n | P3_ARR(14))
#define W_INSTR_SPRINT_ICON (P_APP_GUI_WIDGET_n | P3_ARR(15))
#define W_INSTR_LBL_VOLTAGE       (P_APP_GUI_WIDGET_n | P3_ARR(16))
#define W_INSTR_LBL_COOLANT_TEMP  (P_APP_GUI_WIDGET_n | P3_ARR(17))
#define W_INSTR_LBL_FUEL          (P_APP_GUI_WIDGET_n | P3_ARR(18))
#define W_INSTR_LBL_SPEED_AVG     (P_APP_GUI_WIDGET_n | P3_ARR(19))
#define W_INSTR_LBL_SPEED_MAX     (P_APP_GUI_WIDGET_n | P3_ARR(20))
#define W_INSTR_MENU        (P_APP_GUI_WIDGET_n | P3_ARR(100)) // Menu mode switch
#define W_INSTR_MENU_PANE   (P_APP_GUI_WIDGET_n | P3_ARR(101))

// Menu items
#define P_APP_GUI_MENU_n  (P1_APP | P2_GUI | P3_MENU | P3_ARR(0))
#define M_SEPARATOR       (P_APP_GUI_MENU_n | P3_ARR(253))
#define M_SECTION         (P_APP_GUI_MENU_n | P3_ARR(254))

#define M_CONFIGURE       (P_APP_GUI_MENU_n | P3_ARR(1))
#define M_DTCS            (P_APP_GUI_MENU_n | P3_ARR(2))
#define M_CONFIGURE_DARK  (P_APP_GUI_MENU_n | P3_ARR(10))
#define M_CONFIGURE_UNITS (P_APP_GUI_MENU_n | P3_ARR(11))
#define M_DTCS_REFRESH    (P_APP_GUI_MENU_n | P3_ARR(20))
#define M_DTCS_CLEAR      (P_APP_GUI_MENU_n | P3_ARR(21))



// Instrument panel styles
#define P_APP_GUI_STYLE__TACH         (P_APP_GUI_STYLE_n | P3_ARR(0))
#define P_APP_GUI_STYLE__TACH_IND     (P_APP_GUI_STYLE_n | P3_ARR(1))
#define P_APP_GUI_STYLE__FRAME_CLEAR  (P_APP_GUI_STYLE_n | P3_ARR(2))
#define P_APP_GUI_STYLE__MENU_BAR     (P_APP_GUI_STYLE_n | P3_ARR(3))
#define P_APP_GUI_STYLE__MENU_ITEM    (P_APP_GUI_STYLE_n | P3_ARR(4))

// PropDB keys
#define P_APP_GUI_INFO__DARK        (P1_APP | P2_GUI | P3_INFO | P3_ARR(0))

#define P_APP_GUI_UNITS_n_MSK       (P1_APP | P2_GUI | P3_UNITS | P3_ARR(0) | P4_MSK)
#define P_APP_GUI_UNITS__SPEED      (P1_APP | P2_GUI | P3_UNITS | P3_ARR(0))
#define P_APP_GUI_UNITS__TEMPERATURE (P1_APP | P2_GUI | P3_UNITS | P3_ARR(1))
#define P_APP_GUI_MENU__MODE        (P_APP_GUI_MENU_n | P3_ARR(0))

#define P_SENSOR_ECU_n_MSK                (P1_SENSOR | P2_ECU | P2_ARR(0) | P3_MSK | P4_MSK)
#define P_SENSOR_ECU_n_VALUE              (P1_SENSOR | P2_ECU | P2_ARR(0) | P4_VALUE)
#define P_SENSOR_ECU__SPEED__VALUE        (P1_SENSOR | P2_ECU | P2_ARR(0) | P4_VALUE)
#define P_SENSOR_ECU__SPEED__AVERAGE      (P1_SENSOR | P2_ECU | P2_ARR(0) | P4_AVERAGE)
#define P_SENSOR_ECU__SPEED__MAX          (P1_SENSOR | P2_ECU | P2_ARR(0) | P4_MAX)
#define P_SENSOR_ECU__RPM__VALUE          (P1_SENSOR | P2_ECU | P2_ARR(1) | P4_VALUE)
#define P_SENSOR_ECU__SIDESTAND__VALUE    (P1_SENSOR | P2_ECU | P2_ARR(2) | P4_VALUE)
#define P_SENSOR_ECU__GEAR__VALUE         (P1_SENSOR | P2_ECU | P2_ARR(3) | P4_VALUE)
#define P_SENSOR_ECU__VOLTAGE__VALUE      (P1_SENSOR | P2_ECU | P2_ARR(4) | P4_VALUE)
#define P_SENSOR_ECU__COOLANT_TEMP__VALUE (P1_SENSOR | P2_ECU | P2_ARR(5) | P4_VALUE)
#define P_SENSOR_ECU__FUEL__VALUE         (P_SENSOR_ECU_n_VALUE | P2_ARR(6))


// Update sources
#define P_RSRC_GUI_LOCAL_WIDGET     (P1_RSRC | P2_GUI | P3_LOCAL | P4_WIDGET)


typedef struct {
  UIPanel instr;
  UIPanel splash;
  UIPanel ts_cal;
} AppPanels;

typedef struct {
  lv_point_t  phys; // Physical point coordinate before any LVGL rotation applied
  lv_point_t  ts;   // Raw touchscreen coordinate
} TouchCalPoint;

typedef struct {
  uint16_t  x_scale;
  int16_t   x_offset;
  uint16_t  y_scale;
  int16_t   y_offset;
} TouchCalibration;

// Keep track of touch state via an independent struct so we can simulate inputs
// as needed.
typedef struct {
  lv_point_t  point;
  bool        pressed;
} TouchState;


extern lv_disp_t *g_disp_main;
extern AppPanels g_panels;
extern UIStyles g_ui_styles;

extern bool g_use_touch_calibration;
extern TouchState g_touch_state;
extern TouchCalibration g_touch_cal;
extern PropDB g_prop_db;

#ifdef __cplusplus
extern "C" {
#endif

void gui_prop_msg_handler(UMsgTarget *tgt, UMsg *msg);
void gui_prop_init(void);

lv_theme_t *set_theme_mode(lv_disp_t *disp, bool dark_mode);
void app_styles_init(void);
void app_screens_init(void);


void update_tacho(uint16_t rpm);
void update_sidestand(bool down);
void update_gear_pos(uint32_t gear);
void update_gui_menu_mode(uint32_t mode);
void ui_instruments_init(UIPanel *panel);
void ui_ts_cal_init(UIPanel *panel);
void ui_splash_init(UIPanel *panel);

void ui_set_default(void);

void begin_ts_cal(void);

void set_unit_text(lv_obj_t *lbl, uint32_t prop);

#ifdef __cplusplus
}
#endif

#endif // APP_UI_H


