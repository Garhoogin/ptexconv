#pragma once

#include "color.h"

typedef struct RGB_ {
	unsigned char r;
	unsigned char g;
	unsigned char b;
	unsigned char a;
} RGB;

int lightnessCompare(const void *d1, const void *d2);

int createPaletteSlow(COLOR32 *img, int width, int height, COLOR32 *pal, unsigned int nColors);

void createPaletteExact(COLOR32 *img, int width, int height, COLOR32 *pal, unsigned int nColors);

int createPaletteSlowEx(COLOR32 *img, int width, int height, COLOR32 *pal, unsigned int nColors, int balance, int colorBalance, int enhanceColors, int sortOnlyUsed);

void createPalette_(COLOR32 *img, int width, int height, COLOR32 *pal, int nColors);

int closestpalette(RGB rgb, RGB *palette, int paletteSize, RGB *error);

void doDiffuse(int i, int width, int height, unsigned int *pixels, int errorRed, int errorGreen, int errorBlue, int errorAlpha, float amt);

COLOR32 averageColor(COLOR32 *cols, int nColors);

unsigned int getPaletteError(RGB *px, int nPx, RGB *pal, int paletteSize);

void createMultiplePalettes(COLOR32 *imgBits, int tilesX, int tilesY, COLOR32 *dest, int paletteBase, int nPalettes, int paletteSize, int nColsPerPalette, int paletteOffset, int *progress);

void convertRGBToYUV(int r, int g, int b, int *y, int *u, int *v);

int countColors(COLOR32 *px, int nPx);
