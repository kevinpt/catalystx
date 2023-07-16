#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "ui_units.h"

#ifndef COUNT_OF
#  define COUNT_OF(a) (sizeof(a) / sizeof(*(a)))
#endif


typedef int32_t (*UnitConverter)(int32_t si_value, int32_t fp_scale);

typedef struct {
  UIUnits         unit;
  const char     *label;
  UnitConverter  convert;
} UIUnitDef;


static int32_t convert_to_mph(int32_t si_value, int32_t fp_scale);
static int32_t convert_to_fahrenheit(int32_t si_value, int32_t fp_scale);

static const UIUnitDef s_unit_conversions[] = {
  {UNIT_KPH,        "km/h", NULL},
  {UNIT_MPH,        "mph",  convert_to_mph},
  {UNIT_CELSIUS,    "C",    NULL},
  {UNIT_FAHRENHEIT, "F",    convert_to_fahrenheit}
};



static int32_t convert_to_mph(int32_t si_value, int32_t fp_scale) {
  // kph to mph

  // Multiply by 0.6214 --> 159 / 256
  return si_value * 159 / 256;
}

static int32_t convert_to_fahrenheit(int32_t si_value, int32_t fp_scale) {
  // Multiply by 9/5 --> 461 / 256
  return si_value * 461 / 256 + (32 * fp_scale);
}


int32_t convert_to_unit(int32_t si_value, int32_t fp_scale, UIUnits unit) {
  // Find unit in table
  for(size_t i = 0; i < COUNT_OF(s_unit_conversions); i++) {
    if(s_unit_conversions[i].unit == unit) {
      if(s_unit_conversions[i].convert)
        return s_unit_conversions[i].convert(si_value, fp_scale);
      else // No conversion
        return si_value;
    }
  }

  // Unit not found
  return 0;
}


const char *get_unit_text(UIUnits unit) {
  // Find unit in table
  for(size_t i = 0; i < COUNT_OF(s_unit_conversions); i++) {
    if(s_unit_conversions[i].unit == unit) {
      return s_unit_conversions[i].label;
    }
  }

  // Unit not found
  return "??";
}


