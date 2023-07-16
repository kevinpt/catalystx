#ifndef UI_UNITS_H
#define UI_UNITS_H

typedef enum {
  UNIT_KPH = 0,
  UNIT_MPH,
  UNIT_CELSIUS,
  UNIT_FAHRENHEIT
} UIUnits;


#ifdef __cplusplus
extern "C" {
#endif

int32_t convert_to_unit(int32_t si_value, int32_t fp_scale, UIUnits unit);
const char *get_unit_text(UIUnits unit);

#ifdef __cplusplus
}
#endif

#endif // UI_UNITS_H
