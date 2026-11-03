#ifndef LV_CONF_H
#define LV_CONF_H
/* Real LVGL 9.4 userspace rendering for a NaSeed-owned memfd surface. */
#define LV_COLOR_DEPTH 32
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB
#define LV_USE_OS LV_OS_NONE
#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_COMPLEX 1
#define LV_DRAW_SW_SUPPORT_XRGB8888 1
#define LV_MEM_SIZE (256 * 1024U)
#define LV_USE_LOG 0
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14
#define LV_USE_BUTTON 1
#define LV_USE_LABEL 1
#define LV_USE_FLEX 1
#endif
