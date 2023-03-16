#pragma once
#include "texture.h"

//
// Structure used by texture conversion functions.
//
typedef struct {
	COLOR32 *px;
	int width;
	int height;
	int fmt;
	int dither;
	float diffuseAmount;
	int ditherAlpha;
	int colorEntries;
	int useFixedPalette;
	COLOR *fixedPalette;
	int threshold;
	int balance;
	int colorBalance;
	int enhanceColors;
	TEXTURE *dest;
	void (*callback) (void *);
	void *callbackParam;
	char pnam[17];
} CREATEPARAMS;

//
// Counts the number of colors in an image (transparent counts as a color)
//
int countColors(COLOR32 *px, int nPx);

//
// Convert an image to a direct mode texture
//
int textureConvertDirect(CREATEPARAMS *params);

//
// Convert an image to a paletted texture
//
int textureConvertPalette(CREATEPARAMS *params);

//
// Convert an image to a translucent (a3i5 or a5i3) texture
//
int textureConvertTranslucent(CREATEPARAMS *params);

//progress markers for textureConvert4x4.
extern volatile int g_texCompressionProgress;
extern volatile int g_texCompressionProgressMax;
extern volatile int g_texCompressionFinished;

//
// Convert an image to a 4x4 compressed texture
//
int textureConvert4x4(CREATEPARAMS *params);

//
// Convert a texture given some input parameters.
//
int textureConvert(CREATEPARAMS *params);
