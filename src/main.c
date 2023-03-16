#include <stdio.h>
#include <time.h>
#include <math.h>
#include <string.h>

#include "texture.h"
#include "texconv.h"
#include "palette.h"
#include "bggen.h"

#ifdef _WIN32
#   include <tchar.h>
#   include "gdip.h"
#else
#   define TCHAR char
#   define _T(x) x
#   define _tmain main
#   define _tfopen fopen
#   define _tcscmp strcmp
#   define _tprintf printf
#   define _tcslen strlen
#   define _ttoi atoi

#   define STB_IMAGE_IMPLEMENTATION
#   include "stb_image.h"
#endif

#define MODE_BG      0
#define MODE_TEXTURE 1

int _fltused;

#ifdef _WIN32
extern long _ftol(double d);

long _ftol2_sse(float f) { //ugly hack
	return _ftol(f);
}
#endif

#define VERSION "1.3.0.0"

const char *g_helpString = ""
	"DS Texture Converter command line utility version " VERSION "\n"
	"\n"
	"Usage: ptexconv <option...> image [option...]\n"
	"\n"
	"Global options:\n"
	"   -gb     Generate BG (default)\n"
	"   -gt     Generate texture\n"
	"   -o      Specify output base name\n"
	"   -ob     Output binary (default)\n"
	"   -oc     Output as C header file\n"
	"   -d  <n> Use dithering of n% (default 0%)\n"
	"   -cm <n> Limit palette colors to n, regardless of bit depth\n"
	"   -bb <n> Lightness-Color balance [1, 39] (default 20)\n"
	"   -bc <n> Red-Green color balance [1, 39] (default 20)\n"
	"   -be     Enhance colors in gradients (off by default)\n"
	"   -s      Silent\n"
	"   -h      Display help text\n"
	"\n"
	"BG Options:\n"
	"   -b  <n> Specify output bit depth {4, 8}\n"
	"   -p  <n> Use n palettes in output (Only valid for 4bpp)\n"
	"   -pb <n> Use palette base index n (Only valid for 4bpp)\n"
	"   -cc <n> Compress characters to a maximum of n (default is 1024)\n"
	"   -cn     No character compression\n"
	"   -ns     Do not output screen data\n"
	"   -od     Output as DIB (disables character compression)\n"
	"\n"
	"Texture Options:\n"
	"   -f     Specify format {palette4, palette16, palette256, a3i5, a5i3, tex4x4, direct}\n"
	"   -ot    Output as NNS TGA\n"
	"   -fp    Specify fixed palette file\n\n"
"";

const char *texHeader = ""
	"///////////////////////////////////////\n"
	"// \n"
	"// %s\n"
	"// Generated %d/%d/%d %d:%02d %cM\n"
	"// Format: %s\n"
	"// Colors: %d\n"
	"// Size: %dx%d\n"
	"// \n"
	"///////////////////////////////////////\n\n"
"";

const char *bgHeader = ""
	"///////////////////////////////////////\n"
	"// \n"
	"// %s\n"
	"// Generated %d/%d/%d %d:%02d %cM\n"
	"// Bit depth: %d\n"
	"// Palettes: %d\n"
	"// Palette base: %d\n"
	"// Size: %dx%d\n"
	"// \n"
	"///////////////////////////////////////\n\n"
"";

void getDate(int *month, int *day, int *year, int *hour, int *minute, int *am) {

#ifndef _WIN32
	time_t tm = time(NULL);
	struct tm *local = localtime(&tm);

	*month = local->tm_mon + 1, *day = local->tm_mday, *year = local->tm_year + 1900;
	*hour = local->tm_hour, *minute = local->tm_min;
#else
	SYSTEMTIME time;
	GetSystemTime(&time);

	*month = time.wYear, *day = time.wDay, *year = time.wYear;
	*hour = time.wHour, *minute = time.wMinute;
#endif

	*am = *hour < 12;
	*hour %= 12;
	if (*hour == 0) *hour = 12;
}

void printHelp() {
	puts(g_helpString);
}

COLOR32 *tgdipReadImage(const TCHAR *lpszFileName, int *pWidth, int *pHeight) {

#ifdef _WIN32

#ifdef _UNICODE
	return gdipReadImage(lpszFileName, pWidth, pHeight);
#else //_UNICODE
	wchar_t buffer[260];
	for (int i = 0; i < _tcslen(lpszFileName); i++) {
		buffer[i] = (wchar_t) lpszFileName[i];
	}
	return gdipReadImage(buffer, pWidth, pHeight);
#endif

#else //_WIN32
	int channels;
	return (COLOR32 *) stbi_load(lpszFileName, pWidth, pHeight, &channels, 4);
#endif

}

int isTranslucent(COLOR32 *px, int nWidth, int nHeight) {
	for (int i = 0; i < nWidth * nHeight; i++) {
		int a = px[i] >> 24;
		if (a >= 5 && a <= 250) return 1;
	}
	return 0;
}

int guessFormat(COLOR32 *px, int nWidth, int nHeight) {
	//Guess a good format for the data. Default to 4x4.
	int fmt = CT_4x4;

	//if the texture is 1024x1024, do not choose 4x4.
	if (nWidth * nHeight == 1024 * 1024) fmt = CT_256COLOR;

	//is there translucency?
	if (isTranslucent(px, nWidth, nHeight)) {
		//then choose a3i5 or a5i3. Do this by using color count.
		int colorCount = countColors(px, nWidth * nHeight);
		if (colorCount < 16) {
			//colors < 16, choose a5i3.
			fmt = CT_A5I3;
		} else {
			//otherwise, choose a3i5.
			fmt = CT_A3I5;
		}
	} else {
		//weigh the other format options for optimal size.
		int nColors = countColors(px, nWidth * nHeight);

		//if <= 4 colors, choose 4-color.
		if (nColors <= 4) {
			fmt = CT_4COLOR;
		} else {
			//weigh 16-color, 256-color, and 4x4. 
			//take the number of pixels per color. 
			int pixelsPerColor = 2 * nWidth * nHeight / nColors;
			if (pixelsPerColor >= 3 && !(nWidth * nHeight >= 1024 * 512)) {
				fmt = CT_4x4;
			} else if (nColors < 32) {
				//otherwise, 4x4 probably isn't a good option.
				fmt = CT_16COLOR;
			}
		}
	}

	return fmt;
}

char *getVersion() {
	return VERSION;
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

//lifted almost straight from NitroPaint
void writeNitroTGA(TCHAR *name, TEXELS *texels, PALETTE *palette) {
	FILE *fp = _tfopen(name, _T("wb"));

	int width = TEXW(texels->texImageParam);
	int height = TEXH(texels->texImageParam);
	COLOR32 *pixels = (COLOR32 *) calloc(width * height, sizeof(COLOR32));
	textureRender(pixels, texels, palette, 1);

	unsigned char header[] = { 0x14, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x20, 8,
		'N', 'N', 'S', '_', 'T', 'g', 'a', ' ', 'V', 'e', 'r', ' ', '1', '.', '0', 0, 0, 0, 0, 0 };
	*(uint16_t *) (header + 0xC) = width;
	*(uint16_t *) (header + 0xE) = height;
	*(uint32_t *) (header + 0x22) = sizeof(header) + width * height * 4;
	fwrite(header, sizeof(header), 1, fp);
	fwrite(pixels, width * height * 4, 1, fp);

	char *fstr = stringFromFormat(FORMAT(texels->texImageParam));
	fwrite("nns_frmt", 8, 1, fp);
	uint32_t flen = strlen(fstr) + 0xC;
	fwrite(&flen, 4, 1, fp);
	fwrite(fstr, 1, flen - 0xC, fp);

	//texels
	fwrite("nns_txel", 8, 1, fp);
	uint32_t txelLength = getTexelSize(width, height, texels->texImageParam) + 0xC;
	fwrite(&txelLength, 4, 1, fp);
	fwrite(texels->texel, txelLength - 0xC, 1, fp);

	//write 4x4 if applicable
	if (FORMAT(texels->texImageParam) == CT_4x4) {
		fwrite("nns_pidx", 8, 1, fp);
		uint32_t pidxLength = (txelLength - 0xC) / 2 + 0xC;
		fwrite(&pidxLength, 4, 1, fp);
		fwrite(texels->cmp, pidxLength - 0xC, 1, fp);
	}

	//palette (if applicable)
	if (FORMAT(texels->texImageParam) != CT_DIRECT) {
		fwrite("nns_pnam", 8, 1, fp);
		uint32_t pnamLength = max16Len(palette->name) + 0xC;
		fwrite(&pnamLength, 4, 1, fp);
		fwrite(palette->name, 1, pnamLength - 0xC, fp);

		int nColors = palette->nColors;
		if (FORMAT(texels->texImageParam) == CT_4COLOR && nColors > 4) nColors = 4;
		fwrite("nns_pcol", 8, 1, fp);
		uint32_t pcolLength = nColors * 2 + 0xC;
		fwrite(&pcolLength, 4, 1, fp);
		fwrite(palette->pal, nColors, 2, fp);
	}

	unsigned char gnam[] = { 'n', 'n', 's', '_', 'g', 'n', 'a', 'm', 20, 0, 0, 0, 'p', 't', 'e', 'x', 'c', 'o', 'n', 'v' };
	fwrite(gnam, sizeof(gnam), 1, fp);

	char version[16];
	getVersion(version, 16);
	unsigned char gver[] = { 'n', 'n', 's', '_', 'g', 'v', 'e', 'r', 0, 0, 0, 0 };
	*(uint32_t *) (gver + 8) = strlen(version) + 0xC;
	fwrite(gver, sizeof(gver), 1, fp);
	fwrite(version, strlen(version), 1, fp);

	unsigned char imst[] = { 'n', 'n', 's', '_', 'i', 'm', 's', 't', 0xC, 0, 0, 0 };
	fwrite(imst, sizeof(imst), 1, fp);

	//if c0xp
	if (COL0TRANS(texels->texImageParam)) {
		unsigned char c0xp[] = { 'n', 'n', 's', '_', 'c', '0', 'x', 'p', 0xC, 0, 0, 0 };
		fwrite(c0xp, sizeof(c0xp), 1, fp);
	}

	//write end
	unsigned char end[] = { 'n', 'n', 's', '_', 'e', 'n', 'd', 'b', 0xC, 0, 0, 0 };
	fwrite(end, sizeof(end), 1, fp);

	fclose(fp);
	free(pixels);
}

unsigned char *createBitmapData(int *indices, int width, int height, int depth, int *dataSize) {
	int strideLength = (width * depth + 7) / 8;
	if (strideLength & 3) strideLength = (strideLength + 3) & ~3;

	int imageSize = strideLength * height;
	*dataSize = imageSize;

	int posShift = (depth == 4) ? 1 : 0;
	int idxShift = (depth == 4) ? 4 : 0;

	unsigned char *data = (unsigned char *) calloc(imageSize, 1);
	for (int y = 0; y < height; y++) {
		unsigned char *row = data + y * strideLength;

		for (int x = 0; x < width; x++) {
			unsigned char old = row[x >> posShift];
			int index = x + (height - 1 - y) * width;
			if (depth == 8) {
				row[x] = indices[index];
			} else {
				int idx = indices[index];
				if (x & 1) {
					row[x >> posShift] = old | idx;
				} else {
					row[x >> posShift] = idx << 4;
				}
			}
		}
	}

	return data;
}

void writeBitmap(COLOR32 *palette, int paletteSize, int *indices, int width, int height, const TCHAR *path) {
	FILE *fp = _tfopen(path, _T("wb"));

	unsigned char header[] = { 'B', 'M', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	unsigned char infoHeader[] = {
		0x28, 0, 0, 0,
		0, 0, 0, 0, //width
		0, 0, 0, 0, //height
		1, 0, //planes
		0, 0, //depth
		0, 0, 0, 0, //compression
		0, 0, 0, 0, //size
		0x68, 0x10, 0, 0, //ppm X
		0x68, 0x10, 0, 0, //ppm Y
		0, 0, 0, 0, //palette size
		0, 0, 0, 0 //important colors
	};

	int depth = (paletteSize <= 16) ? 4 : 8;
	int paletteDataSize = paletteSize * 4;
	unsigned char *paletteData = (unsigned char *) calloc(paletteDataSize, 1);
	for (int i = 0; i < paletteSize; i++) {
		COLOR32 c = palette[i];
		paletteData[i * 4 + 0] = (c >> 16) & 0xFF;
		paletteData[i * 4 + 1] = (c >> 8) & 0xFF;
		paletteData[i * 4 + 2] = c & 0xFF;
	}

	//create bitmap data, don't bother with RLE because it's shit lol
	int strideLength = (width * depth + 7) / 8;
	if (strideLength & 3) strideLength = (strideLength + 3) & ~3;
	int bmpDataSize = strideLength * height;
	int posShift = (depth == 4) ? 1 : 0;

	unsigned char *bmpData = (unsigned char *) calloc(bmpDataSize, 1);
	for (int y = 0; y < height; y++) {
		unsigned char *row = bmpData + y * strideLength;

		for (int x = 0; x < width; x++) {
			unsigned char old = row[x >> posShift];
			int index = x + (height - 1 - y) * width;
			if (depth == 8) {
				row[x] = indices[index];
			} else {
				int idx = indices[index];
				if (x & 1) {
					row[x >> 1] = old | idx;
				} else {
					row[x >> 1] = idx << 4;
				}
			}
		}
	}

	*(uint32_t *) (header + 0x02) = sizeof(header) + sizeof(infoHeader) + bmpDataSize + paletteDataSize;
	*(uint32_t *) (header + 0x0A) = sizeof(header) + sizeof(infoHeader) + paletteDataSize;
	*(uint32_t *) (infoHeader + 0x04) = width;
	*(uint32_t *) (infoHeader + 0x08) = height;
	*(uint16_t *) (infoHeader + 0x0E) = depth;
	*(uint32_t *) (infoHeader + 0x14) = bmpDataSize;
	*(uint32_t *) (infoHeader + 0x20) = paletteSize;
	*(uint32_t *) (infoHeader + 0x24) = paletteSize;

	fwrite(header, sizeof(header), 1, fp);
	fwrite(infoHeader, sizeof(infoHeader), 1, fp);
	fwrite(paletteData, paletteDataSize, 1, fp);
	fwrite(bmpData, bmpDataSize, 1, fp);

	fclose(fp);
	free(bmpData);
	free(paletteData);
}

#ifdef _WIN32

float mylog2(float d) { //UGLY!
	float ans;
	_asm {
		fld1
		fld dword ptr[d]
		fyl2x
		fstp dword ptr[ans]
	}
	return ans;
}
#define log2f mylog2

#endif

//based on suggestions for color counts by SGC, interpolated with a log function
int chooseColorCount(int bWidth, int bHeight) {
	int area = bWidth * bHeight;

	//for textures smaller than 256x256, use 8*sqrt(area)
	if (area < 256 * 256) {
		int nColors = (int) (8 * sqrt((float) area));
		nColors = (nColors + 15) & ~15;
		return nColors;
	}

	//larger sizes, increase by 256 every width/height increment
	return (int) (256 * (log2f((float) area) - 10));
}

const char *getFileNameFromPath(const char *path) {
	int lastIndex = -1;
	for (unsigned int i = 0; i < strlen(path); i++) {
		if (path[i] == '/' || path[i] == '\\') lastIndex = i;
	}
	return path + lastIndex + 1;
}

int _tmain(int argc, TCHAR **argv) {
	argc--;
	argv++;

	//check arguments
	if (argc < 1) {
		//print help
		printHelp();
		return 0;
	}

	//help?
	for (int i = 0; i < argc; i++) {
		if (_tcscmp(argv[i], _T("-h")) == 0) {
			printHelp();
			return 0;
		}
	}

	//begin processing arguments. First, determine global settings and conversion mode.
	const TCHAR *fixedPalette = NULL;
	const TCHAR *srcImage = NULL;
	const TCHAR *outBase = NULL;
	int nMaxColors = -1; //default. 256 for BG, automatic for texture
	int silent = 0;
	int diffuse = 0;
	int outputBinary = 1;
	int outputTga = 0, outputDib = 0;
	int mode = MODE_BG;
	int balance = BALANCE_DEFAULT;
	int colorBalance = BALANCE_DEFAULT;
	int enhanceColors = 0;

	//BG settings
	int nMaxChars = 1024;
	int depth = 8;
	int nPalettes = 1;
	int paletteBase = 0;
	int flipX = 1, flipY = 1;
	int outputScreen = 1;


	//Texture settings
	int format = -1; //default, just guess

	for (int i = 0; i < argc; i++) {
		const TCHAR *arg = argv[i];

		if (_tcscmp(arg, _T("-gb")) == 0) {
			mode = MODE_BG;
		} else if (_tcscmp(arg, _T("-gt")) == 0) {
			mode = MODE_TEXTURE;
		} else if (_tcscmp(arg, _T("-o")) == 0) {
			i++;
			if (i < argc) outBase = argv[i];
		} else if (_tcscmp(arg, _T("-ob")) == 0) {
			outputBinary = 1;
		} else if (_tcscmp(arg, _T("-oc")) == 0) {
			outputBinary = 0;
		} else if (_tcscmp(arg, _T("-d")) == 0) {
			i++;
			if (i < argc) diffuse = _ttoi(argv[i]);
		} else if (_tcscmp(arg, _T("-cm")) == 0) {
			i++;
			if (i < argc) nMaxColors = _ttoi(argv[i]);
		} else if (_tcscmp(arg, _T("-s")) == 0) {
			silent = 1;
		} else if (_tcscmp(arg, _T("-bb")) == 0) {
			i++;
			if (i < argc) balance = _ttoi(argv[i]);
		} else if (_tcscmp(arg, _T("-bc")) == 0) {
			i++;
			if (i < argc) colorBalance = _ttoi(argv[i]);
		} else if (_tcscmp(arg, _T("-be")) == 0) {
			enhanceColors = 1;
		}

		//BG option
		else if (_tcscmp(arg, _T("-b")) == 0) {
			i++;
			if (i < argc) depth = _ttoi(argv[i]);
		} else if (_tcscmp(arg, _T("-p")) == 0) {
			i++;
			if (i < argc) nPalettes = _ttoi(argv[i]);
		} else if (_tcscmp(arg, _T("-pb")) == 0) {
			i++;
			if (i < argc) paletteBase = _ttoi(argv[i]);
		} else if (_tcscmp(arg, _T("-cc")) == 0) {
			i++;
			if (i < argc) nMaxChars = _ttoi(argv[i]);
		} else if (_tcscmp(arg, _T("-cn")) == 0) {
			nMaxChars = -1;
		} else if (_tcscmp(arg, _T("-nx")) == 0) {
			flipX = 0;
		} else if (_tcscmp(arg, _T("-ny")) == 0) {
			flipY = 0;
		} else if (_tcscmp(arg, _T("-ns")) == 0) {
			outputScreen = 0;
		} else if (_tcscmp(arg, _T("-od")) == 0) {
			outputDib = 1;
		}

		//Texture option
		else if (_tcscmp(arg, _T("-f")) == 0) {
			i++;
			if (i < argc) {
				const TCHAR *fmtString = argv[i];

				//what format?
				if (_tcscmp(fmtString, _T("a3i5")) == 0) format = CT_A3I5;
				else if (_tcscmp(fmtString, _T("a5i3")) == 0) format = CT_A5I3;
				else if (_tcscmp(fmtString, _T("palette4")) == 0) format = CT_4COLOR;
				else if (_tcscmp(fmtString, _T("palette16")) == 0) format = CT_16COLOR;
				else if (_tcscmp(fmtString, _T("palette256")) == 0) format = CT_256COLOR;
				else if (_tcscmp(fmtString, _T("tex4x4")) == 0) format = CT_4x4;
				else if (_tcscmp(fmtString, _T("direct")) == 0) format = CT_DIRECT;
				else {
					//maybe a format number
					int fid = _ttoi(fmtString);
					if (fid >= 1 && fid <= 7) format = fid;
					else _tprintf(_T("Unknown texture format %s.\n"), fmtString);
				}
			}
		} else if (_tcscmp(arg, _T("-ot")) == 0) {
			outputTga = 1;
		} else if (arg[0] != _T('-')) { //not a switch
			srcImage = arg;
		}

		//unknown switch
		else {
			_tprintf(_T("Ignoring unknown switch %s.\n"), arg);
		}
	}

	//check for errors
	if (srcImage == NULL) {
		puts("No source image specified.");
		return 1;
	}
	if (outBase == NULL) {
		puts("No output name specified.");
		return 1;
	}
	if (depth != 4 && depth != 8) {
		printf("Invaid number of bits specified %d.\n", depth);
		return 1;
	}
	if (diffuse < 0 || diffuse > 100) {
		printf("Diffuse amount out of range: %d\n", diffuse);
		return 1;
	}
	if (mode == MODE_BG && ((depth == 4 && nMaxColors > 16) || (depth == 8 && nMaxColors > 256))) {
		printf("Too many output colors specified: %d\n", nMaxColors);
		return 1;
	}
	if (mode == MODE_BG && outputTga) {
		printf("Cannot output NNS TGA for BGs.\n");
		return 1;
	}
	if (mode == MODE_TEXTURE && outputDib) {
		printf("Cannot output DIB for textures.\n");
		return 1;
	}

	//MBS copy of base
	int baseLength = _tcslen(outBase);
	char *mbsBase = (char *) calloc(baseLength + 1, sizeof(char));
	for (int i = 0; i < baseLength + 1; i++) {
		mbsBase[i] = (char) outBase[i];
	}

	int width, height;
	COLOR32 *px = tgdipReadImage(srcImage, &width, &height);

	if (mode == MODE_BG) {
		//Generate BG
		//fix up automatic flags

		if (nMaxColors == -1) nMaxColors = (depth == 4) ? 16 : 256;
		if (depth == 4 && nMaxColors > 16) nMaxColors = 16;
		if (depth == 8 && nMaxColors > 256) nMaxColors = 256;
		if (depth == 8 && nPalettes != 1) nPalettes = 1;
		if (nPalettes > 16) nPalettes = 16;

		if (outputDib) {
			outputScreen = 0;
			nMaxChars = -1;
			outputBinary = 0;
		}

		if (!silent) printf("Generating BG\nBits: %d\nPalettes: %d\nPalette size: %d\nMax chars: %d\nDiffuse: %d%%\nPalette base: %d\n\n",
			depth, nPalettes, nMaxColors, nMaxChars, diffuse, paletteBase);

		COLOR *pal;
		unsigned char *chars;
		unsigned short *screen;
		int palSize, charSize, screenSize;
		int p1, p1max, p2, p2max;
		bgGenerate(px, width, height, depth, !!diffuse, diffuse / 100.0f, &pal, &chars, &screen, &palSize, &charSize, &screenSize,
			paletteBase, nPalettes, 0, 0, nMaxChars != -1, nMaxColors, 0, 0, nMaxChars, balance, colorBalance, enhanceColors,
			&p1, &p1max, &p2, &p2max);

		if (outputBinary) {
			//output NBFP, NBFC, NBFS.

			//suffix the filename with .nbfp, .nbfc, .nbfs. So reserve 6 characters+base length.
			TCHAR *nameBuffer = (TCHAR *) calloc(baseLength + 6, sizeof(TCHAR));
			memcpy(nameBuffer, outBase, (baseLength + 1) * sizeof(TCHAR));
			memcpy(nameBuffer + baseLength, _T(".nbfp"), 6 * sizeof(TCHAR));

			FILE *fp = _tfopen(nameBuffer, _T("wb"));
			fwrite(pal, palSize, 1, fp);
			fclose(fp);
			if (!silent) _tprintf(_T("Wrote %s\n"), nameBuffer);

			memcpy(nameBuffer + baseLength, _T(".nbfc"), 6 * sizeof(TCHAR));
			fp = _tfopen(nameBuffer, _T("wb"));
			fwrite(chars, charSize, 1, fp);
			fclose(fp);
			if (!silent) _tprintf(_T("Wrote %s\n"), nameBuffer);

			if (outputScreen) {
				memcpy(nameBuffer + baseLength, _T(".nbfs"), 6 * sizeof(TCHAR));
				fp = _tfopen(nameBuffer, _T("wb"));
				fwrite(screen, screenSize, 1, fp);
				fclose(fp);
				if (!silent) _tprintf(_T("Wrote %s\n"), nameBuffer);
			}

			free(nameBuffer);

		} else if (outputDib) { //output DIB file

			//suffix filename with .bmp, reserve 5 characters+base length
			TCHAR *nameBuffer = (TCHAR *) calloc(baseLength + 5, sizeof(TCHAR));
			memcpy(nameBuffer, outBase, (baseLength + 1) * sizeof(TCHAR));
			memcpy(nameBuffer + baseLength, _T(".bmp"), 5 * sizeof(TCHAR));

			int charsX = width / 8, charsY = height / 8;
			int outWidth = charsX * 8, outHeight = charsY * 8;
			int bytesPerChar = depth == 8 ? 64 : 32;
			int *indexBuffer = (int *) calloc(outWidth * outHeight, sizeof(int));
			for (int cy = 0; cy < charsY; cy++) {
				for (int cx = 0; cx < charsX; cx++) {
					unsigned char *thisChar = chars + (cx + cy * (charsX)) * bytesPerChar;
					unsigned short thisScr = screen[cx + cy * charsX]; //this works because no char compression
					int palIndex = (thisScr >> 12) & 0xF;

					for (int y = 0; y < 8; y++) {
						for (int x = 0; x < 8; x++) {
							int indexValue;
							if (depth == 8) {
								indexValue = thisChar[x + y * 8];
							} else {
								indexValue = thisChar[(x / 2) + y * 4];
								if ((x & 1) == 0) indexValue &= 0xF;
								else indexValue >>= 4;
								indexValue |= (palIndex << 4);
							}
							indexBuffer[cx * 8 + x + (cy * 8 + y) * outHeight] = indexValue;
						}
					}
				}
			}

			COLOR32 *palette32 = (COLOR32 *) calloc(palSize / 2, sizeof(COLOR32));
			for (int i = 0; i < palSize / 2; i++) {
				palette32[i] = ColorConvertFromDS(pal[i]);
			}
			writeBitmap(palette32, palSize / 2, indexBuffer, width, height, nameBuffer);

			if (!silent) _tprintf(_T("Wrote %s\n"), nameBuffer);

			free(palette32);
			free(indexBuffer);
			free(nameBuffer);
		} else { //output header and source file

				 //suffix the filename with .c, So reserve 3 characters+base length.
			TCHAR *nameBuffer = (TCHAR *) calloc(baseLength + 3, sizeof(TCHAR));
			memcpy(nameBuffer, outBase, (baseLength + 1) * sizeof(TCHAR));
			memcpy(nameBuffer + baseLength, _T(".c"), 3 * sizeof(TCHAR));

			int month, day, year, hour, minute, am;
			getDate(&month, &day, &year, &hour, &minute, &am);

			//find BG name
			int lastSepIndex = -1;
			for (unsigned i = 0; i < _tcslen(srcImage); i++) {
				if (srcImage[i] == _T('/') || srcImage[i] == _T('\\')) lastSepIndex = i;
			}
			const TCHAR *name = srcImage + lastSepIndex + 1;

			//copy to MBS buffer, stripping extension too
			char *bgName = (char *) calloc(_tcslen(name) + 1, sizeof(char));
			for (unsigned i = 0; i < _tcslen(name); i++) {
				TCHAR tch = name[i];
				if (tch == _T('.')) {
					bgName[i] = '\0';
					break;
				}
				bgName[i] = (char) tch;
			}
			bgName[_tcslen(name)] = '\0';

			//if name doesn't start with a letter, prepend "bg_" to its name.
			char *prefix = ((bgName[0] < 'a' || bgName[0] > 'z') && (bgName[0] < 'A' || bgName[0] > 'Z')) ? "bg_" : "";

			//write
			FILE *fp = _tfopen(nameBuffer, _T("wb"));
			fprintf(fp, bgHeader, bgName, month, day, year, hour, minute, am ? 'A' : 'P', depth, nPalettes,
				paletteBase, width, height);
			fprintf(fp, "#include <nds.h>\n\n#include \"%s.h\"\n\n", getFileNameFromPath(mbsBase));

			//write character
			{
				fprintf(fp, "const unsigned short %s%s_char[] = {\n    ", prefix, bgName);
				unsigned short *schars = (unsigned short *) chars;
				int nShort = charSize >> 1;
				for (int i = 0; i < nShort; i++) {
					fprintf(fp, "0x%04x,%c", schars[i], (i + 1) % 16 == 0 ? '\n' : ' ');
					if (((i + 1) % 16) == 0 && (i < nShort - 1)) fprintf(fp, "    ");
				}
				fprintf(fp, "};\n\n");
			}

			//write palette
			{
				fprintf(fp, "const unsigned short %s%s_pal[] = {\n    ", prefix, bgName);
				int nShort = palSize >> 1;
				for (int i = 0; i < nShort; i++) {
					fprintf(fp, "0x%04x,%c", pal[i], (i + 1) % 16 == 0 ? '\n' : ' ');
					if (((i + 1) % 16) == 0 && (i < nShort - 1)) fprintf(fp, "    ");
				}
				fprintf(fp, "};\n\n");
			}

			if (outputScreen) {
				//write screen
				fprintf(fp, "const unsigned short %s%s_screen[] = {\n    ", prefix, bgName);
				int nShort = screenSize >> 1;
				for (int i = 0; i < nShort; i++) {
					fprintf(fp, "0x%04x,%c", screen[i], (i + 1) % 16 == 0 ? '\n' : ' ');
					if (((i + 1) % 16) == 0 && (i < nShort - 1)) fprintf(fp, "    ");
				}
				fprintf(fp, "};\n\n");
			}
			fclose(fp);

			//header
			nameBuffer[_tcslen(nameBuffer) - 1] = _T('h');
			fp = _tfopen(nameBuffer, _T("wb"));

			//heading and declarations
			fprintf(fp, bgHeader, bgName, month, day, year, hour, minute, am ? 'A' : 'P', depth, nPalettes,
				paletteBase, width, height);
			fprintf(fp, "#pragma once\n\n#include <nds.h>\n\n");
			fprintf(fp, "//\n// Generated character data\n//\n");
			fprintf(fp, "extern const unsigned short %s%s_char[%d];\n\n", prefix, bgName, charSize / 2);
			fprintf(fp, "//\n// Generated palette data\n//\n");
			fprintf(fp, "extern const unsigned short %s%s_pal[%d];\n\n", prefix, bgName, palSize / 2);
			fprintf(fp, "//\n// Generated screen data\n//\n");
			fprintf(fp, "extern const unsigned short %s%s_screen[%d];\n\n", prefix, bgName, screenSize / 2);

			fclose(fp);

			free(nameBuffer);
			free(bgName);
		}

		free(pal);
		free(chars);
		free(screen);
	} else {
		//Generate Texture
		//fix up automatic flags

		if (outputTga) {
			outputBinary = 0;
		}
		if (format == -1) {
			format = guessFormat(px, width, height);
		}
		if (nMaxColors == -1) {
			switch (format) {
				case CT_A3I5:
					nMaxColors = 32;
					break;
				case CT_A5I3:
					nMaxColors = 8;
					break;
				case CT_4COLOR:
					nMaxColors = 4;
					break;
				case CT_16COLOR:
					nMaxColors = 16;
					break;
				case CT_256COLOR:
					nMaxColors = 256;
					break;
				case CT_DIRECT:
					nMaxColors = 0;
					break;
				case CT_4x4:
					nMaxColors = chooseColorCount(width, height);
					break;
			}
		}

		int colorMaxes[] = { 0, 32, 4, 16, 256, 32768, 8, 0 };
		int bppArray[] = { 0, 8, 2, 4, 8, 2, 8, 16 };
		if (nMaxColors > colorMaxes[format]) {
			nMaxColors = colorMaxes[format];
			if (!silent) printf("Color count truncated to %d.\n", nMaxColors);
		}
		if (!silent) printf("Generating texture\nMax colors: %d\nFormat: %d\nDiffuse: %d%%\nSize: %dx%d\n\n",
			nMaxColors, format, diffuse, width, height);

		TEXTURE texture = { 0 };

		CREATEPARAMS params;
		params.callback = NULL;
		params.callbackParam = NULL;
		params.colorEntries = nMaxColors;
		params.dest = &texture;
		params.diffuseAmount = (float) diffuse / 100.0f;
		params.dither = !!diffuse;
		params.ditherAlpha = (format == CT_A3I5 || format == CT_A5I3) && params.dither;
		params.useFixedPalette = fixedPalette != NULL;
		params.fixedPalette = NULL;
		params.fmt = format;
		params.width = width;
		params.height = height;
		params.px = px;
		params.threshold = 0;
		params.balance = balance;
		params.colorBalance = colorBalance;
		params.enhanceColors = enhanceColors;
		memset(params.pnam, 0, sizeof(params.pnam));

		if (fixedPalette != NULL) {
			FILE *fp = _tfopen(fixedPalette, _T("rb"));
			fseek(fp, 0, SEEK_END);
			int size = ftell(fp);
			fseek(fp, 0, SEEK_SET);
			params.fixedPalette = (COLOR *) malloc(size);
			(void) fread(params.fixedPalette, 2, size >> 1, fp);
			fclose(fp);

			if (params.colorEntries > (size >> 1)) {
				params.colorEntries = size >> 1;
				if (!silent) printf("Color count truncated to %d.\n", params.colorEntries);
			}
		}

		textureConvert(&params);
		int texelSize = TEXW(texture.texels.texImageParam) * TEXH(texture.texels.texImageParam) * bppArray[format] / 8;
		int indexSize = (format == CT_4x4) ? (texelSize >> 1) : 0;

		//if binary, output as NTFT, NTFI, NTFP.
		if (outputBinary) {
			//suffix the filename with .ntft, .nfti, .nftp. So reserve 6 characters+base length.
			TCHAR *nameBuffer = (TCHAR *) calloc(baseLength + 6, sizeof(TCHAR));
			memcpy(nameBuffer, outBase, (baseLength + 1) * sizeof(TCHAR));
			memcpy(nameBuffer + baseLength, _T(".ntft"), 6 * sizeof(TCHAR));

			//output texel always
			FILE *fp = _tfopen(nameBuffer, _T("wb"));
			fwrite(texture.texels.texel, 1, texelSize, fp);
			fclose(fp);
			if (!silent) _tprintf(_T("Wrote %s\n"), nameBuffer);

			//output palette if not direct
			if (format != CT_DIRECT) {
				memcpy(nameBuffer + baseLength, _T(".ntfp"), 6 * sizeof(TCHAR));
				fp = _tfopen(nameBuffer, _T("wb"));
				fwrite(texture.palette.pal, 2, texture.palette.nColors, fp);
				fclose(fp);
				if (!silent) _tprintf(_T("Wrote %s\n"), nameBuffer);
			}

			//output index if 4x4
			if (format == CT_4x4) {
				memcpy(nameBuffer + baseLength, _T(".ntfi"), 6 * sizeof(TCHAR));
				fp = _tfopen(nameBuffer, _T("wb"));
				fwrite(texture.texels.cmp, 1, indexSize, fp);
				fclose(fp);
				if (!silent) _tprintf(_T("Wrote %s\n"), nameBuffer);
			}

			free(nameBuffer);
		} else if (outputTga) {
			//output as NNS TGA file
			TCHAR *nameBuffer = (TCHAR *) calloc(baseLength + 5, sizeof(TCHAR));
			memcpy(nameBuffer, outBase, (baseLength + 1) * sizeof(TCHAR));
			memcpy(nameBuffer + baseLength, _T(".tga"), 5 * sizeof(TCHAR));

			writeNitroTGA(nameBuffer, &texture.texels, &texture.palette);
			free(nameBuffer);
		} else {

			//suffix the filename with .c, So reserve 3 characters+base length.
			TCHAR *nameBuffer = (TCHAR *) calloc(baseLength + 3, sizeof(TCHAR));
			memcpy(nameBuffer, outBase, (baseLength + 1) * sizeof(TCHAR));
			memcpy(nameBuffer + baseLength, _T(".c"), 3 * sizeof(TCHAR));

			int month, day, year, hour, minute, am;
			getDate(&month, &day, &year, &hour, &minute, &am);

			//find texture name
			int lastSepIndex = -1;
			for (unsigned i = 0; i < _tcslen(srcImage); i++) {
				if (srcImage[i] == _T('/') || srcImage[i] == _T('\\')) lastSepIndex = i;
			}
			const TCHAR *name = srcImage + lastSepIndex + 1;

			//copy to MBS buffer, stripping extension too
			char *texName = (char *) calloc(_tcslen(name) + 1, sizeof(char));
			for (unsigned i = 0; i < _tcslen(name); i++) {
				TCHAR tch = name[i];
				if (tch == _T('.')) {
					texName[i] = '\0';
					break;
				}
				texName[i] = (char) tch;
			}
			texName[_tcslen(name)] = '\0';

			//if texture name doesn't start with a letter, prepend "tex_" to its name.
			char *prefix = ((texName[0] < 'a' || texName[0] > 'z') && (texName[0] < 'A' || texName[0] > 'Z')) ? "tex_" : "";

			FILE *fp = _tfopen(nameBuffer, _T("wb"));
			fprintf(fp, texHeader, texName, month, day, year, hour, minute, am ? 'A' : 'P', stringFromFormat(format), texture.palette.nColors,
				TEXW(texture.texels.texImageParam), TEXH(texture.texels.texImageParam));
			fprintf(fp, "#include <nds.h>\n\n#include \"%s.h\"\n\n", getFileNameFromPath(mbsBase));

			//write texel
			{
				fprintf(fp, "const unsigned short %s%s_texel[] = {\n    ", prefix, texName);
				unsigned short *txel = (unsigned short *) texture.texels.texel;
				int nShort = texelSize >> 1;
				for (int i = 0; i < nShort; i++) {
					fprintf(fp, "0x%04x,%c", txel[i], (i + 1) % 16 == 0 ? '\n' : ' ');
					if (((i + 1) % 16) == 0 && (i < nShort - 1)) fprintf(fp, "    ");
				}
				fprintf(fp, "};\n\n");
			}

			//write index
			if (format == CT_4x4) {
				fprintf(fp, "const unsigned short %s%s_idx[] = {\n    ", prefix, texName);
				unsigned short *pidx = (unsigned short *) texture.texels.cmp;
				int nShort = indexSize >> 1;
				for (int i = 0; i < nShort; i++) {
					fprintf(fp, "0x%04x,%c", pidx[i], (i + 1) % 16 == 0 ? '\n' : ' ');
					if (((i + 1) % 16) == 0 && (i < nShort - 1)) fprintf(fp, "    ");
				}
				fprintf(fp, "};\n\n");
			}

			//write palette
			if (format != CT_DIRECT) {
				fprintf(fp, "const unsigned short %s%s_pal[] = {\n    ", prefix, texName);
				unsigned short *pcol = (unsigned short *) texture.palette.pal;
				int nShort = texture.palette.nColors;
				for (int i = 0; i < nShort; i++) {
					fprintf(fp, "0x%04x,%c", pcol[i], (i + 1) % 16 == 0 ? '\n' : ' ');
					if (((i + 1) % 16) == 0 && (i < nShort - 1)) fprintf(fp, "    ");
				}
				if (nShort & 0xF) fprintf(fp, "\n"); //force this to be on a new line
				fprintf(fp, "};\n\n");
			}
			fclose(fp);

			if (!silent) _tprintf(_T("Wrote %s\n"), nameBuffer);

			//write header
			nameBuffer[_tcslen(nameBuffer) - 1] = _T('h');
			fp = _tfopen(nameBuffer, _T("wb"));
			fprintf(fp, texHeader, texName, month, day, year, hour, minute, am ? 'A' : 'P', stringFromFormat(format), texture.palette.nColors,
				TEXW(texture.texels.texImageParam), TEXH(texture.texels.texImageParam));
			fprintf(fp, "#pragma once\n\n#include <nds.h>\n\n");
			fprintf(fp, "//\n// Generated texel data\n//\n");
			fprintf(fp, "extern const unsigned short %s%s_texel[%d];\n\n", prefix, texName, texelSize / 2);
			if (format == CT_4x4) {
				fprintf(fp, "//\n// Generated index data\n//\n");
				fprintf(fp, "extern const unsigned short %s%s_idx[%d];\n\n", prefix, texName, texelSize / 4);
			}
			if (format != CT_DIRECT) {
				fprintf(fp, "//\n// Generated palette data\n//\n");
				fprintf(fp, "extern const unsigned short %s%s_pal[%d];\n\n", prefix, texName, texture.palette.nColors);
			}
			fclose(fp);

			if (!silent) _tprintf(_T("Wrote %s\n"), nameBuffer);
			free(texName);
			free(nameBuffer);
			if (params.fixedPalette != NULL) free(params.fixedPalette);
		}
	}
	free(mbsBase);

	return 0;
}

#ifdef _WIN32

extern int __getmainargs(int *_Argc, char ***_Argv, char ***_Env, int _DoWildCard, int *_StartInfo);
extern int __wgetmainargs(int *_Argc, wchar_t ***_Argv, wchar_t ***_Env, int _DoWildCard, int *_StartInfo);

int Entry(PVOID Peb) {
	int argc, startInfo;

#ifdef _UNICODE
	wchar_t **argv, **envp;
	__wgetmainargs(&argc, &argv, &envp, 0, &startInfo);
#else
	char **argv, char **envp;
	__getmainargs(&argc, &argv, &envp, 0, &startInfo);
#endif

	exit(_tmain(argc, argv));
	return 0;
}

#endif
