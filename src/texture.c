#include <stdlib.h>
#include <string.h>

#include "texture.h"

int ilog2(int x);

extern int ImgCountColors(const COLOR32 *px, unsigned int nPx);

int TxRoundTextureSize(int dimension) {
	if (dimension < 8) return 8; //min
	
	//round up
	dimension = (dimension << 1) - 1;
	dimension = 1 << ilog2(dimension);
	return dimension;
}

int TxGetTexelSize(int width, int height, int texImageParam) {
	int nPx = width * height;
	int bits[] = { 0, 8, 2, 4, 8, 2, 8, 16 };
	int b = bits[FORMAT(texImageParam)];
	return (nPx * b) >> 3;
}

int TxGetIndexVramSize(TEXELS *texels) {
	int texImageParam = texels->texImageParam;
	int format = FORMAT(texImageParam);
	int hasIndex = format == CT_4x4;

	int texelSize = TxGetTexelSize(TEXW(texImageParam), TEXH(texImageParam), texImageParam);
	int indexSize = hasIndex ? (texelSize / 2) : 0;
	return indexSize;
}

int TxGetTextureVramSize(TEXELS *texels) {
	int texImageParam = texels->texImageParam;
	int w = TEXW(texImageParam);
	int h = TEXH(texImageParam);
	int fmt = FORMAT(texImageParam);

	int bpps[] = { 0, 8, 2, 4, 8, 3, 8, 16 };
	return bpps[fmt] * w * h / 8;
}

int TxGetTexPlttVramSize(PALETTE *palette) {
	return palette->nColors * sizeof(COLOR);
}

const char *TxNameFromTexFormat(int fmt) {
	const char *fmts[] = { "", "a3i5", "palette4", "palette16", "palette256", "tex4x4", "a5i3", "direct" };
	return fmts[fmt];
}

static COLOR32 TxiBlend(COLOR32 c1, COLOR32 c2, int factor) {
	unsigned int r = ((c1 >> 0) & 0xFF) * (8 - factor) + ((c2 >> 0) & 0xFF) * factor;
	unsigned int g = ((c1 >> 8) & 0xFF) * (8 - factor) + ((c2 >> 8) & 0xFF) * factor;
	unsigned int b = ((c1 >> 16) & 0xFF) * (8 - factor) + ((c2 >> 16) & 0xFF) * factor;
	r = (r + 4) / 8;
	g = (g + 4) / 8;
	b = (b + 4) / 8;
	return ColorRoundToDS18(r | (g << 8) | (b << 16));
}

static COLOR32 TxiSamplePltt(const COLOR *pltt, unsigned int nPltt, unsigned int i) {
	if (i >= nPltt) return 0;
	return ColorConvertFromDS(pltt[i]);
}

static COLOR32 TxiSampleDirect(const unsigned char *txel, const uint16_t *pidx, unsigned int texW, int c0xp, unsigned int x, unsigned int y, const COLOR *pltt, unsigned int nPltt) {
	(void) pidx;
	(void) c0xp;
	(void) pltt;
	(void) nPltt;

	unsigned int iPx = x + y * texW;
	COLOR colSrc = ((COLOR *) txel)[iPx];

	COLOR32 c = ColorConvertFromDS(colSrc);
	if (colSrc & 0x8000) c |= 0xFF000000;
	return c;
}

static COLOR32 TxiSamplePltt4(const unsigned char *txel, const uint16_t *pidx, unsigned int texW, int c0xp, unsigned int x, unsigned int y, const COLOR *pltt, unsigned int nPltt) {
	(void) pidx;

	unsigned int iPx = x + y * texW;
	unsigned int index = (txel[iPx >> 2] >> ((iPx & 3) * 2)) & 0x3;
	if (index == 0 && c0xp) return 0;

	return TxiSamplePltt(pltt, nPltt, index) | 0xFF000000;
}

static COLOR32 TxiSamplePltt16(const unsigned char *txel, const uint16_t *pidx, unsigned int texW, int c0xp, unsigned int x, unsigned int y, const COLOR *pltt, unsigned int nPltt) {
	(void) pidx;

	unsigned int iPx = x + y * texW;
	unsigned int index = (txel[iPx >> 1] >> ((iPx & 1) * 4)) & 0xF;
	if (index == 0 && c0xp) return 0;

	return TxiSamplePltt(pltt, nPltt, index) | 0xFF000000;
}

static COLOR32 TxiSamplePltt256(const unsigned char *txel, const uint16_t *pidx, unsigned int texW, int c0xp, unsigned int x, unsigned int y, const COLOR *pltt, unsigned int nPltt) {
	(void) pidx;

	unsigned int iPx = x + y * texW;
	unsigned int index = txel[iPx];
	if (index == 0 && c0xp) return 0;

	return TxiSamplePltt(pltt, nPltt, index) | 0xFF000000;
}

static COLOR32 TxiSampleA3I5(const unsigned char *txel, const uint16_t *pidx, unsigned int texW, int c0xp, unsigned int x, unsigned int y, const COLOR *pltt, unsigned int nPltt) {
	(void) c0xp;
	(void) pidx;

	uint8_t d = txel[x + y * texW];
	unsigned int index = (d & 0x1F) >> 0;
	unsigned int alpha = (d & 0xE0) >> 5;

	alpha = (alpha << 2) | (alpha >> 1);  // 3-bit -> 5-bit alpha
	alpha = (alpha * 510 + 31) / 62;      // scale to 8-bit

	return TxiSamplePltt(pltt, nPltt, index) | (alpha << 24);
}

static COLOR32 TxiSampleA5I3(const unsigned char *txel, const uint16_t *pidx, unsigned int texW, int c0xp, unsigned int x, unsigned int y, const COLOR *pltt, unsigned int nPltt) {
	(void) pidx;
	(void) c0xp;

	uint8_t d = txel[x + y * texW];
	unsigned int index = (d & 0x07) >> 0;
	unsigned int alpha = (d & 0xF8) >> 3;

	alpha = (alpha * 510 + 31) / 62;      // scale to 8-bit

	return TxiSamplePltt(pltt, nPltt, index) | (alpha << 24);
}

static COLOR32 TxiSampleTex4x4(const unsigned char *txel, const uint16_t *pidx, unsigned int texW, int c0xp, unsigned int x, unsigned int y, const COLOR *pltt, unsigned int nPltt) {
	(void) c0xp;

	unsigned int i = (x / 4) + (y / 4) * (texW / 4);

	uint32_t texel = *(const uint32_t *) (txel + (i << 2));
	uint16_t index = pidx[i];

	unsigned int address = COMP_INDEX(index);

	COLOR32 colors[4] = { 0 };
	colors[0] = TxiSamplePltt(pltt, nPltt, address + 0) | 0xFF000000;
	colors[1] = TxiSamplePltt(pltt, nPltt, address + 1) | 0xFF000000;

	if (!(index & COMP_INTERPOLATE)) {
		colors[2] = TxiSamplePltt(pltt, nPltt, address + 2) | 0xFF000000;
		if (index & COMP_OPAQUE) {
			colors[3] = TxiSamplePltt(pltt, nPltt, address + 3) | 0xFF000000;
		}
	}

	if (index & COMP_INTERPOLATE) {
		if (index & COMP_OPAQUE) {
			//blend colors 0,1 to 2,3
			colors[2] = TxiBlend(colors[0], colors[1], 3);
			colors[3] = TxiBlend(colors[0], colors[1], 5);
		} else {
			//blend colors 0,1 to 2
			colors[2] = TxiBlend(colors[0], colors[1], 4);
		}
	}

	unsigned int j = (x & 3) + ((y & 3) << 2);
	unsigned int pVal = (texel >> (2 * j)) & 0x3;
	return colors[pVal];
}

void TxRenderRect(COLOR32 *px, unsigned int srcX, unsigned int srcY, unsigned int srcW, unsigned int srcH, TEXELS *texels, PALETTE *palette) {
	unsigned int width = TEXW(texels->texImageParam);
	unsigned int height = texels->height;

	typedef COLOR32(*pfnSample) (const unsigned char *texel, const uint16_t *pidx, unsigned int texW, int c0xp,
		unsigned int x, unsigned int y, const COLOR *pltt, unsigned int nPltt);

	static const pfnSample pfnSamples[8] = {
		NULL,
		TxiSampleA3I5,
		TxiSamplePltt4,
		TxiSamplePltt16,
		TxiSamplePltt256,
		TxiSampleTex4x4,
		TxiSampleA5I3,
		TxiSampleDirect
	};
	pfnSample sample = pfnSamples[FORMAT(texels->texImageParam)];
	
	for (unsigned int y = 0; y < srcH; y++) {
		for (unsigned int x = 0; x < srcW; x++) {
			COLOR32 c = 0;
			if (sample != NULL && x < width && y < height) {
				c = sample(texels->texel, texels->cmp, width, COL0TRANS(texels->texImageParam), srcX + x, srcY + y, palette->pal, palette->nColors);
			}

			px[x + y * srcW] = c;
		}
	}
}

void TxRender(COLOR32 *px, TEXELS *texels, PALETTE *palette) {
	TxRenderRect(px, 0, 0, TEXW(texels->texImageParam), texels->height, texels, palette);
}


int ilog2(int x) {
	int n = 0;
	while (x) {
		x >>= 1;
		n++;
	}
	return n - 1;
}

int TxDimensionIsValid(int x) {
	if (x & (x - 1)) return 0;
	if (x < 8 || x > 1024) return 0;
	return 1;
}
