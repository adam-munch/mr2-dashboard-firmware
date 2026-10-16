/*
 * FT81x (FT810/FT811/FT812/FT813) memory map, registers, display list
 * commands and co-processor commands.
 *
 * Names follow the FT81x Series Programmers Guide, so code from the guide
 * or from EVE Screen Designer can be used directly.
 */
#ifndef FT81X_H
#define FT81X_H

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Memory map
 * ------------------------------------------------------------------------ */

#define RAM_G          0x000000UL /* 1 MB general-purpose graphics RAM */
#define ROM_CHIPID     0x0C0000UL /* chip ID, valid right after reset   */
#define ROM_FONT       0x1E0000UL
#define ROM_FONT_ADDR  0x2FFFFCUL
#define RAM_DL         0x300000UL /* 8 KB display list RAM              */
#define RAM_REG        0x302000UL
#define RAM_CMD        0x308000UL /* 4 KB co-processor command FIFO     */

#define RAM_DL_SIZE    8192UL
#define RAM_CMD_SIZE   4096UL

/* ---------------------------------------------------------------------------
 * Registers
 * ------------------------------------------------------------------------ */

#define REG_ID                0x302000UL /* reads 0x7C */
#define REG_FRAMES            0x302004UL
#define REG_CLOCK             0x302008UL
#define REG_FREQUENCY         0x30200CUL
#define REG_RENDERMODE        0x302010UL
#define REG_SNAPY             0x302014UL
#define REG_SNAPSHOT          0x302018UL
#define REG_SNAPFORMAT        0x30201CUL
#define REG_CPURESET          0x302020UL
#define REG_TAP_CRC           0x302024UL
#define REG_TAP_MASK          0x302028UL
#define REG_HCYCLE            0x30202CUL
#define REG_HOFFSET           0x302030UL
#define REG_HSIZE             0x302034UL
#define REG_HSYNC0            0x302038UL
#define REG_HSYNC1            0x30203CUL
#define REG_VCYCLE            0x302040UL
#define REG_VOFFSET           0x302044UL
#define REG_VSIZE             0x302048UL
#define REG_VSYNC0            0x30204CUL
#define REG_VSYNC1            0x302050UL
#define REG_DLSWAP            0x302054UL
#define REG_ROTATE            0x302058UL
#define REG_OUTBITS           0x30205CUL
#define REG_DITHER            0x302060UL
#define REG_SWIZZLE           0x302064UL
#define REG_CSPREAD           0x302068UL
#define REG_PCLK_POL          0x30206CUL
#define REG_PCLK              0x302070UL
#define REG_TAG_X             0x302074UL
#define REG_TAG_Y             0x302078UL
#define REG_TAG               0x30207CUL
#define REG_VOL_PB            0x302080UL
#define REG_VOL_SOUND         0x302084UL
#define REG_SOUND             0x302088UL
#define REG_PLAY              0x30208CUL
#define REG_GPIO_DIR          0x302090UL
#define REG_GPIO              0x302094UL
#define REG_GPIOX_DIR         0x302098UL
#define REG_GPIOX             0x30209CUL
#define REG_INT_FLAGS         0x3020A8UL
#define REG_INT_EN            0x3020ACUL
#define REG_INT_MASK          0x3020B0UL
#define REG_PLAYBACK_START    0x3020B4UL
#define REG_PLAYBACK_LENGTH   0x3020B8UL
#define REG_PLAYBACK_READPTR  0x3020BCUL
#define REG_PLAYBACK_FREQ     0x3020C0UL
#define REG_PLAYBACK_FORMAT   0x3020C4UL
#define REG_PLAYBACK_LOOP     0x3020C8UL
#define REG_PLAYBACK_PLAY     0x3020CCUL
#define REG_PWM_HZ            0x3020D0UL
#define REG_PWM_DUTY          0x3020D4UL
#define REG_MACRO_0           0x3020D8UL
#define REG_MACRO_1           0x3020DCUL
#define REG_CMD_READ          0x3020F8UL
#define REG_CMD_WRITE         0x3020FCUL
#define REG_CMD_DL            0x302100UL
#define REG_TOUCH_MODE        0x302104UL
#define REG_TOUCH_ADC_MODE    0x302108UL
#define REG_TOUCH_CHARGE      0x30210CUL
#define REG_TOUCH_SETTLE      0x302110UL
#define REG_TOUCH_OVERSAMPLE  0x302114UL
#define REG_TOUCH_RZTHRESH    0x302118UL
#define REG_TOUCH_RAW_XY      0x30211CUL
#define REG_TOUCH_RZ          0x302120UL
#define REG_TOUCH_SCREEN_XY   0x302124UL
#define REG_TOUCH_TAG_XY      0x302128UL
#define REG_TOUCH_TAG         0x30212CUL
#define REG_TOUCH_TRANSFORM_A 0x302150UL
#define REG_TOUCH_TRANSFORM_B 0x302154UL
#define REG_TOUCH_TRANSFORM_C 0x302158UL
#define REG_TOUCH_TRANSFORM_D 0x30215CUL
#define REG_TOUCH_TRANSFORM_E 0x302160UL
#define REG_TOUCH_TRANSFORM_F 0x302164UL
#define REG_TOUCH_CONFIG      0x302168UL
#define REG_TRIM              0x302180UL
#define REG_ANA_COMP          0x302184UL
#define REG_SPI_WIDTH         0x302188UL
#define REG_TOUCH_DIRECT_XY   0x30218CUL
#define REG_TOUCH_DIRECT_Z1Z2 0x302190UL
#define REG_DATESTAMP         0x302564UL
#define REG_CMDB_SPACE        0x302574UL
#define REG_CMDB_WRITE        0x302578UL
#define REG_TRACKER           0x309000UL

/* Register values */
#define EVE_REG_ID_VALUE      0x7CU
#define DLSWAP_DONE           0U
#define DLSWAP_LINE           1U
#define DLSWAP_FRAME          2U
#define TOUCHMODE_OFF         0U
#define TOUCHMODE_ONESHOT     1U
#define TOUCHMODE_FRAME       2U
#define TOUCHMODE_CONTINUOUS  3U
#define EVE_GPIO_DISP         0x80U /* REG_GPIO bit 7 drives the panel's DISP pin */
#define EVE_PWM_DUTY_MAX      128U
#define EVE_CMD_FAULT         0xFFFU /* REG_CMD_READ value after a co-processor fault */

/* ---------------------------------------------------------------------------
 * Host commands (sent as 3 bytes: command, parameter, 0x00)
 * ------------------------------------------------------------------------ */

#define HCMD_ACTIVE       0x00U
#define HCMD_STANDBY      0x41U
#define HCMD_SLEEP        0x42U
#define HCMD_PWRDOWN      0x50U
#define HCMD_CLKEXT       0x44U /* use the external crystal / clock */
#define HCMD_CLKINT       0x48U /* use the internal oscillator      */
#define HCMD_CLKSEL       0x61U
#define HCMD_RST_PULSE    0x68U
#define HCMD_PINDRIVE     0x70U
#define HCMD_PIN_PD_STATE 0x71U

/* ---------------------------------------------------------------------------
 * Display list commands
 * ------------------------------------------------------------------------ */

/* Graphics primitives for BEGIN() */
#define BITMAPS      1UL
#define POINTS       2UL
#define LINES        3UL
#define LINE_STRIP   4UL
#define EDGE_STRIP_R 5UL
#define EDGE_STRIP_L 6UL
#define EDGE_STRIP_A 7UL
#define EDGE_STRIP_B 8UL
#define RECTS        9UL

/* Blend functions */
#define ZERO                0UL
#define ONE                 1UL
#define SRC_ALPHA           2UL
#define DST_ALPHA           3UL
#define ONE_MINUS_SRC_ALPHA 4UL
#define ONE_MINUS_DST_ALPHA 5UL

#define DISPLAY()             0x00000000UL
#define CLEAR_COLOR_RGB(r, g, b) \
    ((0x02UL << 24) | (((uint32_t)(r) & 0xFFUL) << 16) | (((uint32_t)(g) & 0xFFUL) << 8) | ((uint32_t)(b) & 0xFFUL))
#define TAG(s)                ((0x03UL << 24) | ((uint32_t)(s) & 0xFFUL))
#define COLOR_RGB(r, g, b) \
    ((0x04UL << 24) | (((uint32_t)(r) & 0xFFUL) << 16) | (((uint32_t)(g) & 0xFFUL) << 8) | ((uint32_t)(b) & 0xFFUL))
#define BLEND_FUNC(src, dst)  ((0x0BUL << 24) | (((uint32_t)(src) & 7UL) << 3) | ((uint32_t)(dst) & 7UL))
#define POINT_SIZE(size)      ((0x0DUL << 24) | ((uint32_t)(size) & 0x1FFFUL))
#define LINE_WIDTH(width)     ((0x0EUL << 24) | ((uint32_t)(width) & 0xFFFUL))
#define CLEAR_COLOR_A(a)      ((0x0FUL << 24) | ((uint32_t)(a) & 0xFFUL))
#define COLOR_A(a)            ((0x10UL << 24) | ((uint32_t)(a) & 0xFFUL))
#define TAG_MASK(mask)        ((0x14UL << 24) | ((uint32_t)(mask) & 1UL))
#define SCISSOR_XY(x, y)      ((0x1BUL << 24) | (((uint32_t)(x) & 0x7FFUL) << 11) | ((uint32_t)(y) & 0x7FFUL))
#define SCISSOR_SIZE(w, h)    ((0x1CUL << 24) | (((uint32_t)(w) & 0xFFFUL) << 12) | ((uint32_t)(h) & 0xFFFUL))
#define BEGIN(prim)           ((0x1FUL << 24) | ((uint32_t)(prim) & 0xFUL))
#define COLOR_MASK(r, g, b, a) \
    ((0x20UL << 24) | (((uint32_t)(r) & 1UL) << 3) | (((uint32_t)(g) & 1UL) << 2) | (((uint32_t)(b) & 1UL) << 1) | ((uint32_t)(a) & 1UL))
#define END()                 (0x21UL << 24)
#define SAVE_CONTEXT()        (0x22UL << 24)
#define RESTORE_CONTEXT()     (0x23UL << 24)
#define CLEAR(c, s, t) \
    ((0x26UL << 24) | (((uint32_t)(c) & 1UL) << 2) | (((uint32_t)(s) & 1UL) << 1) | ((uint32_t)(t) & 1UL))
#define VERTEX_FORMAT(frac)   ((0x27UL << 24) | ((uint32_t)(frac) & 7UL))

/* Vertex in 1/16 pixel units (the default VERTEX_FORMAT(4)), range +/-1024 px. */
#define VERTEX2F(x, y)        ((1UL << 30) | (((uint32_t)(x) & 0x7FFFUL) << 15) | ((uint32_t)(y) & 0x7FFFUL))
/* Vertex in whole pixels (0..511) with bitmap handle and cell. */
#define VERTEX2II(x, y, handle, cell) \
    ((2UL << 30) | (((uint32_t)(x) & 0x1FFUL) << 21) | (((uint32_t)(y) & 0x1FFUL) << 12) | (((uint32_t)(handle) & 0x1FUL) << 7) | ((uint32_t)(cell) & 0x7FUL))

/* ---------------------------------------------------------------------------
 * Co-processor commands
 * ------------------------------------------------------------------------ */

#define CMD_DLSTART          0xFFFFFF00UL
#define CMD_SWAP             0xFFFFFF01UL
#define CMD_INTERRUPT        0xFFFFFF02UL
#define CMD_BGCOLOR          0xFFFFFF09UL
#define CMD_FGCOLOR          0xFFFFFF0AUL
#define CMD_GRADIENT         0xFFFFFF0BUL
#define CMD_TEXT             0xFFFFFF0CUL
#define CMD_BUTTON           0xFFFFFF0DUL
#define CMD_KEYS             0xFFFFFF0EUL
#define CMD_PROGRESS         0xFFFFFF0FUL
#define CMD_SLIDER           0xFFFFFF10UL
#define CMD_SCROLLBAR        0xFFFFFF11UL
#define CMD_TOGGLE           0xFFFFFF12UL
#define CMD_GAUGE            0xFFFFFF13UL
#define CMD_CLOCK            0xFFFFFF14UL
#define CMD_CALIBRATE        0xFFFFFF15UL
#define CMD_SPINNER          0xFFFFFF16UL
#define CMD_STOP             0xFFFFFF17UL
#define CMD_MEMCRC           0xFFFFFF18UL
#define CMD_REGREAD          0xFFFFFF19UL
#define CMD_MEMWRITE         0xFFFFFF1AUL
#define CMD_MEMSET           0xFFFFFF1BUL
#define CMD_MEMZERO          0xFFFFFF1CUL
#define CMD_MEMCPY           0xFFFFFF1DUL
#define CMD_APPEND           0xFFFFFF1EUL
#define CMD_SNAPSHOT         0xFFFFFF1FUL
#define CMD_INFLATE          0xFFFFFF22UL
#define CMD_GETPTR           0xFFFFFF23UL
#define CMD_LOADIMAGE        0xFFFFFF24UL
#define CMD_GETPROPS         0xFFFFFF25UL
#define CMD_LOADIDENTITY     0xFFFFFF26UL
#define CMD_TRANSLATE        0xFFFFFF27UL
#define CMD_SCALE            0xFFFFFF28UL
#define CMD_ROTATE           0xFFFFFF29UL
#define CMD_SETMATRIX        0xFFFFFF2AUL
#define CMD_SETFONT          0xFFFFFF2BUL
#define CMD_TRACK            0xFFFFFF2CUL
#define CMD_DIAL             0xFFFFFF2DUL
#define CMD_NUMBER           0xFFFFFF2EUL
#define CMD_SCREENSAVER      0xFFFFFF2FUL
#define CMD_SKETCH           0xFFFFFF30UL
#define CMD_LOGO             0xFFFFFF31UL
#define CMD_COLDSTART        0xFFFFFF32UL
#define CMD_GETMATRIX        0xFFFFFF33UL
#define CMD_GRADCOLOR        0xFFFFFF34UL
#define CMD_SETROTATE        0xFFFFFF36UL
#define CMD_SNAPSHOT2        0xFFFFFF37UL
#define CMD_SETBASE          0xFFFFFF38UL
#define CMD_MEDIAFIFO        0xFFFFFF39UL
#define CMD_PLAYVIDEO        0xFFFFFF3AUL
#define CMD_SETFONT2         0xFFFFFF3BUL
#define CMD_SETSCRATCH       0xFFFFFF3CUL
#define CMD_ROMFONT          0xFFFFFF3FUL
#define CMD_VIDEOSTART       0xFFFFFF40UL
#define CMD_VIDEOFRAME       0xFFFFFF41UL
#define CMD_SETBITMAP        0xFFFFFF43UL

/* Widget options */
#define OPT_3D        0U
#define OPT_RGB565    0U
#define OPT_MONO      1U
#define OPT_NODL      2U
#define OPT_NOTEAR    4U
#define OPT_FULLSCREEN 8U
#define OPT_MEDIAFIFO 16U
#define OPT_SOUND     32U
#define OPT_FLAT      256U
#define OPT_SIGNED    256U
#define OPT_CENTERX   512U
#define OPT_CENTERY   1024U
#define OPT_CENTER    1536U
#define OPT_RIGHTX    2048U
#define OPT_NOBACK    4096U
#define OPT_NOTICKS   8192U
#define OPT_NOHM      16384U
#define OPT_NOPOINTER 16384U
#define OPT_NOSECS    32768U
#define OPT_NOHANDS   49152U

#endif /* FT81X_H */
