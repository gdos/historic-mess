/***************************************************************************

  nes.c

  Driver file to handle emulation of the Nintendo Entertainment System (Famicom).

  MESS driver by Brad Oliver (bradman@pobox.com), NES sound code by Matt Conte.
  Based in part on the old xNes code, by Nicolas Hamel, Chuck Mason, Brad Oliver,
  Richard Bannister and Jeff Mitchell.

***************************************************************************/

#include "driver.h"
#include "vidhrdw/generic.h"
#include "vidhrdw/ppu2c03b.h"
#include "includes/nes.h"
#include "cpu/m6502/m6502.h"
#include "devices/cartslot.h"
#include "inputx.h"

unsigned char *battery_ram;
unsigned char *main_ram;

static READ_HANDLER ( nes_mirrorram_r )
{
    return main_ram[offset & 0x7ff];
}

static WRITE_HANDLER ( nes_mirrorram_w )
{
    main_ram[offset & 0x7ff] = data;
}

static READ_HANDLER ( nes_bogus_r )
{
    static int val = 0xff;
    val ^= 0xff;
    return val;
}

MEMORY_READ_START( readmem_nes )
    { 0x0000, 0x07ff, MRA_RAM },                /* RAM */
    { 0x0800, 0x1fff, nes_mirrorram_r },        /* mirrors of RAM */
    { 0x2000, 0x3fff, ppu2c03b_0_r },           /* PPU registers */
    { 0x4015, 0x4015, nes_bogus_r },            /* ?? sound status ?? */
    { 0x4016, 0x4016, nes_IN0_r },              /* IN0 - input port 1 */
    { 0x4017, 0x4017, nes_IN1_r },              /* IN1 - input port 2 */
    { 0x4100, 0x5fff, nes_low_mapper_r },       /* Perform unholy acts on the machine */
//  { 0x6000, 0x7fff, MRA_BANK5 },              /* RAM (also trainer ROM) */
//  { 0x8000, 0x9fff, MRA_BANK1 },              /* 4 16k NES_ROM banks */
//  { 0xa000, 0xbfff, MRA_BANK2 },
//  { 0xc000, 0xdfff, MRA_BANK3 },
//  { 0xe000, 0xffff, MRA_BANK4 },
MEMORY_END

MEMORY_WRITE_START( writemem_nes )
    { 0x0000, 0x07ff, MWA_RAM, &main_ram },
    { 0x0800, 0x1fff, nes_mirrorram_w },        /* mirrors of RAM */
    { 0x2000, 0x3fff, ppu2c03b_0_w },           /* PPU registers */
    { 0x4000, 0x4015, NESPSG_0_w },
    { 0x4016, 0x4016, nes_IN0_w },              /* IN0 - input port 1 */
    { 0x4017, 0x4017, nes_IN1_w },              /* IN1 - input port 2 */
    { 0x4100, 0x5fff, nes_low_mapper_w },       /* Perform unholy acts on the machine */
//  { 0x6000, 0x7fff, nes_mid_mapper_w },       /* RAM (sometimes battery-backed) */
//  { 0x8000, 0xffff, nes_mapper_w },           /* Perform unholy acts on the machine */
MEMORY_END


INPUT_PORTS_START( nes )
	PORT_START  /* IN0 */
	PORT_BITX( 0x01, IP_ACTIVE_HIGH, IPT_BUTTON1		| IPF_PLAYER1, "P1 A",			KEYCODE_LALT,		JOYCODE_1_BUTTON1 )
	PORT_BITX( 0x02, IP_ACTIVE_HIGH, IPT_BUTTON2		| IPF_PLAYER1, "P1 B",			KEYCODE_LCONTROL,	JOYCODE_1_BUTTON2 )
	PORT_BIT ( 0x04, IP_ACTIVE_HIGH, IPT_SELECT			| IPF_PLAYER1 )
	PORT_BIT ( 0x08, IP_ACTIVE_HIGH, IPT_START			| IPF_PLAYER1 )
	PORT_BITX( 0x10, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP	| IPF_PLAYER1, "P1 Up",			KEYCODE_UP,			JOYCODE_1_UP )
	PORT_BITX( 0x20, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN	| IPF_PLAYER1, "P1 Down",		KEYCODE_DOWN,		JOYCODE_1_DOWN )
	PORT_BITX( 0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT	| IPF_PLAYER1, "P1 Left",		KEYCODE_LEFT,		JOYCODE_1_LEFT )
	PORT_BITX( 0x80, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT	| IPF_PLAYER1, "P1 Right",		KEYCODE_RIGHT,		JOYCODE_1_RIGHT )

	PORT_START  /* IN1 */
	PORT_BITX( 0x01, IP_ACTIVE_HIGH, IPT_BUTTON1		| IPF_PLAYER2, "P2 A",			KEYCODE_0_PAD,		JOYCODE_2_BUTTON1 )
	PORT_BITX( 0x02, IP_ACTIVE_HIGH, IPT_BUTTON2		| IPF_PLAYER2, "P2 B",			KEYCODE_DEL_PAD,	JOYCODE_2_BUTTON2 )
	PORT_BIT ( 0x04, IP_ACTIVE_HIGH, IPT_SELECT			| IPF_PLAYER2 )
	PORT_BIT ( 0x08, IP_ACTIVE_HIGH, IPT_START			| IPF_PLAYER2 )
	PORT_BITX( 0x10, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP	| IPF_PLAYER2, "P2 Up",			KEYCODE_8_PAD,		JOYCODE_2_UP )
	PORT_BITX( 0x20, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN	| IPF_PLAYER2, "P2 Down",		KEYCODE_2_PAD,		JOYCODE_2_DOWN )
	PORT_BITX( 0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT	| IPF_PLAYER2, "P2 Left",		KEYCODE_4_PAD,		JOYCODE_2_LEFT )
	PORT_BITX( 0x80, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT	| IPF_PLAYER2, "P2 Right",		KEYCODE_6_PAD,		JOYCODE_2_RIGHT )

	PORT_START  /* IN2 - configuration */
	PORT_CONFNAME( 0x0f, 0x00, "P1 Controller")
	PORT_CONFSETTING(    0x00, "Joypad" )
	PORT_CONFSETTING(    0x01, "Zapper" )
	PORT_CONFSETTING(    0x02, "P1/P3 multi-adapter" )
	PORT_CONFNAME( 0xf0, 0x00, "P2 Controller")
	PORT_CONFSETTING(    0x00, "Joypad" )
	PORT_CONFSETTING(    0x10, "Zapper" )
	PORT_CONFSETTING(    0x20, "P2/P4 multi-adapter" )
	PORT_CONFSETTING(    0x30, "Arkanoid paddle" )

	PORT_START  /* IN3 - P1 zapper */
	PORT_ANALOG( 0xff, 0x80, IPT_LIGHTGUN_X | IPF_PLAYER1, 70, 30, 0, 255 )

	PORT_START  /* IN4 - P1 zapper */
	PORT_ANALOG( 0xff, 0x80, IPT_LIGHTGUN_Y | IPF_PLAYER1, 50, 30, 0, 255 )

	PORT_START  /* IN5 - P2 zapper */
	PORT_ANALOG( 0xff, 0x80, IPT_LIGHTGUN_X | IPF_PLAYER2, 70, 30, 0, 255 )

	PORT_START  /* IN6 - P2 zapper */
	PORT_ANALOG( 0xff, 0x80, IPT_LIGHTGUN_Y | IPF_PLAYER2, 50, 30, 0, 255 )

	PORT_START  /* IN7 - configuration */
	PORT_CONFNAME( 0x01, 0x00, "Draw Top/Bottom 8 Lines")
	PORT_CONFSETTING(    0x01, DEF_STR(No) )
	PORT_CONFSETTING(    0x00, DEF_STR(Yes) )
	PORT_CONFNAME( 0x02, 0x00, "Enforce 8 Sprites/line")
	PORT_CONFSETTING(    0x02, DEF_STR(No) )
	PORT_CONFSETTING(    0x00, DEF_STR(Yes) )

	PORT_START  /* IN8 */
	PORT_BIT ( 0x01, IP_ACTIVE_HIGH, IPT_BUTTON1 | IPF_PLAYER3 )
	PORT_BIT ( 0x02, IP_ACTIVE_HIGH, IPT_BUTTON2 | IPF_PLAYER3 )
	PORT_BIT ( 0x04, IP_ACTIVE_HIGH, IPT_SELECT			| IPF_PLAYER3 )
	PORT_BIT ( 0x08, IP_ACTIVE_HIGH, IPT_START			| IPF_PLAYER3 )
	PORT_BIT ( 0x10, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP | IPF_PLAYER3 )
	PORT_BIT ( 0x20, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN | IPF_PLAYER3 )
	PORT_BIT ( 0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_PLAYER3 )
	PORT_BIT ( 0x80, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_PLAYER3 )

	PORT_START  /* IN9 */
	PORT_BIT ( 0x01, IP_ACTIVE_HIGH, IPT_BUTTON1 | IPF_PLAYER4 )
	PORT_BIT ( 0x02, IP_ACTIVE_HIGH, IPT_BUTTON2 | IPF_PLAYER4 )
	PORT_BIT ( 0x04, IP_ACTIVE_HIGH, IPT_SELECT			| IPF_PLAYER4 )
	PORT_BIT ( 0x08, IP_ACTIVE_HIGH, IPT_START			| IPF_PLAYER4 )
	PORT_BIT ( 0x10, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP | IPF_PLAYER4 )
	PORT_BIT ( 0x20, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN | IPF_PLAYER4 )
	PORT_BIT ( 0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_PLAYER4 )
	PORT_BIT ( 0x80, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_PLAYER4 )

	PORT_START  /* IN10 - arkanoid paddle */
	PORT_ANALOG( 0xff, 0x7f, IPT_PADDLE, 25, 3, 0x62, 0xf2 )
INPUT_PORTS_END

INPUT_PORTS_START( famicom )
	PORT_START  /* IN0 */
	PORT_BITX( 0x01, IP_ACTIVE_HIGH, IPT_BUTTON1		| IPF_PLAYER1, "P1 A",			KEYCODE_LALT,		JOYCODE_1_BUTTON1 )
	PORT_BITX( 0x02, IP_ACTIVE_HIGH, IPT_BUTTON2		| IPF_PLAYER1, "P1 B",			KEYCODE_LCONTROL,	JOYCODE_1_BUTTON2 )
	PORT_BIT ( 0x04, IP_ACTIVE_HIGH, IPT_SELECT			| IPF_PLAYER1 )
	PORT_BIT ( 0x08, IP_ACTIVE_HIGH, IPT_START			| IPF_PLAYER1 )
	PORT_BITX( 0x10, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP	| IPF_PLAYER1, "P1 Up",			KEYCODE_UP,			JOYCODE_1_UP )
	PORT_BITX( 0x20, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN	| IPF_PLAYER1, "P1 Down",		KEYCODE_DOWN,		JOYCODE_1_DOWN )
	PORT_BITX( 0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT	| IPF_PLAYER1, "P1 Left",		KEYCODE_LEFT,		JOYCODE_1_LEFT )
	PORT_BITX( 0x80, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT	| IPF_PLAYER1, "P1 Right",		KEYCODE_RIGHT,		JOYCODE_1_RIGHT )

	PORT_START  /* IN1 */
	PORT_BITX( 0x01, IP_ACTIVE_HIGH, IPT_BUTTON1		| IPF_PLAYER2, "P2 A",			KEYCODE_0_PAD,		JOYCODE_2_BUTTON1 )
	PORT_BITX( 0x02, IP_ACTIVE_HIGH, IPT_BUTTON2		| IPF_PLAYER2, "P2 B",			KEYCODE_DEL_PAD,	JOYCODE_2_BUTTON2 )
	PORT_BIT ( 0x04, IP_ACTIVE_HIGH, IPT_SELECT			| IPF_PLAYER2 )
	PORT_BIT ( 0x08, IP_ACTIVE_HIGH, IPT_START			| IPF_PLAYER2 )
	PORT_BITX( 0x10, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP	| IPF_PLAYER2, "P2 Up",			KEYCODE_8_PAD,		JOYCODE_2_UP )
	PORT_BITX( 0x20, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN	| IPF_PLAYER2, "P2 Down",		KEYCODE_2_PAD,		JOYCODE_2_DOWN )
	PORT_BITX( 0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT	| IPF_PLAYER2, "P2 Left",		KEYCODE_4_PAD,		JOYCODE_2_LEFT )
	PORT_BITX( 0x80, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT	| IPF_PLAYER2, "P2 Right",		KEYCODE_6_PAD,		JOYCODE_2_RIGHT )

	PORT_START  /* IN2 - fake */
	PORT_CONFNAME( 0x0f, 0x00, "P1 Controller")
	PORT_CONFSETTING(    0x00, "Joypad" )
	PORT_CONFSETTING(    0x01, "Zapper" )
	PORT_CONFSETTING(    0x02, "P1/P3 multi-adapter" )
	PORT_CONFNAME( 0xf0, 0x00, "P2 Controller")
	PORT_CONFSETTING(    0x00, "Joypad" )
	PORT_CONFSETTING(    0x10, "Zapper" )
	PORT_CONFSETTING(    0x20, "P2/P4 multi-adapter" )
	PORT_CONFSETTING(    0x30, "Arkanoid paddle" )

	PORT_START  /* IN3 - P1 zapper */
	PORT_ANALOG( 0xff, 0x80, IPT_LIGHTGUN_X | IPF_PLAYER1, 70, 30, 0, 255 )

	PORT_START  /* IN4 - P1 zapper */
	PORT_ANALOG( 0xff, 0x80, IPT_LIGHTGUN_Y | IPF_PLAYER1, 50, 30, 0, 255 )

	PORT_START  /* IN5 - P2 zapper */
	PORT_ANALOG( 0xff, 0x80, IPT_LIGHTGUN_X | IPF_PLAYER2, 70, 30, 0, 255 )

	PORT_START  /* IN6 - P2 zapper */
	PORT_ANALOG( 0xff, 0x80, IPT_LIGHTGUN_Y | IPF_PLAYER2, 50, 30, 0, 255 )

	PORT_START  /* IN7 - fake dips */
	PORT_CONFNAME( 0x01, 0x00, "Draw Top/Bottom 8 Lines")
	PORT_CONFSETTING(    0x01, DEF_STR(No) )
	PORT_CONFSETTING(    0x00, DEF_STR(Yes) )
	PORT_CONFNAME( 0x02, 0x00, "Enforce 8 Sprites/line")
	PORT_CONFSETTING(    0x02, DEF_STR(No) )
	PORT_CONFSETTING(    0x00, DEF_STR(Yes) )

	PORT_START  /* IN8 */
	PORT_BIT ( 0x01, IP_ACTIVE_HIGH, IPT_BUTTON1 | IPF_PLAYER3 )
	PORT_BIT ( 0x02, IP_ACTIVE_HIGH, IPT_BUTTON2 | IPF_PLAYER3 )
	PORT_BIT ( 0x04, IP_ACTIVE_HIGH, IPT_SELECT			| IPF_PLAYER3 )
	PORT_BIT ( 0x08, IP_ACTIVE_HIGH, IPT_START			| IPF_PLAYER3 )
	PORT_BIT ( 0x10, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP | IPF_PLAYER3 )
	PORT_BIT ( 0x20, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN | IPF_PLAYER3 )
	PORT_BIT ( 0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_PLAYER3 )
	PORT_BIT ( 0x80, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_PLAYER3 )

	PORT_START  /* IN9 */
	PORT_BIT ( 0x01, IP_ACTIVE_HIGH, IPT_BUTTON1 | IPF_PLAYER4 )
	PORT_BIT ( 0x02, IP_ACTIVE_HIGH, IPT_BUTTON2 | IPF_PLAYER4 )
	PORT_BIT ( 0x04, IP_ACTIVE_HIGH, IPT_SELECT			| IPF_PLAYER4 )
	PORT_BIT ( 0x08, IP_ACTIVE_HIGH, IPT_START			| IPF_PLAYER4 )
	PORT_BIT ( 0x10, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP | IPF_PLAYER4 )
	PORT_BIT ( 0x20, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN | IPF_PLAYER4 )
	PORT_BIT ( 0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_PLAYER4 )
	PORT_BIT ( 0x80, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_PLAYER4 )

	PORT_START  /* IN10 - arkanoid paddle */
	PORT_ANALOG( 0xff, 0x7f, IPT_PADDLE, 25, 3, 0x62, 0xf2 )

	PORT_START /* IN11 - fake keys */
//  PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_BUTTON3 )
	PORT_BITX ( 0x01, IP_ACTIVE_HIGH, IPT_BUTTON3, "Change Disk Side", IP_KEY_DEFAULT, IP_JOY_DEFAULT )
INPUT_PORTS_END

/* This layout is not changed at runtime */
struct GfxLayout nes_vram_charlayout =
{
    8,8,    /* 8*8 characters */
    512,    /* 512 characters */
    2,  /* 2 bits per pixel */
    { 8*8, 0 }, /* the two bitplanes are separated */
    { 0, 1, 2, 3, 4, 5, 6, 7 },
    { 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 },
    16*8    /* every char takes 16 consecutive bytes */
};


static struct GfxDecodeInfo nes_gfxdecodeinfo[] =
{
//    { REGION_GFX1, 0x0000, &nes_charlayout,        0, 8 },
//    { REGION_GFX2, 0x0000, &nes_vram_charlayout,   0, 8 },
MEMORY_END   /* end of array */


static WRITE_HANDLER(nes_vh_sprite_dma_w)
{
	unsigned char *RAM = memory_region(REGION_CPU1);
	ppu2c03b_spriteram_dma(0, &RAM[data * 0x100]);
}

static struct NESinterface nes_interface =
{
    1,
    N2A03_DEFAULTCLOCK ,
    { 100 },
    { 0 },
    { nes_vh_sprite_dma_w },
    { NULL }
};

static struct NESinterface nespal_interface =
{
    1,
    26601712/15,
    { 100 },
    { 0 },
    { nes_vh_sprite_dma_w },
    { NULL }
};

ROM_START( nes )
    ROM_REGION( 0x10000, REGION_CPU1,0 )  /* Main RAM + program banks */
    ROM_REGION( 0x2000,  REGION_GFX1,0 )  /* VROM */
    ROM_REGION( 0x2000,  REGION_GFX2,0 )  /* VRAM */
    ROM_REGION( 0x10000, REGION_USER1,0 ) /* WRAM */
ROM_END

ROM_START( nespal )
    ROM_REGION( 0x10000, REGION_CPU1,0 )  /* Main RAM + program banks */
    ROM_REGION( 0x2000,  REGION_GFX1,0 )  /* VROM */
    ROM_REGION( 0x2000,  REGION_GFX2,0 )  /* VRAM */
    ROM_REGION( 0x10000, REGION_USER1,0 ) /* WRAM */
ROM_END

ROM_START( famicom )
    ROM_REGION( 0x10000, REGION_CPU1,0 )  /* Main RAM + program banks */
    ROM_LOAD_OPTIONAL ("disksys.rom", 0xe000, 0x2000, CRC(5e607dcf))

    ROM_REGION( 0x2000,  REGION_GFX1,0 )  /* VROM */

    ROM_REGION( 0x2000,  REGION_GFX2,0 )  /* VRAM */

    ROM_REGION( 0x10000, REGION_USER1,0 ) /* WRAM */
ROM_END



static MACHINE_DRIVER_START( nes )
	/* basic machine hardware */
	MDRV_CPU_ADD_TAG("main", N2A03, N2A03_DEFAULTCLOCK)        /* 0,894886 Mhz */
	MDRV_CPU_MEMORY(readmem_nes,writemem_nes)
	MDRV_FRAMES_PER_SECOND(60)
	MDRV_VBLANK_DURATION(114*(NTSC_SCANLINES_PER_FRAME-BOTTOM_VISIBLE_SCANLINE))

	MDRV_MACHINE_INIT( nes )
	MDRV_MACHINE_STOP( nes )

	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER)
	MDRV_SCREEN_SIZE(32*8, 30*8)
	MDRV_VISIBLE_AREA(0*8, 32*8-1, 0*8, 30*8-1)
	MDRV_PALETTE_INIT(nes)
	MDRV_VIDEO_START(nes)
	MDRV_VIDEO_UPDATE(nes)

	MDRV_GFXDECODE(nes_gfxdecodeinfo)

	MDRV_PALETTE_LENGTH(4*16*8)
	MDRV_COLORTABLE_LENGTH(4*8)

    /* sound hardware */
	MDRV_SOUND_ADD_TAG("nessound", NES, nes_interface)
MACHINE_DRIVER_END

static MACHINE_DRIVER_START( nespal )
	MDRV_IMPORT_FROM( nes )

	/* basic machine hardware */
	MDRV_CPU_REPLACE("main", N2A03, 26601712/15)        /* 0,894886 Mhz */
	MDRV_FRAMES_PER_SECOND(50)

    /* sound hardware */
	MDRV_SOUND_REPLACE("nessound", NES, nespal_interface)
MACHINE_DRIVER_END

SYSTEM_CONFIG_START(nes)
	CONFIG_DEVICE_CARTSLOT_REQ(1, "nes\0", NULL, NULL, device_load_nes_cart, NULL, NULL, nes_partialcrc)
SYSTEM_CONFIG_END

SYSTEM_CONFIG_START(famicom)
	CONFIG_DEVICE_CARTSLOT_OPT(1, "nes\0", NULL, NULL, device_load_nes_cart, NULL, NULL, nes_partialcrc)
	CONFIG_DEVICE_LEGACY(IO_FLOPPY, 1, "dsk\0fds\0", DEVICE_LOAD_RESETS_NONE, OSD_FOPEN_READ, NULL, NULL, device_load_nes_disk, device_unload_nes_disk, NULL)
SYSTEM_CONFIG_END

/***************************************************************************

  Game driver(s)

***************************************************************************/

/*     YEAR  NAME      PARENT    COMPAT	MACHINE   INPUT     INIT      CONFIG	COMPANY   FULLNAME */
CONS( 1983, famicom,   0,        0,		nes,      famicom,  nes,      famicom,	"Nintendo", "Famicom" )
CONS( 1985, nes,       0,        0,		nes,      nes,      nes,      nes,		"Nintendo", "Nintendo Entertainment System (NTSC)" )
CONS( 1987, nespal,    nes,      0,		nespal,   nes,      nespal,   nes,		"Nintendo", "Nintendo Entertainment System (PAL)" )

