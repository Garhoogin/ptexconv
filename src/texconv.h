#pragma once

#include "texture.h"

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
	TEXTURE *dest;
	void (*callback) (void *);
	void *callbackParam;
	char pnam[17];
} CREATEPARAMS;

int countColors(COLOR32 *px, int nPx);

int convertDirect(CREATEPARAMS *params);

int convertPalette(CREATEPARAMS *params);

int convertTranslucent(CREATEPARAMS *params);

//progress markers for convert4x4.
extern volatile int _globColors;
extern volatile int _globFinal;
extern volatile int _globFinished;

int convert4x4(CREATEPARAMS *params);

//to convert a texture directly.
int textureConvert(CREATEPARAMS *lpParam);
