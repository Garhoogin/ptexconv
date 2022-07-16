#include <stdlib.h>
#include <string.h>

#include "texture.h"

int getTexelSize(int width, int height, int texImageParam) {
	int nPx = width * height;
	int bits[] = { 0, 8, 2, 4, 8, 2, 8, 16 };
	int b = bits[FORMAT(texImageParam)];
	return (nPx * b) >> 3;
}

typedef struct {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
} RGB;

void getrgb(COLOR n, RGB * ret){
	COLOR32 c = ColorConvertFromDS(n);

	ret->r = (uint8_t) ((c & 0xFF) * 255 / 31);
	ret->g = (uint8_t) (((c >> 8) & 0xFF) * 255 / 31);
	ret->b = (uint8_t) (((c >> 16) & 0xFF) * 255 / 31);
	ret->a = (uint8_t) (255 * (n >> 15));
}

int max16Len(char *str) {
	int len = 0;
	for (int i = 0; i < 16; i++) {
		char c = str[i];
		if (!c) return len;
		len++;
	}
	return len;
}

char *stringFromFormat(int fmt) {
	char *fmts[] = {"", "a3i5", "palette4", "palette16", "palette256", "tex4x4", "a5i3", "direct"};
	return fmts[fmt];
}

void textureRender(COLOR32 *px, TEXELS *texels, PALETTE *palette, int flip) {
	int format = FORMAT(texels->texImageParam);
	int c0xp = COL0TRANS(texels->texImageParam);
	int width = TEXW(texels->texImageParam);
	int height = TEXH(texels->texImageParam);
	int nPixels = width * height;
	int txelSize = getTexelSize(width, height, texels->texImageParam);
	switch (format) {
		case CT_DIRECT:
		{
			for (int i = 0; i < nPixels; i++) {
				unsigned short pVal = *(((unsigned short *) texels->texel) + i);
				RGB rgb = { 0 };
				getrgb(pVal, &rgb);
				px[i] = rgb.b | (rgb.g << 8) | (rgb.r << 16) | (rgb.a << 24);
			}
			break;
		}
		case CT_4COLOR:
		{
			int offs = 0;
			for (int i = 0; i < txelSize >> 2; i++) {
				unsigned d = (unsigned) *(((int *) texels->texel) + i);
				for (int j = 0; j < 16; j++) {
					int pVal = d & 0x3;
					d >>= 2;
					if (pVal < palette->nColors) {
						unsigned short col = palette->pal[pVal] | 0x8000;
						if (!pVal && c0xp) col = 0;
						RGB rgb = { 0 };
						getrgb(col, &rgb);
						px[offs] = rgb.b | (rgb.g << 8) | (rgb.r << 16) | (rgb.a << 24);
					}
					offs++;
				}
			}
			break;
		}
		case CT_16COLOR:
		{
			int iters = txelSize;
			for (int i = 0; i < iters; i++) {
				unsigned char pVal = *(((unsigned char *) texels->texel) + i);
				unsigned short col0 = 0;
				unsigned short col1 = 0;
				if ((pVal & 0xF) < palette->nColors) {
					col0 = palette->pal[pVal & 0xF] | 0x8000;
				}
				if ((pVal >> 4) < palette->nColors) {
					col1 = palette->pal[pVal >> 4] | 0x8000;
				}
				if (c0xp) {
					if (!(pVal & 0xF)) col0 = 0;
					if (!(pVal >> 4)) col1 = 0;
				}
				RGB rgb = { 0 };
				getrgb(col0, &rgb);
				px[i * 2] = rgb.b | (rgb.g << 8) | (rgb.r << 16) | (rgb.a << 24);
				getrgb(col1, &rgb);
				px[i * 2 + 1] = rgb.b | (rgb.g << 8) | (rgb.r << 16) | (rgb.a << 24);
			}
			break;
		}
		case CT_256COLOR:
		{
			for (int i = 0; i < txelSize; i++) {
				unsigned char pVal = *(texels->texel + i);
				if (pVal < palette->nColors) {
					unsigned short col = *(((unsigned short *) palette->pal) + pVal) | 0x8000;
					if (!pVal && c0xp) col = 0;
					RGB rgb = { 0 };
					getrgb(col, &rgb);
					px[i] = rgb.b | (rgb.g << 8) | (rgb.r << 16) | (rgb.a << 24);
				}
			}
			break;
		}
		case CT_A3I5:
		{
			for (int i = 0; i < txelSize; i++) {
				unsigned char d = texels->texel[i];
				int alpha = ((d & 0xE0) >> 5) * 255 / 7;
				int index = d & 0x1F;
				if (index < palette->nColors) {
					unsigned short atIndex = *(((unsigned short *) palette->pal) + index);
					RGB r = { 0 };
					getrgb(atIndex, &r);
					r.a = alpha;
					px[i] = r.b | (r.g << 8) | (r.r << 16) | (r.a << 24);
				}
			}
			break;
		}
		case CT_A5I3:
		{
			for (int i = 0; i < txelSize; i++) {
				unsigned char d = texels->texel[i];
				int alpha = ((d & 0xF8) >> 3) * 255 / 31;
				int index = d & 0x7;
				if (index < palette->nColors) {
					unsigned short atIndex = *(((unsigned short *) palette->pal) + index);
					RGB r = { 0 };
					getrgb(atIndex, &r);
					r.a = alpha;
					px[i] = r.b | (r.g << 8) | (r.r << 16) | (r.a << 24);
				}
			}
			break;
		}
		case CT_4x4:
		{
			int squares = (width * height) >> 4;
			RGB colors[4] = { 0 };
			RGB transparent = { 0, 0, 0, 0 };
			for (int i = 0; i < squares; i++) {
				unsigned texel = *(unsigned *) (texels->texel + (i << 2));
				unsigned short data = *(unsigned short *) (texels->cmp + i);

				int address = (data & 0x3FFF) << 1;
				int mode = (data >> 14) & 0x3;
				if (address < palette->nColors) {
					unsigned short * base = ((unsigned short *) palette->pal) + address;
					getrgb(base[0], colors);
					getrgb(base[1], colors + 1);
					colors[0].a = 255;
					colors[1].a = 255;
					if (mode == 0) {
						getrgb(base[2], colors + 2);
						colors[2].a = 255;
						colors[3] = transparent;
					} else if (mode == 1) {
						RGB col0 = *colors;
						RGB col1 = *(colors + 1);
						colors[2].r = (col0.r + col1.r) >> 1;
						colors[2].g = (col0.g + col1.g) >> 1;
						colors[2].b = (col0.b + col1.b) >> 1;
						colors[2].a = 255;
						colors[3] = transparent;
					} else if (mode == 2) {
						getrgb(base[2], colors + 2);
						getrgb(base[3], colors + 3);
						colors[2].a = 255;
						colors[3].a = 255;
					} else {
						RGB col0 = *colors;
						RGB col1 = *(colors + 1);
						colors[2].r = (col0.r * 5 + col1.r * 3) >> 3;
						colors[2].g = (col0.g * 5 + col1.g * 3) >> 3;
						colors[2].b = (col0.b * 5 + col1.b * 3) >> 3;
						colors[2].a = 255;
						colors[3].r = (col0.r * 3 + col1.r * 5) >> 3;
						colors[3].g = (col0.g * 3 + col1.g * 5) >> 3;
						colors[3].b = (col0.b * 3 + col1.b * 5) >> 3;
						colors[3].a = 255;
					}
				}
				for (int j = 0; j < 16; j++) {
					int pVal = texel & 0x3;
					texel >>= 2;
					RGB rgb = colors[pVal];
					int offs = ((i & ((width >> 2) - 1)) << 2) + (j & 3) + (((i / (width >> 2)) << 2) + (j >> 2)) * width;
					px[offs] = rgb.b | (rgb.g << 8) | (rgb.r << 16) | (rgb.a << 24);
				}
			}
			break;
		}
	}
	//flip upside down
	if (flip) {
		COLOR32 *tmp = calloc(width, 4);
		for (int y = 0; y < height / 2; y++) {
			COLOR32 *row1 = px + y * width;
			COLOR32 *row2 = px + (height - 1 - y) * width;
			memcpy(tmp, row1, width * 4);
			memcpy(row1, row2, width * 4);
			memcpy(row2, tmp, width * 4);
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

int textureDimensionIsValid(int x) {
	if (x & (x - 1)) return 0;
	if (x < 8 || x > 1024) return 0;
	return 1;
}

