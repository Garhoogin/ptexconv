#pragma once

#include "color.h"
#include "palette.h"

#define SCREENFORMAT_TEXT 0
#define SCREENFORMAT_AFFINE 1
#define SCREENFORMAT_AFFINEEXT 2

#define SCREENCOLORMODE_16x16 0
#define SCREENCOLORMODE_256x1 1
#define SCREENCOLORMODE_256x16 2

#define TILE_FLIPX 1
#define TILE_FLIPY 2
#define TILE_FLIPXY (TILE_FLIPX|TILE_FLIPY)
#define TILE_FLIPNONE 0

//
// Structure used for character compression. Fill them out and pass them to
// BgPerformCharacterCompression.
//
typedef struct BgTile_ {
	COLOR32 px[64];               //RGBA colors: redundant, speed
	RxYiqColor pxYiq[64];         //YIQA colors
	unsigned char indices[64];    //color indices per pixel
	int masterTile;               //index of master tile for this tile 
	int nRepresents;              //number of tiles this tile represents
	int flipMode;                 //flip orientation of this tile
	int palette;                  //palette index of this tile
} BgTile;

/****************************************************************************\
*
*         /--offset---\  /-------length-------\
*        [ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ]  }  base
*        [ ][ ][ ][ ][ ][x][x][x][x][x][x][x][x][ ][ ][ ]  \
*        [ ][ ][ ][ ][ ][x][x][x][x][x][x][x][x][ ][ ][ ]  |  count
*        [ ][ ][ ][ ][ ][x][x][x][x][x][x][x][x][ ][ ][ ]  /
*        [ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ]
*
\****************************************************************************/
typedef struct BgPaletteRegion_ {
	int base;                          //Index of first palette to use
	int count;                         //Number of palettes to use
	int length;                        //Number of colors per palette
	int offset;                        //Index of first color to use in each palette
} BgPaletteRegion;

typedef struct BgCharacterSetting_ {
	int base;                         //character VRAM base offset
	int compress;                     //enables character compression
	int nMax;                         //max characters if compression enabled
	int alignment;                    //rounds up character count to a multiple of this
} BgCharacterSetting;

typedef struct BgGenerateParameters_ {
	//global
	RxBalanceSetting balance;         //Balance settings to use during conversion

	//palette
	int compressPalette;              //Use palette compression
	BgPaletteRegion paletteRegion;    //Palette region to use for conversion

	//character
	int nBits;                       //Bit depth of graphics data output
	RxDitherSetting dither;          //Dither configuration
	BgCharacterSetting characterSetting;
} BgGenerateParameters;



//
// Call this function after filling out the RGB color info in the tile array.
// The function will associate each tile with its best fitting palette, index
// the tile with that palette, and perform optional dithering.
//
void BgSetupTiles(BgTile *tiles, int nTiles, int nBits, COLOR32 *palette, int paletteSize, int nPalettes, int paletteBase, int paletteOffset, int dither, float diffuse, int balance, int colorBalance, int enhanceColors);

//
// Perform character compresion on the input array of tiles. After tiles are
// combined, the bit depth and palette settings are used to finalize the
// result in the tile array. progress must not be NULL, and ranges from 0-1000.
//
int BgPerformCharacterCompression(BgTile *tiles, int nTiles, int nBits, int nMaxChars, COLOR32 *palette, int paletteSize, int nPalettes,
	int paletteBase, int paletteOffset, int balance, int colorBalance, int *progress);

/****************************************************************************\
*
* Generates a BG with specified parameters.
*
* Parameters:
*   nclr                    Pointer to output palette data
*   ncgr                    Pointer to output character data
*   nscr                    Pointer to output screen data
*   px                      Image pixel data
*   width                   Image width
*   height                  Image height
*   params                  Conversion parameters
*   progress1               Progress 1
*   progress1Max            Progress 1 max
*   progress2               Progress 2
*   progress2Max            Progress 2 max
*
\****************************************************************************/
void BgGenerate(COLOR *pOutPalette, unsigned char **pOutChars, unsigned short **pOutScreen,
	int *outPalSize, int *outCharSize, int *outScreenSize,
	COLOR32 *px, int width, int height, BgGenerateParameters *params, 
	int *progress1, int *progress1Max, int *progress2, int *progress2Max);


void BgAssemble(COLOR32 *imgBits, int width, int height, int nBits, COLOR *pals, int nPalettes,
	unsigned char *chars, int nChars, unsigned short **pOutScreen, int *outScreenSize,
	int balance, int colorBalance, int enhanceColors);
