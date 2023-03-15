#pragma once

#include "color.h"

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
// performCharacterCompression.
//
typedef struct BGTILE_ {
	uint8_t indices[64];
	COLOR32 px[64]; //redundant, speed
	int pxYiq[64][4];
	int masterTile;
	int nRepresents;
	int flipMode;
	int palette;
} BGTILE;

//
// Call this function after filling out the RGB color info in the tile array.
// The function will associate each tile with its best fitting palette, index
// the tile with that palette, and perform optional dithering.
//
void setupBgTiles(BGTILE *tiles, int nTiles, int nBits, COLOR32 *palette, int paletteSize, int nPalettes, int paletteBase, int paletteOffset, int dither, float diffuse);

//
// Same functionality as setupBgTiles, with the added ability to specify
// specific color balance settings.
//
void setupBgTilesEx(BGTILE *tiles, int nTiles, int nBits, COLOR32 *palette, int paletteSize, int nPalettes, int paletteBase, int paletteOffset, int dither, float diffuse, int balance, int colorBalance, int enhanceColors);

//
// Perform character compresion on the input array of tiles. After tiles are
// combined, the bit depth and palette settings are used to finalize the
// result in the tile array. progress must not be NULL, and ranges from 0-1000.
//
int performCharacterCompression(BGTILE *tiles, int nTiles, int nBits, int nMaxChars, COLOR32 *palette, int paletteSize, int nPalettes,
	int paletteBase, int paletteOffset, int balance, int colorBalance, int *progress);

void bgGenerate(COLOR32 *imgBits, int width, int height, int nBits, int dither, float diffuse, 
				COLOR **pOutPalette, unsigned char **pOutChars, unsigned short **pOutScreen, 
				int *outPalSize, int *outCharSize, int *outScreenSize,
				int palette, int nPalettes, int bin, int tileBase, int mergeTiles,
				int paletteSize, int paletteOffsetm, int rowLimit, int nMaxChars,
				int balance, int colorBalance, int enhanceColors,
				int *progress1, int *progress1Max, int *progress2, int *progress2Max);
