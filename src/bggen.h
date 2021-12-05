#pragma once

#include "color.h"

#define TILE_FLIPX 1
#define TILE_FLIPY 2
#define TILE_FLIPXY (TILE_FLIPX|TILE_FLIPY)
#define TILE_FLIPNONE 0

void bgGenerate(COLOR32 *imgBits, int width, int height, int nBits, int dither, float diffuse, 
				COLOR **pOutPalette, unsigned char **pOutChars, unsigned short **pOutScreen, 
				int *outPalSize, int *outCharSize, int *outScreenSize,
				int palette, int nPalettes, int bin, int tileBase, int mergeTiles,
				int paletteSize, int paletteOffsetm, int rowLimit, int nMaxChars,
				int *progress1, int *progress1Max, int *progress2, int *progress2Max);
