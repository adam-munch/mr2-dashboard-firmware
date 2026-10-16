#include "eve_panels.h"

/*
 * Values from the NHD-7.0-800480FT-CSXP datasheet (rev. 08/2024),
 * "TFT Timing Characteristics". Pixel clock = 60 MHz / 2 = 30 MHz, giving
 * 30e6 / (928 * 525) = 61.6 Hz refresh.
 */
const eve_panel_t eve_panel_nhd_70_800480ft_csxp = {
    .hsize = 800,
    .hcycle = 928,
    .hoffset = 88,
    .hsync0 = 0,
    .hsync1 = 48,
    .vsize = 480,
    .vcycle = 525,
    .voffset = 32,
    .vsync0 = 0,
    .vsync1 = 3,
    .pclk = 2,
    .pclk_pol = 1,
    .swizzle = 0,
    .cspread = 0,
    .dither = 1,
    .ext_clock = true,  /* 12 MHz crystal on the module (Y1) */
    .has_touch = false, /* "-CSXP" has no touch panel fitted */
    /*
     * The datasheet asks for 800 Hz..10 kHz. Older batches of this module
     * used a backlight driver limited to ~1 kHz, so 1 kHz suits both. Raise
     * it (up to 10 kHz) if the backlight whines and your module is the
     * FAN5333 revision shown in the datasheet schematic.
     */
    .backlight_pwm_hz = 1000,
};
