/**
 * video hardware for Namco System22
 *
 * TODO:
 *
 * Needs further reverse-engineering:
 * - some unknown display list opcodes
 * - some mysterious Point ROM primitives/codes
 * - locate flag bits to enable lighting effects/depth queuing
 *
 * Prop Cycle bugs:
 * - in Solitar stage, backdrop is not completely covered by polygons (probably should be solid blue)
 * - part of starting platform is drawn behind player even when they are logically in front
 * - some text layer rows are apparently misplaced, compared to real PCB
 *
 * Unsupported features (not used by Prop Cycle)
 * - window clipping and axis reversal (used in car games for rear view mirror)
 * - polygons can be flagged to appear in front or behind text layer
 *
 * Desirable optimizations:
 * - draw polygons from front to back to avoid expensive overdraw
 *
 * Missing special effects that will be added after driver is changed to use direct RBG color
 * - independent fader controls for sprite/polygon/text
 * - gouraud shading (using per-vertex intensity parameter)
 * - improve lighting effects (it's currently mocked up as all or nothing, using two palette banks)
 * - depth cueing (fog)
 * - translucency effects for text layer, sprites
 */

#include <assert.h>
#include "namcos22.h"
#include "namcos3d.h"
#include "matrix3d.h"
#include <math.h>

static int mbDumpScene; /* used for debugging */

static int mbSuperSystem22; /* used to conditionally support Super System22-specific features */

/* mWindowPri and mMasterBias reflect modal polygon rendering parameters */
static INT32 mWindowPri;
static INT32 mMasterBias;

#define CGRAM_SIZE 0x1e000
#define NUM_CG_CHARS ((CGRAM_SIZE*8)/(64*16)) /* 0x3c0 */

/* while processing the display list, a store of matrices are manipulated */
#define MAX_CAMERA 128
static struct Matrix
{
	double M[4][4];
} *mpMatrix;

static int mPtRomSize;
static const data8_t *mpPolyH;
static const data8_t *mpPolyM;
static const data8_t *mpPolyL;

static INT32
GetPolyData( INT32 addr )
{
	INT32 result;
	if( addr<0 || addr>=mPtRomSize )
	{
		return -1; /* HACK */
	}
	result = (mpPolyH[addr]<<16)|(mpPolyM[addr]<<8)|mpPolyL[addr];
	if( result&0x00800000 )
	{
		result |= 0xff000000; /* sign extend */
	}
	return result;
} /* GetPolyData */

/* text layer uses a set of 16x16x8bpp tiles defined in RAM */
static struct GfxLayout cg_layout =
{
	16,16,
	NUM_CG_CHARS,
	4,
	{ 0,1,2,3 },
#ifdef LSB_FIRST
	{ 4*6,4*7, 4*4,4*5, 4*2,4*3, 4*0,4*1,
	  4*14,4*15, 4*12,4*13, 4*10,4*11, 4*8,4*9 },
#else
	{ 4*0,4*1,4*2,4*3,4*4,4*5,4*6,4*7,
	  4*8,4*9,4*10,4*11,4*12,4*13,4*14,4*15 },
#endif
	{ 64*0,64*1,64*2,64*3,64*4,64*5,64*6,64*7,64*8,64*9,64*10,64*11,64*12,64*13,64*14,64*15 },
	64*16
}; /* cg_layout */

data32_t *namcos22_cgram;
data32_t *namcos22_textram;
data32_t *namcos22_polygonram;
data32_t *namcos22_gamma;

static int cgsomethingisdirty;
static unsigned char *cgdirty;
static unsigned char *dirtypal;
static struct tilemap *tilemap;

static data8_t
nthbyte( const data32_t *pSource, int offs )
{
	pSource += offs/4;
	return (pSource[0]<<((offs&3)*8))>>24;
}

static data16_t
nthword( const data32_t *pSource, int offs )
{
	pSource += offs/2;
	return (pSource[0]<<((offs&1)*16))>>16;
}

static void TextTilemapGetInfo( int tile_index )
{
	data16_t data = nthword( namcos22_textram,tile_index );
	/**
	 * xxxx------------ palette select
	 * ----xx---------- flip
	 * ------xxxxxxxxxx code
	 */
	SET_TILE_INFO( NAMCOS22_ALPHA_GFX,data&0x3ff,data>>12,TILE_FLIPYX((data>>10)&3) );
}

/**
 * mydrawgfxzoom is used to insert a zoomed 2d sprite into an already-rendered 3d scene.
 */
static void
mydrawgfxzoom(
	struct mame_bitmap *dest_bmp,const struct GfxElement *gfx,
	unsigned int code,unsigned int color,int flipx,int flipy,int sx,int sy,
	const struct rectangle *clip,int transparency,int transparent_color,
	int scalex, int scaley, INT32 zcoord )
{
	struct rectangle myclip;
	if (!scalex || !scaley) return;
	if(clip)
	{
		myclip.min_x = clip->min_x;
		myclip.max_x = clip->max_x;
		myclip.min_y = clip->min_y;
		myclip.max_y = clip->max_y;

		if (myclip.min_x < 0) myclip.min_x = 0;
		if (myclip.max_x >= dest_bmp->width) myclip.max_x = dest_bmp->width-1;
		if (myclip.min_y < 0) myclip.min_y = 0;
		if (myclip.max_y >= dest_bmp->height) myclip.max_y = dest_bmp->height-1;

		clip=&myclip;
	}
	if( gfx && gfx->colortable )
	{
		const pen_t *pal = &gfx->colortable[gfx->color_granularity * (color % gfx->total_colors)];
		UINT8 *source_base = gfx->gfxdata + (code % gfx->total_elements) * gfx->char_modulo;
		int sprite_screen_height = (scaley*gfx->height+0x8000)>>16;
		int sprite_screen_width = (scalex*gfx->width+0x8000)>>16;
		if (sprite_screen_width && sprite_screen_height)
		{
			int dx = (gfx->width<<16)/sprite_screen_width;
			int dy = (gfx->height<<16)/sprite_screen_height;
			int ex = sx+sprite_screen_width;
			int ey = sy+sprite_screen_height;
			int x_index_base;
			int y_index;
			if( flipx )
			{
				x_index_base = (sprite_screen_width-1)*dx;
				dx = -dx;
			}
			else
			{
				x_index_base = 0;
			}
			if( flipy )
			{
				y_index = (sprite_screen_height-1)*dy;
				dy = -dy;
			}
			else
			{
				y_index = 0;
			}
			if( clip )
			{
				if( sx < clip->min_x)
				{ /* clip left */
					int pixels = clip->min_x-sx;
					sx += pixels;
					x_index_base += pixels*dx;
				}
				if( sy < clip->min_y )
				{ /* clip top */
					int pixels = clip->min_y-sy;
					sy += pixels;
					y_index += pixels*dy;
				}
				if( ex > clip->max_x+1 )
				{ /* clip right */
					int pixels = ex-clip->max_x-1;
					ex -= pixels;
				}
				if( ey > clip->max_y+1 )
				{ /* clip bottom */
					int pixels = ey-clip->max_y-1;
					ey -= pixels;
				}
			}
			if( ex>sx )
			{ /* skip if inner loop doesn't draw anything */
				int y;
				for( y=sy; y<ey; y++ )
				{
					INT32 *pZBuf = namco_zbuffer + NAMCOS22_SCREEN_WIDTH*y;
					UINT8 *source = source_base + (y_index>>16) * gfx->line_modulo;
					UINT16 *dest = (UINT16 *)dest_bmp->line[y];
					int x, x_index = x_index_base;
					for( x=sx; x<ex; x++ )
					{
						if( zcoord<pZBuf[x] )
						{
							int c = source[x_index>>16];
							if( c != transparent_color )
							{
								dest[x] = pal[c];
								pZBuf[x] = zcoord;
							}
						}
						x_index += dx;
					}
					y_index += dy;
				}
			}
		}
	}
}

static void
DrawSprites( struct mame_bitmap *bitmap, const struct rectangle *cliprect )
{
	/*
		0x980000:	00060000 00010000 02ff0000 000007ff
                             ^^^^                       num sprites

		0x980010:	00200020 000002ff 000007ff 00000000
                             ^^^^                       delta xpos
                                      ^^^^              delta ypos

		0x980200:	000007ff 000007ff		delta xpos, delta ypos
		0x980208:	000007ff 000007ff
		0x980210:	000007ff 000007ff
		0x980218:	000007ff 000007ff
		0x980220:	000007ff 000007ff
		0x980228:	000007ff 000007ff
		0x980230:	000007ff 000007ff
		0x980238:	000007ff 000007ff

		0x980400:	?
		0x980600:	?
			0000	0000.0000.0000.0000
			8000	1000.0000.0000.0000
			8080	1000.0000.1000.0000
			8880	1000.1000.1000.0000
			8888	1000.1000.1000.1000
			a888	1010.1000.1000.1000
			a8a8	1010.1000.1010.1000
			aaa8	1010.1010.1010.1000
			aaaa	1010.1010.1010.1010
			eaaa	1110.1010.1010.1010
			eaea	1110.1010.1110.1010
			eeea	1110.1110.1110.1010
			eeee	1110.1110.1110.1110
			feee	1111.1110.1110.1110
			fefe	1111.1110.1111.1110
			fffe	1111.1111.1111.1110
			ffff	1111.1111.1111.1111

		0x980800:	0000 0001 0002 0003 ... 03ff (probably indirection for sprite list)

		eight words per sprite:
		0x984000:	010f 007b	xpos, ypos
		0x984004:	0020 0020	size x, size y
		0x984008:	00ff 0311	00ff, chr x;chr y;flip x;flip y
		0x98400c:	0001 0000	sprite code, ????
		...

		additional sorting/color data for sprite:
		0x9a0000:	C381 Z (sort)
		0x9a0004:	palette, C381 ZC (depth cueing?)
		...
	*/
	int i;
	int deltax, deltay;
	int color,flipx,flipy;
	data32_t xypos, size, attrs, code;
	const data32_t *pSource, *pPal;
	int numcols, numrows, row, col;
	int xpos, ypos, tile;
	int num_sprites;
	int zoomx, zoomy;
	int sizex, sizey;

	deltax = spriteram32[0x14/4]>>16;
	deltay = spriteram32[0x18/4]>>16;
	num_sprites = (spriteram32[0x04/4]>>16)&0x3ff; /* max 1024 sprites? */

	color = 0;
	flipx = 0;
	flipy = 0;
	pSource = &spriteram32[0x4000/4]+num_sprites*4;
	pPal = &spriteram32[0x20000/4]+num_sprites*2;
	for( i=0; i<num_sprites; i++ )
	{
		INT32 zcoord = pPal[0];
		color = pPal[1]>>16;

		xypos = pSource[0];
		xpos = (xypos>>16)-deltax;
		ypos = (xypos&0xffff)-deltay;

		size = pSource[1];

		sizex = size>>16;
		sizey = size&0xffff;
		zoomx = (1<<16)*sizex/0x20;
		zoomy = (1<<16)*sizey/0x20;

		attrs = pSource[2];
		flipy = attrs&0x8;
		numrows = attrs&0x7;
		if( numrows==0 ) numrows = 8;
		if( flipy )
		{
			ypos += sizey*(numrows-1);
			sizey = -sizey;
		}

		attrs>>=4;

		flipx = attrs&0x8;
		numcols = attrs&0x7;
		if( numcols==0 ) numcols = 8;
		if( flipx )
		{
			xpos += sizex*(numcols-1);
			sizex = -sizex;
		}

		code = pSource[3];
		tile = code>>16;

		for( row=0; row<numrows; row++ )
		{
			for( col=0; col<numcols; col++ )
			{
				mydrawgfxzoom( bitmap, Machine->gfx[0],
					tile,
					color,
					flipx, flipy,
					xpos+col*sizex/*-5*/, ypos+row*sizey/*+4*/,
					cliprect,
					TRANSPARENCY_PEN, 255,
					zoomx, zoomy,
					zcoord );
				tile++;
			}
		}
		pSource -= 4;
		pPal -= 2;
	}
} /* DrawSprites */

static void
UpdatePaletteS( void ) /* for Super System22 - apply gamma correction and preliminary fader support */
{
	int i,j;

	int red   = nthbyte( namcos22_gamma, 0x16 );
	int green = nthbyte( namcos22_gamma, 0x17 );
	int blue  = nthbyte( namcos22_gamma, 0x18 );
	int fade  = nthbyte( namcos22_gamma, 0x19 );
//	int flags = nthbyte( namcos22_gamma, 0x1a );

	tilemap_set_palette_offset( tilemap, nthbyte(namcos22_gamma,0x1b)*256 );

	for( i=0; i<NAMCOS22_PALETTE_SIZE/4; i++ )
	{
		if( dirtypal[i] )
		{
			for( j=0; j<4; j++ )
			{
				int which = i*4+j;
				int r = nthbyte(paletteram32,which+0x00000);
				int g = nthbyte(paletteram32,which+0x08000);
				int b = nthbyte(paletteram32,which+0x10000);

				if( fade )
				{ /**
				   * if flags&0x01 is set, fader affects polygon layer
				   * flags&0x02 and flags&0x04 are used to fade text/sprite layer
				   *
				   * for now, ignore flags and fade all palette entries
				   */
					r = (r*(0x100-fade)+red*fade)/256;
					g = (g*(0x100-fade)+green*fade)/256;
					b = (b*(0x100-fade)+blue*fade)/256;
				}

				/* map through gamma table (before or after fader?) */
				r = nthbyte( &namcos22_gamma[0x100/4], r );
				g = nthbyte( &namcos22_gamma[0x200/4], g );
				b = nthbyte( &namcos22_gamma[0x300/4], b );

				palette_set_color( which,r,g,b );
			}
			dirtypal[i] = 0;
		}
	}
} /* UpdatePaletteS */

static void
UpdatePalette( void ) /* for System22 - ignore gamma/fader effects for now */
{
	int i,j;

	tilemap_set_palette_offset( tilemap, 0x7f00 );

	for( i=0; i<NAMCOS22_PALETTE_SIZE/4; i++ )
	{
		if( dirtypal[i] )
		{
			for( j=0; j<4; j++ )
			{
				int which = i*4+j;
				int r = nthbyte(paletteram32,which+0x00000);
				int g = nthbyte(paletteram32,which+0x08000);
				int b = nthbyte(paletteram32,which+0x10000);
				palette_set_color( which,r,g,b );
			}
			dirtypal[i] = 0;
		}
	}
} /* UpdatePalette */

static void
DrawTextLayer( struct mame_bitmap *bitmap, const struct rectangle *cliprect )
{
	unsigned i;
	data32_t data;

	if( cgsomethingisdirty )
	{
		for( i=0; i<64*64; i+=2 )
		{
			data = namcos22_textram[i/2];
			if( cgdirty[(data>>16)&0x3ff] )
			{
				tilemap_mark_tile_dirty( tilemap,i );
			}
			if( cgdirty[data&0x3ff] )
			{
				tilemap_mark_tile_dirty( tilemap,i+1 );
			}
		}
		for( i=0; i<NUM_CG_CHARS; i++ )
		{
			if( cgdirty[i] )
			{
				decodechar( Machine->gfx[NAMCOS22_ALPHA_GFX],i,(UINT8 *)namcos22_cgram,&cg_layout );
				cgdirty[i] = 0;
			}
		}
		cgsomethingisdirty = 0;
	}
	tilemap_draw( bitmap, cliprect, tilemap, 0, 0 );
} /* DrawTextLayer */

/*********************************************************************************************/

static void
ApplyRotation( const INT32 *pSource, double M[4][4] )
{
	struct RotParam param;
	param.thx_sin = (INT16)(pSource[0])/(double)0x7fff;
	param.thx_cos = (INT16)(pSource[1])/(double)0x7fff;
	param.thy_sin = (INT16)(pSource[2])/(double)0x7fff;
	param.thy_cos = (INT16)(pSource[3])/(double)0x7fff;
	param.thz_sin = (INT16)(pSource[4])/(double)0x7fff;
	param.thz_cos = (INT16)(pSource[5])/(double)0x7fff;
	param.rolt = pSource[6];
	namcos3d_Rotate( M, &param );
} /* ApplyRotation */

static const INT32 *
LoadMatrix( const INT32 *pSource, double M[4][4] )
{
	double temp[4][4];
	int r,c;

	matrix3d_Identity( M );
	for(;;)
	{
		switch( *pSource++ )
		{
		case 0x0000: /* translate */
			matrix3d_Translate( M, pSource[0], pSource[1], pSource[2] );
			pSource += 3;
			break;

		case 0x0001: /* rotate */
			ApplyRotation( pSource, M );
			pSource += 7;
			break;

		case 0x0003: /* unknown */
			pSource += 2;
			break;

		case 0x0006: /* custom */
			for( c=0; c<3; c++ )
			{
				for( r=0; r<3; r++ )
				{
					temp[r][c] = *pSource++ / (double)0x7fff;
				}
				temp[c][3] = temp[3][c] = 0;
			}
			temp[3][3] = 1;
			matrix3d_Multiply( M, temp );
			break;

		case 0xffffffff:
			return pSource;

		default:
			logerror( "bad LoadMatrix!!\n" );
			exit(1);
		}
	}
} /* LoadMatrix */



/**
 * +0x00 0	always 0xf?
 * +0x04 1	sin(x) world-view matrix
 * +0x08 2	cos(x)
 * +0x0c 3	sin(y)
 * +0x10 4	cos(y)
 * +0x14 5	sin(z)
 * +0x18 6	cos(z)
 * +0x1c 7	ROLT? always 0x0002?
 * +0x20 8	light power
 * +0x24 9	light ambient
 * +0x28 10	light vector(x)
 * +0x2c 11	light vector(y)
 * +0x30 12	light vector(z)
 * +0x34 13	always 0x0002?
 * +0x38 14	field of view angle (fovx) in degrees
 * +0x3c 15	viewport width
 * +0x40 16	viewport height
 * +0x44 17
 * +0x48 18
 * +0x4c 19
 * +0x50 20	priority (top 7 > .. > 0 behind)
 * +0x54 21	viewport center x
 * +0x58 22	viewport center y
 * +0x5c 23	sin(x) ?
 * +0x60 24	cos(x)
 * +0x64 25	sin(y)
 * +0x68 26	cos(y)
 * +0x6c 27	sin(z)
 * +0x70 28	cos(z)
 * +0x74 29	flags (axis flipping?) 0004, 0003
 * +0x78 30	6630, 0001
 * +0x7c 31	7f02
 */
static double mWindowZoom;
static namcos22_lighting mLighting;
static struct Matrix mWindowTransform;

static int mPrevWindow;

static void
ResetWindow( void )
{
	mPrevWindow = -1;
}

static void
SetupWindow( const INT32 *pWindow, int which )
{
	if( which!=mPrevWindow )
	{ /* only recompute if we're dealing with a new window */
		mPrevWindow = which;
		pWindow += which*0x80/4;

		/*	INT16 vx0  = pWindow[0x54/4];
			INT16 vy0  = pWindow[0x5c/4];
			SetClip(
				float(320 + vx0 - vw),
				float(240 - vy0 - vh),
				float(vw * 2),
				float(vh * 2) );
		*/

		{
	//		double pi = atan (1.0) * 4.0;
	//		double vh = pWindow[0x40/4];
			double vw = pWindow[0x3c/4];
	        double fov = pWindow[0x38/4];
	        if( mbSuperSystem22 )
	        {
				fov /= 32.0; /* Super System22 uses fixed point for field of view angle */
			}
			fov = fov*3.141592654/180.0; /* degrees to radians */
			mWindowZoom = vw/tan(fov/2.0);
		}

		mWindowPri = pWindow[0x50/4]&0x7;

		matrix3d_Identity( mWindowTransform.M );
		ApplyRotation( &pWindow[1], mWindowTransform.M );

		mLighting.power   = (INT16)(pWindow[0x20/4])/(double)0xff;
		mLighting.ambient = (INT16)(pWindow[0x24/4])/(double)0xff;
		mLighting.x       = (INT16)(pWindow[0x28/4])/(double)0x7fff;
		mLighting.y       = (INT16)(pWindow[0x2c/4])/(double)0x7fff;
		mLighting.z       = (INT16)(pWindow[0x30/4])/(double)0x7fff;
	}
} /* SetupWindow */

static void
BlitQuadHelper(
		struct mame_bitmap *pBitmap,
		unsigned color,
		unsigned addr,
		double m[4][4],
		INT32 zcode,
		INT32 flags )
{
	double zmin=0, zmax=0, zrep=0;
	double kScale = 0.5;
	struct VerTex v[5];
	int i;

	color &= 0x7f00;

	for( i=0; i<4; i++ )
	{
		struct VerTex *pVerTex = &v[i];
		double lx = kScale * GetPolyData(  8+i*3+addr );
		double ly = kScale * GetPolyData(  9+i*3+addr );
		double lz = kScale * GetPolyData( 10+i*3+addr );
		pVerTex->x = m[0][0]*lx + m[1][0]*ly + m[2][0]*lz + m[3][0];
		pVerTex->y = m[0][1]*lx + m[1][1]*ly + m[2][1]*lz + m[3][1];
		pVerTex->z = m[0][2]*lx + m[1][2]*ly + m[2][2]*lz + m[3][2];
		pVerTex->u = GetPolyData( 0+2*i+addr )&0xffff;
		pVerTex->v = GetPolyData( 1+2*i+addr )&0xffff;
		pVerTex->i = (GetPolyData(i+addr)>>16)&0xff;
		if( i==0 || pVerTex->z > zmax ) zmax = pVerTex->z;
		if( i==0 || pVerTex->z < zmin ) zmin = pVerTex->z;
	}

	/**
	 * The method for computing represenative z value may vary per polygon.
	 * Possible methods include:
	 * - minimum value: zmin
	 * - maximum value: zmax
	 * - average value: (zmin+zmax)/2
	 * - average of all four z coordinates
	 */
	zrep = (zmin+zmax)/2.0; /* for now just always use the simpler average */

	/**
	 * hardware supports two types priority modes:
	 *
	 * direct: use explicit zcode, ignoring the polygon's z coordinate
	 * ---xxxxx xxxxxxxx xxxxxxxx
	 *
	 * relative: representative z + shift values
	 * ---xxx-- -------- -------- shift absolute priority
	 * ------xx xxxxxxxx xxxxxxxx shift z-representative value
	 */

	{ /* for now, assume all polygons use relative priority */
		INT32 dw = (zcode&0x1c0000)>>18; /* window (master)priority bias */
		INT32 dz = (zcode&0x03ffff); /* bias for representative z coordinate */

		if( dw&4 )
		{
			dw |= ~0x7; /* sign extend */
		}
		dw += mWindowPri;
		if( dw<0 ) dw = 0; else if( dw>7 ) dw = 7; /* cap it at min/max */
		dw <<= 21;

		if( dz&0x020000 )
		{
			dz |= ~0x03ffff; /* sign extend */
		}
		dz += (INT32)zrep;
		dz += mMasterBias;
		if( dz<0 ) dz = 0; else if( dz>0x1fffff ) dz = 0x1fffff; /* cap it at min/max */

		/**
		 * xxx----- -------- -------- master-priority
		 * ---xxxxx xxxxxxxx xxxxxxxx sub-priority
		 */
		zcode = dw|dz;
	}

	BlitTri( pBitmap, &v[0], color, mWindowZoom, zcode, flags, &mLighting ); /* 0,1,2 */
	v[4] = v[0]; /* wrap */
	BlitTri( pBitmap, &v[2], color, mWindowZoom, zcode, flags, &mLighting ); /* 2,3,0 */
} /* BlitQuadHelper */


static void
BlitQuads( struct mame_bitmap *pBitmap, INT32 addr, double m[4][4], INT32 base )
{
	INT32 start = addr;
	INT32 size = GetPolyData(addr++);
	INT32 finish = addr + (size&0xff);
	INT32 flags;
	INT32 color;
	INT32 bias;

	while( addr<finish )
	{
		size = GetPolyData(addr++);
		size &= 0xff;
		switch( size )
		{
		case 0x17:
			flags = GetPolyData(addr+1);
			color = GetPolyData(addr+2);
			BlitQuadHelper( pBitmap,color,addr+3,m,0,flags );
			break;

		case 0x18:
			/**
			 * word 0: ?
			 *		0b3480 ?
			 *		800000 ?
			 *		000040 ?
			 *
			 * word 1: (flags)
			 *		1042 (always set?)
			 *		0200 lighting enable?
			 *		0100 ?
			 *		0020 one-sided
			 *		0001 high priority?
			 *
			 *		1163 // sky
			 *		1262 // score (front)
			 *		1242 // score (hinge)
			 *		1063 // n/a
			 *		1243 // various (2-sided?)
			 *		1263 // everything else (1-sided?)
			 *
			 * word 2: color
			 *			-------- xxxxxxxx unused?
			 *			-xxxxxxx -------- palette select
			 *			x------- -------- ?
			 *
			 * word 3: depth bias
			 */
			flags = GetPolyData(addr+1);
			color = GetPolyData(addr+2);
			bias  = GetPolyData(addr+3);

			BlitQuadHelper( pBitmap,color,addr+4,m,bias,flags );
			break;

		case 0x10:
		case 0x0d:
			/* unknown! */
			break;

		default:
			logerror( "unexpected point data %08x at %08x.%08x\n", size, base, start );
			break;
		}

		if( mbDumpScene )
		{
			int q;
			for( q=0; q<size; q++ )
			{
				logerror( " %06x", GetPolyData(addr+q)&0xffffff );
			}
			logerror( "\n" );
		}

		addr += size;
	}
} /* BlitQuads */

static void
BlitPolyObject( struct mame_bitmap *pBitmap, int code, double m[4][4] )
{
	unsigned addr1 = GetPolyData(code+0x45);
	if( mbDumpScene )
	{
		logerror( "        flags  color  bias   u1     v1     u2     v2     u3     v3     u4     v4     x1     y1     z1     x2     y2     z2     x3     y3     z3     x4     y4     z4\n" );
	}
	for(;;)
	{
		INT32 addr2 = GetPolyData(addr1++);
		if( addr2<0 )
		{
			break;
		}
		BlitQuads( pBitmap, addr2, m, code );
	}
} /* BlitPolyObject */

/**
 * c00000:
 *			00000000 00000000
 *			00000004 00000000
 *			00000001				scene bank select
 *			00000001
 *			00000032
 *			00000280
 *
 * c00800..c0ffff: code for DSP, written by main CPU
 *
 * c10000..c1007f
 *		0000000f (always?)
 *
 *			000019b1 00007d64 // rolx (master camera)
 *			00005ece 000055fe // roly (master camera)
 *			000001dd 00007ffc // rolz (master camera)
 *			00000002 (rolt)
 *
 *		000000c8 // light power
 *		00000014 // light ambient
 *		00000000 00005a82 ffffa57e // light vector
 *
 *		00000002 (always?)
 *
 *		00000780 // field of view
 *		00000140 000000f0
 *
 *		00000000 00000000 00000000
 *
 *		00000007 // priority
 *		00000000 00000000
 *		00000000 00007fff // rolx?
 *		00000000 00007fff // roly?
 *		00000000 00007fff // rolz?
 *		00010000 // flags?
 *		00000000 00000000 // unknown
 *
 * c10080..c100ff
 * c10100..c1017f
 * c10180..c101ff
 * c10200..c1027f
 * c10280..c102ff
 * c10300..c1037f
 * c10380..c103ff
 *
 * c10400..c17fff: scene data
 *
 * c18000..c183ff: bank#2 window attr
 * c18400..c1ffff: bank#2, scene data
 */
static void
DrawPolygons( struct mame_bitmap *bitmap )
{
	static int iShowOnly;
	int iObject = 0;
	int bShowOnly = 0;

	double M[4][4];
	INT32 code,mode;
	INT32 xpos,ypos,zpos;
	INT32 window = 0;
	INT32 i, iSource0, iSource1, iTarget;
	INT32 param;
	const INT32 *pSource, *pDebug;
	const INT32 *pWindow;
	namcos22_polygonram[0x4/4] = 0; /* clear busy flag */

	ResetWindow();

	/*************************************************************************/
	#ifdef MAME_DEBUG
	mbDumpScene = keyboard_pressed( KEYCODE_U );

	bShowOnly = keyboard_pressed(KEYCODE_B);

	if( bShowOnly )
	{
		drawgfx( bitmap, Machine->uifont, "0123456789abcdef"[(iShowOnly>>4)&0xf],
			0,0,0,0,0,NULL,TRANSPARENCY_NONE,0 );
		drawgfx( bitmap, Machine->uifont, "0123456789abcdef"[(iShowOnly>>0)&0xf],
			0,0,0,12,0,NULL,TRANSPARENCY_NONE,0 );
	}
	if( keyboard_pressed( KEYCODE_M ) )
	{
		while( keyboard_pressed( KEYCODE_M ) ){}
		iShowOnly = (iShowOnly+1)&0xff;
	}
	if( keyboard_pressed( KEYCODE_N ) )
	{
		while( keyboard_pressed( KEYCODE_N ) ){}
		iShowOnly = (iShowOnly-1)&0xff;
	}
	#endif /* MAME_DEBUG */
	/*************************************************************************/

	if( namcos22_polygonram[0x10/4] )
	{ /* bank#0 */
		pWindow = (INT32 *)&namcos22_polygonram[0x10000/4];
		pSource = (INT32 *)&namcos22_polygonram[0x10400/4];
	}
	else
	{ /* bank#1 */
		pWindow = (INT32 *)&namcos22_polygonram[0x18000/4];
		pSource = (INT32 *)&namcos22_polygonram[0x18400/4];
	}
	if( !pSource[0] ) return;

	param = 0;
	mode = 0x8000;

	if( mbDumpScene )
	{
		for( i=0; i<8*32; i++ )
		{
			if( (i&0x1f)==0 )
			{
				logerror( "\n%d:\t",i/32 );
			}
			logerror( " %08x", pWindow[i] );
		}
		logerror( "\n" );
	}
	pDebug = pSource;
	for(;;)
	{
		if( mbDumpScene )
		{
			logerror( "\n" );
			while( pDebug<pSource )
			{
				logerror( "%08x ", *pDebug++ );
			}
		}
		code = *pSource++;
		if( ((UINT32)code) < 0x8000 )
		{
			SetupWindow( pWindow,window );
			xpos = *pSource++;
			ypos = *pSource++;
			zpos = *pSource++;
			matrix3d_Identity( M );
			if( mode != 0x8000 )
			{
				ApplyRotation( pSource, M );
				pSource += 7;
			}
			matrix3d_Translate( M, xpos,ypos,zpos );
			if( mode != 0x8001 )
			{
				matrix3d_Multiply( M, mWindowTransform.M );
			}
			if( iObject == iShowOnly || !bShowOnly )
			{
				if( mbDumpScene )
				{
					logerror( "\n#%02x:\n", iObject );
				}
				BlitPolyObject( bitmap, code, M );
			}
			iObject++;
		}
		else
		{
			switch( code )
			{
			case 0x8000: /* Prop Cycle: environment */
			case 0x8001: /* Prop Cycle: score digits */
			case 0x8002: /* Prop Cycle: for large decorations, starting platform */
				mode = code;
				window = *pSource++;
				break;

			case 0x8004: /* direct polygon (not used in Prop Cycle) */
				pSource += 4+4*6;
				/**
				 * bias,color,flags,texpage
				 *
				 * vertex#0: u, v, x, y, z, intensity
				 * vertex#1: u, v, x, y, z, intensity
				 * vertex#2: u, v, x, y, z, intensity
				 * vertex#3: u, v, x, y, z, intensity
				 */
				break;

			case 0x8008: /* load transformation matrix */
				i = *pSource++;
				assert( i<0x80 );
				pSource = LoadMatrix( pSource,mpMatrix[i].M );
				break;

			case 0x8009: /* matrix composition */
				iSource0 = pSource[0];
				iSource1 = pSource[1];
				iTarget  = pSource[2];
				assert( iSource0<0x80 && iSource1<0x80 && iTarget<0x80 );
				memcpy( M, mpMatrix[iSource0].M, sizeof(M) );
				matrix3d_Multiply( M, mpMatrix[iSource1].M );
				memcpy( mpMatrix[iTarget].M, M, sizeof(M) );
				pSource += 3;
				break;

			case 0x800a: /* Prop Cycle: flying bicycle, birds */
				SetupWindow( pWindow,window );
				code = *pSource++;
				i = *pSource++;
				assert( i<0x80 );
				memcpy( M, &mpMatrix[i], sizeof(M) );
				xpos = *pSource++;
				ypos = *pSource++;
				zpos = *pSource++;
				matrix3d_Translate( M, xpos,ypos,zpos );
				matrix3d_Multiply( M, mWindowTransform.M );
				if( iObject == iShowOnly || !bShowOnly )
				{
					if( mbDumpScene )
					{
						logerror( "#%02x:\n", iObject );
					}
					BlitPolyObject( bitmap, code, M );
				}
				iObject++;
				break;

			case 0x8010: /* Prop Cycle: preceeds balloons */
				mMasterBias = 0;
				while( pSource[0] != (INT32)0xffffffff )
				{ /**
				   * This opcode is used to set global attributes affecting whole objects.
				   * It is followed by zero or more (attribute,value) pairs.
				   *
				   * Prop Cycle uses attribute 3 frequently, and ocassionally attribute 2.
				   *
				   * Often 0x8010 is immediately followed by 0xffffffff
				   * This is used to restore default attributes.
				   */
					if( pSource[0]==3 )
					{
						mMasterBias = pSource[1]; /* signed depth bias for whole objects */
					}
					else
					{
						logerror( "0x8010: unknown attr[%d] = %d\n", pSource[0],pSource[1] );
					}
					pSource+=2;
				}
				pSource++; /* skip final 0xffffffff */
				break;

			case 0x8017: /* Rave Racer */
				pSource += 4; /* ??? */
				break;

			case (INT32)0xffffffff:
			case 0xffff:
				if( mbDumpScene )
				{
					int ns_i;
					logerror( "[eof]\n" );
					for( ns_i=0; ns_i<32; ns_i++ )
					{
						logerror( " %08x", *pSource++ );
					}
					while( keyboard_pressed( KEYCODE_U ) ){}
					logerror( "\n\n" );
				}
				return;
			default:
				logerror( "unknown opcode: %08x\n", code );
				return;
			}
		}
	}
} /* DrawPolygons */

/*********************************************************************************************/

READ32_HANDLER( namcos22_cgram_r )
{
	return namcos22_cgram[offset];
}

WRITE32_HANDLER( namcos22_cgram_w )
{
	COMBINE_DATA( &namcos22_cgram[offset] );
	cgdirty[offset/32] = 1;
	cgsomethingisdirty = 1;
}

READ32_HANDLER( namcos22_gamma_r )
{
	return namcos22_gamma[offset];
}

WRITE32_HANDLER( namcos22_gamma_w )
{
	data32_t old = namcos22_gamma[offset];
	COMBINE_DATA( &namcos22_gamma[offset] );
	if( old!=namcos22_gamma[offset] )
	{
		memset( dirtypal, 1, NAMCOS22_PALETTE_SIZE/4 );
	}
	/**
	 * 824000: ffffff00 00ffffff 0000007f 00000000
	 * 824010: 0000ff00 0f00RRGG BBII017f 00010007
	 *                      ^^                      red
	 *                        ^^                    green
	 *                           ^^                 blue
	 *                             ^^               fade (zero for none, 0xff for max)
	 *                               ^^             flags; fader targer
	 *                                                     1: affects polygon layer
	 *                                                     2: affects text(?)
	 *                                                     4: affects sprites(?)
	 *                                 ^^           tilemap palette base
	 *
	 * 824020: 00000001 00000000 00000000 00000000
	 *
	 * 824100: 00 05 0a 0f 13 17 1a 1e ... (red)
	 * 824200: 00 05 0a 0f 13 17 1a 1e ... (green)
	 * 824300: 00 05 0a 0f 13 17 1a 1e ... (blue)
	 */
}

READ32_HANDLER( namcos22_paletteram_r )
{
	return paletteram32[offset];
}

WRITE32_HANDLER( namcos22_paletteram_w )
{
	COMBINE_DATA( &paletteram32[offset] );
	dirtypal[offset&(0x7fff/4)] = 1;
}

READ32_HANDLER( namcos22_textram_r )
{
	return namcos22_textram[offset];
}

WRITE32_HANDLER( namcos22_textram_w )
{
	COMBINE_DATA( &namcos22_textram[offset] );
	tilemap_mark_tile_dirty( tilemap, offset*2 );
	tilemap_mark_tile_dirty( tilemap, offset*2+1 );
}


VIDEO_START( namcos22s )
{
	mpMatrix = auto_malloc(sizeof(struct Matrix)*MAX_CAMERA);
	if( mpMatrix )
	{
		if( namcos3d_Init(
			NAMCOS22_SCREEN_WIDTH,
			NAMCOS22_SCREEN_HEIGHT,
			memory_region(REGION_GFX3),	/* tilemap */
			memory_region(REGION_GFX2)	/* texture */
		) == 0 )
		{
			struct GfxElement *pGfx = decodegfx( (UINT8 *)namcos22_cgram,&cg_layout );
			if( pGfx )
			{
				Machine->gfx[NAMCOS22_ALPHA_GFX] = pGfx;
				pGfx->colortable = Machine->remapped_colortable;
				pGfx->total_colors = NAMCOS22_PALETTE_SIZE/16;
				tilemap = tilemap_create( TextTilemapGetInfo,tilemap_scan_rows,TILEMAP_TRANSPARENT,16,16,64,64 );
				if( tilemap )
				{
					tilemap_set_transparent_pen( tilemap, 0xf );
					dirtypal = auto_malloc(NAMCOS22_PALETTE_SIZE/4);
					if( dirtypal )
					{
						cgdirty = auto_malloc( 0x400 );
						if( cgdirty )
						{
							mPtRomSize = memory_region_length(REGION_GFX4)/3;
							mpPolyL = memory_region(REGION_GFX4);
							mpPolyM = mpPolyL + mPtRomSize;
							mpPolyH = mpPolyM + mPtRomSize;
							return 0; /* no error */
						}
					}
				}
			}
		}
	}
	return -1; /* error */
}

VIDEO_UPDATE( namcos22s )
{
	mbSuperSystem22 = 1;
	UpdatePaletteS();
	fillbitmap( bitmap, get_black_pen(), cliprect );
	namcos3d_Start( bitmap );
	DrawPolygons( bitmap );
	DrawSprites( bitmap, cliprect );
	DrawTextLayer( bitmap, cliprect );
}

VIDEO_UPDATE( namcos22 )
{
	mbSuperSystem22 = 0;
	UpdatePalette();
	fillbitmap( bitmap, get_black_pen(), cliprect );
	namcos3d_Start( bitmap );
	DrawPolygons( bitmap );
	DrawTextLayer( bitmap, cliprect );
}
