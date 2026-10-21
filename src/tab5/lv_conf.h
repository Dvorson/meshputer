// LVGL configuration for the Tab5 UI. Anything not set here keeps LVGL's
// default (lv_conf_internal.h).
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

// Plain malloc: on the ESP32-P4 large blocks land in PSRAM by themselves.
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING  LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_USE_OS LV_OS_NONE  // all LVGL calls come from the Arduino loop task
#define LV_DEF_REFR_PERIOD 16
#define LV_DPI_DEF 220         // 5" 720x1280 panel; scales the default theme's padding

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_26 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_36 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_26

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_THEME_DEFAULT_TRANSITION_TIME 60

#define LV_USE_QRCODE 1

#define LV_BUILD_EXAMPLES 0

#endif // LV_CONF_H
