#include "gdip.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef _MSC_VER
#	define min(a,b) ((a)<(b)?(a):(b))
#	define max(a,b) ((a)>(b)?(a):(b))
#	define __inline
#endif


void ImgFlip(COLOR32 *px, unsigned int width, unsigned int height, int hFlip, int vFlip) {
	//V flip
	if (vFlip) {
		COLOR32 *rowbuf = (COLOR32 *) calloc(width, sizeof(COLOR32));
		for (unsigned int y = 0; y < height / 2; y++) {
			memcpy(rowbuf, px + y * width, width * sizeof(COLOR32));
			memcpy(px + y * width, px + (height - 1 - y) * width, width * sizeof(COLOR32));
			memcpy(px + (height - 1 - y) * width, rowbuf, width * sizeof(COLOR32));
		}
		free(rowbuf);
	}

	//H flip
	if (hFlip) {
		for (unsigned int y = 0; y < height; y++) {
			for (unsigned int x = 0; x < width / 2; x++) {
				COLOR32 left = px[x + y * width];
				COLOR32 right = px[width - 1 - x + y * width];
				px[x + y * width] = right;
				px[width - 1 - x + y * width] = left;
			}
		}
	}
}

void ImgSwapRedBlue(COLOR32 *px, unsigned int width, unsigned int height) {
	for (unsigned int i = 0; i < width * height; i++) {
		COLOR32 c = px[i];
		px[i] = REVERSE(c);
	}
}

static int ImgiPixelComparator(const void *p1, const void *p2) {
	return *(const COLOR32 *) p1 - (*(const COLOR32 *) p2);
}

unsigned int ImgCountColors(const COLOR32 *px, unsigned int nPx) {
	return ImgCountColorsEx(px, nPx, 1, 0);
}

unsigned int ImgCountColorsEx(
	const COLOR32     *px,
	unsigned int       width,
	unsigned int       height,
	ImgCountColorsMode mode
) {
	//sort the colors by raw RGB value. This way, same colors are grouped.
	unsigned int nPx = width * height;
	COLOR32 *copy = (COLOR32 *) malloc(nPx * sizeof(COLOR32));
	memcpy(copy, px, nPx * sizeof(COLOR32));

	if (mode & IMG_CCM_IGNORE_ALPHA) {
		//when ignore alpha is set, force all pixels opaque
		for (unsigned int i = 0; i < nPx; i++) copy[i] |= 0xFF000000;
	}
	if (mode & IMG_CCM_BINARY_ALPHA) {
		//when binary alpha is set, force all alpha value to 0 or 255
		for (unsigned int i = 0; i < nPx; i++) {
			unsigned int a = copy[i] >> 24;
			if (a < 0x80) copy[i] &= ~0xFF000000;
			else          copy[i] |=  0xFF000000;
		}
	}
	if (!(mode & IMG_CCM_NO_IGNORE_TRANSPARENT_COLOR)) {
		//when we ignore tranparent pixel colors, set them all to black.
		for (unsigned int i = 0; i < nPx; i++) {
			//set transparent pixels to transparent black
			unsigned int a = copy[i] >> 24;
			if (a == 0) copy[i] = 0;
		}
	}

	//sort by raw RGBA value
	qsort(copy, nPx, sizeof(COLOR32), ImgiPixelComparator);

	unsigned int nColors = 0;
	for (unsigned int i = 0; i < nPx; i++) {
		unsigned int a = copy[i] >> 24;
		if (a == 0 && (mode & IMG_CCM_NO_COUNT_TRANSPARENT)) continue;

		//has this color come before?
		if (i == 0 || copy[i - 1] != copy[i]) nColors++;
	}
	free(copy);
	return nColors;
}

void ImgCropInPlace(const COLOR32 *px, unsigned int width, unsigned int height, COLOR32 *out, int srcX, int srcY, unsigned int srcWidth, unsigned int srcHeight) {
	//copy from px to out
	for (unsigned int y = 0; y < srcHeight; y++) {
		for (unsigned int x = 0; x < srcWidth; x++) {
			int pxX = x + srcX, pxY = y + srcY;

			if (pxX < 0 || pxX >= (int) width || pxY < 0 || pxY >= (int) height) {
				//fill with transparent for out of bounds
				out[x - srcX + y * srcWidth] = 0;
				continue;
			}

			//write pixel
			out[x + y * srcWidth] = px[pxX + pxY * width];
		}
	}
}

COLOR32 *ImgCrop(const COLOR32 *px, unsigned int width, unsigned int height, int srcX, int srcY, unsigned int srcWidth, unsigned int srcHeight) {
	COLOR32 *out = (COLOR32 *) calloc(srcWidth * srcHeight, sizeof(COLOR32));
	ImgCropInPlace(px, width, height, out, srcX, srcY, srcWidth, srcHeight);
	return out;
}

COLOR32 *ImgComposite(const COLOR32 *back, unsigned int backWidth, unsigned int backHeight, const COLOR32 *front, unsigned int frontWidth, unsigned int frontHeight, unsigned int *outWidth, unsigned int *outHeight) {
	//create output image with dimension min(<backWidth, backHeight>, <frontWidth, frontHeight>)
	unsigned int width = min(backWidth, frontWidth);
	unsigned int height = min(backHeight, frontHeight);
	*outWidth = width;
	*outHeight = height;

	COLOR32 *out = (COLOR32 *) calloc(width * height, sizeof(COLOR32));
	for (unsigned int y = 0; y < height; y++) {
		for (unsigned int x = 0; x < width; x++) {
			COLOR32 fg = front[x + y * frontWidth];
			COLOR32 bg = back[x + y * backWidth];
			unsigned int af = fg >> 24;
			unsigned int ab = bg >> 24;

			if (af == 255) {
				//if foreground opaque, write foreground
				out[x + y * width] = fg;
			} else if (af == 0) {
				//if foreground transparent, write background
				out[x + y * width] = bg;
			} else {
				//compute coefficients
				unsigned int wf = 255 * af;
				unsigned int wb = (255 - af) * ab;
				unsigned int wTotal = wf + wb;

				unsigned int r = (((fg >>  0) & 0xFF) * wf + ((bg >>  0) & 0xFF) * wb) / wTotal;
				unsigned int g = (((fg >>  8) & 0xFF) * wf + ((bg >>  8) & 0xFF) * wb) / wTotal;
				unsigned int b = (((fg >> 16) & 0xFF) * wf + ((bg >> 16) & 0xFF) * wb) / wTotal;
				unsigned int a = wTotal / 255;
				out[x + y * width] = (r << 0) | (g << 8) | (b << 16) | (a << 24);
			}
		}
	}
	return out;
}

unsigned char *ImgCreateAlphaMask(const COLOR32 *px, unsigned int width, unsigned int height, unsigned int threshold, unsigned int *pRows, unsigned int *pStride) {
	unsigned int stride = ((width + 7) / 8), nRows = height;

	unsigned char *bits = (unsigned char *) calloc(stride * nRows, sizeof(unsigned char));
	for (unsigned int y = 0; y < height; y++) {
		unsigned char *row = bits + (y) * stride;
		for (unsigned int x = 0; x < width; x++) {
			unsigned char *pp = row + (x / 8);
			unsigned int bitno = (x & 7) ^ 7;

			*pp &= ~(1 << bitno);
			*pp |= (((px[x + y * width] >> 24) < threshold) << bitno);
		}
	}

	if (pRows != NULL) *pRows = nRows;
	if (pStride != NULL) *pStride = stride;
	return bits;
}

unsigned char *ImgCreateColorMask(const COLOR32 *px, unsigned int width, unsigned int height, unsigned int *pRows, unsigned int *pStride) {
	unsigned int stride = (width * 4 + 3) & ~3, nRows = height;

	unsigned char *bits = (unsigned char *) calloc(stride * nRows, 1);
	for (unsigned int y = 0; y < height; y++) {
		unsigned char *row = bits + (y) * stride;
		for (unsigned int x = 0; x < width; x++) {
			COLOR32 c = px[x + y * width];

			row[x * 4 + 0] = (c >> 16) & 0xFF;
			row[x * 4 + 1] = (c >>  8) & 0xFF;
			row[x * 4 + 2] = (c >>  0) & 0xFF;
		}
	}

	if (pRows != NULL) *pRows = nRows;
	if (pStride != NULL) *pStride = stride;
	return bits;
}

COLOR32 *ImgScale(const COLOR32 *px, unsigned int width, unsigned int height, unsigned int outWidth, unsigned int outHeight) {
	//alloc out
	COLOR32 *out = (COLOR32 *) calloc(outWidth * outHeight, sizeof(COLOR32));

	//trivial case: inSize == outSize
	if (width == outWidth && height == outHeight) {
		memcpy(out, px, width * height * sizeof(COLOR32));
		return out;
	}

	//0x0 input image case: render transparent bitmap
	if (width == 0 || height == 0) {
		return out;
	}

	//scale image
	for (unsigned int y = 0; y < outHeight; y++) {
		for (unsigned int x = 0; x < outWidth; x++) {
			//determine sample rectangle in source image
			unsigned int sX1N = (x + 0) * width , sX1D = outWidth;
			unsigned int sY1N = (y + 0) * height, sY1D = outHeight;
			unsigned int sX2N = (x + 1) * width , sX2D = outWidth;
			unsigned int sY2N = (y + 1) * height, sY2D = outHeight;

			//determine sample rectangle in source image
			double sX1 = ((double) sX1N) / ((double) sX1D);
			double sY1 = ((double) sY1N) / ((double) sY1D);
			double sX2 = ((double) sX2N) / ((double) sX2D);
			double sY2 = ((double) sY2N) / ((double) sY2D);

			//compute sample
			double tr = 0.0, tg = 0.0, tb = 0.0, ta = 0.0;
			double sampleArea = (sX2 - sX1) * (sY2 - sY1);

			//determine the pixel rectangle to sample. Float coordinates are between pixels, and integer
			//coordinates are in the centers of pixels.
			unsigned int sampleRectX = sX1N / sX1D;
			unsigned int sampleRectY = sY1N / sY1D;
			unsigned int sampleRectW = (sX2N + sX2D - 1) / sX2D - sampleRectX;
			unsigned int sampleRectH = (sY2N + sY2D - 1) / sY2D - sampleRectY;

			//compute rectangle trims
			double trimL = sX1 - (double) sampleRectX;
			double trimR = ((double) ((sX2N + sX2D - 1) / sX2D)) - sX2;
			double trimU = sY1 - (double) sampleRectY;
			double trimD = ((double) ((sY2N + sY2D - 1) / sY2D)) - sY2;

			for (unsigned int sy = 0; sy < sampleRectH && (sampleRectY + sy) < height; sy++) {
				double rowH = 1.0;
				if (sy == 0) rowH -= trimU;                 // trim from top
				if (sy == (sampleRectH - 1)) rowH -= trimD; // trim from bottom


				for (unsigned int sx = 0; sx < sampleRectW && (sampleRectX + sx) < width; sx++) {
					double colW = 1.0;
					if (sx == 0) colW -= trimL;                 // trim from left
					if (sx == (sampleRectW - 1)) colW -= trimR; // trim from right

					//sum colors
					COLOR32 col = px[(sampleRectX + sx) + (sampleRectY + sy) * width];
					unsigned int colA = (col >> 24);
					double weight = colW * rowH * colA;
					tr += ((col >>  0) & 0xFF) * weight;
					tg += ((col >>  8) & 0xFF) * weight;
					tb += ((col >> 16) & 0xFF) * weight;
					ta += weight;
				}
			}

			if (ta > 0) {
				tr /= ta;
				tg /= ta;
				tb /= ta;
				ta /= sampleArea;
			} else {
				tr = tg = tb = ta = 0.0;
			}

			unsigned int sampleR = (unsigned int) (int) (tr + 0.5);
			unsigned int sampleG = (unsigned int) (int) (tg + 0.5);
			unsigned int sampleB = (unsigned int) (int) (tb + 0.5);
			unsigned int sampleA = (unsigned int) (int) (ta + 0.5);
			COLOR32 sample = sampleR | (sampleG << 8) | (sampleB << 16) | (sampleA << 24);
			out[x + y * outWidth] = sample;
		}
	}

	return out;
}

COLOR32 *ImgScaleEx(const COLOR32 *px, unsigned int width, unsigned int height, unsigned int outWidth, unsigned int outHeight, ImgScaleSetting setting) {
	if (width == 0 || height == 0) {
		//size of 0x0: fall back
		return ImgScale(px, width, height, outWidth, outHeight);
	}

	switch (setting) {
		case IMG_SCALE_FILL:
			//fill: operation is default
			return ImgScale(px, width, height, outWidth, outHeight);
		case IMG_SCALE_COVER:
		case IMG_SCALE_FIT:
		{
			//cover and fit may pad the image.
			unsigned int scaleW = outWidth, scaleH = outHeight;
			unsigned int width1 = outWidth, height1 = height * outWidth / width;
			unsigned int width2 = width * outHeight / height, height2 = outHeight;

			//cover: choose the larger of the two scales.
			//fit: choose the smaller of the two scales.
			if (setting == IMG_SCALE_COVER) {
				if (width1 > width2) scaleW = width1, scaleH = height1;
				else scaleW = width2, scaleH = height2;
			} else {
				if (width1 < width2) scaleW = width1, scaleH = height1;
				else scaleW = width2, scaleH = height2;
			}

			//scale to dimensions
			COLOR32 *scaled = ImgScale(px, width, height, scaleW, scaleH);

			//construct output image data
			COLOR32 *out = (COLOR32 *) calloc(outWidth * outHeight, sizeof(COLOR32));
			int transX = -((int) outWidth - (int) scaleW) / 2;
			int transY = -((int) outHeight - (int) scaleH) / 2;
			for (unsigned int y = 0; y < outHeight; y++) {
				for (unsigned int x = 0; x < outWidth; x++) {
					int sampleX = x + transX, sampleY = y + transY;
					if (sampleX >= 0 && sampleY >= 0 && (unsigned int) sampleX < scaleW && (unsigned int) sampleY < scaleH) {
						out[x + y * outWidth] = scaled[sampleX + sampleY * scaleW];
					}
				}
			}
			free(scaled);
			return out;
		}
	}

	//bad mode
	return NULL;
}



