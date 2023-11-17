#include <stdlib.h>
#include <string.h>

#include "texture.h"

int ilog2(int x);

static int ImgiPixelComparator(const void *p1, const void *p2) {
	return *(COLOR32 *) p1 - (*(COLOR32 *) p2);
}

int ImgCountColors(COLOR32 *px, int nPx) {
	//sort the colors by raw RGB value. This way, same colors are grouped.
	COLOR32 *copy = (COLOR32 *) malloc(nPx * 4);
	memcpy(copy, px, nPx * 4);
	qsort(copy, nPx, 4, ImgiPixelComparator);
	int nColors = 0;
	int hasTransparent = 0;
	for (int i = 0; i < nPx; i++) {
		int a = copy[i] >> 24;
		if (!a) hasTransparent = 1;
		else {
			COLOR32 color = copy[i] & 0xFFFFFF;
			//has this color come before?
			int repeat = 0;
			if (i) {
				COLOR32 comp = copy[i - 1] & 0xFFFFFF;
				if (comp == color) {
					repeat = 1;
				}
			}
			if (!repeat) {
				nColors++;
			}
		}
	}
	free(copy);
	return nColors + hasTransparent;
}

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

void TxRender(COLOR32 *px, int dstWidth, int dstHeight, TEXELS *texels, PALETTE *palette, int flip) {
	int format = FORMAT(texels->texImageParam);
	int c0xp = COL0TRANS(texels->texImageParam);
	int width = TEXW(texels->texImageParam);
	int height = TEXH(texels->texImageParam);
	int nPixels = width * height;
	int txelSize = TxGetTexelSize(width, height, texels->texImageParam);
	switch (format) {
		case CT_DIRECT:
		{
			for (int i = 0; i < nPixels; i++) {
				int curX = i % width, curY = i / width;
				COLOR pVal = ((COLOR *) texels->texel)[i];
				if (curX < dstWidth && curY < dstHeight) {
					px[curX + curY * dstWidth] = ColorConvertFromDS(pVal) | (GetA(pVal) ? 0xFF000000 : 0);
				}
			}
			break;
		}
		case CT_4COLOR:
		{
			int offs = 0;
			for (int i = 0; i < txelSize >> 2; i++) {
				uint32_t d = ((uint32_t *) texels->texel)[i];
				for (int j = 0; j < 16; j++) {
					int curX = offs % width, curY = offs / width;
					int pVal = d & 0x3;
					d >>= 2;
					if (curX < dstWidth && curY < dstHeight && pVal < palette->nColors) {
						if (!pVal && c0xp) {
							px[offs] = 0;
						} else {
							COLOR col = palette->pal[pVal];
							px[curX + curY * dstWidth] = ColorConvertFromDS(col) | 0xFF000000;
						}
					}
					offs++;
				}
			}
			break;
		}
		case CT_16COLOR:
		{
			for (int i = 0; i < txelSize; i++) {
				uint8_t pVal = texels->texel[i];
				COLOR32 col0 = 0;
				COLOR32 col1 = 0;

				if ((pVal & 0xF) < palette->nColors) {
					col0 = ColorConvertFromDS(palette->pal[pVal & 0xF]) | 0xFF000000;
				}
				if ((pVal >> 4) < palette->nColors) {
					col1 = ColorConvertFromDS(palette->pal[pVal >> 4]) | 0xFF000000;
				}

				if (c0xp) {
					if (!(pVal & 0xF)) col0 = 0;
					if (!(pVal >> 4)) col1 = 0;
				}

				int offs = i * 2;
				int curX = offs % width, curY = offs / width;
				if ((curX + 0) < dstWidth && curY < dstHeight) px[offs + 0] = col0;
				if ((curX + 1) < dstWidth && curY < dstHeight) px[offs + 1] = col1;
			}
			break;
		}
		case CT_256COLOR:
		{
			for (int i = 0; i < txelSize; i++) {
				uint8_t pVal = texels->texel[i];
				int destX = i % width, destY = i / width;
				if (destX < dstWidth && destY < dstHeight && pVal < palette->nColors) {
					if (!pVal && c0xp) {
						px[i] = 0;
					} else {
						COLOR col = palette->pal[pVal];
						px[destX + destY * dstWidth] = ColorConvertFromDS(col) | 0xFF000000;
					}
				}
			}
			break;
		}
		case CT_A3I5:
		{
			for (int i = 0; i < txelSize; i++) {
				uint8_t d = texels->texel[i];
				int alpha = ((d & 0xE0) >> 5) * 255 / 7;
				int index = d & 0x1F;
				if (index < palette->nColors) {
					COLOR32 atIndex = ColorConvertFromDS(palette->pal[index]);
					int destX = i % width, destY = i / width;
					if (destX < dstWidth && destY < dstHeight) {
						px[destX + destY * dstWidth] = atIndex | (alpha << 24);
					}
				}
			}
			break;
		}
		case CT_A5I3:
		{
			for (int i = 0; i < txelSize; i++) {
				uint8_t d = texels->texel[i];
				int alpha = ((d & 0xF8) >> 3) * 255 / 31;
				int index = d & 0x7;
				if (index < palette->nColors) {
					COLOR32 atIndex = ColorConvertFromDS(palette->pal[index]);
					int destX = i % width, destY = i / width;
					if (destX < dstWidth && destY < dstHeight) {
						px[destX + destY * dstWidth] = atIndex | (alpha << 24);
					}
				}
			}
			break;
		}
		case CT_4x4:
		{
			int tilesX = width / 4;
			int tilesY = height / 4;
			int nTiles = tilesX * tilesY;
			for (int i = 0; i < nTiles; i++) {
				int tileX = i % tilesX;
				int tileY = i / tilesX;

				COLOR32 colors[4] = { 0 };
				uint32_t texel = *(uint32_t *) (texels->texel + (i << 2));
				uint16_t index = texels->cmp[i];

				int address = COMP_INDEX(index);
				int mode = index & COMP_MODE_MASK;
				COLOR *base = palette->pal + address;
				if (address + 2 <= palette->nColors) {
					colors[0] = ColorConvertFromDS(base[0]) | 0xFF000000;
					colors[1] = ColorConvertFromDS(base[1]) | 0xFF000000;
				}

				if (mode == (COMP_TRANSPARENT | COMP_FULL)) {
					//require 3 colors
					if (address + 3 <= palette->nColors) {
						colors[2] = ColorConvertFromDS(base[2]) | 0xFF000000;
					}
					colors[3] = 0;
				} else if (mode == (COMP_TRANSPARENT | COMP_INTERPOLATE)) {
					//require 2 colors
					COLOR32 col0 = 0, col1 = 0;
					if (address + 2 <= palette->nColors) {
						col0 = colors[0];
						col1 = colors[1];
					}
					colors[2] = TxiBlend(col0, col1, 4) | 0xFF000000;
					colors[3] = 0;
				} else if (mode == (COMP_OPAQUE | COMP_FULL)) {
					//require 4 colors
					if (address + 4 <= palette->nColors) {
						colors[2] = ColorConvertFromDS(base[2]) | 0xFF000000;
						colors[3] = ColorConvertFromDS(base[3]) | 0xFF000000;
					}
				} else {
					//require 2 colors
					COLOR32 col0 = 0, col1 = 0;
					if (address + 2 <= palette->nColors) {
						col0 = colors[0];
						col1 = colors[1];
					}
					
					colors[2] = TxiBlend(col0, col1, 3) | 0xFF000000;
					colors[3] = TxiBlend(col0, col1, 5) | 0xFF000000;
				}

				for (int j = 0; j < 16; j++) {
					int pVal = texel & 0x3;
					texel >>= 2;

					int destX = (j % 4) + tileX * 4;
					int destY = (j / 4) + tileY * 4;
					if (destX < dstWidth && destY < dstHeight) {
						px[destX + destY * dstWidth] = colors[pVal];
					}
				}
			}
			break;
		}
	}

	//flip upside down
	if (flip) {
		COLOR32 *tmp = calloc(dstWidth, 4);
		for (int y = 0; y < dstHeight / 2; y++) {
			COLOR32 *row1 = px + y * dstWidth;
			COLOR32 *row2 = px + (dstHeight - 1 - y) * dstWidth;
			memcpy(tmp, row1, dstWidth * sizeof(COLOR32));
			memcpy(row1, row2, dstWidth * sizeof(COLOR32));
			memcpy(row2, tmp, dstWidth * sizeof(COLOR32));
		}
		free(tmp);
	}
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
