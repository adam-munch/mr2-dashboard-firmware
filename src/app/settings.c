#include "settings.h"

#define DEFAULTS                     \
    {                                \
        .brightness_pct = 100,       \
        .speed_units = SPEED_KPH,    \
        .shift_flash = 1,            \
        .ecu_mode = 0,               \
    }

settings_t g_settings = DEFAULTS;

void settings_reset(void)
{
    g_settings = (settings_t)DEFAULTS;
}
