#include "driver.h"
#include "vidhrdw/generic.h"

/*******************************************************************************
 Shadow Handling Notes
********************************************************************************
 previously shadows were handled by toggling them on and off with a
 shadows_visible flag.

 Games Not Using Shadows?

 those using gwar_vh_screenrefresh (gwar, bermudat, psychos, chopper1)
	(0-15 , 15 is transparent)
 those using tnk3_vh_screenrefresh (tnk3, athena, fitegolf) sgladiat is similar
	(0-7  , 7  is transparent) * these are using aso colour prom convert *

 Games Using Shadows?

 those using tdfever_vh_screenrefresh (tdfever)
	(0-15 , 14 is shadow, 15 is transparent)
 those using ftsoccer_vh_screenrefresh (ftsoccer)
	(0-15 , 14 is shadow, 15 is transparent)
 those using ikari_vh_screenrefresh (ikari, victroad)
    (0-7  , 6  is shadow, 7  is transparent)

*******************************************************************************/


int snk_bg_tilemap_baseaddr, gwar_sprite_placement;

#define MAX_VRAM_SIZE (64*64*2) /* 0x2000 */

// static int shadows_visible = 0; /* toggles rapidly to fake translucency in ikari warriors */

static void print( struct mame_bitmap *bitmap, int num, int row ){
	char *digit = "0123456789abcdef";

	drawgfx( bitmap,Machine->uifont,
		digit[(num>>4)&0xf],
		0,
		0,0, /* no flip */
		24,row*8+8,
		0,TRANSPARENCY_NONE,0);
	drawgfx( bitmap,Machine->uifont,
		digit[num&0xf],
		0,
		0,0, /* no flip */
		32,row*8+8,
		0,TRANSPARENCY_NONE,0);
}

PALETTE_INIT( snk_3bpp_shadow ){
	int i;
	palette_init_RRRR_GGGG_BBBB(colortable, color_prom);

	if (!(Machine->drv->video_attributes & VIDEO_HAS_SHADOWS))
	usrintf_showmessage("driver should use VIDEO_HAS_SHADOWS");

	/* prepare shadow draw table */
	for (i = 0;i < 8;i++)
		gfx_drawmode_table[i] = DRAWMODE_SOURCE;

	gfx_drawmode_table[6] = DRAWMODE_SHADOW;
	gfx_drawmode_table[7] = DRAWMODE_NONE;

}

PALETTE_INIT( snk_4bpp_shadow ){
	int i;
	palette_init_RRRR_GGGG_BBBB(colortable, color_prom);

	if (!(Machine->drv->video_attributes & VIDEO_HAS_SHADOWS))
	usrintf_showmessage("driver should use VIDEO_HAS_SHADOWS");

	/* prepare shadow draw table */
	for (i = 0;i < 16;i++)
		gfx_drawmode_table[i] = DRAWMODE_SOURCE;

	gfx_drawmode_table[14] = DRAWMODE_SHADOW;
	gfx_drawmode_table[15] = DRAWMODE_NONE;

}

VIDEO_START( snk ){
	dirtybuffer = auto_malloc( MAX_VRAM_SIZE );
	if( !dirtybuffer )
		return 1;
	tmpbitmap = auto_bitmap_alloc( 512, 512 );
	if( !tmpbitmap )
		return 1;
	memset( dirtybuffer, 0xff, MAX_VRAM_SIZE  );
//	shadows_visible = 1;
	return 0;
}

/**************************************************************************************/

static void tnk3_draw_background( struct mame_bitmap *bitmap, int scrollx, int scrolly )
{
	const struct rectangle *clip = &Machine->visible_area;
	const struct GfxElement *gfx = Machine->gfx[1];
	int offs;
	for( offs=0; offs<64*64*2; offs+=2 )
	{
		int tile_number = videoram[offs];
		unsigned char attributes = videoram[offs+1];

		if( tile_number!=dirtybuffer[offs] || attributes != dirtybuffer[offs+1] )
		{
			int sy = ((offs/2)%64)*8;
			int sx = ((offs/2)/64)*8;
			int color = (attributes&0xf)^0x8;

			dirtybuffer[offs] = tile_number;
			dirtybuffer[offs+1] = attributes;

			tile_number += 256*((attributes>>4)&0x3);

			drawgfx( tmpbitmap,gfx,
				tile_number,
				color,
				0,0, /* no flip */
				sx,sy,
				0,TRANSPARENCY_NONE,0);
		}
	}
	copyscrollbitmap( bitmap,tmpbitmap,
		1,&scrollx,1,&scrolly,
		clip,
		TRANSPARENCY_NONE,0 );
}

void tnk3_draw_text( struct mame_bitmap *bitmap, int bank, unsigned char *source ){
	const struct GfxElement *gfx = Machine->gfx[0];
	int offs;

	bank*=256;

	for( offs = 0;offs <0x400; offs++ ){
		drawgfx( bitmap, gfx,
			source[offs]+bank,
			source[offs]>>5,
			0,0, /* no flip */
			16+(offs/32)*8,(offs%32)*8+8,
			0,
			TRANSPARENCY_PEN,15 );
	}
}

void tnk3_draw_status( struct mame_bitmap *bitmap, int bank, unsigned char *source ){
	const struct rectangle *clip = &Machine->visible_area;
	const struct GfxElement *gfx = Machine->gfx[0];
	int offs;

	bank *= 256;

	for( offs = 0; offs<64; offs++ ){
		int tile_number = source[offs+30*32];
		int sy = (offs % 32)*8+8;
		int sx = (offs / 32)*8;

		drawgfx(bitmap,gfx,
			tile_number+bank,
			tile_number>>5,
			0,0, /* no flip */
			sx,sy,
			clip,TRANSPARENCY_NONE,0);

		tile_number = source[offs];
		sx += 34*8;

		drawgfx(bitmap,gfx,
			tile_number+bank,
			tile_number>>5,
			0,0, /* no flip */
			sx,sy,
			clip,TRANSPARENCY_NONE,0);
	}
}

void tnk3_draw_sprites( struct mame_bitmap *bitmap, int xscroll, int yscroll )
{
	const unsigned char *source = spriteram;
	const unsigned char *finish = source+50*4;
	struct rectangle clip = Machine->visible_area;

	while( source<finish )
	{
		int attributes = source[3]; /* YBBX.CCCC */
		int tile_number = source[1];
		int sy = source[0] + ((attributes&0x10)?256:0) - yscroll;
		int sx = source[2] + ((attributes&0x80)?256:0) - xscroll;
		int color = attributes&0xf;

		if( attributes&0x40 ) tile_number += 256;
		if( attributes&0x20 ) tile_number += 512;

		drawgfx(bitmap,Machine->gfx[2],
			tile_number,
			color,
			0,0,
			(256-sx)&0x1ff,sy&0x1ff,
			&clip,TRANSPARENCY_PEN,7);

		source+=4;
	}
}

VIDEO_UPDATE( tnk3 )
{
	unsigned char *ram = memory_region(REGION_CPU1);
	int attributes = ram[0xc800];
	/*
		X-------
		-X------	character bank (for text layer)
		--X-----
		---X----	scrolly MSB (background)
		----X---	scrolly MSB (sprites)
		-----X--
		------X-	scrollx MSB (background)
		-------X	scrollx MSB (sprites)
	*/

	/* to be moved to memmap */
	spriteram = &ram[0xd000];
	videoram = &ram[0xd800];

	{
		int scrolly =  -8+ram[0xcb00]+((attributes&0x10)?256:0);
		int scrollx = -16+ram[0xcc00]+((attributes&0x02)?256:0);
		tnk3_draw_background( bitmap, -scrollx, -scrolly );
	}

	{
		int scrolly =  8+ram[0xc900] + ((attributes&0x08)?256:0);
		int scrollx = 30+ram[0xca00] + ((attributes&0x01)?256:0);
		tnk3_draw_sprites( bitmap, scrollx, scrolly );
	}

	{
		int bank = (attributes&0x40)?1:0;
		tnk3_draw_text( bitmap, bank, &ram[0xf800] );
		tnk3_draw_status( bitmap, bank, &ram[0xfc00] );
	}
}

/************************************************************************************/

VIDEO_START( sgladiat ){
	dirtybuffer = auto_malloc( MAX_VRAM_SIZE );
	if( !dirtybuffer )
		return 1;
	tmpbitmap = auto_bitmap_alloc( 512, 256 );
	if( !tmpbitmap )
		return 1;
	memset( dirtybuffer, 0xff, MAX_VRAM_SIZE  );
//	shadows_visible = 1;
	return 0;
}

static void sgladiat_draw_background( struct mame_bitmap *bitmap, int scrollx, int scrolly )
{
	const struct rectangle *clip = &Machine->visible_area;
	const struct GfxElement *gfx = Machine->gfx[1];
	int offs;
	for( offs=0; offs<64*32; offs++ )
	{
		int tile_number = videoram[offs];

		if( tile_number!=dirtybuffer[offs] )
		{
			int sy = (offs%32)*8;
			int sx = (offs/32)*8;
			int color = 0;

			dirtybuffer[offs] = tile_number;

			drawgfx( tmpbitmap,gfx,
				tile_number,
				color,
				0,0, /* no flip */
				sx,sy,
				0,TRANSPARENCY_NONE,0);
		}
	}

	copyscrollbitmap(bitmap,tmpbitmap,
		1,&scrollx,1,&scrolly,
		clip,
		TRANSPARENCY_NONE,0);
}

VIDEO_UPDATE( sgladiat )
{
	unsigned char *pMem = memory_region(REGION_CPU1);
	int attributes, scrollx, scrolly;

	attributes = pMem[0xd300];

	scrolly =  -8+pMem[0xd600];
	scrollx = -16+pMem[0xd700] + ((attributes&0x02)?256:0);
	sgladiat_draw_background( bitmap, -scrollx, -scrolly );

	scrolly =  8+pMem[0xd400];
	scrollx = 30+pMem[0xd500] + ((attributes&0x01)?256:0);
	tnk3_draw_sprites( bitmap, scrollx, scrolly );

	tnk3_draw_text( bitmap, 0, &pMem[0xf000] );
}

/**************************************************************************************/

static void ikari_draw_background( struct mame_bitmap *bitmap, int xscroll, int yscroll ){
	const struct GfxElement *gfx = Machine->gfx[1];
	const unsigned char *source = &memory_region(REGION_CPU1)[snk_bg_tilemap_baseaddr];

	int offs;
	for( offs=0; offs<32*32*2; offs+=2 ){
		int tile_number = source[offs];
		unsigned char attributes = source[offs+1];

		if( tile_number!=dirtybuffer[offs] ||
			attributes != dirtybuffer[offs+1] ){

			int sy = ((offs/2)%32)*16;
			int sx = ((offs/2)/32)*16;

			dirtybuffer[offs] = tile_number;
			dirtybuffer[offs+1] = attributes;

			tile_number+=256*(attributes&0x3);

			drawgfx(tmpbitmap,gfx,
				tile_number,
				(attributes>>4), /* color */
				0,0, /* no flip */
				sx,sy,
				0,TRANSPARENCY_NONE,0);
		}
	}

	{
		struct rectangle clip = Machine->visible_area;
		clip.min_x += 16;
		clip.max_x -= 16;
		copyscrollbitmap(bitmap,tmpbitmap,
			1,&xscroll,1,&yscroll,
			&clip,
			TRANSPARENCY_NONE,0);
	}
}

static void ikari_draw_text( struct mame_bitmap *bitmap ){
	const struct rectangle *clip = &Machine->visible_area;
	const struct GfxElement *gfx = Machine->gfx[0];
	const unsigned char *source = &memory_region(REGION_CPU1)[0xf800];

	int offs;
	for( offs = 0;offs <0x400; offs++ ){
		int tile_number = source[offs];
		int sy = (offs % 32)*8+8;
		int sx = (offs / 32)*8+16;

		drawgfx(bitmap,gfx,
			tile_number,
			8, /* color - vreg needs mapped! */
			0,0, /* no flip */
			sx,sy,
			clip,TRANSPARENCY_PEN,15);
	}
}

static void ikari_draw_status( struct mame_bitmap *bitmap ){
	/*	this is drawn above and below the main display */

	const struct rectangle *clip = &Machine->visible_area;
	const struct GfxElement *gfx = Machine->gfx[0];
	const unsigned char *source = &memory_region(REGION_CPU1)[0xfc00];

	int offs;
	for( offs = 0; offs<64; offs++ ){
		int tile_number = source[offs+30*32];
		int sy = 20+(offs % 32)*8 - 16;
		int sx = (offs / 32)*8;

		drawgfx(bitmap,gfx,
			tile_number,
			8, /* color */
			0,0, /* no flip */
			sx,sy,
			clip,TRANSPARENCY_NONE,0);

		tile_number = source[offs];
		sx += 34*8;

		drawgfx(bitmap,gfx,
			tile_number,
			8, /* color */
			0,0, /* no flip */
			sx,sy,
			clip,TRANSPARENCY_NONE,0);
	}
}

static void ikari_draw_sprites_16x16( struct mame_bitmap *bitmap, int start, int quantity, int xscroll, int yscroll )
{
//	int transp_mode  = shadows_visible ? TRANSPARENCY_PEN : TRANSPARENCY_PENS;
//	int transp_param = shadows_visible ? 7 : ((1<<7) | (1<<6));

	int which;
	const unsigned char *source = &memory_region(REGION_CPU1)[0xe800];

	struct rectangle clip = Machine->visible_area;
	clip.min_x += 16;
	clip.max_x -= 16;

	for( which = start*4; which < (start+quantity)*4; which+=4 )
	{
		int attributes = source[which+3]; /* YBBX.CCCC */
		int tile_number = source[which+1] + ((attributes&0x60)<<3);
		int sy = - yscroll + source[which]  +((attributes&0x10)?256:0);
		int sx =   xscroll - source[which+2]+((attributes&0x80)?0:256);

		drawgfx(bitmap,Machine->gfx[2],
			tile_number,
			attributes&0xf, /* color */
			0,0, /* flip */
			-16+(sx & 0x1ff), -16+(sy & 0x1ff),
			&clip,TRANSPARENCY_PEN_TABLE,7);
	}
}

static void ikari_draw_sprites_32x32( struct mame_bitmap *bitmap, int start, int quantity, int xscroll, int yscroll )
{
//	int transp_mode  = shadows_visible ? TRANSPARENCY_PEN : TRANSPARENCY_PENS;
//	int transp_param = shadows_visible ? 7 : ((1<<7) | (1<<6));

	int which;
	const unsigned char *source = &memory_region(REGION_CPU1)[0xe000];

	struct rectangle clip = Machine->visible_area;
	clip.min_x += 16;
	clip.max_x -= 16;

	for( which = start*4; which < (start+quantity)*4; which+=4 )
	{
		int attributes = source[which+3];
		int tile_number = source[which+1];
		int sy = - yscroll + source[which] + ((attributes&0x10)?256:0);
		int sx = xscroll - source[which+2] + ((attributes&0x80)?0:256);
		if( attributes&0x40 ) tile_number += 256;

		drawgfx( bitmap,Machine->gfx[3],
			tile_number,
			attributes&0xf, /* color */
			0,0, /* flip */
			-16+(sx & 0x1ff), -16+(sy & 0x1ff),
			&clip,TRANSPARENCY_PEN_TABLE,7 );

	}
}

VIDEO_UPDATE( ikari ){
	const unsigned char *ram = memory_region(REGION_CPU1);

//	shadows_visible = !shadows_visible;

	{
		int attributes = ram[0xc900];
		int scrolly =  8-ram[0xc800] - ((attributes&0x01) ? 256:0);
		int scrollx = 13-ram[0xc880] - ((attributes&0x02) ? 256:0);
		ikari_draw_background( bitmap, scrollx, scrolly );
	}
	{
		int attributes = ram[0xcd00];

		int sp16_scrolly = -7 + ram[0xca00] + ((attributes&0x04) ? 256:0);
		int sp16_scrollx = 44 + ram[0xca80] + ((attributes&0x10) ? 256:0);

		int sp32_scrolly =  9 + ram[0xcb00] + ((attributes&0x08) ? 256:0);
		int sp32_scrollx = 28 + ram[0xcb80] + ((attributes&0x20) ? 256:0);

		ikari_draw_sprites_16x16( bitmap,  0, 25, sp16_scrollx, sp16_scrolly );
		ikari_draw_sprites_32x32( bitmap,  0, 25, sp32_scrollx, sp32_scrolly );
		ikari_draw_sprites_16x16( bitmap, 25, 25, sp16_scrollx, sp16_scrolly );
	}
	ikari_draw_text( bitmap );
	ikari_draw_status( bitmap );
}

/**************************************************************/

static void tdfever_draw_background( struct mame_bitmap *bitmap,
		int xscroll, int yscroll )
{
	const struct GfxElement *gfx = Machine->gfx[1];
	const unsigned char *source = &memory_region(REGION_CPU1)[0xd000]; //d000

	int offs;
	for( offs=0; offs<32*32*2; offs+=2 ){
		int tile_number = source[offs];
		unsigned char attributes = source[offs+1];

		if( tile_number!=dirtybuffer[offs] ||
			attributes != dirtybuffer[offs+1] ){

			int sy = ((offs/2)%32)*16;
			int sx = ((offs/2)/32)*16;

			int color = (attributes>>4); /* color */

			dirtybuffer[offs] = tile_number;
			dirtybuffer[offs+1] = attributes;

			tile_number+=256*(attributes&0xf);

			drawgfx(tmpbitmap,gfx,
				tile_number,
				color,
				0,0, /* no flip */
				sx,sy,
				0,TRANSPARENCY_NONE,0);
		}
	}

	{
		struct rectangle clip = Machine->visible_area;
		copyscrollbitmap(bitmap,tmpbitmap,
			1,&xscroll,1,&yscroll,
			&clip,
			TRANSPARENCY_NONE,0);
	}
}

static void tdfever_draw_sprites( struct mame_bitmap *bitmap, int xscroll, int yscroll ){
//	int transp_mode  = shadows_visible ? TRANSPARENCY_PEN : TRANSPARENCY_PENS;
//	int transp_param = shadows_visible ? 15 : ((1<<15) | (1<<14));

	const struct GfxElement *gfx = Machine->gfx[2];
	const unsigned char *source = &memory_region(REGION_CPU1)[0xe000];

	int which;

	struct rectangle clip = Machine->visible_area;

	for( which = 0; which < 32*4; which+=4 ){
		int attributes = source[which+3];
		int tile_number = source[which+1] + 8*(attributes&0x60);

		int sy = - yscroll + source[which];
		int sx = xscroll - source[which+2];
		if( attributes&0x10 ) sy += 256;
		if( attributes&0x80 ) sx -= 256;

		sx &= 0x1ff; if( sx>512-32 ) sx-=512;
		sy &= 0x1ff; if( sy>512-32 ) sy-=512;

		drawgfx(bitmap,gfx,
			tile_number,
			(attributes&0xf), /* color */
			0,0, /* no flip */
			sx,sy,
			&clip,TRANSPARENCY_PEN_TABLE,15);
	}
}

static void tdfever_draw_text( struct mame_bitmap *bitmap, int attributes, int dx, int dy, int base ){
	int bank = attributes>>4;
	int color = attributes&0xf;

	const struct rectangle *clip = &Machine->visible_area;
	const struct GfxElement *gfx = Machine->gfx[0];

	const unsigned char *source = &memory_region(REGION_CPU1)[base];

	int offs;

	int bank_offset = bank*256;

	for( offs = 0;offs <0x800; offs++ ){
		int tile_number = source[offs];
		int sy = dx+(offs % 32)*8;
		int sx = dy+(offs / 32)*8;

		if( source[offs] != 0x20 ){
			drawgfx(bitmap,gfx,
				tile_number + bank_offset,
				color,
				0,0, /* no flip */
				sx,sy,
				clip,TRANSPARENCY_PEN,15);
		}
	}
}

VIDEO_UPDATE( tdfever ){
	const unsigned char *ram = memory_region(REGION_CPU1);
//	shadows_visible = !shadows_visible;

	{
		unsigned char bg_attributes = ram[0xc880];
		int bg_scroll_y = -30 - ram[0xc800] - ((bg_attributes&0x01)?256:0);
		int bg_scroll_x = 141 - ram[0xc840] - ((bg_attributes&0x02)?256:0);
		tdfever_draw_background( bitmap, bg_scroll_x, bg_scroll_y );
	}

	{
		unsigned char sprite_attributes = ram[0xc900];
		int sprite_scroll_y =   65 + ram[0xc980] + ((sprite_attributes&0x80)?256:0);
		int sprite_scroll_x = -135 + ram[0xc9c0] + ((sprite_attributes&0x40)?256:0);
		tdfever_draw_sprites( bitmap, sprite_scroll_x, sprite_scroll_y );
	}

	{
		unsigned char text_attributes = ram[0xc8c0];
		tdfever_draw_text( bitmap, text_attributes, 0,0, 0xf800 );
	}
}

VIDEO_UPDATE( ftsoccer ){
	const unsigned char *ram = memory_region(REGION_CPU1);
//	shadows_visible = !shadows_visible;
	{
		unsigned char bg_attributes = ram[0xc880];
		int bg_scroll_y = - ram[0xc800] - ((bg_attributes&0x01)?256:0);
		int bg_scroll_x = 16 - ram[0xc840] - ((bg_attributes&0x02)?256:0);
		tdfever_draw_background( bitmap, bg_scroll_x, bg_scroll_y );
	}

	{
		unsigned char sprite_attributes = ram[0xc900];
		int sprite_scroll_y =  31 + ram[0xc980] + ((sprite_attributes&0x80)?256:0);
		int sprite_scroll_x = -40 + ram[0xc9c0] + ((sprite_attributes&0x40)?256:0);
		tdfever_draw_sprites( bitmap, sprite_scroll_x, sprite_scroll_y );
	}

	{
		unsigned char text_attributes = ram[0xc8c0];
		tdfever_draw_text( bitmap, text_attributes, 0,0, 0xf800 );
	}
}

static void gwar_draw_sprites_16x16( struct mame_bitmap *bitmap, int xscroll, int yscroll ){
	const struct GfxElement *gfx = Machine->gfx[2];
	const unsigned char *source = &memory_region(REGION_CPU1)[0xe800];

	const struct rectangle *clip = &Machine->visible_area;

	int which;
	for( which=0; which<(64)*4; which+=4 )
	{
		int attributes = source[which+3]; /* YBBX.BCCC */
		int tile_number = source[which+1];
		int sy = -xscroll + source[which];
		int sx =  yscroll - source[which+2];
		if( attributes&0x10 ) sy += 256;
		if( attributes&0x80 ) sx -= 256;

		if( attributes&0x08 ) tile_number += 256;
		if( attributes&0x20 ) tile_number += 512;
		if( attributes&0x40 ) tile_number += 1024;

		sy &= 0x1ff; if( sy>512-16 ) sy-=512;
		sx = (-sx)&0x1ff; if( sx>512-16 ) sx-=512;

		drawgfx(bitmap,gfx,
			tile_number,
			(attributes&7), /* color */
			0,0, /* flip */
			sx,sy,
			clip,TRANSPARENCY_PEN,15 );
	}
}

void gwar_draw_sprites_32x32( struct mame_bitmap *bitmap, int xscroll, int yscroll ){
	const struct GfxElement *gfx = Machine->gfx[3];
	const unsigned char *source = &memory_region(REGION_CPU1)[0xe000];

	const struct rectangle *clip = &Machine->visible_area;

	int which;
	for( which=0; which<(32)*4; which+=4 )
	{
		int attributes = source[which+3];
		int tile_number = source[which+1] + 8*(attributes&0x60);

		int sy = - xscroll + source[which];
		int sx = yscroll - source[which+2];
		if( attributes&0x10 ) sy += 256;
		if( attributes&0x80 ) sx -= 256;

		sy = (sy&0x1ff);
		sx = ((-sx)&0x1ff);
		if( sy>512-32 ) sy-=512;
		if( sx>512-32 ) sx-=512;

		drawgfx(bitmap,gfx,
			tile_number,
			(attributes&0xf), /* color */
			0,0, /* no flip */
			sx,sy,
			clip,TRANSPARENCY_PEN,15);
	}
}

VIDEO_UPDATE( gwar ){
	const unsigned char *ram = memory_region(REGION_CPU1);
	unsigned char bg_attributes, sp_attributes;

	{
		int bg_scroll_y, bg_scroll_x;

		if( gwar_sprite_placement==2 ) { /* Gwar alternate */
			bg_attributes = ram[0xf880];
			sp_attributes = ram[0xfa80];
			bg_scroll_y = - ram[0xf800] - ((bg_attributes&0x01)?256:0);
			bg_scroll_x  = 16 - ram[0xf840] - ((bg_attributes&0x02)?256:0);
		} else {
			bg_attributes = ram[0xc880];
			sp_attributes = ram[0xcac0];
			bg_scroll_y = - ram[0xc800] - ((bg_attributes&0x01)?256:0);
			bg_scroll_x  = 16 - ram[0xc840] - ((bg_attributes&0x02)?256:0);
		}
		tdfever_draw_background( bitmap, bg_scroll_x, bg_scroll_y );
	}

	{
		int sp16_y = ram[0xc900]+15;
		int sp16_x = ram[0xc940]+8;
		int sp32_y = ram[0xc980]+31;
		int sp32_x = ram[0xc9c0]+8;

		if( gwar_sprite_placement ) /* gwar */
		{
			if( bg_attributes&0x10 ) sp16_y += 256;
			if( bg_attributes&0x40 ) sp16_x += 256;
			if( bg_attributes&0x20 ) sp32_y += 256;
			if( bg_attributes&0x80 ) sp32_x += 256;
		}
		else{ /* psychos, bermudet, chopper1... */
			unsigned char spp_attributes = ram[0xca80];
			if( spp_attributes&0x04 ) sp16_y += 256;
			if( spp_attributes&0x08 ) sp32_y += 256;
			if( spp_attributes&0x10 ) sp16_x += 256;
			if( spp_attributes&0x20 ) sp32_x += 256;
		}
		if (sp_attributes & 0x20)
		{
			gwar_draw_sprites_16x16( bitmap, sp16_y, sp16_x );
			gwar_draw_sprites_32x32( bitmap, sp32_y, sp32_x );
		}
		else {
			gwar_draw_sprites_32x32( bitmap, sp32_y, sp32_x );
			gwar_draw_sprites_16x16( bitmap, sp16_y, sp16_x );
		}
	}

	{
		if( gwar_sprite_placement==2) { /* Gwar alternate */
			unsigned char text_attributes = ram[0xf8c0];
			tdfever_draw_text( bitmap, text_attributes,0,0, 0xc800 );
		}
		else {
			unsigned char text_attributes = ram[0xc8c0];
			tdfever_draw_text( bitmap, text_attributes,0,0, 0xf800 );
		}
	}
}

